#include "engine.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>

#include "constants.hpp"
#include "gl.hpp"

namespace {

// Camera input, from scream.exe (0x407e20, 0x40df60). The original works per
// frame and scales by 60/fps; these are the equivalent per-second values.
constexpr float kEdgeZone = 0.15f;       // edge zones: 15% of the width / view height
constexpr float kPanFactor = 0.005f;     // radians per pixel of zone depth, x CAM_SPEED/1000
constexpr float kRampPerSecond = 1.5f;   // acceleration ramp: +0.025 per frame at 60 fps
constexpr float kPitchLimit = 1.75f;     // pitch is the look vector's y; |y| <= 1.75 (~60.3 deg)
constexpr float kPx = float(sc::SCREEN_W) / Engine::kOrigW;  // canvas units per original pixel (1024x768 mode)
constexpr float kBar = 64 * kPx;         // UI bars above and below the 3D view: 64 pixels
constexpr double kLookDuration = 0.6;    // seconds for scLookAt(..., SC_SMOOTH)
constexpr double kTurboStep = 0.5;       // --turbo: game seconds per frame

float faceYaw(int face) {
    switch (face) {
    case sc::RIGHT: return 90;
    case sc::BACK: return 180;
    case sc::LEFT: return 270;
    default: return 0;
    }
}

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
    settings[sc::RES_FILE] = std::string("scratches.res");
    settings[sc::SHOW_HOTSPOTS] = opts.hotspots ? 1.0 : 0.0;
    flags.insert(sc::WAIT);  // enabled by default in scream.exe (0x40f260)
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
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    SDL_WindowFlags flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (!opts.windowed)
        flags |= SDL_WINDOW_FULLSCREEN;
    window_ = SDL_CreateWindow("SCream Redux", 1024, 768, flags);
    if (!window_ || !(gl_ = SDL_GL_CreateContext(window_))) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "window/GL: %s", SDL_GetError());
        return false;
    }
    SDL_GL_SetSwapInterval(opts.turbo ? 0 : 1);
    if (const char* missing = gl::load()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "missing GL entry point %s", missing);
        return false;
    }
    if (!renderer.init())
        return false;
    SDL_HideCursor();
    sound.init();
    sound.setSilent(opts.turbo);
    loadSettings("scream.cfg");  // like scream.exe at startup; the window keeps the command-line mode
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
    // Timers created with scTimerCreate belong to the location they were made in.
    std::erase_if(timers_, [](const Timer& t) { return !t.global; });
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
    std::erase_if(timers_, [](const Timer& t) { return !t.global; });
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

bool Engine::loadSettings(const std::string& file) {
    auto path = findData(file);
    std::ifstream in(path);
    if (path.empty() || !in) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "settings file %s not found", file.c_str());
        return false;
    }
    static const std::map<std::string, int> keys = {
        {"WIDTH", sc::WIDTH}, {"HEIGHT", sc::HEIGHT}, {"DEPTH", sc::DEPTH}, {"QUALITY", sc::QUALITY},
        {"FULLSCREEN", sc::FULLSCREEN}, {"SURROUND", sc::SURROUND}, {"DEBUG", sc::DEBUG},
        {"LANGUAGE", sc::LANGUAGE}, {"RESOURCE", sc::RESOURCE}, {"LOG", sc::LOG}};
    std::string line;
    while (std::getline(in, line)) {
        line = line.substr(0, line.find(';'));
        std::istringstream words(line);
        std::string key, value;
        if (!(words >> key >> value))
            continue;
        auto it = keys.find(key);
        if (it == keys.end())
            continue;  // SCRIPT, PATH, VSYNC, RATE, AUDIO_DEVICE, BUFFER_SIZE, NUM_BUFFERS
        char* end = nullptr;
        double num = std::strtod(value.c_str(), &end);
        if (end && *end == 0) settings[it->second] = num;
        else settings[it->second] = value;
    }
    return true;
}

void Engine::abortGame() {
    // scream.exe stops the playlist, waits for a key or click, then returns to the main menu.
    sound.playlistStop(-1);
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

void Engine::lookAt(int face, bool smooth) {
    float yaw = face == sc::UP || face == sc::DOWN ? cam.yaw : faceYaw(face);
    const float limit = std::atan(kPitchLimit) * 180.0f / 3.14159265f;
    float pitch = face == sc::UP ? limit : face == sc::DOWN ? -limit : 0.0f;
    if (!smooth) {
        cam.yaw = yaw;
        cam.pitch = pitch;
        camAnim_.active = false;
        return;
    }
    camAnim_ = {true, cam.yaw, cam.yaw + wrap180(yaw - cam.yaw), cam.pitch, pitch, now(), kLookDuration};
}

// The text setter (0x40e150), shared by scFeedback, dialogue and subtitles.
// Text is split by length into 1-4 balanced lines (breaking at the first
// space once a line has reached its share), '|' is a non-breaking space, and
// the display time is 100/SC_TXT_SPEED + length/16 seconds.
void Engine::feedback(const std::string& text, const std::string& voice) {
    if (!voice.empty())
        sound.playVoice(voice);
    if (text.size() >= 400)
        return;  // the original ignores text that doesn't fit its buffer
    if (text.empty()) {
        feedback_.count = 0;  // the original shows an empty line for 4 s
        return;
    }
    SDL_Log("[feedback] %s%s%s", text.c_str(), voice.empty() ? "" : "  ", voice.c_str());

    int n = int(text.size()), last, share;
    if (n > 300) last = 3, share = n / 4;
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
    f.start = now();
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

void Engine::addTimer(double seconds, const std::string& fn, bool global) {
    timers_.push_back({now() + seconds, fn, global});
}

// Zoom images are stretched over the whole view between the bars, whatever
// their size (800x472 originals, 1024x640 in the DC update).
Engine::ZoomRect Engine::zoomRect(const View&) {
    return {0, kBar, float(sc::SCREEN_W), sc::SCREEN_H - 2 * kBar};
}

// The 3D view: full window width, between the 64 pixel UI bars (glViewport(0, 64, W, H-128)).
Viewport Engine::nodeViewport() const {
    int w, h;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    Canvas c = Canvas::fit(w, h);
    int top = int(c.y + kBar * c.scale), bottom = int(c.y + (sc::SCREEN_H - kBar) * c.scale);
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
    const float zoneY = std::floor((kOrigH - 192) * kEdgeZone) * kPx;
    const float cy = c.toCanvasY(my);
    if (mx < w * kEdgeZone) edge = sc::LEFT;
    else if (mx > w * (1 - kEdgeZone)) edge = sc::RIGHT;
    else if (cy < kBar + zoneY) edge = sc::UP;
    else if (cy > sc::SCREEN_H - 2 * kBar - zoneY) edge = sc::DOWN;

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
    Canvas c = Canvas::fit(w, h);
    transition_.u0 = c.x / float(w);
    transition_.u1 = (c.x + sc::SCREEN_W * c.scale) / float(w);
    transition_.v0 = 1 - (c.y + kBar * c.scale) / float(h);  // the capture is bottom row first
    transition_.v1 = 1 - (c.y + (sc::SCREEN_H - kBar) * c.scale) / float(h);
    transition_.progress = progress;
    transition_.active = true;
}

// The capture over the view between the bars; the step is per frame drawn (x 60/fps).
void Engine::drawTransition() {
    Transition& t = transition_;
    if (!t.active || capturing_)
        return;
    renderer.drawImageUV(t.capture, 0, kBar, sc::SCREEN_W, sc::SCREEN_H - 2 * kBar, t.u0, t.v0, t.u1, t.v1,
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
    const float py = y * float(kOrigH) / sc::SCREEN_H;
    for (int k = 0; k <= n; ++k) {
        int lo = kOrigH - 16 * (n - k) - 32, hi = lo + 16;
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
    const float px = kPx;
    float mx, my;
    canvasMouse(mx, my);
    int v = choiceHit(my), hover = v ? n - v + 1 : -1;
    for (int j = 1; j < rows; ++j) {  // row 0 is the empty one
        const std::string& text = choice_->options[size_t(n - j)].first;
        float y = float(kOrigH - offset - 16 * j) * px;
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
    double t = now() - f.start;
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
    const float px = kPx;
    for (int i = first; i < end; ++i) {
        if (t <= f.duration) {
            if (f.fade > 0) f.fade -= fadeIn * frames;
        } else {
            f.fade += fadeOut * frames;
        }
        const std::string& line = f.lines[size_t(i)];
        int x = kOrigW / 2 - int(line.size() / 2) * Font::kMonoAdvance + 1;
        int y = kOrigH - offset - 16 * i;
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
    if (locked || scriptBusy())
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
        cam.yaw = wrap180(cam.yaw + turn * 180.0f / 3.14159265f);
        if (y) cam.pitch = std::atan(*y) * 180.0f / 3.14159265f;
    };
    auto cycleSpeed = [&](int key, const char* name) {  // 50 -> 75 -> 25 -> 50
        double v = 50;
        if (auto* d = std::get_if<double>(&settings[key])) v = *d;
        const char* level;
        if (v > 49 && v < 51) v = 75, level = "FAST";
        else if (v == 75) v = 25, level = "SLOW";
        else v = 50, level = "NORMAL";
        settings[key] = v;
        feedback(std::string(name) + " " + level);
        saveUserSettings();
    };
    switch (vk) {
    case 0x25: case 'A': case 0x61: look(-1.57f, std::nullopt); break;
    case 0x26: case 'D': case 0x64: look(1.57f, std::nullopt); break;
    case 0x27: case 'W': case 0x77: look(0, 1.25f); break;
    case 0x28: case 'S': case 0x73: look(0, -1.25f); break;
    case 0x20: case 'E': case 0x65: look(0, 0.0f); break;
    case 'L': case 0x6c: opts.trace = !opts.trace; break;  // the original toggles its log file
    case 0x70: {  // F1
        bool on = !hintsOn();
        settings[sc::HINTS] = on ? 1.0 : 0.0;
        feedback(on ? "hints ON" : "hints OFF");
        saveUserSettings();
        break;
    }
    case 0x71: {  // F2 (only the setting: this port plays the front channels only)
        bool on = !(std::holds_alternative<double>(settings[sc::SURROUND]) && std::get<double>(settings[sc::SURROUND]) != 0);
        settings[sc::SURROUND] = on ? 1.0 : 0.0;
        feedback(on ? "surround music ON" : "surround music OFF");
        saveUserSettings();
        break;
    }
    case 0x74:  // F5: quicksave
        if (!menuOpen() && gameRunning_ && saveSlot(kQuickSlot))
            feedback("Game saved");
        break;
    case 0x76:  // F7: quickload
        if (!slotDescription(kQuickSlot).empty() && loadSlot(kQuickSlot)) {
            closeMenu();
            feedback("Game loaded");
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
    float v0 = 1 - (c.y + kBar * c.scale) / float(h), v1 = 1 - (c.y + (sc::SCREEN_H - kBar) * c.scale) / float(h);
    renderer.drawImageUV(gammaCapture_, 0, kBar, sc::SCREEN_W, sc::SCREEN_H - 2 * kBar, u0, v0, u1, v1, level[gamma_], 1, 1,
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
        else if (menuOpen() && k == SDLK_ESCAPE)
            menuBack();
        else if (k == SDLK_F11)  // an addition of this port
            SDL_Log("screenshot: %s", renderer.capture("scream-screenshot.png").c_str());
        else if (vk && !video_)
            hotkey(vk);
        break;
    }
    case SDL_EVENT_MOUSE_MOTION:
        mouseX_ = e.motion.x;
        mouseY_ = e.motion.y;
        // Middle-drag look is an addition of this port; it obeys the same gates as edge panning.
        if (dragging_ && motion && !locked && !camAnim_.active && !(view() && view()->isZoom)) {
            cam.yaw = wrap180(cam.yaw - e.motion.xrel * 0.25f);
            const float limit = std::atan(kPitchLimit) * 180.0f / 3.14159265f;
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
    const float rate = kPanFactor * float(camSpeed) * 0.001f * panRamp_ * 60.0f * float(dt) / kPx;
    const float zoneY = std::floor((kOrigH - 192) * kEdgeZone) * kPx;

    float yaw = cam.yaw * 3.14159265f / 180.0f;
    float pitchY = std::tan(cam.pitch * 3.14159265f / 180.0f);
    bool moved = false;
    // Depth is measured in canvas units so speed doesn't depend on window size.
    if (edge == sc::LEFT) { yaw -= (w * kEdgeZone - mx) / c.scale * rate; moved = true; }
    if (edge == sc::RIGHT) { yaw += (mx - w * (1 - kEdgeZone)) / c.scale * rate; moved = true; }
    if (edge == sc::UP && pitchY <= kPitchLimit) { pitchY += (kBar + zoneY - cy) * rate; moved = true; }
    if (edge == sc::DOWN && pitchY >= -kPitchLimit) { pitchY -= (cy - (sc::SCREEN_H - 2 * kBar - zoneY)) * rate; moved = true; }

    panRamp_ = moved ? std::min(1.0f, panRamp_ + kRampPerSecond * float(dt)) : 0.0f;
    cam.yaw = wrap180(yaw * 180.0f / 3.14159265f);
    cam.pitch = std::atan(std::clamp(pitchY, -kPitchLimit, kPitchLimit)) * 180.0f / 3.14159265f;
}

void Engine::update(double dt) {
    frameDt_ = dt;
    if (menuOpen()) {
        sound.update(dt);  // menu music only; the game is paused
        return;
    }
    double t = now();
    updateEffects(dt);
    if (camAnim_.active) {
        double k = std::min(1.0, (t - camAnim_.t0) / camAnim_.duration);
        float s = float(k * k * (3 - 2 * k));  // smoothstep
        cam.yaw = camAnim_.fromYaw + (camAnim_.toYaw - camAnim_.fromYaw) * s;
        cam.pitch = camAnim_.fromPitch + (camAnim_.toPitch - camAnim_.fromPitch) * s;
        if (k >= 1) {
            camAnim_.active = false;
            cam.yaw = wrap180(cam.yaw);
        }
    } else if (motion && !locked && !scriptBusy() && view() && !view()->isZoom && !dragging_ && !aborted_ &&
               !invOpen_ && !journalOpen_) {
        panCamera(dt);
    } else {
        panRamp_ = 0;
    }

    for (size_t i = 0; i < timers_.size();) {
        if (t >= timers_[i].at && !locked) {
            std::string fn = timers_[i].fn;
            timers_.erase(timers_.begin() + long(i));
            script.start(fn);
        } else {
            ++i;
        }
    }
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
        drawHeldCursor();
        return;
    }
    if (choice_ || invOpen_ || journalOpen_ || menuOpen()) kind = sc::NORMAL;
    else if (locked || scriptBusy()) kind = sc::WAIT;
    else if (hover) kind = hover->cursor;
    else if (edge && motion) kind = edge;  // SC_LEFT..SC_DOWN double as the pan cursors
    else kind = sc::NORMAL;
    auto it = cursors.find(kind);
    if (it == cursors.end())
        it = cursors.find(sc::NORMAL);
    if (it == cursors.end())
        return;
    if (auto* t = renderer.texture(res, it->second)) {
        float x, y;
        canvasMouse(x, y);
        renderer.drawImage(*t, x - t->width / 2.0f, y - t->height / 2.0f, float(t->width), float(t->height));
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
        drawGamma();
        drawTransition();
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
    if (invOpen_)
        renderer.setTint(0.9f, 0.75f, 0.5f);  // the inventory's sepia backdrop (0x409680)
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
            renderer.drawImage(*t, zr.x, zr.y, zr.w, zr.h);
            drawZoomEffects(*t, zr);
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
            renderer.drawImage(*t, 0, kBar, sc::SCREEN_W, sc::SCREEN_H - 2 * kBar);

    renderer.setTint(1, 1, 1);
    drawGamma();
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
        if (deferred_) {
            auto action = std::move(deferred_);
            deferred_ = nullptr;
            action();
        }
        double t = realTime();
        double dt = t - last;
        last = t;
        if (opts.turbo)
            dt = kTurboStep;  // fixed, fast game time per frame
        if (!menuOpen())
            gameClock_ += dt;
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
        if (menu_ == Screen::Message && t > messageUntil_) {
            if (afterMessage_ == Screen::None) closeMenu();
            else menu_ = afterMessage_;
        }
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
