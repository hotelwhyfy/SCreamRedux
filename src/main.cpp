// SCream Redux: a reimplementation of the SCream engine (Scratches, 2006/2007)
// that runs the original game scripts and data.

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_main.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#include "api.hpp"
#include "engine.hpp"
#include "lua50.hpp"
#include "platform.hpp"
#include "setup.hpp"

namespace {

void usage() {
    std::puts(
        "usage: scream --data DIR [--data DIR ...] [options]\n"
        "\n"
        "  --data DIR        directory with scream.sc, scratches.sc, *.res (repeatable;\n"
        "                    later directories override earlier ones)\n"
        "  --script NAME     game script to run (default: scream.cfg SCRIPT + .sc or .lua)\n"
        "  --game sdc|tlv    skip the intro and menu and start that game\n"
        "  --start ROOM[:NODE]  skip the game intro and start at a location\n"
        "  --yaw DEG --pitch DEG  initial view direction\n"
        "  --screenshot FILE write a PNG once the scene settles, then quit\n"
        "  --screenshot-at S take it after S seconds instead of waiting to settle\n"
        "  --exec LUA        run Lua as a handler once startup is idle; debugClick(name)\n"
        "                    clicks an object in the current view, debugKey(vk) presses a key\n"
        "  --save-dir DIR    keep saves and preferences in DIR (for tests)\n"
        "  --fullscreen      run fullscreen (default: scream.cfg FULLSCREEN)\n"
        "  --windowed        run in a window\n"
        "  --setup           open the setup launcher first (also: hold Option / Alt at start)\n"
        "  --window WxH      window size (default: scream.cfg WIDTH x HEIGHT)\n"
        "  --hotspots        draw clickable areas (SC_SHOW_HOTSPOTS)\n"
        "  --trace           log every sc* call\n"
        "  --api-status      print implementation status of all sc* functions\n"
        "  --verify-media    decode every image, sound and video in the archives\n"
        "  --check           check the world the script builds (targets, handlers, files)\n"
        "  --turbo           fast-forward: no audio or video, game time runs 30x\n"
        "  --crawl           click every object, use every item reaction, run every choice\n"
        "                    and dialogue from the start state; report warnings and errors\n"
        "  --explore         with --game: play automatically until stuck or finished\n"
        "  --explore-hints F puzzle solutions for --explore (format in explore.cpp)\n");
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

int run(const Options& opts, std::string script) {
    Engine engine(opts);
    api::registerAll(engine);
    if (opts.verifyMedia)
        return engine.verifyMedia();

    auto boot = engine.findData("scream.sc");
    // scream.exe runs SCRIPT.sc, else SCRIPT.lua (0x4061d0); Windows file names
    // ignore case ("SCRATCHES" in the shipped cfg).
    auto findAnyCase = [&](const std::string& name) -> std::filesystem::path {
        if (auto p = engine.findData(name); !p.empty())
            return p;
        auto lower = [](std::string s) {
            for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
            return s;
        };
        for (auto it = opts.dataDirs.rbegin(); it != opts.dataDirs.rend(); ++it) {
            std::error_code ec;
            for (const auto& e : std::filesystem::directory_iterator(*it, ec))
                if (lower(e.path().filename().string()) == lower(name))
                    return e.path();
        }
        return {};
    };
    std::filesystem::path game;
    if (!script.empty()) game = findAnyCase(script);
    else if (!(game = findAnyCase(engine.config.script + ".sc")).empty()) script = game.filename().string();
    else if (!(game = findAnyCase(engine.config.script + ".lua")).empty()) script = game.filename().string();
    else script = engine.config.script + ".sc";
    if (boot.empty() || game.empty()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "need scream.sc and %s in the data directories", script.c_str());
        return 1;
    }
    // The 2006 release (Lua 5.0 scripts) predates the alternate control option.
    {
        std::ifstream f(game, std::ios::binary);
        std::string head(5, '\0');
        f.read(head.data(), 5);
        engine.release2006 = lua50::isChunk(head + std::string(1, '\0'));
    }
    // scream.sc defines the SC_* constants; the game script then builds the
    // world and calls scRun(), which only returns when the player quits.
    if (!engine.script.runFile(boot) || !engine.script.runFile(game))
        return 1;
    return opts.check && engine.checkResult_ ? 2 : 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options opts;
    std::string script;  // default: scream.cfg's SCRIPT
    int setup = 0;       // 1: the setup launcher, then the game; 2: only the launcher
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
        else if (a == "--fullscreen") opts.fullscreen = 1;
        else if (a == "--windowed") opts.fullscreen = 0;
        else if (a == "--setup") setup = 1;
        else if (a == "--setup-only") setup = 2;
        else if (a == "--window") { std::string v = next(); opts.windowW = std::stoi(v); opts.windowH = std::stoi(v.substr(v.find('x') + 1)); }
        else if (a == "--trace") opts.trace = true;
        else if (a == "--hotspots") opts.hotspots = true;
        else if (a == "--api-status") { printApiStatus(); return 0; }
        else if (a == "--verify-media") opts.verifyMedia = true;
        else if (a == "--check") opts.check = true;
        else if (a == "--turbo") opts.turbo = true;
        else if (a == "--crawl") opts.crawl = opts.turbo = true;
        else if (a == "--explore") opts.explore = opts.turbo = true;
        else if (a == "--explore-hints") opts.exploreHints = next(), opts.explore = opts.turbo = true;
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); usage(); return 2; }
    }
    if (opts.dataDirs.empty()) { usage(); return 2; }
    const bool automated = opts.turbo || opts.check || opts.verifyMedia || (!opts.screenshot.empty() && setup != 2);
    if (setup == 0 && !automated && platform::setupKeyHeld())
        setup = 1;
    const SetupTest setupTest{std::getenv("SCREAM_SETUP_KEYS") ? std::getenv("SCREAM_SETUP_KEYS") : "",
                              setup == 2 ? opts.screenshot : std::string()};
    const bool testing = !setupTest.keys.empty() || !setupTest.screenshot.empty();
    if (setup && runSetup(opts.dataDirs, setup == 2, testing ? &setupTest : nullptr) == SetupResult::Quit)
        return 0;
    if (setup == 2)
        return 0;

    int status = run(opts, script);
    SDL_Quit();  // after the engine has released its window, GL context and audio device
    return status;
}
