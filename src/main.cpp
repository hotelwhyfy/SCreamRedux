// SCream Redux: a reimplementation of the SCream engine (Scratches, 2006/2007)
// that runs the original game scripts and data.

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_main.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "api.hpp"
#include "images.hpp"
#include "video.hpp"
#include "engine.hpp"

namespace {

void usage() {
    std::puts(
        "usage: scream --data DIR [--data DIR ...] [options]\n"
        "\n"
        "  --data DIR        directory with scream.sc, scratches.sc, *.res (repeatable;\n"
        "                    later directories override earlier ones)\n"
        "  --script NAME     game script to run (default scratches.sc)\n"
        "  --game sdc|tlv    skip the intro and menu and start that game\n"
        "  --start ROOM[:NODE]  skip the game intro and start at a location\n"
        "  --yaw DEG --pitch DEG  initial view direction\n"
        "  --screenshot FILE write a PNG once the scene settles, then quit\n"
        "  --screenshot-at S take it after S seconds instead of waiting to settle\n"
        "  --exec LUA        run Lua as a handler once startup is idle; debugClick(name)\n"
        "                    clicks an object in the current view, debugKey(vk) presses a key\n"
        "  --save-dir DIR    keep saves and preferences in DIR (for tests)\n"
        "  --fullscreen      run fullscreen\n"
        "  --hotspots        draw clickable areas (SC_SHOW_HOTSPOTS)\n"
        "  --trace           log every sc* call\n"
        "  --api-status      print implementation status of all sc* functions\n"
        "  --verify-media    decode every image, sound and video in the archives\n");
}

// Decode every image, sound and video once; report anything that fails.
int verifyMedia(Engine& engine) {
    for (const char* name : {"scream.res", "scratches.res", "update.res"}) {
        auto path = engine.findData(name);
        if (!path.empty())
            engine.res.mount(ResArchive::open(path));
    }
    int ok = 0, bad = 0, videos = 0;
    for (const auto& e : engine.res.list()) {
        auto data = engine.res.read(e.name);
        bool good = false;
        if (e.type == ResArchive::Type::Ogg) {
            static const uint8_t kTheora[] = {0x80, 't', 'h', 'e', 'o', 'r', 'a'};
            auto head = data->begin() + long(std::min<size_t>(data->size(), 512));
            bool isVideo = std::search(data->begin(), head, kTheora, kTheora + 7) != head;
            if (isVideo) {
                Video v;
                good = v.open(*data);
                for (double t = 0; good && !v.finished(); t += 1)
                    v.advance(t);
                ++videos;
            } else {
                good = Audio::decodeCheck(*data);
            }
        } else {
            good = decodeImage(*data).has_value();
        }
        if (good) ++ok;
        else { ++bad; std::printf("FAILED %s\n", e.name.c_str()); }
    }
    std::printf("%d decoded (%d videos), %d failed\n", ok, videos, bad);
    return bad ? 1 : 0;
}

void printApiStatus() {
    int n;
    const api::Info* t = api::table(n);
    int counts[3] = {};
    for (int i = 0; i < n; ++i) {
        const char* s[] = {"done", "partial", "stub"};
        ++counts[int(t[i].status)];
        std::printf("%-22s %-8s %s\n", t[i].name, s[int(t[i].status)], t[i].note);
    }
    std::printf("\n%d functions: %d done, %d partial, %d stub\n", n, counts[0], counts[1], counts[2]);
}

int run(const Options& opts, const std::string& script) {
    Engine engine(opts);
    api::registerAll(engine);
    if (opts.verifyMedia)
        return verifyMedia(engine);

    auto boot = engine.findData("scream.sc");
    auto game = engine.findData(script);
    if (boot.empty() || game.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "need scream.sc and %s in the data directories", script.c_str());
        return 1;
    }
    // scream.sc defines the SC_* constants; the game script then builds the
    // world and calls scRun(), which only returns when the player quits.
    return engine.script.runFile(boot) && engine.script.runFile(game) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    std::string script = "scratches.sc";
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", a.c_str()); std::exit(2); }
            return argv[++i];
        };
        if (a == "--data") opts.dataDirs.emplace_back(next());
        else if (a == "--script") script = next();
        else if (a == "--game") opts.game = next();
        else if (a == "--start") opts.start = next();
        else if (a == "--save-dir") opts.saveDir = next();
        else if (a == "--yaw") opts.yaw = std::stof(next());
        else if (a == "--pitch") opts.pitch = std::stof(next());
        else if (a == "--screenshot") opts.screenshot = next();
        else if (a == "--screenshot-at") opts.screenshotAt = std::stod(next());
        else if (a == "--exec") opts.exec = next();
        else if (a == "--fullscreen") opts.windowed = false;
        else if (a == "--trace") opts.trace = true;
        else if (a == "--hotspots") opts.hotspots = true;
        else if (a == "--api-status") { printApiStatus(); return 0; }
        else if (a == "--verify-media") opts.verifyMedia = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 2; }
    }
    if (opts.dataDirs.empty()) { usage(); return 2; }

    int status = run(opts, script);
    SDL_Quit();  // after the engine has released its window, GL context and audio device
    return status;
}
