#pragma once

// Low-level playback: each voice is an Ogg Vorbis decoder feeding its own SDL
// audio stream, which SDL mixes and resamples on the audio device. Fades are
// applied as per-stream gain from the main thread.

#include <SDL3/SDL_audio.h>

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

class Audio {
public:
    using Handle = uint32_t;  // 0 = no voice

    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    bool init();
    // Fully decode an Ogg Vorbis file; false if it is corrupt (for --verify-media).
    static bool decodeCheck(const std::vector<uint8_t>& ogg);
    bool ok() const { return device_ != 0; }

    Handle play(std::vector<uint8_t> ogg, const std::string& name, bool loop, float gain);
    void stop(Handle h);
    bool playing(Handle h) const;
    float gain(Handle h) const;
    void setGain(Handle h, float gain);
    // Ramp gain to `target` over `seconds`; optionally stop when it gets there.
    void fade(Handle h, float target, double seconds, bool stopAtEnd = false);

    // Pause or resume every voice except `keep` (used behind the menu).
    void pauseAll(bool paused, Handle keep = 0);
    void stopAll();

    // Advance fades and drop finished voices. Call once per frame.
    void update(double dt);

private:
    struct Voice;
    static void SDLCALL feed(void* userdata, SDL_AudioStream* stream, int additional, int total);

    SDL_AudioDeviceID device_ = 0;
    Handle next_ = 1;
    std::unordered_map<Handle, std::unique_ptr<Voice>> voices_;
};
