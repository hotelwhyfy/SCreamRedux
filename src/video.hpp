#pragma once

// Theora cutscene decoder. The game's videos (vid-*.ogg) are silent Ogg
// Theora streams, 4:2:0 at 24 fps; their sound is played separately by the
// scripts with scFXPlay, so only the picture is decoded here.

#include <cstdint>
#include <memory>
#include <vector>

class Video {
public:
    Video();
    ~Video();
    Video(const Video&) = delete;
    Video& operator=(const Video&) = delete;

    bool open(std::vector<uint8_t> ogg);

    // Decode up to the frame due at `seconds` since the start. Returns true
    // when `rgba()` holds a new picture.
    bool advance(double seconds);
    bool finished() const { return finished_; }

    int width() const { return width_; }
    int height() const { return height_; }
    const std::vector<uint8_t>& rgba() const { return rgba_; }

private:
    struct State;
    void convert();

    std::unique_ptr<State> s_;
    std::vector<uint8_t> data_;
    int width_ = 0, height_ = 0;
    double fps_ = 24;
    long frame_ = -1;  // index of the frame in rgba_
    bool finished_ = false;
    std::vector<uint8_t> rgba_;
};
