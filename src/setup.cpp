// The setup launcher (see setup.hpp). Drawn like the game's own setup menu:
// the engine's font, white text, cyan under the pointer; a click (or Left /
// Right / Return) steps a setting through its values. Settings are written
// into the game's scream.cfg in place, keeping its comments and layout; the
// first save keeps the shipped file as scream.cfg.orig, which "reset" restores.

#include "setup.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>

#include "constants.hpp"
#include "gl.hpp"
#include "renderer.hpp"
#include "resources.hpp"
#include "text.hpp"

namespace {

// scream.cfg as text, read the way scream.exe's parser reads it (0x40f4a0):
// whitespace-separated words, a key followed by its value; the last
// occurrence of a key wins. Values are replaced in place.
class CfgFile {
public:
    bool load(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in)
            return false;
        text_.assign(std::istreambuf_iterator<char>(in), {});
        return true;
    }
    bool save(const std::filesystem::path& path) const {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out << text_;
        return bool(out);
    }
    bool has(const std::string& key) const { return valueAt(key) != std::string::npos; }
    std::string get(const std::string& key, const std::string& def) const {
        size_t at = valueAt(key);
        return at == std::string::npos ? def : word(at);
    }
    void set(const std::string& key, const std::string& value) {
        size_t at = valueAt(key);
        if (at != std::string::npos) {
            text_.replace(at, word(at).size(), value);
            return;
        }
        const std::string nl = text_.find("\r\n") != std::string::npos ? "\r\n" : "\n";
        if (!text_.empty() && text_.back() != '\n')
            text_ += nl;
        text_ += key + "\t\t" + value + nl;
    }

private:
    std::string text_;
    static bool space(char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; }
    std::string word(size_t at) const {
        size_t end = at;
        while (end < text_.size() && !space(text_[end])) ++end;
        return text_.substr(at, end - at);
    }
    // Position of the value word after the last `key` word, or npos.
    size_t valueAt(const std::string& key) const {
        size_t found = std::string::npos, i = 0;
        while (i < text_.size()) {
            while (i < text_.size() && space(text_[i])) ++i;
            size_t start = i;
            while (i < text_.size() && !space(text_[i])) ++i;
            if (i > start && text_.compare(start, i - start, key) == 0) {
                size_t v = i;
                while (v < text_.size() && space(text_[v])) ++v;
                if (v < text_.size()) found = v;
            }
        }
        return found;
    }
};

struct Choice {
    std::string value, text;
};

struct Row {
    std::string label;
    std::string key, key2;  // key2: HEIGHT with WIDTH ("WIDTH HEIGHT" values)
    std::vector<Choice> choices;
    std::string def;        // when the file doesn't have the key
    bool advanced = false;  // the original's Windows settings, no effect in this port
    int current = 0;
};

std::vector<Choice> onOff() { return {{"1", "ON"}, {"0", "OFF"}}; }

std::vector<Row> makeRows() {
    std::vector<Choice> modes;
    for (auto [w, h] : {std::pair{640, 480}, {800, 600}, {1024, 768}, {1152, 864}, {1280, 960}, {1400, 1050},
                        {1600, 1200}, {1920, 1440}})
        modes.push_back({std::to_string(w) + " " + std::to_string(h), std::to_string(w) + "x" + std::to_string(h)});
    return {
        {"screen mode", "WIDTH", "HEIGHT", modes, "1024 768"},
        {"fullscreen", "FULLSCREEN", "", onOff(), "1"},
        {"widescreen views", "WIDESCREEN", "", onOff(), "1"},
        {"vertical sync", "VSYNC", "", onOff(), "1"},
        {"textures", "QUALITY", "", {{"1", "SMOOTH"}, {"0", "PIXELATED"}}, "3"},
        {"surround music", "SURROUND", "", onOff(), "1"},
        {"log file", "LOG", "", onOff(), "0"},
        {"colour depth", "DEPTH", "", {{"0", "AUTO"}, {"-1", "DESKTOP"}, {"16", "16"}, {"24", "24"}, {"32", "32"}}, "32", true},
        {"refresh rate", "RATE", "", {{"0", "AUTO"}, {"60", "60"}, {"75", "75"}, {"85", "85"}, {"100", "100"}}, "0", true},
        {"audio device", "AUDIO_DEVICE", "", {{"0", "DEFAULT"}, {"1", "1"}, {"2", "2"}, {"3", "3"}}, "0", true},
        {"audio buffer", "BUFFER_SIZE", "", {{"8192", "8192"}, {"16384", "16384"}, {"32768", "32768"}, {"65536", "65536"}}, "32768", true},
        {"audio buffers", "NUM_BUFFERS", "", {{"2", "2"}, {"3", "3"}, {"4", "4"}, {"6", "6"}, {"8", "8"}}, "4", true},
        {"debug", "DEBUG", "", onOff(), "0", true},
        {"language", "LANGUAGE", "", {{"en", "EN"}, {"gr", "GR"}}, "en", true},
    };
}

std::string valueOf(const CfgFile& f, const Row& r) {
    std::string v = f.get(r.key, r.def.substr(0, r.def.find(' ')));
    if (!r.key2.empty()) v += " " + f.get(r.key2, r.def.substr(r.def.find(' ') + 1));
    return v;
}

// Point a row at the file's value, adding it as a choice if it isn't one.
void select(Row& r, const std::string& value) {
    auto it = std::find_if(r.choices.begin(), r.choices.end(), [&](const Choice& c) { return c.value == value; });
    if (it == r.choices.end()) {
        std::string text = value;
        if (!r.key2.empty()) std::replace(text.begin(), text.end(), ' ', 'x');
        if (r.key == "QUALITY") text = value == "0" ? "PIXELATED" : "SMOOTH";
        for (char& c : text) c = char(std::toupper(static_cast<unsigned char>(c)));
        r.choices.insert(r.choices.begin(), {value, text});
        it = r.choices.begin();
    }
    r.current = int(it - r.choices.begin());
}

}  // namespace

std::filesystem::path configPath(const std::vector<std::filesystem::path>& dataDirs) {
    for (auto it = dataDirs.rbegin(); it != dataDirs.rend(); ++it)
        if (std::filesystem::is_regular_file(*it / "scream.cfg"))
            return *it / "scream.cfg";
    return dataDirs.empty() ? std::filesystem::path("scream.cfg") : dataDirs.back() / "scream.cfg";
}

SetupResult runSetup(const std::vector<std::filesystem::path>& dataDirs, bool standalone, const SetupTest* test) {
    const std::filesystem::path path = configPath(dataDirs), backup = path.string() + ".orig";
    CfgFile file;
    const bool existed = file.load(path);
    std::vector<Row> rows = makeRows();
    for (Row& r : rows) select(r, valueOf(file, r));
    bool dirty = false;
    std::string status = existed ? "" : "no scream.cfg yet: saving creates one";

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
        return SetupResult::Play;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_Window* window = SDL_CreateWindow("SCream Redux setup", 800, 600,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_GLContext gl = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!gl || gl::load()) {
        if (window) SDL_DestroyWindow(window);
        return SetupResult::Play;
    }
    SDL_GL_SetSwapInterval(1);
    SetupResult result = SetupResult::Quit;
    {
        Renderer renderer;
        renderer.init();
        Resources res;
        for (const char* name : {"scream.res", "scratches.res"})
            for (auto it = dataDirs.rbegin(); it != dataDirs.rend(); ++it)
                if (std::filesystem::is_regular_file(*it / name)) {
                    try { res.mount(ResArchive::open(*it / name)); } catch (const std::exception&) {}
                    break;
                }
        Font font;
        font.load(renderer, res, "font_def.tga");
        const Renderer::Texture* logo = renderer.optionalTexture(res, "menu-logo.tga");

        // Layout on the 800x600 canvas.
        constexpr float px = 1.25f, rowH = 21, labelX = 150, valueX = 470;
        constexpr float firstRow = 172;
        auto rowY = [&](size_t i) { return firstRow + float(i) * rowH + (rows[i].advanced ? 28.0f : 0.0f); };
        const std::vector<std::string> buttons = standalone
            ? std::vector<std::string>{"save", "reset", "close"}
            : std::vector<std::string>{"save", "reset", "play", "quit"};
        constexpr float buttonY = 575;
        auto buttonX = [&](size_t i) {
            float total = 0;
            for (const auto& b : buttons) total += float(b.size()) * 10 * px + 40;
            float x = (sc::SCREEN_W - total + 40) / 2;
            for (size_t k = 0; k < i; ++k) x += float(buttons[k].size()) * 10 * px + 40;
            return x;
        };
        const int items = int(rows.size() + buttons.size());
        int selected = -1;  // keyboard / pointer focus: rows, then buttons

        auto save = [&] {
            std::error_code ec;
            if (existed && !std::filesystem::exists(backup))
                std::filesystem::copy_file(path, backup, ec);
            for (const Row& r : rows) {
                const std::string& v = r.choices[size_t(r.current)].value;
                if (!file.has(r.key) && v == r.def)
                    continue;  // a key the file doesn't have, at its default: leave it out
                if (r.key2.empty()) {
                    file.set(r.key, v);
                } else {
                    file.set(r.key, v.substr(0, v.find(' ')));
                    file.set(r.key2, v.substr(v.find(' ') + 1));
                }
            }
            if (!file.save(path)) {
                status = "could not write " + path.string();
                return false;
            }
            dirty = false;
            status = "saved";
            return true;
        };
        auto reset = [&] {
            // The shipped file as it was (its text too: keys we added go again).
            CfgFile original;
            if (!original.load(backup) && !original.load(path))
                original = CfgFile{};
            file = original;
            for (Row& r : rows) select(r, valueOf(file, r));
            dirty = true;
            status = "the game's original settings (not saved yet)";
        };
        auto activate = [&](int item, int step) {
            if (item < 0) return;
            if (item < int(rows.size())) {
                Row& r = rows[size_t(item)];
                const int n = int(r.choices.size());
                r.current = ((r.current + step) % n + n) % n;
                dirty = true;
                status.clear();
                return;
            }
            const std::string& b = buttons[size_t(item) - rows.size()];
            if (b == "save") save();
            else if (b == "reset") reset();
            else if (b == "play") {
                if (!dirty || save()) result = SetupResult::Play, selected = -2;
            } else selected = -2;  // close / quit
        };
        auto hit = [&](float x, float y) {
            for (size_t i = 0; i < rows.size(); ++i)
                if (y > rowY(i) - 18 && y <= rowY(i) + 4 && x > labelX - 10 && x < 700)
                    return int(i);
            for (size_t i = 0; i < buttons.size(); ++i) {
                float bx = buttonX(i), bw = float(buttons[i].size()) * 10 * px;
                if (y > buttonY - 20 && y <= buttonY + 6 && x > bx - 8 && x < bx + bw + 8)
                    return int(rows.size() + i);
            }
            return -1;
        };

        if (test) {  // queue the test's keys ("ctrl-" for the modifier)
            std::istringstream words(test->keys);
            for (std::string k; words >> k;) {
                SDL_Event e{};
                e.type = SDL_EVENT_KEY_DOWN;
                if (k.rfind("ctrl-", 0) == 0) e.key.mod = SDL_KMOD_CTRL, k = k.substr(5);
                e.key.key = SDL_GetKeyFromName(k.c_str());
                SDL_PushEvent(&e);
            }
        }
        int frames = 0;
        while (selected != -2) {
            SDL_Event e;
            while (SDL_PollEvent(&e)) {
                int w, h;
                SDL_GetWindowSizeInPixels(window, &w, &h);
                const Canvas c = Canvas::fit(w, h);
                const float density = SDL_GetWindowPixelDensity(window);
                switch (e.type) {
                case SDL_EVENT_QUIT: selected = -2; break;
                case SDL_EVENT_MOUSE_MOTION:
                    selected = hit(c.toCanvasX(e.motion.x * density), c.toCanvasY(e.motion.y * density));
                    break;
                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    const int i = hit(c.toCanvasX(e.button.x * density), c.toCanvasY(e.button.y * density));
                    activate(i, e.button.button == SDL_BUTTON_RIGHT ? -1 : 1);
                    break;
                }
                case SDL_EVENT_KEY_DOWN:
                    switch (e.key.key) {
                    case SDLK_ESCAPE: selected = -2; break;
                    case SDLK_UP: selected = selected <= 0 ? items - 1 : selected - 1; break;
                    case SDLK_DOWN: case SDLK_TAB: selected = (selected + 1) % items; break;
                    case SDLK_LEFT: if (selected < int(rows.size())) activate(selected, -1); break;
                    case SDLK_RIGHT: if (selected < int(rows.size())) activate(selected, 1); break;
                    case SDLK_RETURN: case SDLK_SPACE: activate(selected, 1); break;
                    case SDLK_S: if (e.key.mod & (SDL_KMOD_GUI | SDL_KMOD_CTRL)) save(); break;
                    default: break;
                    }
                    break;
                }
                if (selected == -2) break;
            }
            if (selected == -2) break;

            int w, h;
            SDL_GetWindowSizeInPixels(window, &w, &h);
            renderer.beginFrame(w, h);
            auto text = [&](const std::string& s, float x, float y, TextColor col, float alpha = 1) {
                font.drawMono(renderer, s, x, y, px, alpha, col);
            };
            auto centred = [&](const std::string& s, float y, TextColor col, float alpha = 1) {
                text(s, (sc::SCREEN_W - float(s.size()) * 10 * px) / 2, y, col, alpha);
            };
            const TextColor white{1, 1, 1}, cyan{0.1f, 1, 1}, grey{0.55f, 0.55f, 0.55f};
            if (logo) {
                // At most 300 x 110, keeping its shape.
                const float k = std::min(300.0f / float(logo->width), 110.0f / float(logo->height));
                const float lw = float(logo->width) * k, lh = float(logo->height) * k;
                renderer.drawImage(*logo, (sc::SCREEN_W - lw) / 2, 12, lw, lh);
            }
            centred("setup", 145, white);
            for (size_t i = 0; i < rows.size(); ++i) {
                const Row& r = rows[i];
                if (r.advanced && (i == 0 || !rows[i - 1].advanced))
                    centred("original windows settings: no effect in this port", rowY(i) - 26, grey, 0.8f);
                const TextColor col = int(i) == selected ? cyan : r.advanced ? grey : white;
                text(r.label, labelX, rowY(i), col);
                text(r.choices[size_t(r.current)].text, valueX, rowY(i), col);
            }
            std::string shown = path.string();
            if (shown.size() > 52) shown = "..." + shown.substr(shown.size() - 49);
            centred("file: " + shown, 510, grey, 0.8f);
            if (!status.empty() || dirty)
                centred(status.empty() ? "unsaved changes" : status, 540, dirty ? cyan : white);
            for (size_t i = 0; i < buttons.size(); ++i)
                text(buttons[i], buttonX(i), buttonY, int(rows.size() + i) == selected ? cyan : white);
            if (test && !test->screenshot.empty() && ++frames == 3) {
                renderer.capture(test->screenshot.c_str());
                break;
            }
            SDL_GL_SwapWindow(window);
        }
        renderer.evictAll();
    }
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    return result;
}
