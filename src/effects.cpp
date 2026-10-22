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
// original. Exact geometry (the original's dome radius, tiling) is approximate.

#include <algorithm>
#include <cmath>

#include "constants.hpp"
#include "engine.hpp"

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

constexpr double kFps = 60;                      // the original's reference rate
constexpr float kColorStep = 0.0001f * kFps;     // ambience colour change per second
constexpr float kFovBase = 45, kFovMin = 35, kFovMax = 55, kFovStep = 0.005f * kFps;

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
    if (!fx_.initialized || fx_.snap) {
        fx_.sky = sky;
        fx_.horizon = horizon;
        fx_.cloud = cloud;
        fx_.initialized = true;
        fx_.snap = false;
    }
    const float step = kColorStep * float(dt);
    approach(fx_.sky, sky, step);
    approach(fx_.horizon, horizon, step);
    approach(fx_.cloud, cloud, step);

    // Scrolling: clouds and fog by SC_SKY_SPEED/10 (integer, as the original), rain at a fixed rate.
    double skySpeed = 25;
    if (auto it = settings.find(sc::SKY_SPEED); it != settings.end() && std::holds_alternative<double>(it->second))
        skySpeed = std::get<double>(it->second);
    const double drift = 5e-5 * kFps * double(int(skySpeed) / 10) * dt;
    fx_.cloudScroll += drift;
    fx_.fogScroll += drift;
    fx_.rainScroll += 0.035 * kFps * dt;

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

    // SC_DREAM: the field of view drifts between 35 and 55 degrees; otherwise it is 45.
    if (flag(sc::DREAM)) {
        fx_.fov += float(fx_.fovDir) * kFovStep * float(dt);
        if (fx_.fov > kFovMax) fx_.fovDir = -1;
        if (fx_.fov < kFovMin) fx_.fovDir = 1;
    } else {
        fx_.fov = kFovBase;
    }
    cam.fovY = fx_.fov;
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

void Engine::drawNodeScene(const View& v, const Viewport& vp) {
    using P = Renderer::Pass;
    const bool sky = flag(sc::SKY), fog = flag(sc::FOG), rain = flag(sc::RAIN);
    const bool backdrop = sky || fog || rain;
    std::array<const RendererTexture*, 6> masks{};
    if (backdrop)
        masks = faceMasks(v);

    auto repeating = [&](const std::string& name, const std::string& fallback) {
        const RendererTexture* t = renderer.optionalTexture(res, name);
        if (!t) t = renderer.optionalTexture(res, fallback);
        if (t) renderer.setRepeat(*t);
        return t;
    };

    if (sky) {
        renderer.drawDome(cam, vp, nullptr, P{fx_.sky[0], fx_.sky[1], fx_.sky[2], 1, Blend::Opaque}, 1, 1, 0, 0);
        // Horizon glow: gradient.tga runs dark (zenith) to light (horizon).
        if (auto* g = renderer.optionalTexture(res, "gradient.tga"))
            renderer.drawDome(cam, vp, g, P{fx_.horizon[0], fx_.horizon[1], fx_.horizon[2], 1, Blend::Alpha}, 1, 2, 0, 0);
        const RendererTexture* c1 = repeating("clouds1.tga", "sky_def.tga");
        const RendererTexture* c2 = repeating("clouds2.tga", "sky_def.tga");
        const float sc = 4;
        renderer.drawDome(cam, vp, c1, P{fx_.cloud[0], fx_.cloud[1], fx_.cloud[2], fx_.cloudA, Blend::Alpha},
                          sc, sc, 0, float(fx_.cloudScroll));
        renderer.drawDome(cam, vp, c2,
                          P{fx_.cloud[0] - 0.05f, fx_.cloud[1] - 0.05f, fx_.cloud[2] - 0.05f, fx_.cloudB, Blend::Alpha},
                          sc, sc, 0.5f, float(fx_.cloudScroll));
    } else if (fog) {
        renderer.drawDome(cam, vp, nullptr, P{0.25f, 0.25f, 0.25f, 1, Blend::Opaque}, 1, 1, 0, 0);
        const RendererTexture* c = repeating("clouds1.tga", "sky_def.tga");
        for (float u : {0.0f, 0.5f})
            renderer.drawDome(cam, vp, c, P{1, 1, 1, 0.5f, Blend::Alpha}, 2, 2, u, float(fx_.fogScroll));
    } else if (rain) {
        renderer.drawDome(cam, vp, nullptr, P{0, 0, 0, 1, Blend::Opaque}, 1, 1, 0, 0);
        if (const RendererTexture* r = repeating("rain_def.tga", "rain_def.tga"))
            for (float u : {0.0f, 0.25f})
                renderer.drawDome(cam, vp, r, P{1, 1, 1, 0.5f, Blend::AddAlpha}, 8, 4, u, float(fx_.rainScroll));
    }

    P base;
    if (backdrop) {
        base.blend = Blend::Alpha;
        base.masks = &masks;
    }
    renderer.drawNode(v, cam, vp, base);

    auto overlay = [&](float r, float g, float b, float a, Blend blend) {
        P p{r, g, b, a, blend};
        if (backdrop) p.masks = &masks;
        renderer.drawNode(v, cam, vp, p);
    };
    auto wash = [&](float r, float g, float b, float a, Blend blend) {
        renderer.drawDome(cam, vp, nullptr, P{r, g, b, a, blend}, 1, 1, 0, 0);
    };
    applyLighting(overlay, wash);
}

// Lighting overlays shared by nodes and zooms (0x409b50).
void Engine::applyLighting(const std::function<void(float, float, float, float, Blend)>& overlay,
                           const std::function<void(float, float, float, float, Blend)>& wash) {
    if (flag(sc::NIGHT)) {
        overlay(0.1f, 0.1f, 0.5f, 0.5f, Blend::Alpha);
    } else if (flag(sc::RAIN) && !flag(sc::CANDLE)) {
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

void Engine::drawZoomEffects(const RendererTexture& image, const ZoomRect& zr) {
    using P = Renderer::Pass;
    auto overlay = [&](float r, float g, float b, float a, Blend blend) {
        renderer.drawImagePass(image, zr.x, zr.y, zr.w, zr.h, P{r, g, b, a, blend});
    };
    auto wash = [&](float r, float g, float b, float a, Blend blend) {
        renderer.drawRectPass(zr.x, zr.y, zr.w, zr.h, P{r, g, b, a, blend});
    };
    applyLighting(overlay, wash);
}
