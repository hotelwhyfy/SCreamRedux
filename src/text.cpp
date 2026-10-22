#include "text.hpp"

#include "images.hpp"
#include "resources.hpp"

bool Font::load(Renderer& r, Resources& res, const std::string& name) {
    auto data = res.read(name);
    if (!data)
        return false;
    auto img = decodeImage(*data);
    if (!img || img->width != kCell * kGrid || img->height != kCell * kGrid)
        return false;

    for (int c = 0; c < 256; ++c) {
        int cx = (c % kGrid) * kCell, cy = (c / kGrid) * kCell;
        int lo = kCell, hi = -1;
        for (int y = 0; y < kCell; ++y)
            for (int x = 0; x < kCell; ++x)
                if (img->pixels[(size_t(cy + y) * img->width + cx + x) * 4 + 3] > 24) {
                    lo = std::min(lo, x);
                    hi = std::max(hi, x);
                }
        glyphs_[c] = hi < lo ? Glyph{0, 0} : Glyph{lo, hi - lo + 1};
    }
    glyphs_[' '] = {0, kCell / 4};
    glyphs_[0xA0] = glyphs_[' '];  // non-breaking space

    texture_ = r.upload(name, *img);
    name_ = name;
    return true;
}

float Font::width(const std::string& s, float scale) const {
    float w = 0;
    for (unsigned char c : s)
        w += float(glyphs_[c].width + kTracking);
    return w * scale;
}

std::vector<std::string> Font::wrap(const std::string& s, float maxWidth, float scale) const {
    std::vector<std::string> lines;
    std::string line, word;
    auto flushWord = [&] {
        if (word.empty())
            return;
        std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && width(candidate, scale) > maxWidth) {
            lines.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        word.clear();
    };
    for (char c : s) {
        if (c == ' ') flushWord();
        else if (c == '\n') { flushWord(); lines.push_back(line); line.clear(); }
        else word += c;
    }
    flushWord();
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

void Font::draw(Renderer& r, const std::string& s, float x, float y, float scale, float alpha, TextColor color) const {
    const float inv = 1.0f / (kCell * kGrid);
    for (unsigned char c : s) {
        const Glyph& g = glyphs_[c];
        if (g.width > 0) {
            float u0 = float((c % kGrid) * kCell + g.left) * inv, v0 = float((c / kGrid) * kCell) * inv;
            r.drawImageUV(texture_, x, y, g.width * scale, kCell * scale, u0, v0, u0 + g.width * inv, v0 + kCell * inv, alpha,
                          color.r, color.g, color.b);
        }
        x += float(g.width + kTracking) * scale;
    }
}

void Font::drawMono(Renderer& r, const std::string& s, float x, float baseline, float px, float alpha,
                    TextColor color) const {
    const float inv = 1.0f / kGrid, size = kMonoCell * px;
    for (unsigned char c : s) {
        if (glyphs_[c].width > 0) {
            float u0 = float(c % kGrid) * inv, v0 = float(c / kGrid) * inv;
            r.drawImageUV(texture_, x, baseline - size, size, size, u0, v0, u0 + inv, v0 + inv, alpha, color.r,
                          color.g, color.b);
        }
        x += kMonoAdvance * px;
    }
}
