#include "engine.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
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
constexpr float kBar = 64;               // UI bars above and below the 3D view (canvas units)
constexpr double kLookDuration = 0.6;    // seconds for scLookAt(..., SC_SMOOTH)
constexpr float kTextScale = 0.75f;      // 32px font cells drawn at 24 canvas units
constexpr float kTextMargin = 30;        // left/right margin of the text bar

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

double Engine::now() const { return double(SDL_GetTicksNS()) / 1e9; }

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
    SDL_GL_SetSwapInterval(1);
    if (const char* missing = gl::load()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "missing GL entry point %s", missing);
        return false;
    }
    if (!renderer.init())
        return false;
    SDL_HideCursor();
    sound.init();
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
    room = r;
    zooms.clear();
    overlay = nullptr;
    View* n = r->lastVisited.empty() ? nullptr : r->node(r->lastVisited);
    node = n ? n : r->nodes.front().get();
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
    node = n;
    zooms.clear();
    overlay = nullptr;
    std::erase_if(timers_, [](const Timer& t) { return !t.global; });
    return true;
}

bool Engine::switchZoom(const std::string& name) {
    // Zooms usually live in the current room, but shared ones (e.g. "black")
    // are defined once elsewhere, so fall back to a global search.
    View* z = room ? room->zoom(name) : nullptr;
    // "black" is never created by the scripts: it is an engine built-in used
    // as a blank screen around cutscenes and scripted narration.
    if (!z && name == "black") {
        static View black = [] { View v; v.name = "black"; v.isZoom = true; return v; }();
        z = &black;
    }
    for (auto it = world.rooms.begin(); !z && it != world.rooms.end(); ++it)
        z = it->second->zoom(name);
    if (!z) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "no zoom %s", name.c_str());
        return false;
    }
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
    overlay = it->second.get();
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
    // scream.exe stops the playlist, waits for a key or click, then returns to
    // the main menu. Without a menu yet, the game ends there.
    sound.playlistStop(-1);
    aborted_ = true;
}

void Engine::playZoomAudio(const View* zoom, int when) {
    if (zoom)
        for (const auto& [kind, file] : zoom->audio)
            if (kind == when)
                sound.playFx(file);
}

void Engine::zoomOut() {
    overlay = nullptr;
    if (!zooms.empty()) {
        playZoomAudio(zooms.back(), sc::ZOOMOUT);
        zooms.pop_back();
    }
}

void Engine::zoomBack() {
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

void Engine::feedback(const std::string& text, const std::string& voice) {
    feedback_ = text;
    // Reading time scales with SC_TXT_SPEED (default 50, about 20 characters a
    // second); voiced lines also stay up for as long as the voice plays.
    double speed = 50;
    if (auto* s = std::get_if<double>(&settings[sc::TXT_SPEED]); s && *s > 0)
        speed = *s;
    feedbackUntil_ = now() + 1.5 + double(text.size()) / (0.4 * speed);
    feedbackVoiced_ = !voice.empty();
    if (!voice.empty())
        sound.playVoice(voice);
    if (!text.empty())
        SDL_Log("[feedback] %s%s%s", text.c_str(), voice.empty() ? "" : "  ", voice.c_str());
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

// Zoom images are drawn full width at their own aspect, centered vertically.
Engine::ZoomRect Engine::zoomRect(const View& z) {
    const auto* t = renderer.texture(res, z.image);
    float h = t ? sc::SCREEN_W * float(t->height) / float(t->width) : kZoomH;
    return {0, (sc::SCREEN_H - h) / 2, float(sc::SCREEN_W), h};
}

// The 3D view: full window width, between the 64-unit UI bars (glViewport(0, 64, W, H-128)).
Viewport Engine::nodeViewport() const {
    int w, h;
    SDL_GetWindowSizeInPixels(window_, &w, &h);
    Canvas c = Canvas::fit(w, h);
    int top = int(c.y + kBar * c.scale), bottom = int(c.y + (sc::SCREEN_H - kBar) * c.scale);
    return {0, top, w, bottom - top};
}

Object* Engine::hovered(int& edge) {
    edge = 0;
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
    // 800x600 coordinates (so the bottom zone is the deeper one, as in scream.exe).
    Canvas c = Canvas::fit(w, h);
    const float zoneY = std::floor((sc::SCREEN_H - 192) * kEdgeZone);
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

void Engine::stopCutscene() { video_.reset(); }

void Engine::showChoice(const std::string& name) {
    auto it = world.choices.find(name);
    if (it == world.choices.end() || it->second->options.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "scChoiceGo: no choice %s", name.c_str());
        return;
    }
    choice_ = it->second.get();
    feedback_.clear();
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

int Engine::choiceAt(float x, float y) {
    if (!choice_)
        return -1;
    float lh = font_.lineHeight(kTextScale);
    float top = sc::SCREEN_H - 12 - lh * float(choice_->options.size());
    if (x < kTextMargin || x > sc::SCREEN_W - kTextMargin || y < top)
        return -1;
    int i = int((y - top) / lh);
    return i < int(choice_->options.size()) ? i : -1;
}

void Engine::drawChoice() {
    float lh = font_.lineHeight(kTextScale);
    float top = sc::SCREEN_H - 12 - lh * float(choice_->options.size());
    renderer.drawRect(0, top - 8, sc::SCREEN_W, sc::SCREEN_H - top + 8, 0, 0, 0, 0.6f);
    float mx, my;
    canvasMouse(mx, my);
    int hover = choiceAt(mx, my);
    for (size_t i = 0; i < choice_->options.size(); ++i)
        font_.draw(renderer, choice_->options[i].first, kTextMargin, top + lh * float(i), kTextScale,
                   int(i) == hover ? 1.0f : 0.6f);
}

void Engine::drawFeedback() {
    auto lines = font_.wrap(feedback_, sc::SCREEN_W - 2 * kTextMargin, kTextScale);
    float lh = font_.lineHeight(kTextScale);
    float top = sc::SCREEN_H - 12 - lh * float(lines.size());
    renderer.drawRect(0, top - 8, sc::SCREEN_W, sc::SCREEN_H - top + 8, 0, 0, 0, 0.5f);
    for (size_t i = 0; i < lines.size(); ++i) {
        float w = font_.width(lines[i], kTextScale);
        font_.draw(renderer, lines[i], (sc::SCREEN_W - w) / 2, top + lh * float(i), kTextScale);
    }
}

void Engine::click() {
    if (aborted_) {
        quit_ = true;
        return;
    }
    if (video_) {
        stopCutscene();  // click skips the cutscene
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
    if (locked || script.busy())
        return;
    int edge;
    Object* o = hovered(edge);
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
    case SDL_EVENT_KEY_DOWN:
        if (aborted_)
            quit_ = true;
        else if (video_ && (e.key.key == SDLK_ESCAPE || e.key.key == SDLK_SPACE))
            stopCutscene();
        else if (e.key.key == SDLK_ESCAPE)
            quit_ = true;
        else if (e.key.key == SDLK_F12)
            SDL_Log("screenshot: %s", renderer.capture("scream-screenshot.png").c_str());
        break;
    case SDL_EVENT_MOUSE_MOTION:
        mouseX_ = e.motion.x;
        mouseY_ = e.motion.y;
        // Right-drag look is an addition of this port; it obeys the same gates as edge panning.
        if (dragging_ && motion && !locked && !camAnim_.active && !(view() && view()->isZoom)) {
            cam.yaw = wrap180(cam.yaw - e.motion.xrel * 0.25f);
            const float limit = std::atan(kPitchLimit) * 180.0f / 3.14159265f;
            cam.pitch = std::clamp(cam.pitch + e.motion.yrel * 0.25f, -limit, limit);
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT) click();
        else if (e.button.button == SDL_BUTTON_RIGHT) dragging_ = true;
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_RIGHT) dragging_ = false;
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
    const float rate = kPanFactor * float(camSpeed) * 0.001f * panRamp_ * 60.0f * float(dt);
    const float zoneY = std::floor((sc::SCREEN_H - 192) * kEdgeZone);

    float yaw = cam.yaw * 3.14159265f / 180.0f;
    float pitchY = std::tan(cam.pitch * 3.14159265f / 180.0f);
    bool moved = false;
    // Depth is measured in the original's 800-wide pixels so speed doesn't depend on window size.
    if (edge == sc::LEFT) { yaw -= (w * kEdgeZone - mx) / c.scale * rate; moved = true; }
    if (edge == sc::RIGHT) { yaw += (mx - w * (1 - kEdgeZone)) / c.scale * rate; moved = true; }
    if (edge == sc::UP && pitchY <= kPitchLimit) { pitchY += (kBar + zoneY - cy) * rate; moved = true; }
    if (edge == sc::DOWN && pitchY >= -kPitchLimit) { pitchY -= (cy - (sc::SCREEN_H - 2 * kBar - zoneY)) * rate; moved = true; }

    panRamp_ = moved ? std::min(1.0f, panRamp_ + kRampPerSecond * float(dt)) : 0.0f;
    cam.yaw = wrap180(yaw * 180.0f / 3.14159265f);
    cam.pitch = std::atan(std::clamp(pitchY, -kPitchLimit, kPitchLimit)) * 180.0f / 3.14159265f;
}

void Engine::update(double dt) {
    double t = now();
    if (camAnim_.active) {
        double k = std::min(1.0, (t - camAnim_.t0) / camAnim_.duration);
        float s = float(k * k * (3 - 2 * k));  // smoothstep
        cam.yaw = camAnim_.fromYaw + (camAnim_.toYaw - camAnim_.fromYaw) * s;
        cam.pitch = camAnim_.fromPitch + (camAnim_.toPitch - camAnim_.fromPitch) * s;
        if (k >= 1) {
            camAnim_.active = false;
            cam.yaw = wrap180(cam.yaw);
        }
    } else if (motion && !locked && !script.busy() && view() && !view()->isZoom && !dragging_ && !aborted_) {
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
    if (feedbackVoiced_ && sound.voicePlaying())
        feedbackUntil_ = std::max(feedbackUntil_, t + 0.5);
    if (!feedback_.empty() && t > feedbackUntil_)
        feedback_.clear();
}

// Cursor rules from scream.exe's main loop: hidden while locked unless wait
// mode is on (scWait), in which case the wait cursor shows.
void Engine::drawCursor(Object* hover, int edge) {
    int kind;
    if (aborted_ || (locked && !waitMode)) return;
    if (choice_) kind = sc::NORMAL;
    else if (locked || script.busy()) kind = sc::WAIT;
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
    if (video_) {
        // Videos share the zoom frame: full canvas width at their own aspect.
        float vh = sc::SCREEN_W * float(video_->height()) / float(video_->width());
        if (videoTex_.id)
            renderer.drawImage(videoTex_, 0, (sc::SCREEN_H - vh) / 2, sc::SCREEN_W, vh);
        ensureFont();
        if (font_.loaded() && !feedback_.empty())
            drawFeedback();
        return;
    }
    View* v = view();
    if (v && !v->isZoom) {
        for (const auto& f : v->faces)
            renderer.texture(res, f);
        Viewport vp = nodeViewport();
        renderer.drawNode(*v, cam, vp);
        if (showHotspots())
            renderer.drawNodeHotspots(*v, cam, vp);
    } else if (v) {
        ZoomRect zr = zoomRect(*v);
        if (auto* t = renderer.texture(res, v->image))
            renderer.drawImage(*t, zr.x, zr.y, zr.w, zr.h);
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

    ensureFont();
    if (font_.loaded()) {
        if (choice_) drawChoice();
        else if (!feedback_.empty()) drawFeedback();
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
    if (!init())
        return;

    // No menu yet: start the requested game directly, or jump to a location.
    if (!opts.start.empty()) {
        auto colon = opts.start.find(':');
        switchRoom(opts.start.substr(0, colon));
        if (colon != std::string::npos)
            switchNode(opts.start.substr(colon + 1));
    } else {
        size_t idx = opts.game == "tlv" ? 1 : 0;
        if (idx < menu.newGame.size())
            script.start(menu.newGame[idx]);
        else
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "script registered no new-game callback");
    }

    double last = now();
    const double started = last;
    int settledFrames = 0;
    bool execPending = !opts.exec.empty();
    while (!quit_) {
        SDL_Event e;
        while (SDL_PollEvent(&e))
            handleEvent(e);
        double t = now();
        update(t - last);
        last = t;
        if (execPending && !script.busy()) {
            execPending = false;
            script.startCode(opts.exec, "exec");
        }
        render();

        if (!opts.screenshot.empty() && opts.screenshotAt >= 0) {
            if (t - started >= opts.screenshotAt) {
                SDL_Log("screenshot: %s", renderer.capture(opts.screenshot.c_str()).c_str());
                quit_ = true;
            }
        } else if (!opts.screenshot.empty() && !execPending) {
            settledFrames = script.busy() || camAnim_.active ? 0 : settledFrames + 1;
            if (settledFrames == 5) {
                cam.yaw = opts.yaw;
                cam.pitch = opts.pitch;
                render();
                SDL_Log("screenshot: %s", renderer.capture(opts.screenshot.c_str()).c_str());
                quit_ = true;
            }
        }
        SDL_GL_SwapWindow(window_);
    }
}
