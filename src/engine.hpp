#pragma once

#include <SDL3/SDL.h>

#include <array>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
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
    bool windowed = true;
    bool verifyMedia = false;   // decode every asset and exit
    bool check = false;         // validate the built world, then exit (check.cpp)
    bool turbo = false;         // fast-forward for automated runs: no audio or video, fast clock
    bool crawl = false;         // visit every room/view/object and report problems (crawl.cpp)
};

class Engine {
public:
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
        std::string font;
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
    void snapAmbience() { fx_.snap = true; }  // scSetAmbience(..., 1): no fade

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
    // scream.exe's default screen mode; its text layout is in these pixels.
    static constexpr int kOrigW = 1024, kOrigH = 768;
    bool feedbackShown() const { return feedback_.count > 0; }
    // Start a crossfade from what is on screen now (0x407ad0; 0x407b20 starts at 0.75).
    void beginTransition(float progress = 0);
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
    void debugUseHeld(Object* o) { useHeldOn(o); }
    void hotkey(int vk);  // 0x4175b0, keyed by Windows virtual-key codes (also debugKey)
    void addTimer(double seconds, const std::string& fn, bool global);

    // Engine flags, as in scream.exe:
    bool locked = false;    // scLock/scUnlock (0x48bc0c): input off, cursor hidden
    bool waitMode = false;  // scWait/scGo (0x48bc10): show the wait cursor while locked
    bool motion = true;     // scMotion/scStill (0x48bc08): camera input allowed
    void abortGame();       // scAbort: wait for input after the ending, then leave
    void leaveAbortedGame();
    bool loadSettings(const std::string& file);  // scLoadSettings / scream.cfg
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

    // Main menu (menu.cpp).
    enum class Screen { None, Main, ChooseGame, Confirm, Setup, Load, Save, Message };
    Screen menu_ = Screen::None;
    Screen afterMessage_ = Screen::None;
    std::string prompt_;
    std::function<void()> onYes_;
    double messageUntil_ = 0;
    bool menuPending_ = false;   // open the menu once the intro handler finishes
    std::function<void()> deferred_;  // state-replacing menu actions run between frames
    bool quitWhenIdle_ = false;  // quitting: let the outro handler finish first
    Font menuFont_;
    bool menuOpen() const { return menu_ != Screen::None; }
    void closeMenu();
    void showMessage(const std::string& text, Screen after);
    void confirm(const std::string& question, std::function<void()> yes);
    void startNewGame(int index);
    void quitGame();
    void menuClick();
    void menuBack();
    void drawMenu();
    std::vector<std::string> menuButtons() const;
    std::vector<std::string> menuLines() const;
    int menuLineAt(float x, float y) const;
    int menuButtonAt(float x, float y) const;
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
    Box slotBox(int slot) const;
    int slotAt(float x, float y) const;
    void inventoryClick();
    void useHeldOn(Object* o);
    void drawInventory();
    void journalClick();
    void drawJournal();
    void drawHeldCursor();

    // Sky, weather and lighting (effects.cpp).
    struct Effects {
        bool initialized = false, snap = false;
        std::array<float, 3> sky{}, horizon{}, cloud{};
        double cloudScroll = 0, fogScroll = 0, rainScroll = 0;
        float cloudA = 1, cloudB = 0, cloudTimer = 0;  // layer A starts visible
        int cloudState = 0;
        double lightningClock = 0, flickerClock = 0;
        float fov = 45;
        int fovDir = 1;
    } fx_;
    void updateEffects(double dt);
    std::array<const RendererTexture*, 6> faceMasks(const View& v);
    void drawNodeScene(const View& v, const Viewport& vp);
    void applyLighting(const std::function<void(float, float, float, float, Blend)>& overlay,
                       const std::function<void(float, float, float, float, Blend)>& wash);    Object* hovered(int& edge);
    Viewport nodeViewport() const;
    void panCamera(double dt);
    void drawCursor(Object* hover, int edge);
    // Where a zoom image is drawn on the 800x600 canvas. Hotspots are authored
    // in a logical 800x472 space spanning the whole image, whatever its pixel
    // size (the DC update ships 1024x640 replacements for 800x472 originals).
    struct ZoomRect { float x, y, w, h; };
    ZoomRect zoomRect(const View& z);
    void drawZoomEffects(const RendererTexture& image, const ZoomRect& zr);
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

    struct CamAnim {
        bool active = false;
        float fromYaw = 0, toYaw = 0, fromPitch = 0, toPitch = 0;
        double t0 = 0, duration = 0;
    } camAnim_;

    struct Timer {
        double at;
        std::string fn;
        bool global;
    };
    std::vector<Timer> timers_;

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
