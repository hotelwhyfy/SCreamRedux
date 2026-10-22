#include "audio.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <atomic>

#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include "stb_vorbis.c"

struct Audio::Voice {
    std::string name;
    std::vector<uint8_t> data;  // compressed; the decoder reads from it
    stb_vorbis* vorbis = nullptr;
    int channels = 2;
    bool loop = false;
    std::atomic<bool> exhausted{false};  // written by the audio thread
    SDL_AudioStream* stream = nullptr;

    float gain = 1, fadeFrom = 1, fadeTo = 1;
    double fadeTime = 0, fadeLength = 0;
    bool fading = false, stopAtEnd = false;
    bool flushed = false;

    ~Voice() {
        if (stream)
            SDL_DestroyAudioStream(stream);  // after this the callback can no longer run
        if (vorbis)
            stb_vorbis_close(vorbis);
    }
};

Audio::Audio() = default;

Audio::~Audio() {
    voices_.clear();
    if (device_)
        SDL_CloseAudioDevice(device_);
}

bool Audio::decodeCheck(const std::vector<uint8_t>& ogg) {
    int err = 0;
    stb_vorbis* v = stb_vorbis_open_memory(ogg.data(), int(ogg.size()), &err, nullptr);
    if (!v)
        return false;
    float buf[8192];
    int channels = stb_vorbis_get_info(v).channels;
    while (stb_vorbis_get_samples_float_interleaved(v, channels, buf, 8192) > 0) {}
    bool ok = stb_vorbis_get_error(v) == VORBIS__no_error;
    stb_vorbis_close(v);
    return ok;
}

bool Audio::init() {
    device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!device_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "no audio device: %s", SDL_GetError());
        return false;
    }
    return true;
}

// Audio thread: decode just enough to satisfy SDL's request.
void SDLCALL Audio::feed(void* userdata, SDL_AudioStream* stream, int additional, int) {
    auto* v = static_cast<Voice*>(userdata);
    float buf[4096];
    const int frameBytes = int(sizeof(float)) * v->channels;
    while (additional > 0 && !v->exhausted) {
        int frames = std::min(int(sizeof buf / sizeof buf[0]) / v->channels, additional / frameBytes + 1);
        int got = stb_vorbis_get_samples_float_interleaved(v->vorbis, v->channels, buf, frames * v->channels);
        if (got == 0) {
            if (v->loop && stb_vorbis_seek_start(v->vorbis))
                continue;
            v->exhausted = true;
            break;
        }
        SDL_PutAudioStreamData(stream, buf, got * frameBytes);
        additional -= got * frameBytes;
    }
}

Audio::Handle Audio::play(std::vector<uint8_t> ogg, const std::string& name, bool loop, float gain) {
    if (!device_)
        return 0;
    auto v = std::make_unique<Voice>();
    v->name = name;
    v->data = std::move(ogg);
    int err = 0;
    v->vorbis = stb_vorbis_open_memory(v->data.data(), int(v->data.size()), &err, nullptr);
    if (!v->vorbis) {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "cannot decode %s (stb_vorbis error %d)", name.c_str(), err);
        return 0;
    }
    stb_vorbis_info info = stb_vorbis_get_info(v->vorbis);
    v->channels = info.channels;
    v->loop = loop;
    v->gain = v->fadeTo = gain;

    SDL_AudioSpec src{SDL_AUDIO_F32, info.channels, int(info.sample_rate)};
    v->stream = SDL_CreateAudioStream(&src, nullptr);
    if (!v->stream) {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "SDL_CreateAudioStream: %s", SDL_GetError());
        return 0;
    }
    SDL_SetAudioStreamGain(v->stream, gain);
    SDL_SetAudioStreamGetCallback(v->stream, feed, v.get());
    if (!SDL_BindAudioStream(device_, v->stream)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_AUDIO, "SDL_BindAudioStream: %s", SDL_GetError());
        return 0;
    }
    Handle h = next_++;
    voices_[h] = std::move(v);
    return h;
}

void Audio::stop(Handle h) { voices_.erase(h); }

bool Audio::playing(Handle h) const { return voices_.count(h) != 0; }

float Audio::gain(Handle h) const {
    auto it = voices_.find(h);
    return it == voices_.end() ? 0 : it->second->gain;
}

void Audio::setGain(Handle h, float gain) {
    auto it = voices_.find(h);
    if (it == voices_.end())
        return;
    Voice& v = *it->second;
    v.fading = false;
    v.gain = v.fadeTo = gain;
    SDL_SetAudioStreamGain(v.stream, gain);
}

void Audio::fade(Handle h, float target, double seconds, bool stopAtEnd) {
    auto it = voices_.find(h);
    if (it == voices_.end())
        return;
    Voice& v = *it->second;
    if (seconds <= 0) {
        setGain(h, target);
        if (stopAtEnd)
            stop(h);
        return;
    }
    v.fading = true;
    v.fadeFrom = v.gain;
    v.fadeTo = target;
    v.fadeTime = 0;
    v.fadeLength = seconds;
    v.stopAtEnd = stopAtEnd;
}

void Audio::update(double dt) {
    for (auto it = voices_.begin(); it != voices_.end();) {
        Voice& v = *it->second;
        if (v.fading) {
            v.fadeTime += dt;
            double k = std::min(1.0, v.fadeTime / v.fadeLength);
            v.gain = float(v.fadeFrom + (v.fadeTo - v.fadeFrom) * k);
            SDL_SetAudioStreamGain(v.stream, v.gain);
            if (k >= 1) {
                v.fading = false;
                if (v.stopAtEnd) {
                    it = voices_.erase(it);
                    continue;
                }
            }
        }
        // Finished once the decoder ran dry and SDL has played what was queued.
        // The flush releases the few frames the resampler holds back as
        // look-ahead; without it the queue never fully drains.
        if (v.exhausted) {
            if (!v.flushed) {
                SDL_FlushAudioStream(v.stream);
                v.flushed = true;
            }
            if (SDL_GetAudioStreamQueued(v.stream) == 0) {
                it = voices_.erase(it);
                continue;
            }
        }
        ++it;
    }
}
