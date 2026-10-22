#pragma once

#include <SDL3/SDL.h>

#include <filesystem>
#include <map>
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
    std::string game = "sdc";   // which new-game callback to run without a menu: sdc | tlv
    std::string screenshot;     // write a PNG once the scene settles, then quit
    double screenshotAt = -1;   // ...or after this many seconds, settled or not
    std::string exec;           // Lua to run as a handler once startup is idle (testing)
    float yaw = 0, pitch = 0;
    bool trace = false;         // log every sc* call
    bool hotspots = false;      // SC_SHOW_HOTSPOTS debug overlay
    bool windowed = true;
    bool verifyMedia = false;   // decode every asset and exit
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
    } menu;

    std::map<int, std::string> hints;  // scHintsSet
    int hintState = 0;                 // scHints
    std::vector<Item*> inventory;
    Item* held = nullptr;              // item picked up from the inventory (used as the cursor)
    std::map<int, std::string> journal;  // scJournalAdd: entry index -> text

    // scJournalSet, called while the journal item is bound: the book opens from that item.
    struct JournalConfig {
        std::string image, font, openSound, turnSound, closeSound;
        Item* item = nullptr;
    } journalConfig;
    void openJournal();  // also scJournalShow
    void closeJournal();
    std::string subtitles;
    std::map<int, std::pair<int, int>> ambience;  // scSetAmbience: key -> (preset, flag)

    // Locate a file in the data directories (last match wins).
    std::filesystem::path findData(const std::string& name) const;

    bool init();  // scInit: window, GL, resources
    void run();   // scRun: main loop until quit

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
    void addTimer(double seconds, const std::string& fn, bool global);

    // Engine flags, as in scream.exe:
    bool locked = false;    // scLock/scUnlock (0x48bc0c): input off, cursor hidden
    bool waitMode = false;  // scWait/scGo (0x48bc10): show the wait cursor while locked
    bool motion = true;     // scMotion/scStill (0x48bc08): camera input allowed
    void abortGame();       // scAbort: wait for input after the ending, then leave
    bool loadSettings(const std::string& file);  // scLoadSettings / scream.cfg
    double now() const;

private:
    void mountResources();
    void handleEvent(const SDL_Event& e);
    void update(double dt);
    void render();
    void click();
    void rightClick();
    // A game handler is running (input waits). The --exec driver doesn't count.
    bool scriptBusy() const { return script.busyExcept("__exec"); }

    // Inventory and journal screens (ui.cpp).
    bool invOpen_ = false;
    bool journalOpen_ = false;
    size_t journalSpread_ = 0;
    std::vector<std::vector<std::string>> journalPages_;  // wrapped lines per page
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
    void layoutJournal();
    void journalClick();
    void drawJournal();
    void drawHeldCursor();    Object* hovered(int& edge);
    Viewport nodeViewport() const;
    void panCamera(double dt);
    void drawCursor(Object* hover, int edge);
    // Where a zoom image is drawn on the 800x600 canvas. Hotspots are authored
    // in a logical 800x472 space spanning the whole image, whatever its pixel
    // size (the DC update ships 1024x640 replacements for 800x472 originals).
    struct ZoomRect { float x, y, w, h; };
    ZoomRect zoomRect(const View& z);
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

    std::string feedback_;
    double feedbackUntil_ = 0;
    bool feedbackVoiced_ = false;

    Font font_;
    void ensureFont();
    void canvasMouse(float& x, float& y) const;
    void drawFeedback();

    // Open dialogue choice (scChoiceGo): options are laid out in the bottom bar.
    std::unique_ptr<Video> video_;
    double videoStart_ = 0;
    Renderer::Texture videoTex_;

    Choice* choice_ = nullptr;
    int choiceAt(float x, float y);
    void drawChoice();
};
