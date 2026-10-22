#pragma once

// Bitmap fonts: 512x512 RGBA atlases with a 16x16 grid of 32px cells in
// Windows-1252 order (font-michael.tga, font-peter.tga, font-menu.tga, ...).
// There is no width table, so glyph widths are measured from the alpha
// channel when the font is loaded.

#include <array>
#include <string>
#include <vector>

#include "renderer.hpp"

class Resources;

class Font {
public:
    bool load(Renderer& r, Resources& res, const std::string& name);
    bool loaded() const { return texture_.id != 0; }
    const std::string& name() const { return name_; }

    float width(const std::string& s, float scale) const;
    float lineHeight(float scale) const { return kCell * kLineSpacing * scale; }

    // Greedy word wrap to `maxWidth` canvas units.
    std::vector<std::string> wrap(const std::string& s, float maxWidth, float scale) const;
    void draw(Renderer& r, const std::string& s, float x, float y, float scale, float alpha = 1) const;

private:
    static constexpr int kCell = 32, kGrid = 16;
    static constexpr float kLineSpacing = 0.85f;
    static constexpr int kTracking = 1;  // extra pixels between glyphs

    struct Glyph { int left = 0, width = 0; };
    std::array<Glyph, 256> glyphs_{};
    Renderer::Texture texture_;
    std::string name_;
};
