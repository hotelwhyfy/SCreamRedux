#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <deque>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include "constants.hpp"
#include "renderer.hpp"
#include "resources.hpp"
#include "script.hpp"
#include "sound.hpp"
#include "text.hpp"
#include "video.hpp"
#include "world.hpp"

// scream.exe lays out its 2D screens in pixels of the configured mode
// (scream.cfg WIDTH x HEIGHT; glOrtho(0, W, 0, H)): the DC's cfg asks for
// 1024x768, the 2006 one for 800x600. We draw that mode onto the 800x600
// canvas, scaled by px. A mode that isn't 4:3 is laid out as the 4:3 mode of
// the same height (the 3D view still spans the window).
struct ScreenMode {
    int w = 1024, h = 768;
    float px = 800.0f / 1024.0f;  // canvas units per mode pixel
    float bar = 50;              // the 64 pixel bars above and below the view
    void set(int height) {
        h = height > 0 ? height : 768;
        w = h * 4 / 3;
        px = float(sc::SCREEN_H) / float(h);
        bar = 64 * px;
    }
};
inline ScreenMode gMode;

struct Options {
    std::vector<std::filesystem::path> dataDirs;  // searched in order; later ones win
    std::string start;          // "room" or "room:node": skip the menu and go there
    std::string saveDir;        // saves and preferences (default: the user's app data folder)
    std::string game;           // sdc | tlv: skip the menu and start that game
    std::string screenshot;     // write a PNG once the scene settles, then quit
    double screenshotAt = -1;   // ...or after this many seconds, settled or not
    std::string exec;           // Lua to run as a handler once startup is idle (testing)
    float yaw = 0, pitch = 0;
    bool trace = false;         // log every sc* call
    bool hotspots = false;      // SC_SHOW_HOTSPOTS debug overlay
    int fullscreen = -1;        // --fullscreen 1 / --windowed 0; -1: from scream.cfg
    int windowW = 0, windowH = 0;  // --window; 0: scream.cfg WIDTH x HEIGHT
    bool verifyMedia = false;   // decode every asset and exit
    bool check = false;         // validate the built world, then exit (check.cpp)
    bool turbo = false;         // fast-forward for automated runs: no audio or video, fast clock
    bool crawl = false;         // visit every room/view/object and report problems (crawl.cpp)
    bool explore = false;       // play the game automatically until stuck or finished (explore.cpp)
    std::string exploreHints;   // --explore-hints: puzzle solutions for the explorer (explore.cpp)
};

class Engine {
public:
    bool release2006 = false;  // the 2006 game script (Lua 5.0); set before the scripts run
    using Value = std::variant<double, std::string>;

    explicit Engine(Options opts);
    ~Engine();

    Options opts;
    Resources res;
    World world;
    ScriptHost script;
    Renderer renderer;
    Sound sound{res};
    Camera cam;

    // scSet / scGet storage and scEnable / scDisable flags (SC_SKY, SC_RAIN, ...).
    std::map<int, Value> settings;
    std::set<int> flags;
    std::map<int, std::string> cursors;  // scCursor
    std::string font;                    // scFont

    struct Menu {
        std::vector<std::string> images;      // logo, back, new, settings, load, save, quit
        std::vector<std::string> newGame;     // callbacks, one per selectable game
        std::map<int, std::string> extra;     // scMenuSetEx
        bool saveFunc = false;
    } menu;

    std::map<int, std::string> hints;  // scHintsSet
    int hintState = 0;                 // scHints
    std::vector<Item*> inventory;
    Item* held = nullptr;              // item picked up from the inventory (used as the cursor)
    // The journal's text, as scream.exe keeps it: entries are wrapped into lines
    // when added (0x40e380), once per index; `first` is the top line shown and
    // `last` one past the last entry (0x4df8b58, 0x4df8b5c).
    struct Journal {
        std::vector<std::string> lines;
        std::set<int> added;  // 0x4df7bb8; scJournalClear leaves it alone
        int first = 0, last = 0;
    } journal;
    void journalAdd(int index, const std::string& text);
    void journalClear() { journal.lines.clear(), journal.first = journal.last = 0; }

    // scJournalSet, called while the journal item is bound: the book opens from that item.
    struct JournalConfig {
        std::string image, font, openSound, turnSound, closeSound;
        Item* item = nullptr;
    } journalConfig;
    void openJournal();  // also scJournalShow
    void closeJournal();
    std::map<int, std::pair<int, int>> ambience;  // scSetAmbience: key -> (preset, flag)

    bool flag(int f) const;  // scEnable/scDisable state
    void snapAmbience(int key) { fx_.snapKeys.insert(key); }  // scSetAmbience(key, preset, 1): no fade

    // Locate a file in the data directories (last match wins).
    std::filesystem::path findData(const std::string& name) const;

    bool init();  // scInit: window, GL, resources
    void run();   // scRun: main loop until quit
    int checkWorld();  // --check: report missing targets, handlers and files; returns the count
    int checkResult_ = 0;
    // --crawl (crawl.cpp): one step per frame; false when finished.
    bool crawlStep();
    bool crawlRender_ = false;
    std::shared_ptr<struct Crawler> crawler_;  // shared_ptr: the type is only complete in crawl.cpp
    // --explore (explore.cpp): one step per frame; false when stuck or finished.
    bool exploreStep();
    std::shared_ptr<struct Explorer> explorer_;

    // Where the player is.
    Room* room = nullptr;
    View* node = nullptr;
    std::vector<View*> zooms;  // zoom stack; top is shown
    View* view() const { return zooms.empty() ? node : zooms.back(); }

    Overlay* overlay = nullptr;  // drawn over the current view

    // As in scream.exe's switch routine (0x4158d0): SC_ZOOM always pushes,
    // SC_ZOOMOUT pops one level, SC_ZOOMBACK pops everything back to the node,
    // and entering a room runs its scOnEnter handler.
    bool switchRoom(const std::string& name);
    bool switchNode(const std::string& name);
    bool switchZoom(const std::string& name);
    bool switchOverlay(const std::string& name);
    void playZoomAudio(const View* zoom, int when);  // scAudio(SC_ZOOM|SC_ZOOMOUT, ...)
    void zoomOut();
    void zoomBack();

    void lookAt(int face, bool smooth);
    bool cameraMoving() const { return camAnim_.active; }

    void feedback(const std::string& text, const std::string& voice = {});
    bool feedbackShown() const { return feedback_.count > 0; }
    // Start a crossfade from what is on screen now (0x407ad0; 0x407b20 starts at 0.75).
    void beginTransition(float progress = 0, bool unlock = true);
    bool hintsOn() const;  // SC_HINTS (0x4df8d38)
    void clearFeedback() { feedback_.count = 0; }
    void loadSubtitles(const std::string& file);
    void unloadSubtitles() { subs_ = {}; }
    void showChoice(const std::string& name);  // scChoiceGo

    // scCutscene: returns false if the video can't be played (the caller then doesn't wait).
    bool playCutscene(const std::string& file);
    bool cutscenePlaying() const { return video_ != nullptr; }
    void stopCutscene();
    void perform(const Action& a);
    // Act on an object exactly as a mouse click would (bindings + action).
    void activate(Object* o);
    // Find an enabled object by name in the current view (for debugClick).
    Object* findInView(const std::string& name);
    // Debug input for --exec: move the pointer (canvas units) and press a button.
    void debugMouse(float x, float y);
    void debugPress(int button);
    void debugMenu(int mode);  // open the menu at once in a mode (0 main, 1 load, 2 save, 4 setup)
    void debugUseHeld(Object* o) { useHeldOn(o); }
    void hotkey(int vk);  // 0x4175b0, keyed by Windows virtual-key codes (also debugKey)
    void addTimer(int id, int seconds, const std::string& fn, bool global);

    // Engine flags, as in scream.exe:
    bool locked = false;    // scLock/scUnlock (0x48bc0c): input off, cursor hidden
    bool waitMode = false;  // scWait/scGo (0x48bc10): show the wait cursor while locked
    bool motion = true;     // scMotion/scStill (0x48bc08): camera input allowed
    int sleeping_ = 0;      // scSleep calls waiting
    bool sleepSkip_ = false;  // Esc during a DC scSleep
    void abortGame();       // scAbort: wait for input after the ending, then leave
    void leaveAbortedGame();
    bool loadSettings(const std::string& file);  // scLoadSettings / scream.cfg
    // scream.cfg's keys with scream.exe's defaults (0x40f260).
    struct Config {
        std::string script = "main", resource = "game.res", path = ".\\", language = "en";
        int width = 1024, height = 768, depth = 32, quality = 3, fullscreen = 1, surround = 1, log = 0,
            vsync = 1, rate = 0, audioDevice = 0, bufferSize = 32768, numBuffers = 4, debug = 0;
    } config;
    void applyConfig();
    void setLogging(bool on);  // SC_LOG: append the log to scream.log
    double now() const { return gameClock_; }  // game time; stops while the menu is open
    double realTime() const;

    struct LuaValue {
        char type;  // 'n' number, 'b' boolean, 's' string
        double number;
        std::string text;
    };

    // Main menu (menu.cpp); also scMenuShow.
    void openMenu();

private:
    void mountResources();
    void handleEvent(const SDL_Event& e);
    void update(double dt);
    void render();
    void click();
    void rightClick();

    // Saved games (save.cpp).
    std::map<std::string, LuaValue> luaGlobals();
    void setLuaGlobal(const std::string& name, const LuaValue* v);
    std::string serializeState();
    bool restoreState(const std::string& data);
    void captureBaseline();
    std::filesystem::path saveDir() const;
    std::filesystem::path slotPath(int slot) const;
    std::string slotDescription(int slot) const;
    bool slotUsed(int slot) const;
    double quickLoadAt_ = -1;  // F7: when the second over the black zoom is up
    void updateQuickLoad();
    std::filesystem::path legacySlotPath(int slot) const;
    bool loadLegacySlot(int slot);
    std::string saveDescription(int slot);
    bool saveSlot(int slot);
    bool loadSlot(int slot);
    std::string baseline_;  // world + engine state when the game script finished building
    std::map<std::string, LuaValue> luaBaseline_;
    int currentGame_ = -1;
    bool gameRunning_ = false;
    bool menuShown_ = false;  // 0x4df8d34: once the menu has been shown, Esc opens it instead of quitting
    int gamma_ = 0;           // 0x4df8b88: 0 off, 1-3 brighten the view by 0.33 / 0.66 / 1.0
    Renderer::Texture gammaCapture_;
    static constexpr int kQuickSlot = 11;  // F5/F7 (the original's scream.011)
    void drawGamma();
    double gameClock_ = 0;
    double textClock_ = 0;  // feedback timing; unlike the game clock it runs on in the menu

    // Main menu (menu.cpp). The modes are the original's (menu struct +0x260).
    enum class Screen { None = -1, Main = 0, Load = 1, Save = 2, Setup = 4 };
    Screen menu_ = Screen::None;
    // The "black" zoom shown for a while when the menu opens (1 s) or closes (2 s).
    double menuBlackUntil_ = 0;
    bool menuClosing_ = false;
    std::function<void()> afterMenu_;  // what closing the menu leads to, run once it is closed
    // Prompt list (0x412fa0): the click that asked, its lines (first added at the bottom).
    int promptHit_ = -1;
    std::vector<std::string> promptLines_;
    bool menuPending_ = false;   // open the menu once the intro handler finishes
    bool quitWhenIdle_ = false;  // quitting: let the outro handler finish first
    bool menuOpen() const { return menu_ != Screen::None; }
    void closeMenu(std::function<void()> then = {});
    void updateMenu();
    void startNewGame(int index);
    void quitGame();
    void menuClick();
    void menuAction(int hit, int answer);
    void ask(int hit, std::vector<std::string> lines);
    int menuHit(float x, float y) const;
    int promptRow(float y) const;
    bool slotExists(int file) const;
    void readMenuSlots();
    std::array<std::string, 11> menuSlots_;  // row texts; row 0 is the quicksave
    bool slideshow_ = false;  // 0x4df8ddc, the setup menu's slideshow mode
    void slideshowStep();
    void setLook(float yaw, float y);  // 0x407d10, in the original's angles
    void getLook(float& yaw, float& y) const;  // 0x407d30
    float pitchLimit() const;
    void drawMenu();
    void drawMenuBackground();
    void loadUserSettings();
    void saveUserSettings();
    // A game handler is running (input waits). The --exec driver doesn't count.
    bool scriptBusy() const { return script.busyExcept("__exec"); }

    // Inventory and journal screens (ui.cpp).
    bool invOpen_ = false;
    bool journalOpen_ = false;
    Font journalFont_;
    struct Box { float x, y, w, h; };
    bool canOpenInventory() const;
    void openInventory();
    void closeInventory();
    bool hintsActive() const;
    Box slotBox(int slot, bool draw = false) const;
    float invFade_ = 0;  // 0x48baf0 while the inventory is open: 0 .. 0.5
    void drawInventoryBackdrop();
    int slotAt(float x, float y) const;
    void inventoryClick();
    void useHeldOn(Object* o);
    void drawInventory();
    void journalClick();
    void drawJournal();
    void drawHeldCursor(bool glow);

    // Sky, weather and lighting (effects.cpp).
    struct Effects {
        bool initialized = false, snap = false;  // snap: every colour at once (after a load)
        std::set<int> snapKeys;                  // colours to set at once (scSetAmbience flag)
        std::array<float, 3> sky{}, horizon{}, cloud{};
        // SC_SCREEN_COLOR (0x40d4e0): tint of a second, alpha blended pass over the scene.
        std::array<float, 4> screen{}, screenStep{};
        int screenPreset = 0;
        bool screenFading = false;
        double cloudScroll = 0, fogScroll = 0, rainScroll = 0;
        double fogStep = 0;           // the fog's scroll per call (it is called twice per frame)
        double cloudScroll2006 = 0, fogScroll2006 = 0, fogStep2006 = 0;
        float gradientSlide = 0;      // 0x48bba4: the horizon gradient's one-off slide
        bool skyDrawn = false;        // colour fades only run while the sky is drawn
        float cloudA = 1, cloudB = 0, cloudTimer = 0;  // layer A starts visible
        int cloudState = 0;
        double lightningClock = 0, flickerClock = 0;
        float fov = 45;
        int fovDir = 1;
    } fx_;
    void updateEffects(double dt);
    std::array<const RendererTexture*, 6> faceMasks(const View& v);
    void drawNodeScene(const View& v, const Viewport& vp);
    void drawClouds(const Viewport& vp);  // the two crossfading cloud layers (0x40d990)
    std::optional<Blend> drawBackdrop(const Viewport& vp, bool zoom);
    std::optional<Blend> drawBackdrop2006(const Viewport& vp, bool zoom);
    const RendererTexture* clouds2006();
    const RendererTexture* repeatingTexture(const std::string& name);
    void applyLighting(const std::function<void(float, float, float, float, Blend)>& overlay,
                       const std::function<void(float, float, float, float, Blend)>& wash);
    Object* hovered(int& edge);
    Viewport nodeViewport() const;
    void panCamera(double dt);
    // Alternate control (SC_FPS_STYLE, 0x4df8e10): the view follows the mouse's
    // offset from the screen centre, the pointer springs back to the centre.
    bool fpsStyle() const;
    bool fpsActive() const;  // in a panorama, where it applies
    void fpsCamera(double dt, bool recenter = true);
    void recenterPointer(bool now);
    void updateRelativeMouse();
    bool relativeMouse_ = false;
    void drawCursor(Object* hover, int edge);
    // Where a zoom image is drawn on the 800x600 canvas. Hotspots are authored
    // in a logical 800x472 space spanning the whole image, whatever its pixel
    // size (the DC update ships 1024x640 replacements for 800x472 originals).
    struct ZoomRect { float x, y, w, h; };
    ZoomRect zoomRect(const View& z);
    void drawZoomScene(const View& z, const RendererTexture& image, const ZoomRect& zr);
    static constexpr float kZoomW = 800, kZoomH = 472;
    bool showHotspots() const {
        auto it = settings.find(sc::SHOW_HOTSPOTS);
        return it != settings.end() && std::holds_alternative<double>(it->second) && std::get<double>(it->second) != 0;
    }

    SDL_Window* window_ = nullptr;
    SDL_GLContext gl_ = nullptr;
    bool quit_ = false;
    float mouseX_ = 0, mouseY_ = 0;
    bool dragging_ = false;
    bool aborted_ = false;
    float panRamp_ = 0;  // acceleration of edge panning, 0..1

    struct CamAnim {  // scLookAt(..., SC_SMOOTH), in the original's angles
        bool active = false, yawDone = false, yDone = false;
        float yaw = 0, y = 0, toYaw = 0, toY = 0;
    } camAnim_;
    void stepLook(double dt);

    std::vector<GameTimer> globalTimers_;  // scTimerCreateGlobal (world +0x4787cbc)
    double timerTick_ = 0;                 // game time of the last one-second timer tick
    void updateTimers();

    // Feedback text, as scream.exe's text setter (0x40e150) and renderer
    // (0x40aca0) keep it: up to four lines (lines[0] is the bottom one), shown
    // for `duration` seconds after `start`, fading in and then out.
    struct FeedbackText {
        std::array<std::string, 4> lines;
        int count = 0;  // 0: nothing shown
        double start = 0;
        int duration = 0;
        bool fresh = false;  // set by the setter; the renderer restarts the fade
        float fade = 1;      // 1 = transparent
    } feedback_;
    double frameDt_ = 0;  // last frame's length, for the per-frame fades

    // View transitions: before a state change the old screen is rendered and
    // captured (0x407ad0); each screen then draws the capture's view area over
    // itself, fading it out by SC_MOV_SPEED / 1000 per 60 Hz frame.
    struct Transition {
        bool active = false;
        float progress = 0;  // 0x48baf0: the capture's alpha is 1 - progress
        Renderer::Texture capture;
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;  // view area in the capture
        float x0 = 0, x1 = 0;                  // its left/right edge in canvas units (the full window width)
    } transition_;
    bool capturing_ = false;
    void drawTransition();

    // scLoadSubtitle: .srt cues shown through the feedback text (0x4180b0).
    struct Subtitles {
        bool active = false;
        std::string data;  // the file, read as the original reads it (fscanf/fgets)
        size_t pos = 0;
        float counter = 0;          // whole seconds since loading (0x48bc58)
        double lastTick = -1e9;     // time of the last counter tick (0x48bc64)
        float start = 0, end = 0;   // current cue (0x48bc5c, 0x48bc60)
        bool pending = false;       // cue text waiting for its start time (0x48bc54)
        std::string text;           // 0x4df5c18
    } subs_;
    void updateSubtitles();

    Font font_;
    void ensureFont();
    void canvasMouse(float& x, float& y) const;
    void drawFeedback();

    // Open dialogue choice (scChoiceGo): options are laid out in the bottom bar.
    std::unique_ptr<Video> video_;
    double videoStart_ = 0;
    Renderer::Texture videoTex_;

    Choice* choice_ = nullptr;
    int choiceAt(float x, float y);  // option index under the pointer, or -1
    int choiceHit(float y) const;
    void drawChoice();
};
