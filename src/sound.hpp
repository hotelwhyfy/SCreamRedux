#pragma once

// The game's audio model, as the scripts use it:
//  - scFXPlay: fire-and-forget effects.
//  - scFXPlayEx(channel, file, wait, loop): one sound per named channel
//    (SC_FX, SC_STEP, SC_3DA..SC_3DD); playing replaces what was there.
//  - scFXSet(channel, SC_VOLUME|SC_FADEIN|SC_FADEOUT, value): volume is 0..10;
//    fades do not stop the sound (enterCar fades the wind out, exitCar back in).
//  - Voices (scFeedbackEx, dialogue) play on SC_DIALOGUE.
//  - Music (scTrackPlay / scTrackStop / scTrackFadeOut) on channels 1-3:
//    the front file, plus the -RL/-RR files when surround music is on
//    (0x4161f0). A playlist (scPlaylist*, 0x4165e0..0x4168f0, driven from
//    0x40ee00) alternates between that bank and channels 4-6.
//  - Fades are rates, as in scream.exe's audio thread (0x401310): every tick
//    the gain moves 0.0025/F toward its target (F = 0: no fade).

#include <array>
#include <map>
#include <string>
#include <vector>

#include "audio.hpp"

class Resources;
struct Track;

class Sound {
public:
    explicit Sound(Resources& res) : res_(res) {}

    bool init() { return audio_.init(); }
    // Turbo mode: nothing plays (so nothing is waited for), but missing files are still reported.
    void setSilent(bool silent) { silent_ = silent; }
    void update(double dt);

    void playFx(const std::string& file);
    void playChannel(int channel, const std::string& file, bool loop);
    bool channelPlaying(int channel) const;
    void setChannel(int channel, int what, double value);
    void stopChannel(int channel);

    void playVoice(const std::string& file);
    bool voicePlaying() const;
    // Shift the music's gain (scScriptGo turns it down by 0.25 during dialogue).
    void duckMusic(float delta);

    void playTrack(const Track& t);
    void stopTrack();
    void fadeOutTrack(int f);

    void playlistClear();
    void playlistQueue(const Track* t);
    void playlistStart();
    void playlistStop(int f, bool useTrackFade);  // scPlaylistStop(f); nil/negative: see sound.cpp
    void playlistNext();
    void playlistPrevious();
    void playlistFirst();
    void playlistLast();
    void setSurround(bool on) { surround_ = on; }

    // The original's channel table (1-17; 1-3 and 4-6 the music banks, 8
    // SC_STEP, 9-12 SC_3DA-D, 13 SC_DIALOGUE, 14 SC_FX), as saved games keep it.
    struct ChannelRecord {
        std::string file;
        bool loaded = false, playing = false, loop = false;
        float volume = 1, current = 1;  // +0x340 set volume, +0x33c gain now
        int fadeIn = 0, fadeOut = 0;    // the last fade rates
    };
    std::array<ChannelRecord, 18> channelRecords() const;
    void restoreChannels(const std::array<ChannelRecord, 18>& records);

    // For saved games: looping effect channels and the current music track.
    struct Loop {
        int channel;
        std::string file;
        float volume;
    };
    std::vector<Loop> loopingChannels() const;
    std::string currentTrack() const;
    void stopAll();
    void fadeAll(int f);
    void stopChannels();  // every game channel and music bank (not the menu music)  // every channel and bank down to silence at rate f

    // The menu pauses game audio and plays its own music.
    void pauseGame(bool paused);
    void playMenuMusic(const std::string& file);
    void stopMenuMusic();

private:
    Audio::Handle start(const std::string& file, bool loop, float gain);

    Resources& res_;
    bool silent_ = false;
    Audio audio_;
    std::map<int, Audio::Handle> channels_;
    std::map<int, std::pair<std::string, bool>> channelFiles_;  // channel -> (file, loop)
    Audio::Handle menuMusic_ = 0;
    std::map<int, float> channelVolume_;  // 0..1, from scFXSet(SC_VOLUME)
    std::map<int, std::pair<int, int>> channelFades_;  // channel -> (fade-in, fade-out) rates

    // Music banks: [0] = channels 1-3 (scTrackPlay, playlist bank A),
    // [1] = channels 4-6 (playlist bank B); front, rear left, rear right.
    using Bank = std::array<Audio::Handle, 3>;
    std::array<Bank, 2> banks_{};
    struct BankInfo {
        std::array<std::string, 3> files;
        bool loop = false;
        float volume = 1;
        int fadeIn = 0, fadeOut = 0;
    };
    std::array<BankInfo, 2> bankInfo_{};
    std::string musicName_;
    float duck_ = 0;  // 0.25 while scScriptGo dialogue plays (0x4df5c08)
    bool surround_ = false;
    void startBank(int bank, const Track& t, bool loop, float gain, int fadeIn);
    void stopBank(int bank);
    void fadeBank(int bank, float target, int f);
    bool bankPlaying(int bank) const { return audio_.playing(banks_[size_t(bank)][0]); }

    std::vector<const Track*> playlist_;
    int playlistIndex_ = 0;    // 1-based; 0 when empty or stopped
    int playlistAudible_ = 0;  // index whose track the current bank holds
    int playlistBank_ = 0;
    int playlistPlays_ = 1;    // plays of the current track so far
    bool playlistPlaying_ = false;
    std::array<int, 2> unloadAfter_{}, unloadCount_{};  // stop a bank after n one-second ticks
    double secondClock_ = 0;
};
