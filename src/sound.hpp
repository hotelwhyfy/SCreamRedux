#pragma once

// The game's audio model, as the scripts use it:
//  - scFXPlay: fire-and-forget effects.
//  - scFXPlayEx(channel, file, wait, loop): one sound per named channel
//    (SC_FX, SC_STEP, SC_3DA..SC_3DD); playing replaces what was there.
//  - scFXSet(channel, SC_VOLUME|SC_FADEIN|SC_FADEOUT, value): volume is 0..10;
//    fades do not stop the sound (enterCar fades the wind out, exitCar back in).
//  - Voices (scFeedbackEx, dialogue) play on SC_DIALOGUE.
//  - One music track at a time (scTrackPlay / scTrackStop / scTrackFadeOut).
//  - A playlist of tracks (scPlaylist*), as in scream.exe (0x416590..0x4168f0,
//    driven from 0x40ee00): a 1-based current index that Next/Previous clamp,
//    First/Last jump; the current track plays, and when it ends the index
//    advances and wraps; changing the index crossfades to the new track.

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
    void stopTrack(const std::string& name = {});
    void fadeOutTrack(double seconds);

    void playlistClear();
    void playlistQueue(const Track* t);
    void playlistStart();
    void playlistStop(double fadeSeconds);  // < 0: the track's own fade
    void playlistNext();
    void playlistPrevious();
    void playlistFirst();
    void playlistLast();

    // For saved games: looping effect channels and the current music track.
    struct Loop {
        int channel;
        std::string file;
        float volume;
    };
    std::vector<Loop> loopingChannels() const;
    std::string currentTrack() const;
    void stopAll();

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

    Audio::Handle music_ = 0;
    std::string musicName_;
    bool musicFadeOut_ = false;  // the playing track's SC_FADEOUT flag

    std::vector<const Track*> playlist_;
    int playlistIndex_ = 0;    // 1-based; 0 when empty
    int playlistAudible_ = 0;  // index whose track is in music_
    bool playlistPlaying_ = false;
};
