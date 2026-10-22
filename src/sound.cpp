#include "sound.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>

#include "constants.hpp"
#include "resources.hpp"
#include "world.hpp"

namespace {

constexpr double kTrackFade = 2.0;  // seconds, for tracks flagged SC_FADEIN/SC_FADEOUT

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

void Sound::update(double dt) {
    if (playlistPlaying_ && !playlist_.empty()) {
        if (playlistAudible_ == playlistIndex_ && !audio_.playing(music_)) {
            // Track finished: move on, wrapping to the first (0x40eff5).
            playlistIndex_ = playlistIndex_ % int(playlist_.size()) + 1;
        }
        if (playlistAudible_ != playlistIndex_ || !audio_.playing(music_)) {
            if (audio_.playing(music_))
                audio_.fade(music_, 0, kTrackFade, true);
            const Track& t = *playlist_[size_t(playlistIndex_ - 1)];
            float gain = volume10(t.volume);
            music_ = start(t.files[0], false, t.fadeIn ? 0 : gain);
            if (t.fadeIn)
                audio_.fade(music_, gain, kTrackFade);
            musicName_ = t.name;
            musicFadeOut_ = t.fadeOut != 0;
            playlistAudible_ = playlistIndex_;
        }
    }
    audio_.update(dt);
}

void Sound::playlistClear() {
    playlistStop(-1);
    playlist_.clear();
    playlistIndex_ = playlistAudible_ = 0;
}

void Sound::playlistQueue(const Track* t) {
    if (!t)
        return;
    playlist_.push_back(t);
    if (playlistIndex_ == 0)
        playlistIndex_ = 1;
}

void Sound::playlistStart() {
    if (!playlist_.empty())
        playlistPlaying_ = true;
}

void Sound::playlistStop(double fadeSeconds) {
    if (!playlistPlaying_)
        return;
    playlistPlaying_ = false;
    fadeOutTrack(fadeSeconds >= 0 ? fadeSeconds : (musicFadeOut_ ? kTrackFade : 0));
    playlistAudible_ = 0;
}

void Sound::playlistNext() {
    if (!playlist_.empty())
        playlistIndex_ = std::min(playlistIndex_ + 1, int(playlist_.size()));
}

void Sound::playlistPrevious() {
    if (!playlist_.empty())
        playlistIndex_ = std::max(playlistIndex_ - 1, 1);
}

void Sound::playlistFirst() {
    if (!playlist_.empty())
        playlistIndex_ = 1;
}

void Sound::playlistLast() {
    if (!playlist_.empty())
        playlistIndex_ = int(playlist_.size());
}

void Sound::playFx(const std::string& file) { start(file, false, 1); }

void Sound::playChannel(int channel, const std::string& file, bool loop) {
    stopChannel(channel);
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
    case sc::FADEIN: {
        float target = channelVolume_.count(channel) ? channelVolume_[channel] : 1.0f;
        if (!audio_.playing(h))
            break;
        if (audio_.gain(h) >= target)
            audio_.setGain(h, 0);  // freshly started sound: fade in from silence
        audio_.fade(h, target, value);
        break;
    }
    case sc::FADEOUT:
        audio_.fade(h, 0, value);
        break;
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

std::string Sound::currentTrack() const { return audio_.playing(music_) ? musicName_ : std::string(); }

void Sound::stopAll() {
    audio_.stopAll();
    channels_.clear();
    channelFiles_.clear();
    channelVolume_.clear();
    music_ = menuMusic_ = 0;
    musicName_.clear();
    playlist_.clear();
    playlistIndex_ = playlistAudible_ = 0;
    playlistPlaying_ = false;
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
    if (audio_.playing(music_))
        audio_.setGain(music_, std::clamp(audio_.gain(music_) + delta, 0.0f, 1.0f));
}

void Sound::playTrack(const Track& t) {
    if (music_ && audio_.playing(music_)) {
        if (musicName_ == t.name)
            return;
        if (musicFadeOut_) audio_.fade(music_, 0, kTrackFade, true);
        else audio_.stop(music_);
    }
    // In stereo mode only the front (or only) file of a track is played;
    // the -RL/-RR rear files are for the original engine's surround option.
    float gain = volume10(t.volume);
    music_ = start(t.files[0], t.loop != 0, t.fadeIn ? 0 : gain);
    if (t.fadeIn)
        audio_.fade(music_, gain, kTrackFade);
    musicName_ = t.name;
    musicFadeOut_ = t.fadeOut != 0;
}

void Sound::stopTrack(const std::string& name) {
    if (!name.empty() && name != musicName_)
        return;
    audio_.stop(music_);
    music_ = 0;
    musicName_.clear();
}

void Sound::fadeOutTrack(double seconds) {
    audio_.fade(music_, 0, seconds, true);
    music_ = 0;
    musicName_.clear();
}
