#include "images.hpp"

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_TGA
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

std::optional<Image> decodeImage(const std::vector<uint8_t>& data) {
    int w, h, n;
    stbi_uc* p = stbi_load_from_memory(data.data(), int(data.size()), &w, &h, &n, 4);
    if (!p)
        return std::nullopt;
    Image img{w, h, std::vector<uint8_t>(p, p + size_t(w) * h * 4)};
    stbi_image_free(p);
    return img;
}

bool writePng(const char* path, int width, int height, const uint8_t* rgba, bool flipVertically) {
    stbi_flip_vertically_on_write(flipVertically);
    return stbi_write_png(path, width, height, 4, rgba, width * 4) != 0;
}
