#include "engine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <ctime>
#include <fstream>
#include <optional>
#include <sstream>

#include "constants.hpp"
#include "gl.hpp"
#include "platform.hpp"

namespace {

// Camera input, from scream.exe (0x407e20, 0x40df60). The original works per
// frame and scales by 60/fps; these are the equivalent per-second values.
constexpr float kEdgeZone = 0.15f;       // edge zones: 15% of the width / view height
constexpr float kPanFactor = 0.005f;     // radians per pixel of zone depth, x CAM_SPEED/1000
constexpr float kRampPerSecond = 1.5f;   // acceleration ramp: +0.025 per frame at 60 fps
constexpr float kPitchLimit = 1.75f;     // pitch is the look vector's y; |y| <= 1.75 (~60.3 deg)
constexpr float kFpsLook = 0.0003f;      // alternate control: radians per pixel off centre, per frame (0x483238)
constexpr float kFpsPitchDown = -2.0f;   // ... which lets the view go lower (0x48323c)
constexpr double kTurboStep = 0.5;       // --turbo: game seconds per frame

float wrap180(float a) {
    a = std::fmod(a + 180.0f, 360.0f);
    return (a < 0 ? a + 360.0f : a) - 180.0f;
}

}  // namespace

Engine::Engine(Options o) : opts(std::move(o)) {
    cam.yaw = opts.yaw;
    cam.pitch = opts.pitch;
    cam.fovY = sc::DEF_FOV;
    settings[sc::HINTS] = 1.0;
    settings[sc::TXT_SPEED] = 50.0;
    settings[sc::MOV_SPEED] = 25.0;
    settings[sc::CAM_SPEED] = 50.0;
    settings[sc::RES_FILE] = config.resource;  // "game.res" until the script or cfg names one
    settings[sc::RES_PATH] = config.path;
    settings[sc::SHOW_HOTSPOTS] = opts.hotspots ? 1.0 : 0.0;
    flags.insert(sc::WAIT);  // both enabled by default in scream.exe (0x40f38e, 0x40f3a2),
    flags.insert(sc::SKY);   // so the opening garden has its sky before any scEnable(SC_SKY)
    applyConfig();
    loadSettings("scream.cfg");  // at startup, before the scripts (0x4061d0)
}

Engine::~Engine() {
    if (gl_) {
        renderer.release(videoTex_);
        renderer.evictAll();
        SDL_GL_DestroyContext(gl_);
    }
    if (window_)
        SDL_DestroyWindow(window_);
}

double Engine::realTime() const { return double(SDL_GetTicksNS()) / 1e9; }

std::filesystem::path Engine::findData(const std::string& name) const {
    for (auto it = opts.dataDirs.rbegin(); it != opts.dataDirs.rend(); ++it)
        if (std::filesystem::is_regular_file(*it / name))
            return *it / name;
    return {};
}

void Engine::mountResources() {
    // Engine resources first, then the game archive, then update.res on top
    // (what the DC's scupdate.exe would have merged into scratches.res).
    std::string resFile = "scratches.res";
    if (auto* s = std::get_if<std::string>(&settings[sc::RES_FILE]))
        resFile = *s;
    for (const std::string& name : {std::string("scream.res"), resFile, std::string("update.res")}) {
        auto path = findData(name);
        if (path.empty()) {
            if (name != "update.res")
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "resource file %s not found", name.c_str());
            continue;
        }
        try {
            auto a = ResArchive::open(path);
            SDL_Log("mounted %s (%zu entries)", path.string().c_str(), a->size());
            res.mount(std::move(a));
        } catch (const std::exception& e) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", e.what());
        }
    }
}

bool Engine::init() {
    if (window_)
        return true;
    // macOS: in fullscreen the menu bar (and title bar) would drop down when
    // the mouse reaches the top edge, which the edge panning does all the time.
    SDL_SetHint(SDL_HINT_VIDEO_MAC_FULLSCREEN_MENU_VISIBILITY, "0");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    // The mode comes from scream.cfg (or scSet / scEnable(SC_FULLSCREEN) before
    // scInit) unless the command line says otherwise. Fullscreen uses the
    // desktop mode and scales the configured one onto it; test runs are windowed.
    auto setting = [&](int key, double def) {
        auto* v = std::get_if<double>(&settings[key]);
        return v ? *v : def;
    };
    bool fullscreen = opts.fullscreen >= 0 ? opts.fullscreen != 0 : setting(sc::FULLSCREEN, 1) != 0;
    if (opts.turbo || !opts.screenshot.empty())
        fullscreen = false;
    int winW = opts.windowW > 0 ? opts.windowW : int(setting(sc::WIDTH, 1024));
    int winH = opts.windowH > 0 ? opts.windowH : int(setting(sc::HEIGHT, 768));
    gMode.set(int(setting(sc::HEIGHT, 768)));
    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (fullscreen)
        flags |= SDL_WINDOW_FULLSCREEN;
    window_ = SDL_CreateWindow("SCream Redux", winW, winH, flags);
    mouseX_ = winW / 2.0f;  // the pointer starts centred (alternate control looks at its offset)
    mouseY_ = winH / 2.0f;
    if (!window_ || !(gl_ = SDL_GL_CreateContext(window_))) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "window/GL: %s", SDL_GetError());
        return false;
    }
    // VSYNC: nonzero asks for a swap interval of 1 (0x419917); 0 leaves the driver's default.
    if (opts.turbo) SDL_GL_SetSwapInterval(0);
    else if (config.vsync) SDL_GL_SetSwapInterval(1);
    if (const char* missing = gl::load()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "missing GL entry point %s", missing);
        return false;
    }
    if (!renderer.init())
        return false;
    // macOS: Settings… in the application menu opens the setup launcher beside the game.
    if (!opts.turbo)
        platform::installSettingsMenu([dirs = opts.dataDirs] {
            std::vector<std::string> args{"--setup-only"};
            for (const auto& d : dirs) args.insert(args.end(), {"--data", d.string()});
            platform::spawnSelf(args);
        });
    if (release2006)
        renderer.useDome2006();
    SDL_HideCursor();
    sound.init();
    sound.setSilent(opts.turbo);
    mountResources();
    return true;
}

bool Engine::switchRoom(const std::string& name) {
    Room* r = world.room(name);
    if (!r || r->nodes.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "scSwitch: no room %s", name.c_str());
        return false;
    }
    beginTransition();
    room = r;
    zooms.clear();
    overlay = nullptr;
    View* n = r->lastVisited.empty() ? nullptr : r->node(r->lastVisited);
    node = n ? n : r->nodes.front().get();
    r->lastVisited = node->name;
    // scSwitch binds what it switches to (0x4158d0); a room then its node.
    // Other kinds keep their binding (one slot per kind).
    world.bound.room = r;
    world.bound.node = world.bound.view = node;
    if (!r->onEnter.empty())
        script.start(r->onEnter);
    return true;
}

bool Engine::switchNode(const std::string& name) {
    View* n = room ? room->node(name) : nullptr;
    if (!n) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "no node %s in room %s", name.c_str(),
                    room ? room->name.c_str() : "(none)");
        return false;
    }
    beginTransition();
    node = n;
    // The room remembers it: re-entering the room comes back here, unless a
    // script says otherwise with scSetLastVisited (0x415bd2: room+0xdd8 = node).
    room->lastVisited = n->name;
    world.bound.room = room;
    world.bound.node = world.bound.view = n;
    zooms.clear();
    overlay = nullptr;
    return true;
}

bool Engine::switchZoom(const std::string& name) {
    // Found by name across all rooms, the first created winning (0x403fd0).
    View* z = world.zoom(name);
    // "black" is never created by the scripts: it is an engine built-in used
    // as a blank screen around cutscenes and scripted narration.
    if (!z && name == "black") {
        static View black = [] { View v; v.name = "black"; v.isZoom = true; return v; }();
        z = &black;
    }
    if (!z) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "no zoom %s", name.c_str());
        return false;
    }
    beginTransition();
    world.bound.zoom = world.bound.view = z;
    zooms.push_back(z);
    overlay = nullptr;
    playZoomAudio(z, sc::ZOOM);
    return true;
}

bool Engine::switchOverlay(const std::string& name) {
    auto it = world.overlays.find(name);
    if (it == world.overlays.end()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "no overlay %s", name.c_str());
        return false;
    }
    beginTransition();
    overlay = it->second.get();
    world.bound.overlay = overlay;
    sound.playFx(overlay->args[1]);
    return true;
}

// scream.cfg (0x40f4a0, also scLoadSettings): the file is read as a stream of
// words (fscanf "%s"), so there is no line, comment or section syntax: a word
// that equals a key (case-sensitive) takes the next word as its value; others,
// like "[SCREAM]" or the comment text, are skipped. A number that doesn't parse
// leaves its word to be read as a key and gets the previous number read; a word
// like "1.5" yields 1 and leaves ".5". Nothing is range-checked.
bool Engine::loadSettings(const std::string& file) {
    auto path = findData(file);
    std::ifstream in(path);
    if (path.empty() || !in) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "settings file %s not found", file.c_str());
        return false;
    }
    std::deque<std::string> words;
    for (std::string w; in >> w;)
        words.push_back(w);
    Config& c = config;
    int lastNumber = 0;
    auto text = [&](std::string& v) {
        if (!words.empty()) v = words.front(), words.pop_front();
    };
    auto number = [&](int& v) {
        if (!words.empty()) {
            const std::string& w = words.front();
            size_t i = (w[0] == '-' || w[0] == '+') ? 1 : 0, digits = i;
            while (digits < w.size() && std::isdigit(static_cast<unsigned char>(w[digits]))) ++digits;
            if (digits > i) {
                lastNumber = std::stoi(w.substr(0, digits));
                std::string rest = w.substr(digits);
                words.pop_front();
                if (!rest.empty()) words.push_front(rest);
            }
        }
        v = lastNumber;
    };
    while (!words.empty()) {
        std::string key = words.front();
        words.pop_front();
        if (key == "SCRIPT") text(c.script);
        else if (key == "RESOURCE") text(c.resource), settings[sc::RES_FILE] = c.resource;
        else if (key == "PATH") text(c.path), c.path += "\\", settings[sc::RES_PATH] = c.path;
        else if (key == "WIDTH") number(c.width);
        else if (key == "HEIGHT") number(c.height);
        else if (key == "DEPTH") number(c.depth);
        else if (key == "QUALITY") number(c.quality);
        else if (key == "FULLSCREEN") number(c.fullscreen);
        else if (key == "SURROUND") number(c.surround);
        else if (key == "LOG") number(c.log);
        else if (key == "VSYNC") number(c.vsync);
        else if (key == "RATE") number(c.rate);
        else if (key == "AUDIO_DEVICE") number(c.audioDevice);
        else if (key == "BUFFER_SIZE") number(c.bufferSize);
        else if (key == "NUM_BUFFERS") number(c.numBuffers);
        else if (key == "DEBUG") number(c.debug);
        else if (key == "LANGUAGE") text(c.language);
        else if (key == "WIDESCREEN") number(c.widescreen);  // ours; scream.exe skips the words
    }
    applyConfig();
    return true;
}

// The cfg values live in the same variables as scSet/scGet/scEnable.
namespace {
std::ofstream gLogFile;
SDL_LogOutputFunction gDefaultLog = nullptr;
void* gDefaultLogData = nullptr;

void logOutput(void* data, int category, SDL_LogPriority priority, const char* message) {
    if (gDefaultLog)
        gDefaultLog(data, category, priority, message);
    if (gLogFile.is_open()) {
        char stamp[32];
        std::time_t t = std::time(nullptr);
        std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
        gLogFile << '[' << stamp << "] " << message << std::endl;
    }
}
}  // namespace

// SC_LOG (scream.cfg LOG, scEnable(SC_LOG), the L key): the original appends
// its log to scream.log in the game folder (0x404a30); ours goes to the save folder.
void Engine::setLogging(bool on) {
    if (!gDefaultLog) {
        SDL_GetLogOutputFunction(&gDefaultLog, &gDefaultLogData);
        SDL_SetLogOutputFunction(logOutput, gDefaultLogData);
    }
    if (on && !gLogFile.is_open())
        gLogFile.open(saveDir() / "scream.log", std::ios::app);
    else if (!on && gLogFile.is_open())
        gLogFile.close();
    settings[sc::LOG] = on ? 1.0 : 0.0;
}

void Engine::applyConfig() {
    const Config& c = config;
    settings[sc::WIDTH] = double(c.width);
    settings[sc::HEIGHT] = double(c.height);
    settings[sc::DEPTH] = double(c.depth);
    settings[sc::QUALITY] = double(c.quality);
    settings[sc::FULLSCREEN] = double(c.fullscreen);
    settings[sc::SURROUND] = double(c.surround);
    settings[sc::LOG] = double(c.log);
    settings[sc::DEBUG] = double(c.debug);
    settings[sc::LANGUAGE] = c.language;
    gMode.set(c.height);
    renderer.setNearest(c.quality == 0);
    setLogging(c.log != 0);
}

void Engine::abortGame() {
    // scream.exe stops the playlist, waits for a key or click, then returns to the main menu.
    sound.playlistStop(-1, true);
    aborted_ = true;
}

void Engine::leaveAbortedGame() {
    aborted_ = false;
    gameRunning_ = false;
    restoreState(baseline_);
    openMenu();
}

void Engine::playZoomAudio(const View* zoom, int when) {
    if (zoom)
        for (const auto& [kind, file] : zoom->audio)
            if (kind == when)
                sound.playFx(file);
}

void Engine::zoomOut() {
    if (!zooms.empty())
        beginTransition();
    overlay = nullptr;
    if (!zooms.empty()) {
        playZoomAudio(zooms.back(), sc::ZOOMOUT);
        zooms.pop_back();
        // Back to a parent zoom, scream.exe switches to it again, which binds it;
        // back to the node, nothing is rebound.
        if (!zooms.empty()) {
            world.bound.zoom = world.bound.view = zooms.back();
        }
    }
}

void Engine::zoomBack() {
    if (!zooms.empty())
        beginTransition();
    overlay = nullptr;
    if (!zooms.empty())
        playZoomAudio(zooms.back(), sc::ZOOMOUT);
    zooms.clear();
}

// scLookAt (0x412670). Instant: the angles of a table (0x41263c); up looks
// at y 6.28 and down at y -1.57, both facing the front. Smooth: a frame by
// frame turn (stepLook) to a second table (0x412654), where back is 0 or 6.28,
// whichever is nearer, and down looks level at the front (as compiled).
void Engine::lookAt(int face, bool smooth) {
    camAnim_.active = false;
    if (!smooth) {
        switch (face) {
        case sc::FRONT: setLook(3.14f, 0); break;
        case sc::BACK: setLook(0, 0); break;
        case sc::LEFT: setLook(1.57f, 0); break;
        case sc::RIGHT: setLook(4.71f, 0); break;
        case sc::UP: setLook(3.14f, 6.28f); break;
        case sc::DOWN: setLook(3.14f, -1.57f); break;
        }
        return;
    }
    if (face < sc::FRONT || face > sc::DOWN)
        return;
    if (flag(sc::WAIT))  // 0x412590: the wait cursor while it turns
        waitMode = true;
    CamAnim& a = camAnim_;
    getLook(a.yaw, a.y);
    a.yaw = (a.yaw * 0.15923566f - std::floor(a.yaw * 0.15923566f)) * 2.0f * 3.14f;  // into [0, 6.28)
    a.toY = 0;
    switch (face) {
    case sc::BACK: a.toYaw = a.yaw > 3.14f ? 6.28f : 0; break;
    case sc::LEFT: a.toYaw = 1.57f; break;
    case sc::RIGHT: a.toYaw = 4.71f; break;
    case sc::UP: a.toYaw = 3.14f, a.toY = 6.28f; break;
    default: a.toYaw = 3.14f; break;  // front, and down
    }
    a.yawDone = a.yDone = false;
    a.active = true;
    locked = true;  // 0x48bc0c for the whole turn, cleared at the end (so also any scLock)
}

// One frame of the smooth turn: each angle moves by (1 + distance) / 100 per
// frame (x 60/fps) until it reaches its target; the yaw is done once a step
// covers the distance. Two cases where scream.exe's loop would never end are
// made to end: a distance below -1 (its step turns negative and runs away),
// handled as moving by (1 + |distance|) toward the target, and a pitch that is
// already exactly on target, which the original never marks done.
void Engine::stepLook(double dt) {
    CamAnim& a = camAnim_;
    const float k = 60.0f * float(dt) * 0.01f;
    if (release2006) {
        // The 2006 engine turns both angles at a constant 0.015 per frame (P 0x4103d0).
        const float step = 0.015f * 60.0f * float(dt);
        auto move = [&](float& v, float to, bool& done) {
            if (done) return;
            if (std::abs(to - v) <= step) v = to, done = true;
            else v += to > v ? step : -step;
        };
        move(a.yaw, a.toYaw, a.yawDone);
        move(a.y, a.toY, a.yDone);
    }
    if (!release2006 && !a.yawDone) {
        const float d = a.toYaw - a.yaw;
        const float reach = d < 0 ? 1 - d : d;
        const float step = (1 + (d < -1 ? -d : d)) * k;
        a.yaw += d <= 0 ? -step : step;
        if (step >= reach) a.yawDone = true;
    }
    if (!release2006 && !a.yDone) {
        const float d = a.toY - a.y;
        const float step = (1 + std::abs(d)) * k;
        if (a.y < a.toY) {
            a.y += d < -1 ? step : (1 + d) * k;
            if (a.y >= a.toY) a.yDone = true;
        } else if (a.y > a.toY) {
            a.y -= d < -1 ? step : (1 + d) * k;
            if (!(a.y > a.toY)) a.yDone = true;
        } else {
            a.yDone = true;
        }
    }
    const float fovBase = release2006 ? 53.0f : 45.0f;
    if (flag(sc::DREAM))  // the field of view eases back to its base (0x4128e8)
        fx_.fov += fx_.fov > fovBase ? -0.5f : fx_.fov < fovBase ? 0.5f : 0.0f;
    setLook(a.yaw, a.y);
    if (a.yawDone && a.yDone) {
        a.active = false;
        locked = false;
        if (flag(sc::WAIT)) waitMode = false;  // 0x4125b0
    }
}

// The text setter (0x40e150), shared by scFeedback, dialogue and subtitles.
// Text is split by length into 1-4 balanced lines (breaking at the first
// space once a line has reached its share), '|' is a non-breaking space, and
// the display time is 100/SC_TXT_SPEED + length/16 seconds.
void Engine::feedback(const std::string& text, const std::string& voice) {
    if (!voice.empty())
        sound.playVoice(voice);
    if (text.size() >= (release2006 ? 256u : 400u))
        return;  // the original ignores text that doesn't fit its buffer
    if (text.empty()) {
        feedback_.count = 0;  // the original shows an empty line for 4 s
        return;
    }
    SDL_Log("[feedback] %s%s%s", text.c_str(), voice.empty() ? "" : "  ", voice.c_str());

    int n = int(text.size()), last, share;
    if (release2006) {  // lines of about 70 characters on its 800 pixel screen (P 0x40c500)
        if (n >= 211) last = 3, share = n / 4;
        else if (n >= 140) last = 2, share = n / 3;
        else if (n >= 70) last = 1, share = n / 2;
        else last = 0, share = 70;
    } else if (n > 300) last = 3, share = n / 4;
    else if (n > 200) last = 2, share = n / 3;
    else if (n > 100) last = 1, share = n / 2;
    else last = 0, share = 100;

    FeedbackText& f = feedback_;
    f.lines = {};
    std::string buf = text;
    int slot = last, run = 0;
    size_t from = 0;
    for (size_t i = 0; i < buf.size(); ++i, ++run) {
        if (buf[i] == ' ' && run >= share - 1) {
            // The original writes below lines[0] when a text splits into more
            // lines than planned; those extra lines are dropped here.
            if (slot >= 0)
                f.lines[size_t(slot)] = buf.substr(from, i - from);
            --slot;
            from = i + 1;
            ++i;  // the character after the break is taken without a check
            run = 0;
        } else if (buf[i] == '|') {
            buf[i] = ' ';
        }
    }
    if (slot >= 0)
        f.lines[size_t(slot)] = buf.substr(std::min(from, buf.size()));
    // (Slots the split didn't reach keep stale text in the original; here they are empty.)

    double speed = 50;
    if (auto* v = std::get_if<double>(&settings[sc::TXT_SPEED]); v && *v > 0)
        speed = *v;
    f.count = last + 1;
    f.start = textClock_;
    f.duration = int(100 / speed) + (n <= 16 ? 32 : n) / 16;
    f.fresh = true;
}

// scLoadSubtitle opens an .srt file next to the game data (0x418080).
void Engine::loadSubtitles(const std::string& file) {
    // The original's state is static and never reset; a fresh start per file is
    // equivalent for the game's single subtitle file.
    subs_ = {};
    std::ifstream in(findData(file), std::ios::binary);
    if (!in) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "scLoadSubtitle: cannot open %s", file.c_str());
        return;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    for (char c : ss.str())
        if (c != '\r')  // the original reads the file in text mode
            subs_.data += c;
    subs_.active = true;
}

// The subtitle player (0x4180b0), run every frame. Its clock is a counter of
// whole seconds since loading. When the current cue's end time is reached
// the text is cleared and the next cue is read; its text is shown on the
// first tick at or after its start time. Reading stops at the end of the
// file or at a cue numbered 13 (the empty last cue of sdc-intro.srt).
void Engine::updateSubtitles() {
    Subtitles& s = subs_;
    if (!s.active)
        return;
    auto skipSpace = [&] { while (s.pos < s.data.size() && std::isspace((unsigned char)s.data[s.pos])) ++s.pos; };
    auto token = [&] {
        skipSpace();
        size_t from = s.pos;
        while (s.pos < s.data.size() && !std::isspace((unsigned char)s.data[s.pos])) ++s.pos;
        return s.data.substr(from, s.pos - from);
    };
    auto line = [&] {  // fgets, without the newline; false at the end of the file
        if (s.pos >= s.data.size()) return std::optional<std::string>();
        size_t nl = s.data.find('\n', s.pos), to = nl == std::string::npos ? s.data.size() : nl;
        std::string l = s.data.substr(s.pos, to - s.pos);
        s.pos = nl == std::string::npos ? s.data.size() : nl + 1;
        return std::optional<std::string>(l);
    };
    // HH:MM:SS,mmm in tenths of a second, times 0.1. The original's hour factor
    // is 12000 (not 36000), and once the counter passes 100 an end time's
    // factor stops stepping down; both are kept.
    auto parseTime = [&](const std::string& tok, bool isEnd) {
        float sum = 0;
        int mult = 1000;
        for (size_t p = 0; p < tok.size(); p += 3) {
            if (tok[p] != ':' && tok[p] != ',') {
                int v = std::atoi(tok.c_str() + p);
                if (mult == 1) sum += float(v / 100);
                else if (mult == 10) sum += float(v * 10);
                else if (mult == 100) sum += float(v * 600);
                else if (mult == 1000) sum += float(v * 12000);
            }
            if (!isEnd || s.counter <= 100)
                mult /= 10;
        }
        return sum * 0.1f;
    };

    double t = now();
    if (s.end <= s.counter) {
        feedback("");
        s.pending = true;
        if (s.pos >= s.data.size())
            return;  // end of file: the original keeps clearing the text every frame
        if (std::atoi(token().c_str()) == 13) {
            s.active = false;
            return;
        }
        s.start = parseTime(token(), false);
        token();  // "-->"
        s.end = parseTime(token(), true);
        line();  // rest of the timing line
        s.text.clear();
        while (auto l = line()) {
            s.text += *l + " ";
            if (l->empty())
                break;
        }
    } else if (t - s.lastTick >= 1.0) {
        s.lastTick = t;
        if (s.start <= s.counter && s.pending) {
            feedback(s.text);
            s.pending = false;
        }
        s.counter += 1;
    }
}

void Engine::perform(const Action& a) {
    switch (a.kind) {
    // Walking somewhere plays the destination's footstep (scFootstep).
    case sc::NODE:
    case sc::ROOM:
        if (a.kind == sc::NODE ? switchNode(a.arg) : switchRoom(a.arg))
            if (node && !node->footstep.empty())
                sound.playChannel(sc::STEP, node->footstep, false);
        break;
    case sc::ZOOM: switchZoom(a.arg); break;
    case sc::OVERLAY: switchOverlay(a.arg); break;
    case sc::ZOOMOUT: zoomOut(); break;
    case sc::ZOOMBACK: zoomBack(); break;
    case sc::FEEDBACK: feedback(a.arg); break;
    case sc::FEEDBACK_EX: feedback(a.arg, a.arg2); break;
    case sc::CUSTOM: script.start(a.arg); break;
    default:
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "unhandled action kind %d (%s)", a.kind, a.arg.c_str());
    }
}

// scTimerCreate(id, seconds, fn) (0x412220) puts a timer in the bound room,
// scTimerCreateGlobal (0x412330) in the world. An id already in use re-arms
// that timer, except the newest one, which the search misses (slots are
// 1-based and it scans 0..count-1): re-using its id adds a second timer.
// Slot 0 is an unused entry with id 0, so a timer with id 0 never runs.
void Engine::addTimer(int id, int seconds, const std::string& fn, bool global) {
    Room* r = world.bound.room;
    if (!global && !r) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "scTimerCreate: no room bound");
        return;
    }
    std::vector<GameTimer>& list = global ? globalTimers_ : r->timers;
    if (id == 0)
        return;
    for (size_t i = 0; i + 1 < list.size(); ++i)
        if (list[i].id == id) {
            list[i] = {id, seconds, 0, true, fn};
            return;
        }
    if (list.size() >= 31) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Too many timers! (fatal in scream.exe)");
        return;
    }
    list.push_back({id, seconds, 0, true, fn});
}

// Once a second (0x412010): global timers count only in the node view
// (state 4000), the current room's timers in any state. A timer of N seconds
// fires on its Nth tick, then stays inactive until created again.
void Engine::updateTimers() {
    if (now() - timerTick_ < 1.0)
        return;
    timerTick_ = now();
    auto tick = [&](std::vector<GameTimer>& list) {
        for (size_t i = 0; i < list.size(); ++i) {
            GameTimer& t = list[i];
            if (!t.active || ++t.elapsed < t.seconds)
                continue;
            t.elapsed = 0;
            t.active = false;
            script.start(t.fn);
        }
    };
    const bool nodeState = view() && !view()->isZoom && !invOpen_ && !journalOpen_ && !overlay && !choice_ && !video_;
    if (nodeState)
        tick(globalTimers_);
    if (room)
        tick(room->timers);
}

// Zoom images are stretched over the whole view between the bars, whatever
// their size (800x472 originals, 1024x640 in the DC update).
Engine::ZoomRect Engine::zoomRect(const View&) {
    return {0, gMode.bar, float(sc::SCREEN_W), sc::SCREEN_H - 2 * gMode.bar};
}

// The view area of the 4:3 canvas only (2D screens over a 3D backdrop: zooms, the menu).
Viewport Engine::canvasViewport() const {
    int w, h;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    const Canvas c = Canvas::fit(w, h);
    const int top = int(c.y + gMode.bar * c.scale), bottom = int(c.y + (sc::SCREEN_H - gMode.bar) * c.scale);
    // Rounded as the image's own pixels are (by pixel centre), so the backdrop
    // covers exactly the columns the 2D image does.
    const int left = int(std::lround(c.x)), right = int(std::lround(c.x + sc::SCREEN_W * c.scale));
    return {left, top, right - left, bottom - top};
}

// The 3D view: full window width, between the 64 pixel UI bars (glViewport(0, 64, W, H-128)).
Viewport Engine::nodeViewport() const {
    if (!config.widescreen)
        return canvasViewport();  // 4:3 like the original's screen, black at the sides
    int w, h;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    Canvas c = Canvas::fit(w, h);
    int top = int(c.y + gMode.bar * c.scale), bottom = int(c.y + (sc::SCREEN_H - gMode.bar) * c.scale);
    return {0, top, w, bottom - top};
}

Object* Engine::hovered(int& edge) {
    edge = 0;
    if (invOpen_ || journalOpen_ || overlay)
        return nullptr;
    int w, h;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    float scale = SDL_GetWindowPixelDensity(window_);
    float mx = mouseX_ * scale, my = mouseY_ * scale;
    View* v = view();
    if (!v)
        return nullptr;

    if (v->isZoom) {
        Canvas c = Canvas::fit(w, h);
        ZoomRect zr = zoomRect(*v);
        float x = (c.toCanvasX(mx) - zr.x) * kZoomW / zr.w, y = (c.toCanvasY(my) - zr.y) * kZoomH / zr.h;
        for (auto& o : v->objects)
            if (o->enabled && o->rect.contains(int(x), int(y)))
                return o.get();
        return nullptr;
    }

    // Edge zones (0x40df60): 15% of the width at the sides; vertically 15% of
    // (H - 192), measured from the top bar and from H - 128, in the original's
    // screen pixels (so the bottom zone is the deeper one, as in scream.exe).
    Canvas c = Canvas::fit(w, h);
    const float zoneY = std::floor((gMode.h - 192) * kEdgeZone) * gMode.px;
    const float cy = c.toCanvasY(my);
    bool left, right, up, down;
    if (fpsStyle()) {
        // Alternate control: the zones close in to a pixel either side of the
        // centre, so the arrow shows the way the view is turning.
        const float dx = (mx - w / 2.0f) / c.scale / gMode.px, dy = (my - h / 2.0f) / c.scale / gMode.px;
        left = dx < -1, right = dx > 1, up = dy < -1, down = dy > 1;
    } else {
        left = mx < w * kEdgeZone, right = mx > w * (1 - kEdgeZone);
        up = cy < gMode.bar + zoneY, down = cy > sc::SCREEN_H - 2 * gMode.bar - zoneY;
    }
    if (slideshow_) {
        // Slideshow mode's zone test (0x407d50): a vertical zone wins in a
        // corner and only counts while the view can still turn that way. A
        // zone takes the click (0x4173be), so no hotspot shows there (0x414448).
        float yaw, y;
        getLook(yaw, y);
        edge = left ? sc::LEFT : right ? sc::RIGHT : 0;
        if (up && y <= 1.25f) edge = sc::UP;
        else if (down && y >= -1.25f) edge = sc::DOWN;
        if (edge)
            return nullptr;
    } else if (left) edge = sc::LEFT;
    else if (right) edge = sc::RIGHT;
    else if (up) edge = sc::UP;
    else if (down) edge = sc::DOWN;

    Viewport vp = nodeViewport();
    if (my < vp.y || my >= vp.y + vp.h)
        return nullptr;
    int face;
    float fx, fy;
    if (!pickFace(viewRay(cam, mx - vp.x, my - vp.y, vp.w, vp.h), face, fx, fy, sc::DEF_TEX_SIZE))
        return nullptr;
    for (auto& o : v->objects)
        if (o->enabled && o->face == face && o->rect.contains(int(fx), int(fy)))
            return o.get();
    return nullptr;
}

bool Engine::playCutscene(const std::string& file) {
    if (opts.turbo) {
        if (!res.exists(file))
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "missing cutscene %s", file.c_str());
        return false;
    }
    auto data = res.read(file);
    if (!data) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "missing cutscene %s", file.c_str());
        return false;
    }
    auto v = std::make_unique<Video>();
    if (!v->open(std::move(*data))) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "cannot play %s", file.c_str());
        return false;
    }
    video_ = std::move(v);
    videoStart_ = now();
    if (video_->advance(0))
        renderer.uploadInto(videoTex_, video_->width(), video_->height(), video_->rgba().data());
    return true;
}

void Engine::stopCutscene() {
    if (video_)
        beginTransition(0, false);  // from the last video frame (0x416d13), which leaves the lock alone
    video_.reset();
}

void Engine::beginTransition(float progress, bool unlock) {
    // scream.exe sets its lock flag (0x48bc0c) while it renders the old screen
    // for the capture and clears it afterwards, so every switch, journal/menu
    // change, camera key and overlay dismissal also releases scLock. Scripts
    // rely on it (scLock() ... scSwitch(SC_ZOOMOUT, "") with no scUnlock).
    if (unlock)
        locked = false;
    if (!window_ || capturing_ || opts.turbo)
        return;
    capturing_ = true;
    render();
    capturing_ = false;
    renderer.captureFrame(transition_.capture);
    int w = transition_.capture.width, h = transition_.capture.height;
    if (w <= 0 || h <= 0)
        return;
    // The view spans the whole window width (nodeViewport), wider than the 4:3
    // canvas on a widescreen window, so the capture covers that width too;
    // otherwise the sides would cut to the new view while the middle fades.
    Canvas c = Canvas::fit(w, h);
    transition_.u0 = 0;
    transition_.u1 = 1;
    transition_.x0 = -c.x / c.scale;
    transition_.x1 = (w - c.x) / c.scale;
    transition_.v0 = 1 - (c.y + gMode.bar * c.scale) / float(h);  // the capture is bottom row first
    transition_.v1 = 1 - (c.y + (sc::SCREEN_H - gMode.bar) * c.scale) / float(h);
    transition_.progress = progress;
    transition_.active = true;
}

// The capture over the view between the bars; the step is per frame drawn (x 60/fps).
void Engine::drawTransition() {
    Transition& t = transition_;
    if (!t.active || capturing_)
        return;
    renderer.drawImageUV(t.capture, t.x0, gMode.bar, t.x1 - t.x0, sc::SCREEN_H - 2 * gMode.bar, t.u0, t.v0, t.u1, t.v1,
                         1 - t.progress);
    double speed = 25;
    if (auto* v = std::get_if<double>(&settings[sc::MOV_SPEED])) speed = *v;
    t.progress += float(speed * 0.001 * frameDt_ * 60);
    if (t.progress >= 1) {
        t.active = false;
        t.progress = 0;
    }
}

void Engine::showChoice(const std::string& name) {
    auto it = world.choices.find(name);
    if (it == world.choices.end() || it->second->options.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "scChoiceGo: no choice %s", name.c_str());
        return;
    }
    choice_ = it->second.get();
    clearFeedback();
}

void Engine::canvasMouse(float& x, float& y) const {
    int w, h;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    float density = SDL_GetWindowPixelDensity(window_);
    Canvas c = Canvas::fit(w, h);
    x = c.toCanvasX(mouseX_ * density);
    y = c.toCanvasY(mouseY_ * density);
}

void Engine::ensureFont() {
    // Until the script picks one with scFont, use the engine's own font_def.tga.
    const std::string& want = font.empty() ? std::string("font_def.tga") : font;
    if (font_.name() != want && !font_.load(renderer, res, want))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "cannot load font %s", want.c_str());
}

// Choices, as scream.exe lays them out (0x415850, 0x40b250, hit test 0x417c90),
// in 1024x768 pixels: one row per option plus an empty bottom row, stacked up
// from the bottom bar 16 pixels apart with option 0 on top, left-aligned at
// x = 16. With three options the four rows drop 8 pixels (the bottom row off
// screen) and the hit rows shift by one. Hit rows span the full width and sit
// 8 pixels above the text. Returns the hit row's number (0 = none).
int Engine::choiceHit(float y) const {
    const int n = int(choice_->options.size());
    const float py = y * float(gMode.h) / sc::SCREEN_H;
    for (int k = 0; k <= n; ++k) {
        int lo = gMode.h - 16 * (n - k) - 32, hi = lo + 16;
        if (py >= float(lo) && py <= float(hi))
            return n == 3 ? k : k + 1;
    }
    return 0;
}

int Engine::choiceAt(float, float y) {
    if (!choice_)
        return -1;
    int v = choiceHit(y);
    return v >= 1 && v <= int(choice_->options.size()) ? v - 1 : -1;
}

void Engine::drawChoice() {
    const int n = int(choice_->options.size()), rows = n + 1, offset = rows == 4 ? -8 : 8;
    const float px = gMode.px;
    float mx, my;
    canvasMouse(mx, my);
    int v = choiceHit(my), hover = v ? n - v + 1 : -1;
    for (int j = 1; j < rows; ++j) {  // row 0 is the empty one
        const std::string& text = choice_->options[size_t(n - j)].first;
        float y = float(gMode.h - offset - 16 * j) * px;
        font_.drawMono(renderer, text, 16 * px, y, px, 1, {0.1f, 0.75f, 0.75f});
        if (j == hover)
            font_.drawMono(renderer, text, 16 * px, y, px, 1, {0.1f, 1, 1});
    }
}

// The feedback renderer (0x40aca0), in the original's 1024x768 screen pixels.
// Lines stack upward from the bottom bar, 16 pixels apart, centred assuming
// the 10 pixel advance; four-line texts show three lines at a time, the
// first three for half the display time. The fade runs once per line drawn:
// in at SC_TXT_SPEED/1000 and out at SC_TXT_SPEED/10000 per 60 Hz frame.
void Engine::drawFeedback() {
    FeedbackText& f = feedback_;
    if (f.count == 0)
        return;
    double speed = 50;
    if (auto* v = std::get_if<double>(&settings[sc::TXT_SPEED]); v && *v > 0)
        speed = *v;
    const float frames = float(frameDt_ * 60), fadeIn = float(speed * 0.001), fadeOut = float(speed * 0.0001);
    if (f.fresh) {
        f.fresh = false;
        f.fade = 1;
    }
    double t = textClock_ - f.start;
    int first = 0, end = f.count, offset;
    switch (f.count) {
    case 2: offset = 16; break;
    case 3: offset = 8; break;
    case 4:
        if (t >= f.duration * 0.5) end = 3, offset = 8;
        else first = 1, offset = -8;
        break;
    default: offset = 24;
    }
    const float px = gMode.px;
    for (int i = first; i < end; ++i) {
        if (t <= f.duration) {
            if (f.fade > 0) f.fade -= fadeIn * frames;
        } else {
            f.fade += fadeOut * frames;
        }
        const std::string& line = f.lines[size_t(i)];
        int x = gMode.w / 2 - int(line.size() / 2) * Font::kMonoAdvance + 1;
        int y = gMode.h - offset - 16 * i;
        font_.drawMono(renderer, line, float(x) * px, float(y) * px, px, std::clamp(1 - f.fade, 0.0f, 1.0f));
    }
    if (f.fade >= 1)
        f.count = 0;
}

void Engine::click() {
    if (menuOpen()) {
        menuClick();
        return;
    }
    if (aborted_) {
        leaveAbortedGame();
        return;
    }
    if (video_) {
        stopCutscene();  // click skips the cutscene
        return;
    }
    if (journalOpen_) {
        journalClick();
        return;
    }
    if (invOpen_) {
        inventoryClick();
        return;
    }
    if (overlay) {
        // A click dismisses the overlay, with a crossfade and its leave sound.
        beginTransition();
        sound.playFx(overlay->args[2]);
        overlay = nullptr;
        return;
    }
    if (choice_) {
        float x, y;
        canvasMouse(x, y);
        int i = choiceAt(x, y);
        if (i >= 0) {
            std::string fn = choice_->options[size_t(i)].second;
            choice_->lastSelected = i;
            choice_ = nullptr;
            script.start(fn);
        }
        return;
    }
    if (locked)
        return;
    if (slideshow_ && view() && !view()->isZoom) {
        // A click in an edge zone turns the view one step (0x4173be), even
        // under scStill or scWait or with an item held.
        int zone;
        hovered(zone);
        if (zone) {
            beginTransition(0.75f, false);
            slideshowStep();
            return;
        }
    }
    if (scriptBusy())
        return;
    int edge;
    Object* o = hovered(edge);
    if (o && held) {
        useHeldOn(o);
        return;
    }
    if (!o || o->click.empty())
        return;
    activate(o);
}

void Engine::activate(Object* o) {
    if (opts.trace)
        SDL_Log("[click] %s/%s/%s", room ? room->name.c_str() : "", view()->name.c_str(), o->name.c_str());
    world.bindClicked(room, view(), o);
    perform(o->click);
}

// The keyboard handler (0x4175b0). Key groups are as compiled in scream.exe,
// including the arrows: Left turns left, Up turns right, Right looks up.
void Engine::hotkey(int vk) {
    auto look = [&](float turn, std::optional<float> y) {  // 0x407d30 / 0x407d10, after a short fade
        beginTransition(0.75f);
        camAnim_.active = false;
        float yaw, cur;
        getLook(yaw, cur);
        setLook(yaw + turn, y ? *y : cur);
    };
    auto cycleSpeed = [&](int key, const char* name) {  // 50 -> 75 -> 25 -> 50
        double v = 50;
        if (auto* d = std::get_if<double>(&settings[key])) v = *d;
        const char* level;
        if (v > 49 && v < 51) v = 75, level = "FAST";
        else if (v == 75) v = 25, level = "SLOW";
        else v = 50, level = "NORMAL";
        settings[key] = v;
        if (release2006 && key == sc::TXT_SPEED && v == 75)
            name = "test speed";  // as the 2006 executables spell it
        feedback(std::string(name) + " " + level);
        saveUserSettings();
    };
    switch (vk) {
    case 0x25: case 'A': case 0x61: look(-1.57f, std::nullopt); break;
    case 0x26: case 'D': case 0x64: look(1.57f, std::nullopt); break;
    case 0x27: case 'W': case 0x77: look(0, 1.25f); break;
    case 0x28: case 'S': case 0x73: look(0, -1.25f); break;
    case 0x20: case 'E': case 0x65: look(0, 0.0f); break;
    case 'L': case 0x6c: setLogging(!gLogFile.is_open()); break;  // 0x4177b8
    case 0x70: {  // F1
        bool on = !hintsOn();
        settings[sc::HINTS] = on ? 1.0 : 0.0;
        feedback(on ? "hints ON" : "hints OFF");
        saveUserSettings();
        break;
    }
    case 0x71: {  // F2: the -RL/-RR music files play along from the next track on
        bool on = !(std::holds_alternative<double>(settings[sc::SURROUND]) && std::get<double>(settings[sc::SURROUND]) != 0);
        settings[sc::SURROUND] = on ? 1.0 : 0.0;
        feedback(on ? "surround music ON" : "surround music OFF");
        saveUserSettings();
        break;
    }
    case 0x74:  // F5: quicksave, anywhere but the menu (0x417aa5)
        if (!menuOpen()) {
            saveSlot(kQuickSlot);
            feedback("Game saved");
        }
        break;
    case 0x76:  // F7 (0x41765f): drop the held item, fade everything out over the
                // "black" zoom for a second, then load the quicksave (updateQuickLoad)
        if (slotUsed(kQuickSlot) && quickLoadAt_ < 0) {
            closeMenu();
            held = nullptr;
            sound.fadeAll(1);
            switchZoom("black");
            quickLoadAt_ = realTime() + 1;
        }
        break;
    case 0x78: cycleSpeed(sc::CAM_SPEED, "camera speed"); break;  // F9
    case 0x79: cycleSpeed(sc::TXT_SPEED, "text speed"); break;    // F10
    case 0x7b: {  // F12
        static const char* names[] = {"gamma correction DISABLED", "gamma correction LOW", "gamma correction NORMAL",
                                      "gamma correction HIGH"};
        gamma_ = (gamma_ + 1) % 4;
        feedback(names[gamma_]);
        saveUserSettings();
        break;
    }
    case 0x1b:  // Esc (the menu handles its own)
        if (escWaits_ > 0) {  // a waiting loop takes it (scSleep, voices, dialogue)
            escSkip_ = true;
            break;
        }
        if (menuOpen() || choice_)
            break;
        if (invOpen_) {
            beginTransition();
            invOpen_ = false;
        } else if (journalOpen_) {
            closeJournal();
        } else if (overlay) {
            beginTransition();
            sound.playFx(overlay->args[2]);
            overlay = nullptr;
        } else if (menuShown_) {
            if (!scriptBusy()) openMenu();
        } else {
            quit_ = true;  // 0x410740
        }
        break;
    }
}

// Gamma correction (0x4082a0): the view area is added onto itself at 0.33 / 0.66 / 1.0.
void Engine::drawGamma() {
    static const float level[] = {0, 0.33f, 0.66f, 1.0f};
    if (gamma_ <= 0 || gamma_ > 3 || capturing_)
        return;
    renderer.captureFrame(gammaCapture_);
    int w = gammaCapture_.width, h = gammaCapture_.height;
    if (w <= 0 || h <= 0)
        return;
    Canvas c = Canvas::fit(w, h);
    float u0 = c.x / float(w), u1 = (c.x + sc::SCREEN_W * c.scale) / float(w);
    float v0 = 1 - (c.y + gMode.bar * c.scale) / float(h), v1 = 1 - (c.y + (sc::SCREEN_H - gMode.bar) * c.scale) / float(h);
    renderer.drawImageUV(gammaCapture_, 0, gMode.bar, sc::SCREEN_W, sc::SCREEN_H - 2 * gMode.bar, u0, v0, u1, v1, level[gamma_], 1, 1,
                         1, Blend::AddAlpha);
}

void Engine::debugMouse(float x, float y) {
    int w, h;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    Canvas c = Canvas::fit(w, h);
    float density = SDL_GetWindowPixelDensity(window_);
    mouseX_ = (c.x + x * c.scale) / density;
    mouseY_ = (c.y + y * c.scale) / density;
}

void Engine::debugPress(int button) {
    if (button == 1) click();
    else if (button == 2) rightClick();
}

Object* Engine::findInView(const std::string& name) {
    if (View* v = view())
        for (auto& o : v->objects)
            if (o->enabled && o->name == name)
                return o.get();
    return nullptr;
}

void Engine::handleEvent(const SDL_Event& e) {
    switch (e.type) {
    case SDL_EVENT_QUIT: quit_ = true; break;
    case SDL_EVENT_KEY_DOWN: {
        // The original's keys are Windows virtual-key codes.
        SDL_Keycode k = e.key.key;
        int vk = 0;
        if (k >= SDLK_A && k <= SDLK_Z) vk = 'A' + int(k - SDLK_A);
        else if (k >= SDLK_F1 && k <= SDLK_F12) vk = 0x70 + int(k - SDLK_F1);
        else if (k >= SDLK_KP_1 && k <= SDLK_KP_9) vk = 0x61 + int(k - SDLK_KP_1);
        else if (k == SDLK_ESCAPE) vk = 0x1b;
        else if (k == SDLK_SPACE) vk = 0x20;
        else if (k == SDLK_LEFT) vk = 0x25;
        else if (k == SDLK_UP) vk = 0x26;
        else if (k == SDLK_RIGHT) vk = 0x27;
        else if (k == SDLK_DOWN) vk = 0x28;
        else if (k == SDLK_KP_COMMA) vk = 0x6c;  // VK_SEPARATOR
        if (aborted_)
            leaveAbortedGame();
        else if (video_ && (k == SDLK_ESCAPE || k == SDLK_SPACE))
            stopCutscene();
        else if (menuOpen() && k == SDLK_ESCAPE) {
            // The original's key handler ignores Esc in the menu and its prompts (0x417934).
        }
        else if (k == SDLK_F11)  // an addition of this port
            SDL_Log("screenshot: %s", renderer.capture("scream-screenshot.png").c_str());
        else if (vk && !video_)
            hotkey(vk);
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        if (relativeMouse_) {  // alternate control: our own pointer, moved by the deltas
            int w, h;
            SDL_GetWindowSize(window_, &w, &h);
            mouseX_ = std::clamp(mouseX_ + e.motion.xrel, 0.0f, float(w - 1));
            mouseY_ = std::clamp(mouseY_ + e.motion.yrel, 0.0f, float(h - 1));
        } else {
            mouseX_ = e.motion.x;
            mouseY_ = e.motion.y;
        }
        // Middle-drag look is an addition of this port; it obeys the same gates as edge panning.
        if (dragging_ && motion && !locked && !camAnim_.active && !(view() && view()->isZoom)) {
            cam.yaw = wrap180(cam.yaw - e.motion.xrel * 0.25f);
            const float limit = std::atan(release2006 ? 1.0f : kPitchLimit) * 180.0f / 3.14159265f;
            cam.pitch = std::clamp(cam.pitch + e.motion.yrel * 0.25f, -limit, limit);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT) click();
        else if (e.button.button == SDL_BUTTON_RIGHT) rightClick();
        else if (e.button.button == SDL_BUTTON_MIDDLE) dragging_ = true;
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_MIDDLE) dragging_ = false;
        break;
    }
}

// Edge panning as in scream.exe (0x407e20): speed grows with how deep the
// cursor is in the zone, scaled by SC_CAM_SPEED and an acceleration ramp that
// resets whenever the view stops. Pitch is the y of the look vector (|y| <= 1.75).
void Engine::panCamera(double dt) {
    int edge;
    hovered(edge);
    int w, h;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    Canvas c = Canvas::fit(w, h);
    float density = SDL_GetWindowPixelDensity(window_);
    float mx = mouseX_ * density, cy = c.toCanvasY(mouseY_ * density);

    double camSpeed = 50;
    if (auto* v = std::get_if<double>(&settings[sc::CAM_SPEED])) camSpeed = *v;
    // Zone depth is in the original's screen pixels (1024x768 mode).
    const float rate = kPanFactor * float(camSpeed) * 0.001f * panRamp_ * 60.0f * float(dt) / gMode.px;
    const float zoneY = std::floor((gMode.h - 192) * kEdgeZone) * gMode.px;

    float yaw, pitchY;  // pitchY: the DC's look y, or the 2006 pitch angle
    getLook(yaw, pitchY);
    const float kPitchLimit = pitchLimit();
    bool moved = false;
    // Depth is measured in canvas units so speed doesn't depend on window size.
    if (edge == sc::LEFT) { yaw -= (w * kEdgeZone - mx) / c.scale * rate; moved = true; }
    if (edge == sc::RIGHT) { yaw += (mx - w * (1 - kEdgeZone)) / c.scale * rate; moved = true; }
    if (edge == sc::UP && pitchY <= kPitchLimit) { pitchY += (gMode.bar + zoneY - cy) * rate; moved = true; }
    if (edge == sc::DOWN && pitchY >= -kPitchLimit) { pitchY -= (cy - (sc::SCREEN_H - 2 * gMode.bar - zoneY)) * rate; moved = true; }

    panRamp_ = moved ? std::min(1.0f, panRamp_ + kRampPerSecond * float(dt)) : 0.0f;
    setLook(yaw, std::clamp(pitchY, -kPitchLimit, kPitchLimit));
}

// One application of the camera function (0x407e20) for a slideshow click:
// a quarter turn sideways, and up or down straight to a look y of 2.0, level
// only when coming from exactly 1.75 (which in practice never happens).
void Engine::slideshowStep() {
    camAnim_.active = false;
    int w, h;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    Canvas c = Canvas::fit(w, h);
    float density = SDL_GetWindowPixelDensity(window_);
    const float mx = mouseX_ * density, cy = c.toCanvasY(mouseY_ * density);
    const float zoneY = std::floor((gMode.h - 192) * kEdgeZone) * gMode.px;
    float yaw, y;
    getLook(yaw, y);
    if (mx < w * kEdgeZone) yaw -= 0.7855f;
    else if (mx > w * (1 - kEdgeZone)) yaw += 0.7855f;
    // The 2006 engine steps its pitch angle between 0 and +-1.57 (P 0x407a40).
    const float edge = release2006 ? 1.57f : 1.75f, to = release2006 ? 1.57f : 2.0f;
    if (cy < gMode.bar + zoneY && !(y > edge)) y = y == -edge ? 0.0f : to;
    else if (cy > sc::SCREEN_H - 2 * gMode.bar - zoneY && !(y < -edge)) y = y == edge ? 0.0f : -to;
    if (yaw > 6.28f) yaw = 0;
    else if (yaw < 0) yaw = 6.28f;
    setLook(yaw, y);
}

// 0x407d10: the original's view angle (3.14 = front, 0 = back, 1.57 = left)
// and its vertical look: the DC's look vector is (sin a, y, -cos a); the
// 2006 engine's is (sin a, sin y, -cos a), y an angle (P 0x407a40).
void Engine::setLook(float yaw, float y) {
    cam.yaw = wrap180((yaw - 3.14f) * 180.0f / 3.14159265f);
    cam.pitch = std::atan(release2006 ? std::sin(y) : y) * 180.0f / 3.14159265f;
}

void Engine::getLook(float& yaw, float& y) const {
    yaw = cam.yaw * 3.14159265f / 180.0f + 3.14f;
    y = std::tan(cam.pitch * 3.14159265f / 180.0f);
    if (release2006)
        y = std::asin(std::clamp(y, -1.0f, 1.0f));
}

float Engine::pitchLimit() const { return release2006 ? 1.57f : kPitchLimit; }

// Alternate control as it acts. A fix of this port: with slideshow mode on as
// well, slideshow mode wins. In scream.exe both apply and the pair is
// unusable: the cursor is drawn fixed in the middle while the real pointer
// moves unseen (slideshow skips the recentring, 0x416a62; the sprite stays
// at the centre, 0x416b91), and the edge zones shrink to a pixel round the
// centre (0x40df60), so almost every click is a tiny alternate-control turn.
bool Engine::fpsStyle() const { return fpsSetting() && !slideshow_; }

bool Engine::fpsSetting() const {
    auto it = settings.find(sc::FPS_STYLE);
    auto* v = it == settings.end() ? nullptr : std::get_if<double>(&it->second);
    return v && *v != 0;
}

bool Engine::fpsActive() const {
    return fpsStyle() && window_ && !menuOpen() && !invOpen_ && !journalOpen_ && !choice_ && !video_ && !aborted_ &&
           view() && !view()->isZoom;
}

// Alternate control (0x407e20 with 0x4df8e10 = 1): every frame the view turns
// by the pointer's distance from the screen centre (beyond one pixel), with no
// camera speed and no ramp; up to a look y of 1.75, down to -2.0.
void Engine::fpsCamera(double dt, bool recenter) {
    int w, h;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    Canvas c = Canvas::fit(w, h);
    float density = SDL_GetWindowPixelDensity(window_);
    // Offsets in the original's screen pixels (1024x768 mode).
    const float dx = (mouseX_ * density - w / 2.0f) / c.scale / gMode.px;
    const float dy = (mouseY_ * density - h / 2.0f) / c.scale / gMode.px;
    const float step = kFpsLook * 60.0f * float(dt);
    float yaw = cam.yaw * 3.14159265f / 180.0f;
    float pitchY = std::tan(cam.pitch * 3.14159265f / 180.0f);
    if (dx < 0) yaw -= (-dx - 1) * step;
    else if (dx > 0) yaw += (dx - 1) * step;
    if (dy < 0 && pitchY <= kPitchLimit) pitchY += (-dy - 1) * step;
    else if (dy > 0 && pitchY >= kFpsPitchDown) pitchY -= (dy - 1) * step;
    cam.yaw = wrap180(yaw * 180.0f / 3.14159265f);
    cam.pitch = std::atan(pitchY) * 180.0f / 3.14159265f;
    if (recenter)
        recenterPointer(false);
}

// The pointer springs back (0x416a8c): a tenth of the way to the centre per
// frame (at least a pixel), or straight there (leaving the menu or journal).
void Engine::recenterPointer(bool now) {
    if (!window_ || opts.turbo || (!now && (locked || capturing_)))
        return;
    int w, h;
    SDL_GetWindowSize(window_, &w, &h);
    const float cx = w / 2.0f, cy = h / 2.0f;
    float x = mouseX_, y = mouseY_;
    if (now) {
        x = cx;
        y = cy;
    } else {
        const float k = 1 - std::pow(0.9f, 60.0f * float(frameDt_));
        auto pull = [&](float v, float to) {
            float d = (to - v) * k;
            if (std::abs(to - v) >= 1 && std::abs(d) < 1) d = to > v ? 1.0f : -1.0f;
            return std::abs(to - v) < 1 ? to : v + d;
        };
        x = pull(x, cx);
        y = pull(y, cy);
    }
    if (x == mouseX_ && y == mouseY_)
        return;
    mouseX_ = x;
    mouseY_ = y;
    // In relative mode the system pointer is captured and only ours moves.
    if (!relativeMouse_ && SDL_GetMouseFocus() == window_)  // never move it while it is elsewhere (test runs)
        SDL_WarpMouseInWindow(window_, x, y);
}

// scream.exe springs the system pointer back with SetCursorPos. Doing that on
// macOS makes a quick back-and-forth count as "shake to locate", which shows a
// huge pointer; so while alternate control drives the view, the system pointer
// is captured (relative mode) and the pointer that springs back is our own.
void Engine::updateRelativeMouse() {
    const bool want = fpsActive() && !opts.turbo && SDL_GetKeyboardFocus() == window_;
    if (want == relativeMouse_)
        return;
    relativeMouse_ = want;
    SDL_SetWindowRelativeMouseMode(window_, want);
    if (!want)
        SDL_WarpMouseInWindow(window_, mouseX_, mouseY_);  // the system pointer reappears where ours was
}

// The setup launcher (or an editor) saved scream.cfg while the game runs:
// read it again and apply what can change live. scream.exe read it once;
// this is the port's addition, with nothing shown in-game.
void Engine::watchConfig() {
    if (opts.turbo || realTime() < configCheck_)
        return;
    configCheck_ = realTime() + 1;
    std::error_code ec;
    const auto path = findData("scream.cfg");
    const auto time = path.empty() ? std::filesystem::file_time_type{} : std::filesystem::last_write_time(path, ec);
    if (ec || time == configTime_)
        return;
    const bool first = configTime_ == std::filesystem::file_time_type{};
    configTime_ = time;
    if (first || !window_)
        return;  // read at startup already
    const Config before = config;
    config = Config{};  // keys taken out of the file go back to their defaults
    loadSettings("scream.cfg");
    SDL_Log("scream.cfg changed: applied");
    if (opts.fullscreen < 0 && config.fullscreen != before.fullscreen)
        SDL_SetWindowFullscreen(window_, config.fullscreen != 0);
    if (opts.windowW <= 0 && !config.fullscreen && (config.width != before.width || config.height != before.height))
        SDL_SetWindowSize(window_, config.width, config.height);
    if (config.vsync != before.vsync)
        SDL_GL_SetSwapInterval(config.vsync ? 1 : 0);
    if ((config.quality == 0) != (before.quality == 0))
        renderer.refilterAll();
}

void Engine::updateQuickLoad() {
    if (quickLoadAt_ < 0 || realTime() < quickLoadAt_)
        return;
    quickLoadAt_ = -1;
    zoomOut();
    sound.stopAll();
    if (loadSlot(kQuickSlot))
        gameRunning_ = true;
    feedback("Game loaded");
}

void Engine::update(double dt) {
    frameDt_ = dt;
    watchConfig();
    updateQuickLoad();
    sound.setSurround(std::holds_alternative<double>(settings[sc::SURROUND]) && std::get<double>(settings[sc::SURROUND]) != 0);
    if (menuOpen()) {
        sound.update(dt);  // menu music only; the game is paused
        updateEffects(dt);  // the menu background's clouds move on
        updateMenu();
        return;
    }
    double t = now();
    updateEffects(dt);
    if (camAnim_.active) {
        stepLook(dt);
    } else if (motion && !locked && !scriptBusy() && view() && !view()->isZoom && !dragging_ && !aborted_ &&
               !invOpen_ && !journalOpen_ && !slideshow_) {  // slideshow: the view only turns on clicks (0x416a62)
        if (fpsStyle()) fpsCamera(dt);
        else panCamera(dt);
    } else {
        panRamp_ = 0;
    }

    updateTimers();
    if (video_) {
        if (video_->advance(t - videoStart_))
            renderer.uploadInto(videoTex_, video_->width(), video_->height(), video_->rgba().data());
        if (video_->finished())
            stopCutscene();
    }
    sound.update(dt);
    script.update();
    updateSubtitles();
}

// Cursor rules from scream.exe's main loop: hidden while locked unless wait
// mode is on (scWait), in which case the wait cursor shows.
void Engine::drawCursor(Object* hover, int edge) {
    int kind;
    if (capturing_) return;
    if (aborted_ || (locked && !waitMode && !menuOpen())) return;
    if (held && !locked && !menuOpen()) {
        // Cursor mode 0xcb (0x414582, 0x4140fe): over a hotspot whose click is
        // feedback or a handler (not navigation), or in the inventory over
        // another item.
        bool glow;
        if (invOpen_) {
            float x, y;
            canvasMouse(x, y);
            int slot = slotAt(x, y);
            glow = slot >= 0 && inventory[size_t(slot)] != held;
        } else {
            glow = hover && (hover->click.kind == sc::FEEDBACK || hover->click.kind == sc::CUSTOM ||
                             hover->click.kind == sc::FEEDBACK_EX);
        }
        drawHeldCursor(glow);
        return;
    }
    if (menuOpen() && menuBlackUntil_ > 0 && flag(sc::WAIT)) kind = sc::WAIT;  // the menu's scSleep
    else if (menuOpen()) kind = sc::MENU_CURSOR;  // set on opening (0x412ed9)
    else if (journalOpen_) {
        // Over the pages it turns them, below them it closes the book (0x413f80).
        float x, y;
        canvasMouse(x, y);
        kind = y / gMode.px > 604 ? sc::ZOOMOUT : sc::USE;
    } else if (invOpen_) {
        float x, y;  // over an item, with nothing held (0x414070)
        canvasMouse(x, y);
        kind = slotAt(x, y) >= 0 ? sc::USE : sc::NORMAL;
    } else if (choice_) kind = sc::NORMAL;
    else if (locked || scriptBusy()) kind = sc::WAIT;
    else if (hover) kind = hover->cursor;
    else if (edge && (motion || slideshow_)) kind = edge;  // SC_LEFT..SC_DOWN double as the pan cursors
    else kind = sc::NORMAL;
    auto it = cursors.find(kind);
    if (it == cursors.end())
        it = cursors.find(sc::NORMAL);
    if (it == cursors.end())
        return;
    if (auto* t = renderer.texture(res, it->second)) {
        float x, y;
        canvasMouse(x, y);
        if (fpsActive()) {  // alternate control: the cursor stays at the centre (0x416b91)
            x = sc::SCREEN_W / 2.0f;
            y = sc::SCREEN_H / 2.0f;
        }
        // Always a 64 pixel quad (0x40b400), whatever the image size, reaching
        // `lead` pixels up and left of the pointer: 8 for the menu, forward and
        // look cursors, 24 for normal, use and zoom-out, else centred.
        int lead = 32;
        if (kind == sc::MENU_CURSOR || kind == sc::FORWARD || kind == sc::LOOK) lead = 8;
        else if (kind == sc::NORMAL || kind == sc::USE || kind == sc::ZOOMOUT || kind == sc::ZOOMBACK) lead = 24;
        const float px = gMode.px;
        renderer.drawImage(*t, x - lead * px, y - lead * px, 64 * px, 64 * px);
    }
}

void Engine::render() {
    int w, h;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    renderer.beginFrame(w, h);
    if (aborted_)
        return;
    if (menuOpen()) {
        drawMenu();
        drawCursor(nullptr, 0);
        return;
    }
    if (video_) {
        // Videos share the zoom frame: full canvas width at their own aspect.
        float vh = sc::SCREEN_W * float(video_->height()) / float(video_->width());
        if (videoTex_.id)
            renderer.drawImage(videoTex_, 0, (sc::SCREEN_H - vh) / 2, sc::SCREEN_W, vh);
        ensureFont();
        if (font_.loaded())
            drawFeedback();
        return;
    }
    View* v = view();
    if (journalOpen_) {
        drawJournal();
        drawGamma();
        drawTransition();
        drawFeedback();
        drawCursor(nullptr, 0);
        return;
    }
    // The inventory screen (0x409680) replaces the scene: the frame captured
    // on opening, in sepia, fading to half while the icons fade in.
    if (invOpen_) {
        drawInventoryBackdrop();
    } else {
        if (v && !v->isZoom) {
            for (const auto& f : v->faces)
                renderer.texture(res, f);
            Viewport vp = nodeViewport();
            drawNodeScene(*v, vp);
            if (showHotspots())
                renderer.drawNodeHotspots(*v, cam, vp);
        } else if (v) {
            ZoomRect zr = zoomRect(*v);
            if (auto* t = renderer.texture(res, v->image)) {
                drawZoomScene(*v, *t, zr);
            }
            if (showHotspots()) {
                float sx = zr.w / kZoomW, sy = zr.h / kZoomH;
                for (auto& o : v->objects)
                    if (o->enabled)
                        renderer.drawRect(zr.x + o->rect.x * sx, zr.y + o->rect.y * sy, o->rect.w * sx, o->rect.h * sy, 1, 0.8f, 0, 0.3f);
            }
        }
        // Overlays cover the 3D view area (0x40aae0 draws them between the UI bars).
        if (overlay)
            if (auto* t = renderer.texture(res, overlay->args[0]))
                renderer.drawImage(*t, 0, gMode.bar, sc::SCREEN_W, sc::SCREEN_H - 2 * gMode.bar);
    }
    drawGamma();
    if (!invOpen_)
        drawTransition();

    ensureFont();
    if (invOpen_)
        drawInventory();
    if (font_.loaded()) {
        if (choice_) drawChoice();
        else drawFeedback();
    }

    int edge;
    Object* o = hovered(edge);
    drawCursor(o, edge);

    std::string title = "SCream Redux";
    if (room) title += " | " + room->name + " / " + (v ? v->name : "");
    if (o) title += " | " + o->name;
    SDL_SetWindowTitle(window_, title.c_str());
}

void Engine::run() {
    if (opts.check) {
        checkResult_ = checkWorld();
        return;
    }
    if (!init())
        return;
    // The script has built the world: this is the state every new game starts from.
    captureBaseline();
    // From now on scBind(SC_OBJECT) follows the engine state, as in scream.exe.
    world.objectScope = [this] {
        using S = World::Scope;
        if (video_ || invOpen_ || journalOpen_ || menuOpen() || overlay || choice_)
            return S::None;
        return zooms.empty() ? S::Node : S::Zoom;
    };
    // The DC exe starts with alternate control on (0x40f451, nothing saves it);
    // the 2006 one has no such option. We keep the player's choice across runs.
    settings[sc::FPS_STYLE] = release2006 ? 0.0 : 1.0;
    loadUserSettings();

    if (!opts.start.empty()) {
        // Debug: jump straight to a location.
        auto colon = opts.start.find(':');
        gameRunning_ = menuShown_ = switchRoom(opts.start.substr(0, colon));
        if (colon != std::string::npos)
            switchNode(opts.start.substr(colon + 1));
    } else if (!opts.game.empty()) {
        menuShown_ = true;
        startNewGame(opts.game == "tlv" ? 1 : 0);
    } else {
        // As the original: the intro handler (logo videos), then the main menu.
        if (auto it = menu.extra.find(sc::INTRO); it != menu.extra.end())
            script.start(it->second);
        menuPending_ = true;
    }

    double last = realTime();
    const double started = last;
    int settledFrames = 0;
    bool execPending = !opts.exec.empty();
    while (!quit_) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            handleEvent(e);
        double t = realTime();
        double dt = t - last;
        last = t;
        if (opts.turbo)
            dt = kTurboStep;  // fixed, fast game time per frame
        if (!menuOpen())
            gameClock_ += dt;
        textClock_ += dt;
        updateRelativeMouse();
        update(dt);
        if (opts.crawl && !crawlStep())
            quit_ = true;
        if (opts.explore && !exploreStep())
            quit_ = true;

        if (menuPending_ && !script.busy()) {
            menuPending_ = false;
            openMenu();
        }
        if (quitWhenIdle_ && !script.busy())
            quit_ = true;
        if (execPending && !script.busy()) {
            execPending = false;
            script.startCode(opts.exec, "exec");
        }
        if (!(opts.crawl || opts.explore) || crawlRender_) {
            crawlRender_ = false;
            render();
        }

        if (!opts.screenshot.empty() && opts.screenshotAt >= 0) {
            if (t - started >= opts.screenshotAt) {
                SDL_Log("screenshot: %s", renderer.capture(opts.screenshot.c_str()).c_str());
                quit_ = true;
            }
        } else if (!opts.screenshot.empty() && !execPending) {
            settledFrames = script.busy() || camAnim_.active || menuPending_ ? 0 : settledFrames + 1;
            if (settledFrames == 5) {
                if (!menuOpen()) {
                    cam.yaw = opts.yaw;
                    cam.pitch = opts.pitch;
                }
                render();
                SDL_Log("screenshot: %s", renderer.capture(opts.screenshot.c_str()).c_str());
                quit_ = true;
            }
        }
        SDL_GL_SwapWindow(window_);
    }
}
