// Main menu: new game (choosing between the two games), setup, load, save,
// quit, and back to the game. Uses the game's menu images (scMenuSet), menu
// music and background (scMenuSetEx) and the wording found in scream.exe
// ("Which game do you want to play?", "Replace saved game?", ...). The exact
// on-screen layout of the original isn't recovered yet, so this one is ours.

#include <SDL3/SDL_log.h>

#include <fstream>

#include "constants.hpp"
#include "engine.hpp"

namespace {

constexpr int kSlots = 8;
constexpr float kButtonW = 128, kButtonH = 64, kButtonY = 470;
constexpr float kTextScale = 0.8f, kLinesTop = 250;

const char* speedName(double v) { return v < 40 ? "SLOW" : v > 60 ? "FAST" : "NORMAL"; }
double nextSpeed(double v) { return v < 40 ? 50 : v > 60 ? 25 : 75; }

double settingNum(const std::map<int, Engine::Value>& s, int key, double def) {
    auto it = s.find(key);
    return it != s.end() && std::holds_alternative<double>(it->second) ? std::get<double>(it->second) : def;
}

}  // namespace

void Engine::openMenu() {
    if (menuOpen())
        return;
    menu_ = Screen::Main;
    invOpen_ = journalOpen_ = false;
    sound.pauseGame(true);
    if (auto it = menu.extra.find(sc::STEREO); it != menu.extra.end())
        sound.playMenuMusic(it->second);
}

void Engine::closeMenu() {
    menu_ = Screen::None;
    sound.stopMenuMusic();
    sound.pauseGame(false);
}

void Engine::showMessage(const std::string& text, Screen after) {
    prompt_ = text;
    menu_ = Screen::Message;
    afterMessage_ = after;
    messageUntil_ = realTime() + 1.5;
}

void Engine::confirm(const std::string& question, std::function<void()> yes) {
    prompt_ = question;
    onYes_ = std::move(yes);
    menu_ = Screen::Confirm;
}

void Engine::startNewGame(int index) {
    if (index < 0 || index >= int(menu.newGame.size()))
        return;
    closeMenu();
    restoreState(baseline_);
    currentGame_ = index;
    gameRunning_ = true;
    aborted_ = false;
    cam.yaw = cam.pitch = 0;
    script.start(menu.newGame[size_t(index)]);
}

void Engine::quitGame() {
    closeMenu();
    sound.stopAll();
    // scMenuSetEx(SC_OUTRO, fn) names the credits handler played on the way out.
    auto it = menu.extra.find(sc::OUTRO);
    if (it != menu.extra.end() && script.start(it->second))
        quitWhenIdle_ = true;
    else
        quit_ = true;
}

std::vector<std::string> Engine::menuButtons() const {
    std::vector<std::string> b = {"new", "set", "load"};
    if (gameRunning_) b.push_back("save");
    b.push_back("quit");
    if (gameRunning_) b.push_back("back");
    return b;
}

// Text lines of the current sub-screen; the last one is the way back where there is one.
std::vector<std::string> Engine::menuLines() const {
    switch (menu_) {
    case Screen::ChooseGame:
        return {"Scratches: Director's Cut", "Scratches: The Last Visit", "back"};
    case Screen::Confirm:
        return {"yes", "no"};
    case Screen::Setup: {
        auto onOff = [&](int key) { return settingNum(settings, key, 0) != 0 ? std::string("ON") : std::string("OFF"); };
        return {"hints  " + onOff(sc::HINTS),
                std::string("camera speed  ") + speedName(settingNum(settings, sc::CAM_SPEED, 50)),
                std::string("text speed  ") + speedName(settingNum(settings, sc::TXT_SPEED, 50)),
                "back"};
    }
    case Screen::Load:
    case Screen::Save: {
        std::vector<std::string> lines;
        for (int i = 0; i < kSlots; ++i) {
            std::string d = slotDescription(i);
            lines.push_back(std::to_string(i + 1) + ".  " + (d.empty() ? "----" : d));
        }
        lines.push_back("back");
        return lines;
    }
    default:
        return {};
    }
}

int Engine::menuLineAt(float x, float y) const {
    float lh = menuFont_.lineHeight(kTextScale) * 1.2f;
    int i = int((y - kLinesTop) / lh);
    int n = int(menuLines().size());
    return y >= kLinesTop && i < n && x > 40 && x < sc::SCREEN_W - 40 ? i : -1;
}

int Engine::menuButtonAt(float x, float y) const {
    auto buttons = menuButtons();
    float total = float(buttons.size()) * kButtonW;
    float left = (sc::SCREEN_W - total) / 2;
    if (y < kButtonY || y >= kButtonY + kButtonH || x < left || x >= left + total)
        return -1;
    return int((x - left) / kButtonW);
}

void Engine::menuBack() {
    if (menu_ == Screen::Main) {
        if (gameRunning_) closeMenu();
    } else if (menu_ != Screen::Message) {
        menu_ = Screen::Main;
    }
}

void Engine::menuClick() {
    float x, y;
    canvasMouse(x, y);
    switch (menu_) {
    case Screen::Main: {
        int b = menuButtonAt(x, y);
        if (b < 0) return;
        const std::string name = menuButtons()[size_t(b)];
        if (name == "new") {
            if (gameRunning_) confirm("Start a new game?", [this] { menu_ = Screen::ChooseGame; });
            else menu_ = Screen::ChooseGame;
        } else if (name == "set") {
            menu_ = Screen::Setup;
        } else if (name == "load") {
            menu_ = Screen::Load;
        } else if (name == "save") {
            menu_ = Screen::Save;
        } else if (name == "quit") {
            confirm("Quit the game?", [this] { quitGame(); });
        } else if (name == "back") {
            closeMenu();
        }
        return;
    }
    case Screen::ChooseGame: {
        int i = menuLineAt(x, y);
        if (i == 2) menu_ = Screen::Main;
        else if (i >= 0) deferred_ = [this, i] { startNewGame(i); };
        return;
    }
    case Screen::Confirm: {
        int i = menuLineAt(x, y);
        if (i == 0) {
            menu_ = Screen::Main;
            auto yes = std::move(onYes_);
            yes();
        } else if (i == 1) {
            menu_ = Screen::Main;
        }
        return;
    }
    case Screen::Setup: {
        int i = menuLineAt(x, y);
        if (i == 0) settings[sc::HINTS] = settingNum(settings, sc::HINTS, 1) != 0 ? 0.0 : 1.0;
        else if (i == 1) settings[sc::CAM_SPEED] = nextSpeed(settingNum(settings, sc::CAM_SPEED, 50));
        else if (i == 2) settings[sc::TXT_SPEED] = nextSpeed(settingNum(settings, sc::TXT_SPEED, 50));
        else if (i == 3) menu_ = Screen::Main;
        if (i >= 0 && i < 3) saveUserSettings();
        return;
    }
    case Screen::Load:
    case Screen::Save: {
        int i = menuLineAt(x, y);
        if (i == kSlots) {
            menu_ = Screen::Main;
        } else if (i >= 0 && menu_ == Screen::Save) {
            auto doSave = [this, i] {
                showMessage(saveSlot(i) ? "Game saved" : "Could not save game!", Screen::Main);
            };
            if (slotDescription(i).empty()) doSave();
            else confirm("Replace saved game?", doSave);
        } else if (i >= 0 && !slotDescription(i).empty()) {
            auto doLoad = [this, i] { deferred_ = [this, i] {
                if (loadSlot(i)) {
                    gameRunning_ = true;
                    showMessage("Game loaded", Screen::None);
                } else {
                    showMessage("Could not load game!", Screen::Main);
                }
            }; };
            if (gameRunning_) confirm("Load a saved game?", doLoad);
            else doLoad();
        }
        return;
    }
    default:
        return;
    }
}

void Engine::drawMenu() {
    if (!menuFont_.loaded() || menuFont_.name() != menu.font)
        menuFont_.load(renderer, res, menu.font.empty() ? std::string("font_def.tga") : menu.font);

    if (auto it = menu.extra.find(sc::IMAGE); it != menu.extra.end())
        if (auto* t = renderer.texture(res, it->second))
            renderer.drawImage(*t, 0, (sc::SCREEN_H - 472) / 2.0f, sc::SCREEN_W, 472);
    float mx, my;
    canvasMouse(mx, my);

    if (menu_ == Screen::Main) {
        if (!menu.images.empty())
            if (auto* t = renderer.texture(res, menu.images[0]))
                renderer.drawImage(*t, (sc::SCREEN_W - 512) / 2.0f, 70, 512, 256);
        auto buttons = menuButtons();
        float left = (sc::SCREEN_W - float(buttons.size()) * kButtonW) / 2;
        int hover = menuButtonAt(mx, my);
        // Images, in scMenuSet order: logo, back, new, set, load, save, quit.
        static const std::map<std::string, int> imageIndex = {
            {"back", 1}, {"new", 2}, {"set", 3}, {"load", 4}, {"save", 5}, {"quit", 6}};
        for (size_t i = 0; i < buttons.size(); ++i) {
            size_t img = size_t(imageIndex.at(buttons[i]));
            if (img < menu.images.size())
                if (auto* t = renderer.texture(res, menu.images[img]))
                    renderer.drawImage(*t, left + float(i) * kButtonW, kButtonY, kButtonW, kButtonH,
                                       int(i) == hover ? 1.0f : 0.65f);
        }
        return;
    }

    renderer.drawRect(0, 64, sc::SCREEN_W, sc::SCREEN_H - 128, 0, 0, 0, 0.55f);
    if (!menuFont_.loaded())
        return;
    std::string title = menu_ == Screen::ChooseGame ? "Which game do you want to play?"
                      : menu_ == Screen::Setup     ? "Setup"
                      : menu_ == Screen::Load      ? "Load a saved game"
                      : menu_ == Screen::Save      ? "Save the game"
                                                   : prompt_;
    float tw = menuFont_.width(title, kTextScale);
    menuFont_.draw(renderer, title, (sc::SCREEN_W - tw) / 2, 160, kTextScale);
    if (menu_ == Screen::Message)
        return;
    auto lines = menuLines();
    int hover = menuLineAt(mx, my);
    float lh = menuFont_.lineHeight(kTextScale) * 1.2f;
    for (size_t i = 0; i < lines.size(); ++i) {
        float w = menuFont_.width(lines[i], kTextScale);
        menuFont_.draw(renderer, lines[i], (sc::SCREEN_W - w) / 2, kLinesTop + lh * float(i), kTextScale,
                       int(i) == hover ? 1.0f : 0.6f);
    }
}

// Player preferences from the setup screen, kept next to the saves.
void Engine::loadUserSettings() {
    std::ifstream in(saveDir() / "settings.cfg");
    int key;
    double value;
    while (in >> key >> value)
        if (key == sc::HINTS || key == sc::CAM_SPEED || key == sc::TXT_SPEED)
            settings[key] = value;
}

void Engine::saveUserSettings() {
    std::ofstream out(saveDir() / "settings.cfg", std::ios::trunc);
    for (int key : {sc::HINTS, sc::CAM_SPEED, sc::TXT_SPEED})
        out << key << " " << settingNum(settings, key, 0) << "\n";
}
