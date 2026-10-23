#include "newproject.hpp"

#include <imgui.h>
#include <stb_image_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <regex>
#include <sstream>

#include "constants_text.hpp"
#include "engine.hpp"
#include "images.hpp"
#include "luadoc.hpp"
#include "renderer.hpp"

namespace editor {

namespace {

namespace fs = std::filesystem;

// ---- drawing -------------------------------------------------------------

using Rgb = std::array<float, 3>;

Image blank(int w, int h, Rgb c = {0, 0, 0}, float a = 0) {
    Image img{w, h, std::vector<uint8_t>(size_t(w) * size_t(h) * 4)};
    for (size_t i = 0; i < img.pixels.size(); i += 4) {
        img.pixels[i] = uint8_t(c[0] * 255), img.pixels[i + 1] = uint8_t(c[1] * 255);
        img.pixels[i + 2] = uint8_t(c[2] * 255), img.pixels[i + 3] = uint8_t(a * 255);
    }
    return img;
}

// Coverage of a shape, 0..1 per pixel; shapes are filled with 4x4 samples.
struct Mask {
    int w, h;
    std::vector<float> v;
    Mask(int w_, int h_) : w(w_), h(h_), v(size_t(w_) * size_t(h_), 0.0f) {}
    float& at(int x, int y) { return v[size_t(y) * size_t(w) + size_t(x)]; }
    float get(int x, int y) const { return x < 0 || y < 0 || x >= w || y >= h ? 0 : v[size_t(y) * size_t(w) + size_t(x)]; }

    template <class Inside>
    void fill(float x0, float y0, float x1, float y1, Inside inside) {
        for (int y = std::max(0, int(y0)); y < std::min(h, int(y1) + 1); ++y)
            for (int x = std::max(0, int(x0)); x < std::min(w, int(x1) + 1); ++x) {
                int n = 0;
                for (int sy = 0; sy < 4; ++sy)
                    for (int sx = 0; sx < 4; ++sx) n += inside(x + (sx + 0.5f) / 4, y + (sy + 0.5f) / 4);
                at(x, y) = std::max(at(x, y), n / 16.0f);
            }
    }
    void polygon(const std::vector<std::pair<float, float>>& p) {
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (auto [x, y] : p) x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
        fill(x0, y0, x1, y1, [&](float x, float y) {
            bool in = false;
            for (size_t i = 0, j = p.size() - 1; i < p.size(); j = i++)
                if ((p[i].second > y) != (p[j].second > y) &&
                    x < (p[j].first - p[i].first) * (y - p[i].second) / (p[j].second - p[i].second) + p[i].first)
                    in = !in;
            return in;
        });
    }
    void circle(float cx, float cy, float r, float inner = 0) {
        fill(cx - r, cy - r, cx + r, cy + r, [&](float x, float y) {
            float d = std::hypot(x - cx, y - cy);
            return d <= r && d >= inner;
        });
    }
    void rect(float x0, float y0, float x1, float y1) {
        fill(x0, y0, x1, y1, [&](float x, float y) { return x >= x0 && x < x1 && y >= y0 && y < y1; });
    }
    Mask grown(int r) const {
        Mask m(w, h);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                float best = 0;
                for (int dy = -r; dy <= r; ++dy)
                    for (int dx = -r; dx <= r; ++dx)
                        if (dx * dx + dy * dy <= r * r) best = std::max(best, get(x + dx, y + dy));
                m.at(x, y) = best;
            }
        return m;
    }
};

void paint(Image& img, const Mask& m, Rgb c, float alpha = 1) {
    for (int y = 0; y < img.height; ++y)
        for (int x = 0; x < img.width; ++x) {
            float a = m.get(x, y) * alpha;
            if (a <= 0) continue;
            uint8_t* p = &img.pixels[(size_t(y) * size_t(img.width) + size_t(x)) * 4];
            float da = p[3] / 255.0f, oa = a + da * (1 - a);
            for (int k = 0; k < 3; ++k)
                p[k] = uint8_t(std::clamp((c[size_t(k)] * 255 * a + p[k] * da * (1 - a)) / std::max(oa, 1e-6f), 0.0f, 255.0f));
            p[3] = uint8_t(oa * 255);
        }
}

// White with a dark outline: how the placeholder cursors and icons read on any picture.
void outlined(Image& img, const Mask& m, Rgb c = {1, 1, 1}) {
    paint(img, m.grown(2), {0.05f, 0.05f, 0.05f}, 0.9f);
    paint(img, m, c);
}

// ---- the font ----------------------------------------------------------------

// The engine's font layout (NOTES.md "Fonts"): a 512 x 512 atlas of 16 x 16
// cells of 32 pixels in Windows-1252 order, white glyphs in the alpha,
// centred in the cell with the baseline near row 23. Rasterised here from
// Dear ImGui's built-in ProggyClean (a free monospace font).
Image makeFont() {
    ImFontAtlas atlas;
    ImFontConfig cfg;
    cfg.SizePixels = 30;
    cfg.OversampleH = cfg.OversampleV = 1;
    static const ImWchar ranges[] = {0x20, 0xFF, 0x2013, 0x2026, 0};
    cfg.GlyphRanges = ranges;
    ImFont* font = atlas.AddFontDefault(&cfg);
    unsigned char* px;
    int aw, ah;
    atlas.GetTexDataAsAlpha8(&px, &aw, &ah);
    Image img = blank(512, 512, {1, 1, 1}, 0);
    // Windows-1252's 0x80-0x9F, where it differs from Latin-1.
    static const uint16_t cp1252[32] = {0x20AC, 0, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                        0x2039, 0x0152, 0, 0x017D, 0, 0, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                        0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0, 0x017E, 0x0178};
    for (int ch = 32; ch < 256; ++ch) {
        ImWchar u = ImWchar(ch >= 0x80 && ch < 0xA0 ? cp1252[ch - 0x80] : ch);
        // Typographic quotes and dashes fall back to their plain forms.
        if (u == 0x2018 || u == 0x2019) u = '\'';
        if (u == 0x201C || u == 0x201D) u = '"';
        if (u == 0x2013 || u == 0x2014) u = '-';
        const ImFontGlyph* g = u ? font->FindGlyphNoFallback(u) : nullptr;
        if (!g || !g->Visible) continue;
        int cx = (ch % 16) * 32, cy = (ch / 16) * 32;
        float penX = cx + (32 - g->AdvanceX) / 2, top = cy + 23 - font->Ascent;
        int gx0 = int(g->U0 * aw), gy0 = int(g->V0 * ah), gx1 = int(g->U1 * aw), gy1 = int(g->V1 * ah);
        for (int y = gy0; y < gy1; ++y)
            for (int x = gx0; x < gx1; ++x) {
                int tx = int(penX + g->X0) + (x - gx0), ty = int(top + g->Y0) + (y - gy0);
                if (tx < cx || ty < cy || tx >= cx + 32 || ty >= cy + 32) continue;
                img.pixels[(size_t(ty) * 512 + size_t(tx)) * 4 + 3] = px[y * aw + x];
            }
    }
    return img;
}

// Text drawn with the atlas, `size` pixels per cell, monospace like the engine (10/16 advance).
void text(Image& img, const Image& font, const std::string& s, float x, float y, float size, Rgb c, bool centred = true,
          bool outline = false) {
    const float adv = size * 10 / 16;
    if (centred) x -= adv * float(s.size()) / 2;
    Mask m(img.width, img.height);
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char ch = static_cast<unsigned char>(s[i]);
        int cx = (ch % 16) * 32, cy = (ch / 16) * 32;
        float ox = x + float(i) * adv - (size - adv) / 2;
        for (int ty = std::max(0, int(y)); ty < std::min(img.height, int(y + size)); ++ty)
            for (int tx = std::max(0, int(ox)); tx < std::min(img.width, int(ox + size)); ++tx) {
                float u = (tx - ox) / size * 32, v = (ty - y) / size * 32;
                int sx = std::clamp(int(u), 0, 31), sy = std::clamp(int(v), 0, 31);
                float a = font.pixels[(size_t(cy + sy) * 512 + size_t(cx + sx)) * 4 + 3] / 255.0f;
                m.at(tx, ty) = std::max(m.at(tx, ty), a);
            }
    }
    if (outline) paint(img, m.grown(2), {0, 0, 0}, 0.8f);
    paint(img, m, c);
}

// ---- cursors -------------------------------------------------------------

// 64 x 64, with the point where the engine puts the pointer (NOTES.md
// "Cursors"): 8 px in for menu / forward / look, 24 for normal / use /
// zoom-out, the centre for wait and the arrows.
Image makeCursor(const std::string& kind) {
    Image img = blank(64, 64, {1, 1, 1}, 0);
    Mask m(64, 64);
    auto arrow = [&](float x, float y, float s) {  // the usual pointer, tip at (x, y)
        m.polygon({{x, y}, {x, y + 26 * s}, {x + 7 * s, y + 20 * s}, {x + 12 * s, y + 30 * s}, {x + 17 * s, y + 28 * s},
                   {x + 12 * s, y + 18 * s}, {x + 20 * s, y + 18 * s}});
    };
    if (kind == "normal") {
        arrow(24, 24, 1);
    } else if (kind == "menu") {
        arrow(8, 8, 1);
    } else if (kind == "forward") {  // a pointer and a chevron ahead of it: walk
        arrow(8, 8, 0.8f);
        m.polygon({{36, 24}, {52, 40}, {46, 46}, {36, 36}, {26, 46}, {20, 40}});
        m.polygon({{36, 38}, {50, 52}, {44, 58}, {36, 50}, {28, 58}, {22, 52}});
    } else if (kind == "look") {  // a magnifier with the lens on the pointer
        m.circle(14, 14, 11, 7);
        m.polygon({{21, 24}, {25, 20}, {44, 39}, {40, 43}});
    } else if (kind == "use") {  // a ring round the pointer and a dot: take / use
        m.circle(24, 24, 13, 9);
        m.circle(24, 24, 4);
    } else if (kind == "back") {  // a downward chevron: step back out of the close-up
        m.polygon({{12, 16}, {24, 28}, {36, 16}, {42, 22}, {24, 40}, {6, 22}});
    } else if (kind == "wait") {  // an hourglass
        m.polygon({{18, 14}, {46, 14}, {46, 18}, {35, 32}, {46, 46}, {46, 50}, {18, 50}, {18, 46}, {29, 32}, {18, 18}});
    } else if (kind == "left") {
        m.polygon({{14, 32}, {38, 14}, {38, 50}});
    } else if (kind == "right") {
        m.polygon({{50, 32}, {26, 14}, {26, 50}});
    } else if (kind == "up") {
        m.polygon({{32, 14}, {50, 38}, {14, 38}});
    } else if (kind == "down") {
        m.polygon({{32, 50}, {50, 26}, {14, 26}});
    }
    outlined(img, m);
    return img;
}

// ---- the test room -------------------------------------------------------

// Something drawn on a face, and the hotspot the script places over it.
struct Feature {
    int face;  // 1..6 (SC_FRONT ...)
    Rect rect;
    std::string label;
    Rgb colour;
};

const char* const kFaceNames[] = {"FRONT", "BACK", "LEFT", "RIGHT", "UP", "DOWN"};

// A face of an empty grey test room: walls with a grid and the face's name,
// a floor and a ceiling; features are panels with their label.
Image makeFace(const Image& font, const std::string& node, int face, const std::vector<Feature>& features, int size) {
    Image img = blank(size, size, {0, 0, 0}, 1);
    const float k = float(size) / 1024;
    Rgb wall = face == 5 ? Rgb{0.62f, 0.62f, 0.66f} : face == 6 ? Rgb{0.30f, 0.27f, 0.25f} : Rgb{0.55f, 0.53f, 0.50f};
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            uint8_t* p = &img.pixels[(size_t(y) * size_t(size) + size_t(x)) * 4];
            float fy = y / k, fx = x / k;
            Rgb c = wall;
            if (face <= 4 && fy > 768) c = {0.30f, 0.27f, 0.25f};          // the floor's edge
            if (face <= 4 && fy < 96) c = {0.62f, 0.62f, 0.66f};           // the ceiling's edge
            float shade = 1 - 0.15f * std::fabs(fy - 512) / 512;           // a little depth
            bool grid = std::fmod(fx, 128) < 2 / k || std::fmod(fy, 128) < 2 / k;
            for (size_t i = 0; i < 3; ++i) p[i] = uint8_t(std::clamp(c[i] * shade * (grid ? 0.8f : 1) * 255, 0.0f, 255.0f));
        }
    for (const auto& f : features) {
        if (f.face != face) continue;
        Mask m(size, size);
        m.rect(f.rect.x * k, f.rect.y * k, (f.rect.x + f.rect.w) * k, (f.rect.y + f.rect.h) * k);
        paint(img, m.grown(int(4 * k)), {0.1f, 0.1f, 0.1f});
        paint(img, m, f.colour);
        text(img, font, f.label, (f.rect.x + f.rect.w / 2.0f) * k, (f.rect.y + f.rect.h / 2.0f - 24) * k, 48 * k,
             {1, 1, 1});
    }
    std::string label = node + " " + kFaceNames[face - 1];
    text(img, font, label, 512 * k, (face <= 4 ? 140 : 480) * k, 64 * k, {0.15f, 0.15f, 0.15f});
    return img;
}

// The close-up of the desk, 1024 x 640 like the DC's HD zooms; hotspots are in
// 800 x 472 units over the whole image.
Image makeDesk(const Image& font, bool withKey, const Rect& key) {
    Image img = blank(1024, 640, {0.35f, 0.22f, 0.12f}, 1);
    for (int y = 0; y < 640; ++y)
        for (int x = 0; x < 1024; ++x) {  // wood grain
            uint8_t* p = &img.pixels[(size_t(y) * 1024 + size_t(x)) * 4];
            float g = 0.85f + 0.15f * std::sin(y * 0.09f + std::sin(x * 0.013f) * 3);
            p[0] = uint8_t(p[0] * g), p[1] = uint8_t(p[1] * g), p[2] = uint8_t(p[2] * g);
        }
    const float sx = 1024.0f / 800, sy = 640.0f / 472;
    if (withKey) {
        Mask m(1024, 640);
        float cx = (key.x + key.w * 0.25f) * sx, cy = (key.y + key.h / 2.0f) * sy;
        m.circle(cx, cy, 30, 14);
        m.rect(cx + 28, cy - 7, (key.x + key.w * 0.95f) * sx, cy + 7);
        m.rect((key.x + key.w * 0.75f) * sx, cy, (key.x + key.w * 0.82f) * sx, cy + 26);
        m.rect((key.x + key.w * 0.87f) * sx, cy, (key.x + key.w * 0.94f) * sx, cy + 20);
        paint(img, m.grown(3), {0.15f, 0.1f, 0.0f});
        paint(img, m, {0.95f, 0.78f, 0.3f});
    }
    text(img, font, withKey ? "THE DESK - A KEY" : "THE DESK", 512, 40, 48, {1, 0.95f, 0.85f}, true, true);
    text(img, font, "click below to step back", 512, 560, 32, {1, 1, 1}, true, true);
    return img;
}

Image makeKeyIcon() {
    Image img = blank(128, 128, {1, 1, 1}, 0);
    Mask m(128, 128);
    m.circle(36, 64, 22, 10);
    m.rect(56, 58, 112, 70);
    m.rect(92, 64, 100, 86);
    m.rect(104, 64, 112, 80);
    outlined(img, m, {0.95f, 0.78f, 0.3f});
    return img;
}

// A screen-sized picture with a title (the splash, the menu's background).
Image makeScreen(const Image& font, const std::string& title, const std::string& sub, int w, int h) {
    Image img = blank(w, h, {0, 0, 0}, 1);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            uint8_t* p = &img.pixels[(size_t(y) * size_t(w) + size_t(x)) * 4];
            float t = float(y) / float(h);
            p[0] = uint8_t(20 + 30 * t), p[1] = uint8_t(24 + 26 * t), p[2] = uint8_t(40 + 30 * t);
        }
    text(img, font, title, w / 2.0f, h * 0.38f, 48, {0.9f, 0.9f, 0.95f});
    if (!sub.empty()) text(img, font, sub, w / 2.0f, h * 0.38f + 64, 24, {0.6f, 0.65f, 0.75f});
    return img;
}

// A menu entry: 128 x 64, the word in the left of the middle, as the original's.
Image makeMenuWord(const Image& font, const std::string& word) {
    Image img = blank(128, 64, {1, 1, 1}, 0);
    text(img, font, word, 16, 18, 28, {1, 1, 1}, false);
    return img;
}

Image makeLogo(const Image& font, const std::string& title) {
    Image img = blank(512, 256, {1, 1, 1}, 0);
    text(img, font, title, 256, 100, 56, {1, 1, 1}, true, true);
    return img;
}

// The sky's horizon gradient: white, transparent at the top, opaque from three quarters down.
Image makeGradient() {
    Image img = blank(256, 256, {1, 1, 1}, 0);
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x)
            img.pixels[(size_t(y) * 256 + size_t(x)) * 4 + 3] =
                uint8_t(std::clamp((y / 256.0f - 0.25f) / 0.5f, 0.0f, 1.0f) * 255);
    return img;
}

Image mirrored(const Image& in) {
    Image out = in;
    for (int y = 0; y < in.height; ++y)
        for (int x = 0; x < in.width; ++x)
            std::memcpy(&out.pixels[(size_t(y) * size_t(in.width) + size_t(x)) * 4],
                        &in.pixels[(size_t(y) * size_t(in.width) + size_t(in.width - 1 - x)) * 4], 4);
    return out;
}

bool writeImage(const fs::path& path, const Image& img) {
    auto ext = path.extension();
    if (ext == ".tga") return stbi_write_tga(path.string().c_str(), img.width, img.height, 4, img.pixels.data()) != 0;
    std::vector<uint8_t> rgb(size_t(img.width) * size_t(img.height) * 3);
    for (size_t i = 0, j = 0; i < img.pixels.size(); i += 4, j += 3) std::memcpy(&rgb[j], &img.pixels[i], 3);
    return stbi_write_jpg(path.string().c_str(), img.width, img.height, 3, rgb.data(), 92) != 0;
}

// ---- the template script -------------------------------------------------

std::string place(const Rect& r, const char* face) {
    return "scPlace(" + std::to_string(r.x) + ", " + std::to_string(r.y) + ", " + std::to_string(r.w) + ", " +
           std::to_string(r.h) + ", " + face + ")";
}

}  // namespace

std::string constantsScript() {
    // Every `NAME = value` in the engine's constants.hpp becomes SC_NAME = value.
    std::string out = "-- SCream constants (the values the engine's sc* functions expect).\n\n";
    std::string text = kConstantsText;
    std::regex def(R"(\b([A-Z][A-Z0-9_]*)\s*=\s*(-?[0-9]+(\.[0-9]+)?)f?\b)");
    for (auto it = std::sregex_iterator(text.begin(), text.end(), def); it != std::sregex_iterator(); ++it) {
        std::string name = (*it)[1];
        if (name == "MENU_CURSOR") continue;  // the engine's alias for SC_MENU
        if (name.size() == 4 && name[0] == 'A' && name[1] == '3') name = name.substr(1);  // A3DA -> SC_3DA
        if (name == "SCREEN_W" || name == "SCREEN_H") continue;
        out += "SC_" + name + " = " + std::string((*it)[2]) + "\n";
    }
    return out;
}

bool packArchive(const fs::path& assets, const fs::path& out, std::string& error) {
    struct File {
        std::string name;
        uint32_t type;
        std::vector<char> data;
    };
    std::vector<File> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(assets, ec)) {
        std::string ext = e.path().extension().string();
        for (char& c : ext) c = char(std::tolower(static_cast<unsigned char>(c)));
        uint32_t type = ext == ".jpg" ? 1 : ext == ".tga" ? 2 : ext == ".ogg" ? 3 : 0;
        if (!type || !e.is_regular_file()) continue;
        std::string name = e.path().filename().string();
        if (name.size() > 79) {
            error = "name longer than 79 characters: " + name;
            return false;
        }
        std::ifstream f(e.path(), std::ios::binary);
        files.push_back({name, type, std::vector<char>(std::istreambuf_iterator<char>(f), {})});
    }
    std::sort(files.begin(), files.end(), [](auto& a, auto& b) { return a.name < b.name; });
    // NOTES.md ".res archive format": a 256-byte title, the count, 92-byte entries, then the files.
    std::vector<char> head(256, 0);
    std::memcpy(head.data(), "SCream resource file", 20);
    auto u32 = [](std::vector<char>& v, uint32_t x) {
        for (int i = 0; i < 4; ++i) v.push_back(char((x >> (8 * i)) & 0xff));
    };
    u32(head, uint32_t(files.size()));
    uint32_t offset = uint32_t(256 + 4 + 92 * files.size());
    for (const auto& f : files) {
        std::vector<char> name(80, 0);
        std::memcpy(name.data(), f.name.data(), f.name.size());
        head.insert(head.end(), name.begin(), name.end());
        u32(head, f.type);
        u32(head, offset);
        u32(head, uint32_t(f.data.size()));
        offset += uint32_t(f.data.size());
    }
    std::ofstream o(out, std::ios::binary);
    o.write(head.data(), std::streamsize(head.size()));
    for (const auto& f : files) o.write(f.data.data(), std::streamsize(f.data.size()));
    if (!o) {
        error = "can't write " + out.string();
        return false;
    }
    return true;
}

bool importPanorama(const fs::path& image, const fs::path& outDir, const std::string& base, int size, std::string& error) {
    std::ifstream f(image, std::ios::binary);
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(f)), {});
    auto img = decodeImage(data);
    if (!img) {
        error = "can't read " + image.string();
        return false;
    }
    auto sample = [&](float u, float v, int k) {  // bilinear, wrapping sideways
        float x = u * img->width - 0.5f, y = std::clamp(v * img->height - 0.5f, 0.0f, img->height - 1.0f);
        int x0 = int(std::floor(x)), y0 = int(y), y1 = std::min(y0 + 1, img->height - 1);
        float fx = x - x0, fy = y - y0;
        auto px = [&](int xx, int yy) {
            xx = ((xx % img->width) + img->width) % img->width;
            return float(img->pixels[(size_t(yy) * size_t(img->width) + size_t(xx)) * 4 + size_t(k)]);
        };
        return (px(x0, y0) * (1 - fx) + px(x0 + 1, y0) * fx) * (1 - fy) + (px(x0, y1) * (1 - fx) + px(x0 + 1, y1) * fx) * fy;
    };
    static const char suffix[] = "fblrud";
    for (int face = 1; face <= 6; ++face) {
        const FaceBasis& b = faceBasis(face);
        Image out = blank(size, size, {0, 0, 0}, 1);
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                float u = 2 * (x + 0.5f) / size - 1, v = 2 * (y + 0.5f) / size - 1;
                float dx = b.center.x + b.right.x * u + b.down.x * v, dy = b.center.y + b.right.y * u + b.down.y * v,
                      dz = b.center.z + b.right.z * u + b.down.z * v;
                // Front (-z) is the middle of the panorama; +90 degrees of yaw is the right.
                float lon = std::atan2(dx, -dz), lat = std::atan2(dy, std::hypot(dx, dz));
                float eu = 0.5f + lon / (2 * 3.14159265f), ev = 0.5f - lat / 3.14159265f;
                for (int k = 0; k < 3; ++k)
                    out.pixels[(size_t(y) * size_t(size) + size_t(x)) * 4 + size_t(k)] = uint8_t(std::clamp(sample(eu, ev, k), 0.0f, 255.0f));
            }
        auto path = outDir / (base + suffix[face - 1] + ".jpg");
        if (!writeImage(path, out)) {
            error = "can't write " + path.string();
            return false;
        }
    }
    return true;
}

bool createProject(const fs::path& dir, const std::string& title, std::string& error) {
    std::error_code ec;
    if (fs::exists(dir) && !fs::is_empty(dir, ec)) {
        error = dir.string() + " is not empty";
        return false;
    }
    fs::path assets = dir / "assets";
    fs::create_directories(assets, ec);
    if (ec) {
        error = ec.message();
        return false;
    }
    bool ok = true;
    auto save = [&](const std::string& name, const Image& img) { ok = writeImage(assets / name, img) && ok; };

    Image font = makeFont();
    save("font-game.tga", font);
    for (const char* c : {"normal", "menu", "forward", "look", "use", "back", "wait", "left", "right", "up", "down"})
        save(std::string("cur-") + c + ".tga", makeCursor(c));
    save("menu-logo.tga", makeLogo(font, title));
    const char* words[][2] = {{"menu-back.tga", "back"}, {"menu-new.tga", "new"},   {"menu-set.tga", "setup"},
                              {"menu-load.tga", "load"}, {"menu-save.tga", "save"}, {"menu-quit.tga", "quit"}};
    for (auto& w : words) save(w[0], makeMenuWord(font, w[1]));
    save("back-menu.tga", makeScreen(font, "", "", 800, 472));
    save("back-loading.tga", makeScreen(font, title, "loading...", 800, 472));
    save("gradient.tga", makeGradient());
    Image clouds = cloudTexture2006();
    save("clouds1.tga", clouds);
    save("clouds2.tga", mirrored(clouds));
    save("inv-key.tga", makeKeyIcon());

    // Two nodes and a close-up; the panels drawn on the faces are the hotspots.
    const Rect door{384, 320, 256, 448}, toDesk{320, 560, 384, 208}, desk{192, 480, 640, 288}, back{320, 560, 384, 208};
    const Rect key{330, 190, 150, 90}, out{0, 400, 800, 72};
    std::vector<Feature> n1 = {{1, door, "DOOR", {0.35f, 0.22f, 0.14f}}, {4, toDesk, "TO THE DESK", {0.2f, 0.4f, 0.25f}}};
    std::vector<Feature> n2 = {{1, desk, "DESK", {0.42f, 0.27f, 0.15f}}, {2, back, "BACK", {0.2f, 0.4f, 0.25f}}};
    for (int f = 1; f <= 6; ++f) {
        const char s = "fblrud"[f - 1];
        save(std::string("studio_n1") + s + ".jpg", makeFace(font, "n1", f, n1, 1024));
        save(std::string("studio_n2") + s + ".jpg", makeFace(font, "n2", f, n2, 1024));
    }
    save("studio-desk.jpg", makeDesk(font, true, key));
    save("studio-desk_empty.jpg", makeDesk(font, false, key));
    if (!ok) {
        error = "can't write the assets in " + assets.string();
        return false;
    }

    // The script, laid out as the original's: text, handlers, builders, boot.
    std::string q = doc::quote(title);
    std::ostringstream s;
    s << "-- " << title << ": a SCream game.\n"
      << "-- Text first (one global per line), then the handlers, the world, and the boot code.\n\n"
      << "msg0001 = \"An empty room. The paint is still fresh.\"\n"
      << "msg0002 = \"The door is locked.\"\n"
      << "msg0003 = \"A small brass key.\"\n"
      << "msg0004 = \"The key turns, and the door opens...\"\n"
      << "msg0005 = \"The end. Thank you for playing.\"\n"
      << "msg0006 = \"That doesn't work.\"\n"
      << "msg0007 = \"Plain wood.\"\n\n"
      << "function newGame()\n"
      << "  scSwitch(SC_ROOM, \"studio\")\n"
      << "  scFeedback(msg0001)\n"
      << "end\n\n"
      << "function save()\n"
      << "  return " << q << "\n"
      << "end\n\n"
      << "function takeKey()\n"
      << "  scDisable(SC_OBJECT)\n"
      << "  scTextureUpdate(SC_ZOOM, \"studio-desk_empty.jpg\", 1)\n"
      << "  scBind(SC_ITEM, \"key\")\n"
      << "  scInventoryAdd()\n"
      << "  scFeedback(msg0003)\n"
      << "end\n\n"
      << "function openDoor()\n"
      << "  scBind(SC_ITEM, \"key\")\n"
      << "  scInventoryRemove()\n"
      << "  scLock()\n"
      << "  scFeedback(msg0004)\n"
      << "  scSleep(3)\n"
      << "  finale()\n"
      << "end\n\n"
      << "function finale()\n"
      << "  scSwitch(SC_ZOOM, \"black\")\n"
      << "  scFeedback(msg0005)\n"
      << "  scSleep(4)\n"
      << "  scAbort()\n"
      << "end\n\n"
      << "function createInventory()\n"
      << "  scCreate(SC_ITEM, \"key\")\n"
      << "  scInventorySet(\"inv-key.tga\", \"Brass Key\", msg0006, \"\")\n"
      << "end\n\n"
      << "function createStudio()\n"
      << "  scCreate(SC_ROOM, \"studio\")\n"
      << "  scBegin(SC_ROOM)\n\n";
    auto node = [&](const char* name, const char* prefix) {
        s << "  scCreate(SC_NODE, \"" << name << "\")\n  scBegin(SC_NODE)\n";
        for (int f = 0; f < 6; ++f)
            s << "  scTexture(SC_" << kFaceNames[f] << ", \"" << prefix << "fblrud"[f] << ".jpg\")\n";
        s << "\n";
    };
    node("n1", "studio_n1");
    s << "  scCreate(SC_OBJECT, \"door\")\n  " << place(door, "SC_FRONT") << "\n"
      << "  scOnHover(SC_USE) scOnClick(SC_FEEDBACK, msg0002)\n"
      << "  scReaction(\"key\", SC_CUSTOM, \"openDoor\")\n\n"
      << "  scCreate(SC_OBJECT, \"to_n2\")\n  " << place(toDesk, "SC_RIGHT") << "\n"
      << "  scOnHover(SC_FORWARD) scOnClick(SC_NODE, \"n2\")\n"
      << "  scEnd(SC_NODE)\n\n";
    node("n2", "studio_n2");
    s << "  scCreate(SC_OBJECT, \"desk\")\n  " << place(desk, "SC_FRONT") << "\n"
      << "  scOnHover(SC_LOOK) scOnClick(SC_ZOOM, \"desk\")\n\n"
      << "  scCreate(SC_OBJECT, \"to_n1\")\n  " << place(back, "SC_BACK") << "\n"
      << "  scOnHover(SC_FORWARD) scOnClick(SC_NODE, \"n1\")\n"
      << "  scEnd(SC_NODE)\n\n"
      << "  scCreate(SC_ZOOM, \"desk\")\n  scBegin(SC_ZOOM)\n"
      << "  scTexture(SC_ZOOM, \"studio-desk.jpg\")\n\n"
      << "  scCreate(SC_OBJECT, \"key\")\n  " << place(key, "SC_ZOOM") << "\n"
      << "  scOnHover(SC_USE) scOnClick(SC_CUSTOM, \"takeKey\")\n\n"
      << "  scCreate(SC_OBJECT, \"surface\")\n  " << place({60, 60, 680, 330}, "SC_ZOOM") << "\n"
      << "  scOnHover(SC_LOOK) scOnClick(SC_FEEDBACK, msg0007)\n\n"
      << "  scCreate(SC_OBJECT, \"out\")\n  " << place(out, "SC_ZOOM") << "\n"
      << "  scOnHover(SC_ZOOMOUT) scOnClick(SC_ZOOMOUT, \"\")\n"
      << "  scEnd(SC_ZOOM)\n\n"
      << "  scEnd(SC_ROOM)\n"
      << "end\n\n"
      << "scSet(SC_RES_PATH, \".\\\\\")\n"
      << "scSet(SC_RES_FILE, \"game.res\")\n"
      << "scSet(SC_SPLASH, \"back-loading.tga\")\n"
      << "scInit()\n"
      << "scCursor(SC_MENU, \"cur-menu.tga\")\n"
      << "scCursor(SC_ZOOMOUT, \"cur-back.tga\")\n"
      << "scCursor(SC_FORWARD, \"cur-forward.tga\")\n"
      << "scCursor(SC_LOOK, \"cur-look.tga\")\n"
      << "scCursor(SC_NORMAL, \"cur-normal.tga\")\n"
      << "scCursor(SC_USE, \"cur-use.tga\")\n"
      << "scCursor(SC_WAIT, \"cur-wait.tga\")\n"
      << "scCursor(SC_LEFT, \"cur-left.tga\")\n"
      << "scCursor(SC_RIGHT, \"cur-right.tga\")\n"
      << "scCursor(SC_UP, \"cur-up.tga\")\n"
      << "scCursor(SC_DOWN, \"cur-down.tga\")\n"
      << "scFont(\"font-game.tga\")\n"
      << "scMenuSet(\"menu-logo.tga\", \"menu-back.tga\", \"menu-new.tga\", \"menu-set.tga\", \"menu-load.tga\", "
         "\"menu-save.tga\", \"menu-quit.tga\", \"newGame\")\n"
      << "scMenuSetEx(SC_IMAGE, \"back-menu.tga\")\n"
      << "scMenuSetSaveFunc()\n"
      << "createInventory()\n"
      << "createStudio()\n"
      << "scRun()\n"
      << "scTerminate()\n";
    std::ofstream(dir / "game.lua", std::ios::binary) << s.str();
    std::ofstream(dir / "scream.sc", std::ios::binary) << constantsScript();
    // As the shipped scream.cfg; the scripts' own key names (scLoadSettings, NOTES.md).
    std::ofstream(dir / "scream.cfg", std::ios::binary)
        << "; SCream config file\n\n[SCREAM]\n\n"
        << "SCRIPT\t\tgame\t\t; Name of the game file\n"
        << "RESOURCE\tgame.res\t; Resource file\n"
        << "PATH\t\t.\\\\\t\t; Path of the resource file\n"
        << "WIDTH\t\t1024\t\t; Screen width\n"
        << "HEIGHT\t\t768\t\t\t; Screen height\n"
        << "QUALITY\t\t1\t\t\t; Quality level (0=low, 1=high)\n"
        << "FULLSCREEN\t0\t\t\t; Toggle fullscreen (0=windowed, 1=fullscreen)\n"
        << "VSYNC\t\t1\t\t\t; Toggle vertical sync (0=off, 1=on)\n"
        << "SURROUND\t0\t\t\t; Toggle surround music mode (0=off, 1=on)\n"
        << "LOG\t\t\t0\t\t\t; Logging (0=disable, 1=enable)\n";
    return packArchive(assets, dir / "game.res", error);
}

}  // namespace editor
