#pragma once

#include <cstdint>
#include <optional>
#include <vector>

// Decoded image, always RGBA8, top row first.
struct Image {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> pixels;
};

std::optional<Image> decodeImage(const std::vector<uint8_t>& data);
bool writePng(const char* path, int width, int height, const uint8_t* rgba, bool flipVertically);
