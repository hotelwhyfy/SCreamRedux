#pragma once

// Bitmap fonts: 512x512 RGBA atlases with a 16x16 grid of 32px cells in
// Windows-1252 order (font-michael.tga, font-peter.tga, font-menu.tga, ...).
//
// scream.exe draws all game text (feedback, subtitles, choices, journal) as
// monospace: one display list per character (0x40bf20) holding a 16x16 pixel
// quad of the whole cell followed by a 10 pixel advance. drawMono() does the
// same. The proportional draw()/width()/wrap() measure glyph widths from the
// alpha channel and serve our own additions (the inventory label).

#include <array>
#include <string>
#include <vector>

#include "renderer.hpp"

class Resources;

struct TextColor {
    float r = 1, g = 1, b = 1;
};

class Font {
public:
    bool load(Renderer& r, Resources& res, const std::string& name);
    bool loaded() const { return texture_.id != 0; }
    const std::string& name() const { return name_; }

    float width(const std::string& s, float scale) const;
    float lineHeight(float scale) const { return kCell * kLineSpacing * scale; }

    // Greedy word wrap to `maxWidth` canvas units.
    std::vector<std::string> wrap(const std::string& s, float maxWidth, float scale) const;
    void draw(Renderer& r, const std::string& s, float x, float y, float scale, float alpha = 1,
              TextColor color = {}) const;

    // Original metrics, in screen pixels scaled by `px` canvas units per pixel.
    // `baseline` is the bottom of the 16 pixel cell, as passed to 0x40c040.
    static constexpr int kMonoCell = 16, kMonoAdvance = 10;
    void drawMono(Renderer& r, const std::string& s, float x, float baseline, float px, float alpha = 1,
                  TextColor color = {}, Blend blend = Blend::Alpha) const;

private:
    static constexpr int kCell = 32, kGrid = 16;
    static constexpr float kLineSpacing = 0.85f;
    static constexpr int kTracking = 1;  // extra pixels between glyphs

    struct Glyph { int left = 0, width = 0; };
    std::array<Glyph, 256> glyphs_{};
    Renderer::Texture texture_;
    std::string name_;
};
