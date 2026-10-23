// Main menu, as scream.exe draws and drives it: renderer 0x408550 (called
// from the main loop 0x416a10 in state 0xfa5), hit test 0x417c90, click and
// hover handler 0x413640, prompt list 0x412fa0 / 0x40b250 / 0x413e70, open
// 0x412cb0, close 0x4130d0. Layout in pixels of the original's 1024x768 mode
// (top-down here; the original's GL y runs up), drawn on our canvas at gMode.px.
//
// Modes (menu struct +0x260): 0 main, 1 load, 2 save, 4 setup. The renderer
// also takes -1 (main without "back": no game running) and 3 (load with the
// quicksave row, when scream.011 exists). The menu images (scMenuSet: logo,
// back, new, set, load, save, quit) sit in square textures, 512 for the logo
// and 128 for the rest, which the quads stretch whole; an image fills the
// top-left of its texture, so it shows at the quad's scale.

#include <fstream>

#include "constants.hpp"
#include "engine.hpp"

extern "C" {
#include "lua.h"
}

namespace {

constexpr int kFiles = 10;  // scream.001-010; scream.011 (F5) shows as row 0
enum { kLogo, kBack, kNew, kSet, kLoad, kSave, kQuit };

// Setup items (0x408550 jump table 0x408520), label then value, 29 characters.
const char* const kSetupLabels[] = {"hints                ", "surround music       ", "camera speed         ",
                                    "text speed           ", "gamma correction     ", "slideshow mode       ",
                                    "alternate control    "};

const char* speedName(double v) { return v == 25 ? "    SLOW" : v == 50 ? "  NORMAL" : v == 75 ? "    FAST" : ""; }

double settingNum(const std::map<int, Engine::Value>& s, int key, double def) {
    auto it = s.find(key);
    return it != s.end() && std::holds_alternative<double>(it->second) ? std::get<double>(it->second) : def;
}

}  // namespace

// 0x412cb0: the game is captured, the "black" zoom shows for scSleep(1), then
// the menu fades in from it (updateMenu) and its music starts.
void Engine::openMenu() {
    if (menuOpen())
        return;
    beginTransition();
    menuShown_ = true;  // 0x4131c0
    menu_ = Screen::Main;
    promptHit_ = -1;
    menuClosing_ = false;
    menuBlackUntil_ = realTime() + (opts.turbo ? 0 : 1);  // turbo (test runs): no interlude
    invOpen_ = journalOpen_ = false;
    sound.pauseGame(true);
    readMenuSlots();
}

// 0x4130d0: the menu music fades, the "black" zoom shows for scSleep(2), then
// the game (or whatever `then` starts) fades in from it.
void Engine::closeMenu(std::function<void()> then) {
    if (!menuOpen() || menuClosing_) {
        if (then) then();
        return;
    }
    beginTransition();
    sound.stopMenuMusic();
    promptHit_ = -1;
    menuClosing_ = true;
    menuBlackUntil_ = realTime() + (opts.turbo ? 0 : 2);
    afterMenu_ = std::move(then);
}

// End of the black interlude: scSwitch(SC_ZOOMOUT) crossfades out of it.
void Engine::updateMenu() {
    if (menuBlackUntil_ == 0 || realTime() < menuBlackUntil_)
        return;
    menuBlackUntil_ = 0;
    beginTransition();
    if (!menuClosing_) {
        if (auto it = menu.extra.find(sc::STEREO); it != menu.extra.end())
            sound.playMenuMusic(it->second);
        return;
    }
    menuClosing_ = false;
    menu_ = Screen::None;
    sound.pauseGame(false);
    if (fpsStyle()) recenterPointer(true);  // 0x4136fc
    auto then = std::move(afterMenu_);
    afterMenu_ = nullptr;
    if (then) then();
}

void Engine::debugMenu(int mode) {
    if (!menuOpen()) {
        menuShown_ = true;
        sound.pauseGame(true);
    }
    menu_ = mode == 1 ? Screen::Load : mode == 2 ? Screen::Save : mode == 4 ? Screen::Setup : Screen::Main;
    menuBlackUntil_ = 0;
    menuClosing_ = false;
    promptHit_ = -1;
    readMenuSlots();
}

void Engine::startNewGame(int index) {
    if (index < 0 || index >= int(menu.newGame.size()))
        return;
    closeMenu([this, index] {
        restoreState(baseline_);
        currentGame_ = index;
        gameRunning_ = true;
        aborted_ = false;
        cam.yaw = cam.pitch = 0;
        script.start(menu.newGame[size_t(index)]);
    });
}

void Engine::quitGame() {
    closeMenu([this] {
        sound.stopAll();
        // scMenuSetEx(SC_OUTRO, fn) names the credits handler played on the way out.
        auto it = menu.extra.find(sc::OUTRO);
        if (it != menu.extra.end() && script.start(it->second))
            quitWhenIdle_ = true;
        else
            quit_ = true;
    });
}

// The slot texts, read when the menu opens (0x412cb0) and after a save:
// "N. description" for file N, row 0 for the quicksave; empty rows draw nothing.
void Engine::readMenuSlots() {
    for (int i = 0; i <= kFiles; ++i) {
        const int file = i == 0 ? kQuickSlot : i;
        menuSlots_[size_t(i)] = slotUsed(file) ? std::to_string(i) + ". " + slotDescription(file) : std::string();
    }
}

bool Engine::slotExists(int file) const { return !menuSlots_[size_t(file == kQuickSlot ? 0 : file)].empty(); }

// 0x417c90 in the menu state. Returns 0 back, 1 new, 2 setup, 3 load, 4 save,
// 5 quit, 6 + row for list/setup rows (16 - n for the n-th row from the
// bottom, 16 px apart, 6 px below the text), -1 for nothing.
int Engine::menuHit(float cx, float cy) const {
    const int x = int(cx / gMode.px), y = int(cy / gMode.px);
    if (menu_ == Screen::Main) {
        if (x < gMode.w / 2 - 64 || x > gMode.w / 2 + 64)
            return -1;
        for (int e = 1; e <= 6; ++e)  // 36 px apart, quit at the bottom
            if (y >= gMode.h - 108 - 36 * (e - 1) && y <= gMode.h - 72 - 36 * (e - 1))
                return 6 - e;
        return -1;
    }
    if (x >= gMode.w / 2 - 256 && x <= gMode.w / 2 + 256)
        for (int e = 0; e <= kFiles; ++e) {
            if (y < gMode.h - 126 - 16 * e || y > gMode.h - 110 - 16 * e)
                continue;
            // Loading only takes rows with a saved game (the top one is the quicksave).
            if (menu_ != Screen::Load || slotExists(e == kFiles ? kQuickSlot : kFiles - e))
                return 16 - e;
        }
    if (x >= gMode.w / 2 - 64 && x <= gMode.w / 2 + 64 && y >= gMode.h - 105 && y <= gMode.h - 70)
        return 0;
    return -1;
}

// Prompt rows (0x417c90, state 0xfa7 over the menu): 1 for the line second
// from the bottom (yes / Director's Cut), 2 for the bottom one (no / The Last Visit).
int Engine::promptRow(float cy) const {
    const int y = int(cy / gMode.px);
    if (y >= gMode.h - 32 && y <= gMode.h - 16) return 1;
    if (y >= gMode.h - 16 && y <= gMode.h) return 2;
    return 0;
}

// 0x412fa0, first call: the lines go in bottom-up, the question last (on top).
void Engine::ask(int hit, std::vector<std::string> lines) {
    clearFeedback();
    promptHit_ = hit;
    promptLines_ = std::move(lines);
}

void Engine::menuClick() {
    if (menuBlackUntil_ > 0)
        return;
    float x, y;
    canvasMouse(x, y);
    if (promptHit_ >= 0) {  // 0x413e70: the answer re-runs the click that asked
        int row = promptRow(y);
        if (row == 0)
            return;
        int hit = promptHit_;
        promptHit_ = -1;
        promptLines_.clear();
        menuAction(hit, row);
        return;
    }
    int hit = menuHit(x, y);
    if (hit >= 0)
        menuAction(hit, 0);
}

// 0x413640's click branch (jump table 0x4135c8). `answer` is 0 on the click
// itself, else the prompt row picked (1 yes, 2 no).
void Engine::menuAction(int hit, int answer) {
    switch (hit) {
    case 0:  // back
        if (menu_ == Screen::Main) {
            if (gameRunning_) closeMenu();
        } else {
            beginTransition();
            menu_ = Screen::Main;
        }
        return;
    case 1:  // new
        if (release2006) {
            // One game: no choice; during a game it asks first (P 0x411260).
            if (gameRunning_ && answer == 0) ask(1, {"no", "yes", "Start a new game?"});
            else if (!gameRunning_ || answer == 1) startNewGame(0);
            return;
        }
        if (gameRunning_) {
            if (answer == 0) {
                ask(1, {"no", "yes", "Start a new game?"});
            } else if (answer == 1) {
                gameRunning_ = false;  // no way back: the game must be picked now
                ask(1, {"Scratches: The Last Visit", "Scratches: Director's Cut", "Which game do you want to play?"});
            }
        } else if (answer == 0) {
            ask(1, {"Scratches: The Last Visit", "Scratches: Director's Cut", "Which game do you want to play?"});
        } else {
            startNewGame(answer == 1 ? 0 : 1);  // scMenuSet's first game function, or its second
        }
        return;
    case 2:  // setup
        beginTransition();
        menu_ = Screen::Setup;
        return;
    case 3:  // load, when there is something to load
        for (int f = 1; f <= kFiles + 1; ++f)
            if (slotExists(f)) {
                beginTransition();
                menu_ = Screen::Load;
                return;
            }
        return;
    case 4:  // save, during a game
        if (gameRunning_) {
            beginTransition();
            menu_ = Screen::Save;
        }
        return;
    case 5:  // quit
        if (answer == 0) ask(5, {"no", "yes", "Quit the game?"});
        else if (answer == 1) quitGame();
        return;
    default: break;
    }

    if (menu_ == Screen::Load) {
        if (gameRunning_ && answer == 0)
            return ask(hit, {"no", "yes", "Load a saved game?"});
        if (gameRunning_ && answer != 1)
            return;
        const int file = hit == 6 ? kQuickSlot : hit - 6;
        menu_ = Screen::Main;
        closeMenu([this, file] {
            // "Game loaded" either way; a failure only goes to the log (0x413a89).
            if (loadSlot(file))
                gameRunning_ = true;
            feedback("Game loaded");
        });
    } else if (menu_ == Screen::Save) {
        if (hit == 6)
            return;  // the quicksave row isn't for the menu
        const int file = hit - 6;
        if (slotExists(file) && answer == 0)
            return ask(hit, {"no", "yes", "Replace saved game?"});
        if (answer > 1)
            return;
        menu_ = Screen::Main;
        closeMenu([this, file] {
            saveSlot(file);
            feedback("Game saved");  // also when the write failed (0x413c01)
            readMenuSlots();
        });
    } else if (menu_ == Screen::Setup) {
        // 0x413620: the first five go through the key handler (F1, F2, F9, F10, F12).
        static const int keys[] = {0x70, 0x71, 0x78, 0x79, 0x7b};
        const int item = hit - 6;
        if (release2006 && item > 5)
            return;
        if (item < 5) {
            hotkey(keys[item]);
        } else if (item == 5) {
            // Slideshow mode (0x4df8ddc): the view turns in steps on clicks (Engine::slideshowStep).
            slideshow_ = !slideshow_;
            feedback(slideshow_ ? "slideshow mode ON" : "slideshow mode OFF");
            setLook(3.14f, 0);  // both switches face the front, level (0x413cb9)
        } else if (item == 6) {
            // Alternate control (0x4df8e10, SC_FPS_STYLE): the view follows the mouse (Engine::fpsCamera).
            bool on = settingNum(settings, sc::FPS_STYLE, 0) == 0;
            settings[sc::FPS_STYLE] = on ? 1.0 : 0.0;
            saveUserSettings();  // our addition: the original forgets it
            feedback(on ? "alternate control ENABLED" : "alternate control DISABLED");
            setLook(3.14f, 0);
        } else if (item == 7) {
            // A row that is never drawn but still answers clicks (0x413d5e): it
            // names the language, then calls a Lua function language(), which
            // no released script defines (the call then fails silently).
            if (auto* l = std::get_if<std::string>(&settings[sc::LANGUAGE]); l && *l == "en")
                feedback("english");
            lua_State* L = script.state();
            lua_getglobal(L, "language");
            const bool defined = lua_isfunction(L, -1);
            lua_pop(L, 1);
            if (defined)
                script.start("language");
        }
    }
}

// The menu background (0x4083c0, scMenuSetEx(SC_IMAGE)): the image added
// (ONE, ONE), the sky's cloud layers (0x40d990) over it, then the image
// again with its alpha, so the clouds drift behind the house. The DC draws it
// on a fixed quad, 1312 x 1389 pixels down from y 704, made for an 800 x 472
// image in a 1024 texture: the view area of the 1024 x 768 mode. The 2006
// engine draws it at its own size, centred (P 0x407ed0).
void Engine::drawMenuBackground() {
    auto it = menu.extra.find(sc::IMAGE);
    if (it == menu.extra.end())
        return;
    auto* t = renderer.texture(res, it->second);
    if (!t)
        return;
    float x, y, w, h;
    if (release2006) {
        w = float(t->width), h = float(t->height);
        x = (gMode.w - w) / 2, y = (gMode.h - h) / 2;
    } else {
        w = 1312.0f * float(t->width) / 1024, h = 1389.0f * float(t->height) / 1024;
        x = 0, y = float(gMode.h - 704);
    }
    x *= gMode.px, y *= gMode.px, w *= gMode.px, h *= gMode.px;
    renderer.drawImageUV(*t, x, y, w, h, 0, 0, 1, 1, 1, 1, 1, 1, Blend::AddOne);
    drawClouds(nodeViewport());
    renderer.drawImage(*t, x, y, w, h);
}

void Engine::drawMenu() {
    ensureFont();  // the text uses the current scFont (texture 0x18), not one of the menu's own
    if (menuBlackUntil_ > 0) {  // the "black" zoom
        drawTransition();
        if (font_.loaded()) drawFeedback();
        return;
    }
    drawMenuBackground();

    auto image = [&](int i) -> const Renderer::Texture* {
        return size_t(i) < menu.images.size() ? renderer.texture(res, menu.images[size_t(i)]) : nullptr;
    };
    // A quad of qw x qh pixels over a size x size texture: the image is its top-left part.
    auto quad = [&](const Renderer::Texture* t, int x, int y, int qw, int qh, int size, float r, float g, float b,
                    float a, Blend blend) {
        if (t)
            renderer.drawImageUV(*t, float(x) * gMode.px, float(y) * gMode.px, float(qw * t->width / size) * gMode.px,
                                 float(qh * t->height / size) * gMode.px, 0, 0, 1, 1, a, r, g, b, blend);
    };
    auto plain = [&](const Renderer::Texture* t, int x, int y, int qw, int qh, int size) {
        quad(t, x, y, qw, qh, size, 1, 1, 1, 1, Blend::Alpha);
    };
    auto glow = [&](const Renderer::Texture* t, int x, int y, int qw, int qh, int size) {  // hover
        quad(t, x, y, qw, qh, size, 0, 1, 1, 0.5f, Blend::AddAlpha);
    };
    auto text = [&](const std::string& s, int x, int y, bool hover) {
        if (!font_.loaded()) return;
        font_.drawMono(renderer, s, float(x) * gMode.px, float(y) * gMode.px, gMode.px);
        if (hover) font_.drawMono(renderer, s, float(x) * gMode.px, float(y) * gMode.px, gMode.px, 0.5f, {0, 1, 1}, Blend::AddAlpha);
    };

    float mx, my;
    canvasMouse(mx, my);
    const int hover = promptHit_ >= 0 ? promptHit_ : menuHit(mx, my);

    // The logo, in every mode: centred, its top 420 px above the middle (92 at 768).
    if (auto* logo = image(kLogo))
        plain(logo, gMode.w / 2 - 256 + (512 - logo->width) / 2, gMode.h / 2 - 420 + (512 - logo->height) / 2, 512, 512, 512);

    // Everything else is placed by the "new" image's size: 128x64 gives dx 0, dy 136.
    const auto* fresh = image(kNew);
    const int dx = (128 - (fresh ? fresh->width : 128)) / 2, dy = (112 - (fresh ? fresh->height : 64)) / 2 + 112;

    if (menu_ == Screen::Main) {
        // Back, new, setup, load, save, quit stacked 36 px apart, 112x120 quads
        // (so 112x60 for the 128x64 images); back only during a game.
        bool anySave = false;
        for (int f = 1; f <= kFiles + 1; ++f) anySave = anySave || slotExists(f);
        for (int k = gameRunning_ ? 0 : 1; k <= 5; ++k) {
            const bool disabled = (k == 3 && !anySave) || (k == 4 && !gameRunning_);
            const int x = gMode.w / 2 - 56 + dx, y = gMode.h / 2 - 64 + dy + 36 * k;
            const float c = disabled ? 0.5f : 1.0f;
            quad(image(kBack + k), x, y, 112, 120, 128, c, c, c, 1, Blend::Alpha);
            if (hover == k && !disabled)
                glow(image(kBack + k), x, y, 112, 120, 128);
        }
    } else {
        // Title image 96x96 (96x48 shown), the rows, and back below them.
        const int title = menu_ == Screen::Save ? kSave : menu_ == Screen::Setup ? kSet : kLoad;
        plain(image(title), gMode.w / 2 - 48 + dx, gMode.h / 2 - 108 + dy, 96, 96, 128);
        if (menu_ == Screen::Setup) {
            for (int k = 0; k < (release2006 ? 6 : 7); ++k) {  // 2006: no alternate control
                std::string v;
                switch (k) {
                case 0: v = hintsOn() ? "      ON" : "     OFF"; break;
                case 1: v = settingNum(settings, sc::SURROUND, 0) != 0 ? "      ON" : "     OFF"; break;
                case 2: v = speedName(settingNum(settings, sc::CAM_SPEED, 50)); break;
                case 3: v = speedName(settingNum(settings, sc::TXT_SPEED, 50)); break;
                case 4: v = gamma_ == 1 ? "     LOW" : gamma_ == 2 ? "  MEDIUM" : gamma_ == 3 ? "    HIGH" : "DISABLED"; break;
                case 5: v = slideshow_ ? "      ON" : "     OFF"; break;
                case 6: v = settingNum(settings, sc::FPS_STYLE, 0) != 0 ? " ENABLED" : "DISABLED"; break;
                }
                text(kSetupLabels[k] + v, gMode.w / 2 - 145, gMode.h - 276 + 16 * k, hover == 6 + k);
            }
        } else {
            // Rows 1-10 are the files; row 0, the quicksave, only when loading and there is one (mode 3).
            const bool quick = menu_ == Screen::Load && slotExists(kQuickSlot);
            for (int i = quick ? 0 : 1; i <= kFiles; ++i)
                text(menuSlots_[size_t(i)], gMode.w / 2 - 120, gMode.h - 276 + 16 * i, hover == 6 + i);
        }
        const int y = gMode.h / 2 + (menu_ == Screen::Setup ? 116 : 126) + dy;
        plain(image(kBack), gMode.w / 2 - 64 + dx, y, 128, 128, 128);
        if (hover == 0)
            glow(image(kBack), gMode.w / 2 - 64 + dx, y, 128, 128, 128);
    }
    drawGamma();
    drawTransition();

    if (!font_.loaded())
        return;
    if (promptHit_ < 0) {
        drawFeedback();  // e.g. the setup items' "hints ON"
        return;
    }
    // The prompt list (0x40b250 with EAX=1): centred, stacked up from the
    // bottom 16 px apart, the question (last line) white, the others cyan.
    const int n = int(promptLines_.size()), offset = n == 4 ? -8 : 8;
    const int row = promptRow(my), lit = row == 1 ? 1 : row == 2 ? 0 : -1;
    for (int i = 0; i < n; ++i) {
        const std::string& s = promptLines_[size_t(i)];
        const float x = float(gMode.w / 2 - int(s.size() / 2) * Font::kMonoAdvance - 4) * gMode.px;
        const float y = float(gMode.h - offset - 16 * i) * gMode.px;
        TextColor c = i == n - 1 ? TextColor{1, 1, 1} : TextColor{0.1f, 0.75f, 0.75f};
        font_.drawMono(renderer, s, x, y, gMode.px, 1, c);
        if (i == lit)
            font_.drawMono(renderer, s, x, y, gMode.px, 1, {0.1f, 1, 1});
    }
}

// Player preferences from the setup screen, kept next to the saves.
void Engine::loadUserSettings() {
    std::ifstream in(saveDir() / "settings.cfg");
    std::string key;
    double value;
    while (in >> key >> value) {
        if (key == "gamma") {
            gamma_ = std::clamp(int(value), 0, 3);
            continue;
        }
        int k = std::atoi(key.c_str());
        if (k == sc::HINTS || k == sc::CAM_SPEED || k == sc::TXT_SPEED || k == sc::SURROUND || k == sc::FPS_STYLE)
            settings[k] = value;
    }
}

void Engine::saveUserSettings() {
    std::ofstream out(saveDir() / "settings.cfg", std::ios::trunc);
    for (int key : {sc::HINTS, sc::CAM_SPEED, sc::TXT_SPEED, sc::SURROUND, sc::FPS_STYLE})
        out << key << " " << settingNum(settings, key, 0) << "\n";
    out << "gamma " << gamma_ << "\n";
}
