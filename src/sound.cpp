#include "sound.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>

#include "constants.hpp"
#include "resources.hpp"
#include "world.hpp"

namespace {

// A fade of rate F moves the gain by 0.0025/F per tick of the audio thread
// (Sleep(1) in a loop, 0x419540). How long Sleep(1) lasts is Windows' timer
// resolution at run time (15.6 ms by default, ~1 ms when any program on the
// machine asks for it; the shipped wrap_oal.dll asks for only 25 ms), so it
// varied between players' PCs. The design says F was meant as seconds from
// full volume: the playlist cuts a fading bank after F seconds and the
// scripts do "scTrackFadeOut(1) scSleep(1) scTrackStop()". That is a 2.5 ms tick.
constexpr double kAudioTick = 0.0025;
double fadeSeconds(float from, float to, int f) { return std::abs(to - from) * f * kAudioTick / 0.0025; }

float volume10(double v) { return float(std::clamp(v, 0.0, 10.0) / 10.0); }

}  // namespace

Audio::Handle Sound::start(const std::string& file, bool loop, float gain) {
    if (file.empty() || !audio_.ok())
        return 0;
    if (silent_) {
        if (!res_.exists(file))
            SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "missing sound %s", file.c_str());
        return 0;
    }
    auto data = res_.read(file);
    if (!data) {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "missing sound %s", file.c_str());
        return 0;
    }
    return audio_.play(std::move(*data), file, loop, gain);
}

// The playlist thread (0x40ee00).
void Sound::update(double dt) {
    // Banks due to be cut, counted in one-second ticks (0x40ee0f).
    secondClock_ += dt;
    if (secondClock_ >= 1) {
        secondClock_ = 0;
        for (int b = 0; b < 2; ++b)
            if (unloadAfter_[size_t(b)] != 0 && ++unloadCount_[size_t(b)] >= unloadAfter_[size_t(b)]) {
                stopBank(b);
                unloadAfter_ = {};
                unloadCount_ = {};
            }
    }
    if (playlistPlaying_ && !playlist_.empty() && playlistIndex_ >= 1) {
        const int count = int(playlist_.size());
        if (playlistIndex_ == playlistAudible_) {
            const Track& t = *playlist_[size_t(playlistIndex_ - 1)];
            if (!bankPlaying(playlistBank_)) {
                // Ended: SC loop field = number of plays (0 = for ever).
                if (playlistPlays_ == t.loop) {
                    playlistIndex_ = playlistIndex_ % count + 1;
                    playlistPlays_ = 1;
                } else {
                    ++playlistPlays_;
                    playlistAudible_ = 0;  // play it again
                }
            } else if (audio_.remainingSeconds(banks_[size_t(playlistBank_)][0]) < t.fadeOut &&
                       playlistPlays_ == t.loop) {
                // SC_FADEOUT seconds before the end, the next one starts and
                // this one fades at that rate, cut at the next second tick.
                unloadAfter_[size_t(playlistBank_)] = 1;
                fadeBank(playlistBank_, 0, t.fadeOut);
                playlistIndex_ = playlistIndex_ % count + 1;
            }
        }
        if (playlistIndex_ != playlistAudible_ && playlistIndex_ <= count) {
            // The other bank: the old one is left to play out (no fade on Next/Previous).
            playlistBank_ ^= 1;
            stopBank(playlistBank_);
            const Track& t = *playlist_[size_t(playlistIndex_ - 1)];
            startBank(playlistBank_, t, false, std::max(0.0f, float(t.volume) * 0.1f - duck_),
                      playlistPlays_ == 1 ? t.fadeIn : 0);
            musicName_ = t.name;
            playlistAudible_ = playlistIndex_;
        }
    }
    audio_.update(dt);
}

void Sound::startBank(int bank, const Track& t, bool loop, float gain, int fadeIn) {
    stopBank(bank);
    Bank& b = banks_[size_t(bank)];
    const int files = surround_ && t.surround ? 3 : 1;
    BankInfo& info = bankInfo_[size_t(bank)];
    info = {};
    for (int i = 0; i < files; ++i) info.files[size_t(i)] = t.files[size_t(i)];
    info.loop = loop;
    info.volume = gain;
    info.fadeIn = fadeIn;
    for (int i = 0; i < files; ++i) {
        b[size_t(i)] = start(t.files[size_t(i)], loop, fadeIn > 0 ? 0 : gain);
        if (fadeIn > 0)
            audio_.fade(b[size_t(i)], gain, fadeSeconds(0, gain, fadeIn));
    }
}

void Sound::stopBank(int bank) {
    bankInfo_[size_t(bank)] = {};
    for (Audio::Handle& h : banks_[size_t(bank)]) {
        audio_.stop(h);
        h = 0;
    }
}

void Sound::fadeBank(int bank, float target, int f) {
    if (f <= 0)
        return;
    bankInfo_[size_t(bank)].fadeOut = f;
    for (Audio::Handle h : banks_[size_t(bank)])
        audio_.fade(h, target, fadeSeconds(audio_.gain(h), target, f));
}

void Sound::playlistClear() {
    if (playlistPlaying_)
        playlistStop(0, false);
    playlist_.clear();
    playlistIndex_ = playlistAudible_ = 0;
}

void Sound::playlistQueue(const Track* t) {
    if (!t || playlist_.size() >= 38)
        return;
    playlist_.push_back(t);
    if (playlist_.size() == 1)
        playlistIndex_ = 1;
}

void Sound::playlistStart() {
    if (playlist_.empty())
        return;
    playlistPlaying_ = true;
    if (playlistIndex_ == 0)
        playlistIndex_ = 1;
}

// scPlaylistStop(f) (0x4166c0): the track fades at rate f (1 if less than f
// seconds are left) and is cut f seconds later; f = 0 (or nil) lets it play
// out. A negative f takes the SC_FADEOUT of the last queued track (as compiled).
void Sound::playlistStop(int f, bool useTrackFade) {
    if (!playlistPlaying_)
        return;
    playlistPlays_ = 1;
    playlistPlaying_ = false;
    if (useTrackFade)
        f = playlist_.empty() ? 0 : playlist_.back()->fadeOut;
    if (f > 0) {
        const bool soon = audio_.remainingSeconds(banks_[size_t(playlistBank_)][0]) < f;
        fadeBank(playlistBank_, 0, soon ? 1 : f);
        unloadAfter_[size_t(playlistBank_)] = soon ? 1 : f;
    }
    playlistIndex_ = playlistAudible_ = 0;
}

void Sound::playlistNext() {
    if (playlist_.empty()) return;
    playlistAudible_ = playlistIndex_;
    playlistIndex_ = std::min(playlistIndex_ + 1, int(playlist_.size()));
}

void Sound::playlistPrevious() {
    if (playlist_.empty()) return;
    playlistAudible_ = playlistIndex_;
    playlistIndex_ = std::max(playlistIndex_ - 1, 1);
}

void Sound::playlistFirst() {
    if (playlist_.empty()) return;
    playlistAudible_ = playlistIndex_;
    playlistIndex_ = 1;
}

void Sound::playlistLast() {
    if (playlist_.empty()) return;
    playlistAudible_ = playlistIndex_;
    playlistIndex_ = int(playlist_.size());
}

void Sound::playFx(const std::string& file) { start(file, false, 1); }

void Sound::playChannel(int channel, const std::string& file, bool loop) {
    stopChannel(channel);
    channelFades_.erase(channel);
    auto vol = channelVolume_.find(channel);
    channels_[channel] = start(file, loop, vol == channelVolume_.end() ? 1 : vol->second);
    channelFiles_[channel] = {file, loop};
}

bool Sound::channelPlaying(int channel) const {
    auto it = channels_.find(channel);
    return it != channels_.end() && audio_.playing(it->second);
}

void Sound::setChannel(int channel, int what, double value) {
    Audio::Handle h = channels_.count(channel) ? channels_[channel] : 0;
    switch (what) {
    case sc::VOLUME:
        channelVolume_[channel] = volume10(value);
        audio_.setGain(h, volume10(value));
        break;
    case sc::FADEIN: {  // 0x401b70: from silence up to the channel's volume
        float target = channelVolume_.count(channel) ? channelVolume_[channel] : 1.0f;
        const int f = int(std::lround(value));
        if (f <= 0) {
            audio_.setGain(h, target);
            break;
        }
        audio_.setGain(h, 0);
        audio_.fade(h, target, fadeSeconds(0, target, f));
        channelFades_[channel].first = f;
        break;
    }
    case sc::FADEOUT: {  // 0x401b20: down to silence; the sound keeps playing
        const int f = int(std::lround(value));
        if (f > 0) {
            audio_.fade(h, 0, fadeSeconds(audio_.gain(h), 0, f));
            channelFades_[channel].second = f;
        }
        break;
    }
    }
}

void Sound::stopChannel(int channel) {
    auto it = channels_.find(channel);
    if (it != channels_.end()) {
        audio_.stop(it->second);
        channels_.erase(it);
    }
}

std::vector<Sound::Loop> Sound::loopingChannels() const {
    std::vector<Loop> out;
    for (const auto& [ch, h] : channels_) {
        auto f = channelFiles_.find(ch);
        if (f != channelFiles_.end() && f->second.second && audio_.playing(h))
            out.push_back({ch, f->second.first, audio_.gain(h)});
    }
    return out;
}

std::string Sound::currentTrack() const { return bankPlaying(0) || bankPlaying(1) ? musicName_ : std::string(); }

void Sound::stopAll() {
    audio_.stopAll();
    channels_.clear();
    channelFiles_.clear();
    channelVolume_.clear();
    channelFades_.clear();
    banks_ = {};
    bankInfo_ = {};
    menuMusic_ = 0;
    musicName_.clear();
    playlist_.clear();
    playlistIndex_ = playlistAudible_ = 0;
    playlistPlaying_ = false;
    playlistPlays_ = 1;
    unloadAfter_ = unloadCount_ = {};
    duck_ = 0;
}

void Sound::pauseGame(bool paused) { audio_.pauseAll(paused, menuMusic_); }

void Sound::playMenuMusic(const std::string& file) {
    if (audio_.playing(menuMusic_))
        return;
    menuMusic_ = start(file, true, 0.8f);
}

void Sound::stopMenuMusic() {
    audio_.fade(menuMusic_, 0, 1.0, true);
    menuMusic_ = 0;
}

void Sound::playVoice(const std::string& file) { playChannel(sc::DIALOGUE, file, false); }
bool Sound::voicePlaying() const { return channelPlaying(sc::DIALOGUE); }

void Sound::duckMusic(float delta) {
    duck_ = std::max(0.0f, duck_ - delta);
    for (const Bank& b : banks_)
        for (Audio::Handle h : b)
            if (audio_.playing(h))
                audio_.setGain(h, std::clamp(audio_.gain(h) + delta, 0.0f, 1.0f));
}

// scTrackPlay (0x4161f0): channels 1-3 are stopped and the track starts over,
// at SC_VOLUME / 10, faded in at rate SC_FADEIN, looping if scTrackSet said so.
// Ignored while the playlist plays, as are scTrackStop and scTrackFadeOut.
void Sound::playTrack(const Track& t) {
    if (playlistPlaying_)
        return;
    startBank(0, t, t.loop != 0, float(t.volume) * 0.1f, t.fadeIn);
    musicName_ = t.name;
}

void Sound::stopTrack() {  // 0x416390
    if (playlistPlaying_)
        return;
    stopBank(0);
    musicName_.clear();
}

// scTrackFadeOut(f) (0x416400): fade at rate f, or 1 when less than f seconds
// are left; the track carries on silently until it is stopped or replaced.
void Sound::fadeOutTrack(int f) {
    if (playlistPlaying_)
        return;
    if (f < 0)
        f = playlist_.empty() ? 0 : playlist_.back()->fadeOut;  // as compiled
    if (audio_.remainingSeconds(banks_[0][0]) < f)
        f = 1;
    fadeBank(0, 0, f);
}

std::array<Sound::ChannelRecord, 18> Sound::channelRecords() const {
    std::array<ChannelRecord, 18> out{};
    for (int bank = 0; bank < 2; ++bank)
        for (int i = 0; i < 3; ++i) {
            const BankInfo& info = bankInfo_[size_t(bank)];
            ChannelRecord& r = out[size_t(1 + bank * 3 + i)];
            const Audio::Handle h = banks_[size_t(bank)][size_t(i)];
            if (info.files[size_t(i)].empty())
                continue;
            r.file = info.files[size_t(i)];
            r.loaded = true;
            r.playing = audio_.playing(h);
            r.loop = info.loop;
            r.volume = info.volume;
            r.current = audio_.gain(h);
            r.fadeIn = info.fadeIn;
            r.fadeOut = info.fadeOut;
        }
    for (const auto& [ch, h] : channels_) {
        if (ch < 1 || ch > 17)
            continue;
        auto f = channelFiles_.find(ch);
        if (f == channelFiles_.end())
            continue;
        ChannelRecord& r = out[size_t(ch)];
        r.file = f->second.first;
        r.loop = f->second.second;
        r.loaded = true;
        r.playing = audio_.playing(h);
        auto v = channelVolume_.find(ch);
        r.volume = v == channelVolume_.end() ? 1.0f : v->second;
        r.current = audio_.gain(h);
        if (auto fd = channelFades_.find(ch); fd != channelFades_.end())
            r.fadeIn = fd->second.first, r.fadeOut = fd->second.second;
    }
    return out;
}

// Loading a game (0x411060): every channel but SC_STEP and SC_FX that was
// playing starts again at its volume, then its fades are applied again.
void Sound::restoreChannels(const std::array<ChannelRecord, 18>& records) {
    for (int ch = 1; ch <= 17; ++ch) {
        const ChannelRecord& r = records[size_t(ch)];
        if (ch == sc::STEP || ch == sc::FX || !r.loaded || !r.playing || r.file.empty())
            continue;
        if (ch <= 6) {
            const int bank = (ch - 1) / 3, i = (ch - 1) % 3;
            BankInfo& info = bankInfo_[size_t(bank)];
            info.files[size_t(i)] = r.file;
            info.loop = r.loop;
            info.volume = r.volume;
            Audio::Handle& h = banks_[size_t(bank)][size_t(i)];
            h = start(r.file, r.loop, r.volume);
            if (r.fadeOut > 0) audio_.fade(h, 0, fadeSeconds(r.volume, 0, r.fadeOut)), info.fadeOut = r.fadeOut;
            if (r.fadeIn > 0) audio_.setGain(h, 0), audio_.fade(h, r.volume, fadeSeconds(0, r.volume, r.fadeIn));
            continue;
        }
        channelVolume_[ch] = r.volume;
        playChannel(ch, r.file, r.loop);
        if (r.fadeOut > 0) setChannel(ch, sc::FADEOUT, r.fadeOut);
        if (r.fadeIn > 0) setChannel(ch, sc::FADEIN, r.fadeIn);
    }
}

void Sound::fadeAll(int f) {
    for (int b = 0; b < 2; ++b) fadeBank(b, 0, f);
    for (const auto& [ch, h] : channels_) audio_.fade(h, 0, fadeSeconds(audio_.gain(h), 0, f));
}

void Sound::stopChannels() {
    stopBank(0);
    stopBank(1);
    for (auto& [ch, h] : channels_) audio_.stop(h);
    channels_.clear();
}
