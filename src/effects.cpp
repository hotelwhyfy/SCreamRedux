// Weather, sky and lighting effects, following scream.exe's render path
// (0x409460 node, 0x40a830 zoom, 0x409b50 lighting, 0x40cb00/0x40d990 sky,
// 0x40dc80 fog, 0x40dda0 rain, 0x407c30 dream). Per-frame constants of the
// original (tuned for 60 fps) are converted to per-second rates.
//
// The black parts of each face's _msk.tga let a backdrop show through:
// SC_SKY draws a tinted sky dome with two crossfading cloud layers,
// otherwise SC_FOG draws drifting fog and SC_RAIN rain streaks. Then come the
// panorama and its overlays: SC_NIGHT tints it blue, SC_RAIN (without a
// candle) adds lightning, SC_CANDLE / SC_LAMP a flickering warm light, and
// SC_DREAM makes the field of view breathe. SC_GLOW is never read by the
// original.

#include <algorithm>
#include <cmath>
#include <optional>

#include "constants.hpp"
#include "engine.hpp"
#include "images.hpp"

namespace {

using Rgb = std::array<float, 3>;

// scSetAmbience presets (0x40cb00, 0x40d120): sky base, horizon glow, clouds.
struct Preset {
    Rgb sky, horizon, cloud;
};

Preset preset(int p) {
    switch (p) {
    case sc::DAWN: return {{1.0f, 0.8f, 0.55f}, {0.8f, 0.05f, 0.05f}, {1.0f, 0.7f, 0.55f}};
    case sc::DUSK: return {{0.45f, 0.5f, 0.6f}, {1.0f, 0.0f, 0.0f}, {0.95f, 0.55f, 0.45f}};
    case sc::STORM: return {{0.7f, 0.7f, 0.9f}, {0.15f, 0.15f, 0.8f}, {0.3f, 0.3f, 0.3f}};
    default: return {{0.9f, 0.9f, 0.9f}, {0.55f, 0.65f, 0.8f}, {0.75f, 0.75f, 0.75f}};  // SC_MORNING
    }
}

// SC_SCREEN_COLOR presets (0x40d4e0): RGBA of the tint pass; MORNING draws nothing.
std::array<float, 4> screenPreset(int p) {
    switch (p) {
    case sc::DAWN: return {1.0f, 0.55f, 0.55f, 0.025f};
    case sc::DUSK: return {1.0f, 0.25f, 0.25f, 0.02f};
    case sc::STORM: return {0.0f, 0.0f, 0.15f, 0.15f};
    default: return {1.0f, 1.0f, 1.0f, 0.0f};  // SC_MORNING
    }
}

constexpr double kFps = 60;                      // the original's reference rate
constexpr float kColorStep = 0.0001f * kFps;     // ambience colour change per second
constexpr float kFovBase = 45, kFovStep = 0.005f * kFps;

// Lightning schedule while it rains (0x409b50): (time, state) over a 30.5 s cycle.
constexpr std::pair<double, int> kLightning[] = {
    {10.0, 1}, {10.1, 0}, {10.15, 2}, {10.35, 0}, {15.0, 1}, {15.5, 0},
    {20.0, 1}, {20.1, 0}, {20.2, 1}, {20.3, 0}, {30.0, 3}};
constexpr double kLightningCycle = 30.5;

// Candle/lamp flicker: (time, state) over 2.5 s, all shifted by 0.25 s with SC_LAMP.
constexpr std::pair<double, int> kFlicker[] = {
    {0.1, 1}, {0.2, 0}, {0.3, 1}, {0.4, 0}, {0.75, 1}, {0.85, 0}, {0.9, 1}, {1.0, 2}, {1.1, 1},
    {1.2, 0}, {1.3, 1}, {1.4, 0}, {1.8, 1}, {1.9, 0}, {2.0, 1}, {2.15, 0}, {2.2, 1}, {2.3, 2}, {2.4, 1}};
constexpr double kFlickerCycle = 2.5;

template <size_t N>
int scheduleState(const std::pair<double, int> (&s)[N], double t) {
    int state = 0;
    for (const auto& [at, st] : s)
        if (t >= at) state = st;
    return state;
}

void approach(Rgb& c, const Rgb& target, float step) {
    for (size_t i = 0; i < 3; ++i)
        c[i] = c[i] < target[i] ? std::min(target[i], c[i] + step) : std::max(target[i], c[i] - step);
}

// The 2006 engine's cloud texture, made at startup (P 0x40be00, 0x415cd0-
// 0x416140): Hugo Elias style value noise. A 32x32 map of smoothed integer
// noise (made three times, each with the next rand() % 5000 as its seed,
// each replacing the last), four octaves summed over 256x256, then an
// exponential cover filter; every texel is R = G = B = A.
Image cloudTexture2006() {
    uint32_t seed = 1;  // Watcom's rand(), never seeded
    auto rnd = [&] {
        seed = seed * 1103515245u + 12345u;
        return int((seed >> 16) & 0x7fff);
    };
    auto noise = [](int x, int y, int r) {
        int32_t n = int32_t(uint32_t(x) + uint32_t(y) * 57u + uint32_t(r) * 131u);
        n = int32_t(uint32_t(n) << 13) ^ n;
        const uint32_t un = uint32_t(n);
        const int32_t t = int32_t(un * (un * un * 15731u + 789221u) + 1376312589u) & 0x7fffffff;
        return 1.0f - float(t) / 1073741824.0f;
    };
    std::array<float, 32 * 32> map{};
    for (int pass = 0; pass < 3; ++pass) {
        const int r = rnd() % 5000;
        float t[34][34] = {};
        for (int y = 1; y <= 32; ++y)
            for (int x = 1; x <= 32; ++x) t[x][y] = 128 + noise(x, y, r) * 128;
        for (int i = 1; i <= 32; ++i) {
            t[0][i] = t[32][i], t[33][i] = t[1][i];
            t[i][0] = t[i][32], t[i][33] = t[i][1];
        }
        t[0][0] = t[32][32], t[33][33] = t[1][1], t[0][33] = t[32][1], t[33][0] = t[1][32];
        for (int y = 1; y <= 32; ++y)
            for (int x = 1; x <= 32; ++x)
                map[size_t((x - 1) * 32 + (y - 1))] =
                    t[x][y] / 4 + (t[x + 1][y] + t[x - 1][y] + t[x][y + 1] + t[x][y - 1]) / 8 +
                    (t[x + 1][y + 1] + t[x + 1][y - 1] + t[x - 1][y + 1] + t[x - 1][y - 1]) / 16;
    }
    auto sample = [&](float x, float y) {
        const int xi = int(x), yi = int(y);
        const float xf = x - float(xi), yf = y - float(yi);
        const int x0 = xi & 31, y0 = yi & 31, x1 = (xi + 1) & 31, y1 = (yi + 1) & 31;
        auto m = [&](int a, int b) { return map[size_t(a * 32 + b)]; };
        const float bot = m(x0, y0) + xf * (m(x1, y0) - m(x0, y0)), top = m(x0, y1) + xf * (m(x1, y1) - m(x0, y1));
        return bot + yf * (top - bot);
    };
    Image img;
    img.width = img.height = 256;
    img.pixels.resize(256 * 256 * 4);
    std::vector<float> sum(256 * 256, 0.0f);
    for (int o = 0; o < 4; ++o) {
        const float scale = 1.0f / float(1 << (3 - o)), amp = 1.0f / float(1 << o);
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 256; ++x) sum[size_t(y * 256 + x)] += sample(float(x) * scale, float(y) * scale) * amp;
    }
    for (size_t i = 0; i < sum.size(); ++i) {
        const float c = std::max(sum[i] - 205.0f, 0.0f);
        const auto v = uint8_t(std::lround(255.0f - std::pow(0.996f, c) * 255.0f));
        for (int k = 0; k < 4; ++k) img.pixels[i * 4 + size_t(k)] = v;
    }
    return img;
}

}  // namespace

bool Engine::flag(int f) const { return flags.count(f) != 0; }

void Engine::updateEffects(double dt) {
    auto ambiencePreset = [&](int key) {
        auto it = ambience.find(key);
        return it == ambience.end() ? sc::MORNING : it->second.first;
    };
    Rgb sky = preset(ambiencePreset(sc::SKY_COLOR)).sky;
    Rgb horizon = preset(ambiencePreset(sc::SKY_COLOR)).horizon;
    Rgb cloud = preset(ambiencePreset(sc::CLOUD_COLOR)).cloud;
    const bool all = !fx_.initialized || fx_.snap;
    if (all || fx_.snapKeys.count(sc::SKY_COLOR)) {
        fx_.sky = sky;
        fx_.horizon = horizon;
    }
    if (all || fx_.snapKeys.count(sc::CLOUD_COLOR)) fx_.cloud = cloud;
    // The fades step only on frames that draw the sky; the cloud colour is
    // updated once per layer, so twice as fast (0x40cb00, 0x40d120).
    const float step = fx_.skyDrawn ? kColorStep * float(dt) : 0;
    fx_.skyDrawn = false;
    approach(fx_.sky, sky, step);
    approach(fx_.horizon, horizon, step);
    approach(fx_.cloud, cloud, 2 * step);

    // Screen colour: every channel steps |target - start| * 0.001 per frame (10 * 0.0001,
    // fixed when the change starts), so all arrive together after 1000 frames; done
    // within 0.001 of the target.
    const int screenTarget = ambiencePreset(sc::SCREEN_COLOR);
    const auto target = screenPreset(screenTarget);
    if (all || fx_.snapKeys.count(sc::SCREEN_COLOR)) {
        fx_.screen = target;
        fx_.screenPreset = screenTarget;
        fx_.screenFading = false;
    } else if (screenTarget != fx_.screenPreset) {
        if (!fx_.screenFading) {
            for (size_t i = 0; i < 4; ++i) fx_.screenStep[i] = std::abs(target[i] - fx_.screen[i]) * 10 * 0.0001f;
            fx_.screenFading = true;
        }
        bool done = true;
        for (size_t i = 0; i < 4; ++i) {
            const float s = fx_.screenStep[i] * float(kFps * dt);
            float& c = fx_.screen[i];
            c = c < target[i] ? std::min(target[i], c + s) : std::max(target[i], c - s);
            done = done && std::abs(target[i] - c) < 0.001f;
        }
        if (done) {
            fx_.screen = target;
            fx_.screenPreset = screenTarget;
            fx_.screenFading = false;
        }
    }
    fx_.initialized = true;
    fx_.snap = false;
    fx_.snapKeys.clear();

    // Scrolling: clouds and fog by SC_SKY_SPEED/10 (integer, as the original), rain at a fixed rate.
    double skySpeed = 15;  // scream.exe's default (0x40f370); the scripts never set it
    if (auto it = settings.find(sc::SKY_SPEED); it != settings.end() && std::holds_alternative<double>(it->second))
        skySpeed = std::get<double>(it->second);
    const double drift = 5e-5 * kFps * double(int(skySpeed) / 10) * dt;
    fx_.cloudScroll += drift;
    fx_.fogStep = drift;
    fx_.fogScroll += 2 * drift;
    fx_.rainScroll += (release2006 ? 0.045 : 0.035) * kFps * dt;  // 2006: P 0x40c1b0
    fx_.cloudScroll2006 += 0.00015 * kFps * dt;                   // P 0x40bf00, whatever SC_SKY_SPEED
    fx_.fogStep2006 = 0.0001 * kFps * dt;                          // per call, two calls a frame (P 0x40bff0)
    fx_.fogScroll2006 += 2 * fx_.fogStep2006;

    // Cloud layers crossfade (0x40d990): A and B take turns, with pauses in between.
    const float fade = 0.002f * float(kFps * dt), pause = 0.015f * float(kFps * dt);
    switch (fx_.cloudState) {
    case 0: if ((fx_.cloudB += fade) >= 0.5f) fx_.cloudState = 1; break;
    case 1: fx_.cloudA -= fade; if ((fx_.cloudB += fade) > 1) fx_.cloudState = 3; break;
    case 3: if ((fx_.cloudTimer += pause) > 1) { fx_.cloudState = 4; fx_.cloudTimer = 0; } break;
    case 4: if ((fx_.cloudA += fade) >= 0.5f) fx_.cloudState = 5; break;
    case 5: fx_.cloudB -= fade; if ((fx_.cloudA += fade) > 1) fx_.cloudState = 6; break;
    case 6: if ((fx_.cloudTimer += pause) > 1) { fx_.cloudState = 0; fx_.cloudTimer = 0; } break;
    }
    fx_.cloudA = std::clamp(fx_.cloudA, 0.0f, 1.0f);
    fx_.cloudB = std::clamp(fx_.cloudB, 0.0f, 1.0f);

    if (flag(sc::RAIN) && !flag(sc::CANDLE)) {
        fx_.lightningClock += dt;
        if (fx_.lightningClock >= kLightningCycle) fx_.lightningClock = 0;
    }
    if (flag(sc::CANDLE) || flag(sc::LAMP)) {
        fx_.flickerClock += dt;
        if (fx_.flickerClock >= kFlickerCycle + (flag(sc::LAMP) ? 0.25 : 0)) fx_.flickerClock = 0;
    }

    // SC_DREAM: the field of view drifts 10 degrees either side of its base,
    // 45 in the DC and 53 in the 2006 engine (P 0x407840); otherwise the base.
    const float base = release2006 ? 53.0f : kFovBase;
    if (flag(sc::DREAM)) {
        fx_.fov += float(fx_.fovDir) * kFovStep * float(dt);
        if (fx_.fov > base + 10) fx_.fovDir = -1;
        if (fx_.fov < base - 10) fx_.fovDir = 1;
    } else {
        fx_.fov = base;
    }
    cam.fovY = fx_.fov;
    if (release2006) {  // gluPerspective(53, W / H) into the view, faces on a 30 x 40 x 30 box (P 0x40c370)
        cam.aspectScale = float(gMode.h - 128) / float(gMode.h);
        cam.boxY = 40.0f / 30.0f;
    }
}

// Face masks: the face texture's name with _msk.tga, when the archive has one.
std::array<const RendererTexture*, 6> Engine::faceMasks(const View& v) {
    std::array<const RendererTexture*, 6> masks{};
    for (size_t f = 0; f < 6; ++f) {
        const std::string& face = v.faces[f];
        auto dot = face.rfind('.');
        if (dot != std::string::npos)
            masks[f] = renderer.optionalTexture(res, face.substr(0, dot) + "_msk.tga");
    }
    return masks;
}

const RendererTexture* Engine::repeatingTexture(const std::string& name) {
    const RendererTexture* t = renderer.optionalTexture(res, name);
    if (!t) t = renderer.optionalTexture(res, "sky_def.tga");  // the 2006 data
    if (t) renderer.setRepeat(*t);
    return t;
}

// The backdrop behind the masks (0x409460 nodes, 0x40a830 zooms), drawn
// from black: SC_SKY a cube in the sky colour, the horizon gradient and the
// clouds; else SC_FOG a dark grey cube (nodes only) and the fog; else SC_RAIN a
// white cube and the rain. Returns the blend the mask is then drawn with.
std::optional<Blend> Engine::drawBackdrop(const Viewport& vp, bool zoom) {
    using P = Renderer::Pass;
    const Renderer::FaceTextures none{};
    if (release2006)
        return drawBackdrop2006(vp, zoom);
    if (flag(sc::SKY)) {
        renderer.drawCube(cam, vp, none, P{fx_.sky[0], fx_.sky[1], fx_.sky[2], 1, Blend::AddOne});
        // gradient.tga on the four sides (transparent at the top, the horizon
        // colour at the bottom), the bottom face solid, the top face clear
        // (0x40c1a0 with slot 0x3e). Its top edge starts at t = 1/1280 and
        // slides down by 0.001 per draw to 0.251 once after launch.
        const RendererTexture* g = renderer.optionalTexture(res, "gradient.tga");
        const float t0 = 1.0f / 1280, t1 = 1279.0f / 1280, o = fx_.gradientSlide;
        if (fx_.gradientSlide <= 0.25f) fx_.gradientSlide += 0.001f;
        const std::array<float, 4> side{t0, t0 + o, t1, t1};
        const Renderer::FaceUv uv{side, side, side, side, side, side};
        const Renderer::FaceTextures faces{g, g, g, g, nullptr, nullptr};
        renderer.drawCube(cam, vp, faces, P{fx_.horizon[0], fx_.horizon[1], fx_.horizon[2], 1, Blend::Alpha}, &uv,
                          1u << (sc::UP - 1));
        drawClouds(vp);
        fx_.skyDrawn = true;
        return Blend::SrcColorOne;
    }
    if (flag(sc::FOG)) {
        if (!zoom)
            renderer.drawCube(cam, vp, none, P{0.25f, 0.25f, 0.25f, 1, Blend::AddOne});
        // 0x40dc80 advances its scroll on every call, and nodes call it twice.
        const RendererTexture* c = repeatingTexture("clouds1.tga");
        for (int i = zoom ? 1 : 0; i < 2; ++i)
            renderer.drawDome(cam, vp, c, P{1, 1, 1, 0.5f, Blend::Alpha}, Renderer::DomeFrame::Clouds, 1,
                              float(fx_.fogScroll - (1 - i) * fx_.fogStep));
        return zoom ? Blend::DstColorOne : Blend::SrcColorOne;
    }
    if (flag(sc::RAIN)) {
        renderer.drawCube(cam, vp, none, P{1, 1, 1, 1, Blend::AddOne});
        if (const RendererTexture* r = repeatingTexture("rain_def.tga")) {  // 0x40dda0
            renderer.drawDome(cam, vp, r, P{1, 1, 1, 0.5f, Blend::DstColor}, Renderer::DomeFrame::RainA, 2,
                              float(fx_.rainScroll));
            renderer.drawDome(cam, vp, r, P{1, 1, 1, 0.5f, Blend::OneInvSrcAlpha}, Renderer::DomeFrame::RainB, 2,
                              float(fx_.rainScroll));
        }
        return zoom ? Blend::SrcColorDstColor : Blend::DstColorOne;
    }
    return std::nullopt;
}

// The 2006 engine's backdrop (P 0x408d80): SC_SKY is a white cube under one
// layer of its generated clouds; SC_FOG a dark grey cube (nodes) and, twice,
// the clouds on two side-on hemispheres drifting apart; SC_RAIN as the DC's.
std::optional<Blend> Engine::drawBackdrop2006(const Viewport& vp, bool zoom) {
    using P = Renderer::Pass;
    const Renderer::FaceTextures none{};
    if (flag(sc::SKY)) {
        renderer.drawCube(cam, vp, none, P{1, 1, 1, 1, Blend::AddOne});
        drawClouds(vp);
        return Blend::SrcColorOne;
    }
    if (flag(sc::FOG)) {
        if (!zoom)
            renderer.drawCube(cam, vp, none, P{0.25f, 0.25f, 0.25f, 1, Blend::AddOne});
        const RendererTexture* c = clouds2006();
        for (int i = zoom ? 1 : 0; i < 2; ++i) {
            const float u = float(fx_.fogScroll2006 - (1 - i) * fx_.fogStep2006);
            renderer.drawDome(cam, vp, c, P{1, 1, 1, 1, Blend::OneInvSrcAlpha}, Renderer::DomeFrame::RainA, 0.25f, 0, u);
            renderer.drawDome(cam, vp, c, P{1, 1, 1, 1, Blend::OneInvSrcAlpha}, Renderer::DomeFrame::RainB, 0.25f, 0, -u);
        }
        return zoom ? Blend::DstColorOne : Blend::SrcColorOne;
    }
    if (flag(sc::RAIN)) {
        renderer.drawCube(cam, vp, none, P{1, 1, 1, 1, Blend::AddOne});
        if (const RendererTexture* r = repeatingTexture("rain_def.tga")) {
            renderer.drawDome(cam, vp, r, P{1, 1, 1, 0.5f, Blend::DstColor}, Renderer::DomeFrame::RainA, 2,
                              float(fx_.rainScroll));
            renderer.drawDome(cam, vp, r, P{1, 1, 1, 0.5f, Blend::OneInvSrcAlpha}, Renderer::DomeFrame::RainB, 2,
                              float(fx_.rainScroll));
        }
        return zoom ? Blend::SrcColorDstColor : Blend::DstColorOne;
    }
    return std::nullopt;
}

const RendererTexture* Engine::clouds2006() {
    const RendererTexture* t = renderer.optionalTexture(res, "#clouds2006");
    if (!t) {
        renderer.upload("#clouds2006", cloudTexture2006());
        t = renderer.optionalTexture(res, "#clouds2006");
    }
    if (t) renderer.setRepeat(*t);
    return t;
}

// Nodes (0x409460): the backdrop, the face masks added onto it, then the
// panorama multiplied in: node x (backdrop + mask^2), so the masks' black parts
// show the photo's own sky darkened by the backdrop. With no backdrop the
// panorama is drawn as it is. Lighting passes cover the whole panorama.
void Engine::drawNodeScene(const View& v, const Viewport& vp) {
    using P = Renderer::Pass;
    if (auto maskBlend = drawBackdrop(vp, false)) {
        renderer.drawCube(cam, vp, faceMasks(v), P{1, 1, 1, 1, *maskBlend});  // a face without one: white
        renderer.drawNode(v, cam, vp, P{1, 1, 1, 1, Blend::DstColorZero});
    } else {
        renderer.drawNode(v, cam, vp, P{});
    }
    auto overlay = [&](float r, float g, float b, float a, Blend blend) {
        renderer.drawNode(v, cam, vp, P{r, g, b, a, blend});
    };
    auto wash = [&](float r, float g, float b, float a, Blend blend) {
        renderer.drawCube(cam, vp, Renderer::FaceTextures{}, P{r, g, b, a, blend});
    };
    applyLighting(overlay, wash);
}

// The two cloud layers (0x40d990) on the dome, sharing its uv, scrolled on v.
void Engine::drawClouds(const Viewport& vp) {
    using P = Renderer::Pass;
    if (release2006) {  // one layer, untinted (P 0x40bf00)
        renderer.drawDome(cam, vp, clouds2006(), P{1, 1, 1, 1, Blend::Alpha}, Renderer::DomeFrame::Clouds, 1,
                          float(fx_.cloudScroll2006));
        return;
    }
    const RendererTexture* c1 = repeatingTexture("clouds1.tga");
    const RendererTexture* c2 = repeatingTexture("clouds2.tga");
    const float v = float(fx_.cloudScroll);
    renderer.drawDome(cam, vp, c1, P{fx_.cloud[0], fx_.cloud[1], fx_.cloud[2], fx_.cloudA, Blend::Alpha},
                      Renderer::DomeFrame::Clouds, 1, v);
    renderer.drawDome(cam, vp, c2,
                      P{fx_.cloud[0] - 0.05f, fx_.cloud[1] - 0.05f, fx_.cloud[2] - 0.05f, fx_.cloudB, Blend::Alpha},
                      Renderer::DomeFrame::Clouds, 1, v);
}

// Lighting overlays shared by nodes and zooms (0x409b50).
void Engine::applyLighting(const std::function<void(float, float, float, float, Blend)>& overlay,
                           const std::function<void(float, float, float, float, Blend)>& wash) {
    if (flag(sc::NIGHT)) {
        overlay(0.1f, 0.1f, 0.5f, 0.5f, Blend::Alpha);
    } else if (!flag(sc::RAIN) || flag(sc::CANDLE)) {
        // The scene again in the SC_SCREEN_COLOR tint (DC only).
        const auto& c = fx_.screen;
        if (c[3] > 0) overlay(c[0], c[1], c[2], c[3], Blend::Alpha);
    } else {
        switch (scheduleState(kLightning, fx_.lightningClock)) {
        case 0: overlay(0.5f, 0.5f, 0.5f, 0.5f, Blend::Alpha); break;
        case 1: overlay(0.25f, 0.25f, 0.25f, 0.25f, Blend::AddOne); wash(1, 1, 1, 0.05f, Blend::Alpha); break;
        case 2: overlay(0.5f, 0.5f, 0.5f, 0.5f, Blend::AddOne); wash(1, 1, 1, 0.15f, Blend::Alpha); break;
        case 3: overlay(0.75f, 0.75f, 0.75f, 0.75f, Blend::AddOne); wash(1, 1, 1, 0.2f, Blend::Alpha); break;
        }
    }
    if (flag(sc::CANDLE) || flag(sc::LAMP)) {
        const float lamp = flag(sc::LAMP) ? 1.0f : 0.0f;
        const double t = fx_.flickerClock - 0.25 * lamp;
        float washA, lightA;
        switch (scheduleState(kFlicker, t)) {
        case 1: washA = 0.05f - lamp * 0.035f; lightA = 0.5f - lamp * 0.35f; break;
        case 2: washA = 0.075f - lamp * 0.055f; lightA = 0.75f - lamp * 0.55f; break;
        default: washA = 0.01f - lamp * 0.05f; lightA = 0.1f - lamp * 0.5f; break;
        }
        if (washA > 0) wash(1, 0.75f, 0.5f, washA, Blend::AddAlpha);
        if (lightA > 0) overlay(1, 0.75f, 0.5f, lightA, Blend::AddAlpha);
    }
}

// Zooms (0x40a830): unless SC_NIGHT, the backdrop as seen from the node, then
// the zoom's mask (<image>_msk.tga; none: white) over the view with the
// effect's blend, then the zoom multiplied in. Without an effect the mask is
// added onto black, so a masked zoom still shows black through its mask.
void Engine::drawZoomScene(const View& z, const RendererTexture& image, const ZoomRect& zr) {
    using P = Renderer::Pass;
    if (flag(sc::NIGHT)) {
        renderer.drawImage(image, zr.x, zr.y, zr.w, zr.h);
    } else {
        const RendererTexture* mask = nullptr;
        if (auto dot = z.image.rfind('.'); dot != std::string::npos)
            mask = renderer.optionalTexture(res, z.image.substr(0, dot) + "_msk.tga");
        Blend maskBlend = drawBackdrop(nodeViewport(), true).value_or(Blend::AddOne);
        if (mask) renderer.drawImagePass(*mask, zr.x, zr.y, zr.w, zr.h, P{1, 1, 1, 1, maskBlend});
        else renderer.drawRectPass(zr.x, zr.y, zr.w, zr.h, P{1, 1, 1, 1, maskBlend});
        renderer.drawImagePass(image, zr.x, zr.y, zr.w, zr.h, P{1, 1, 1, 1, Blend::DstColorZero});
    }
    auto overlay = [&](float r, float g, float b, float a, Blend blend) {
        renderer.drawImagePass(image, zr.x, zr.y, zr.w, zr.h, P{r, g, b, a, blend});
    };
    auto wash = [&](float r, float g, float b, float a, Blend blend) {
        renderer.drawRectPass(zr.x, zr.y, zr.w, zr.h, P{r, g, b, a, blend});
    };
    applyLighting(overlay, wash);
}
