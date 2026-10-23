#include "app.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "gl.hpp"
#include "ui.hpp"

namespace editor {

namespace {

std::filesystem::path findScript(const AppOptions& opts) {
    if (!opts.script.empty()) return opts.script;
    // The data's scream.cfg names the script (SCRIPT); its .lua source is what we edit.
    for (auto it = opts.dataDirs.rbegin(); it != opts.dataDirs.rend(); ++it) {
        std::ifstream cfg(*it / "scream.cfg");
        std::string word, name;
        while (cfg >> word)
            if (word == "SCRIPT" && cfg >> name) break;
        if (!name.empty() && std::filesystem::exists(*it / (name + ".lua"))) return *it / (name + ".lua");
    }
    return {};
}

std::filesystem::path tempDir() {
    auto dir = std::filesystem::temp_directory_path() / ("scream-edit-" + std::to_string(SDL_GetCurrentThreadID()));
    std::filesystem::create_directories(dir / "saves");
    return dir;
}

// --input: synthetic events pushed at the given frames, for testing the UI.
struct InputStep {
    int frame;
    std::string action;
    std::vector<std::string> args;
};

std::vector<InputStep> parseInput(const std::string& script) {
    std::vector<InputStep> out;
    std::istringstream all(script);
    std::string part;
    while (std::getline(all, part, ';')) {
        std::istringstream ss(part);
        InputStep st;
        if (!(ss >> st.frame >> st.action)) continue;
        for (std::string w; ss >> w;) st.args.push_back(w);
        out.push_back(st);
    }
    return out;
}

void pushInput(SDL_Window* window, const InputStep& st, float& mx, float& my) {
    SDL_Event e{};
    SDL_WindowID wid = SDL_GetWindowID(window);
    auto arg = [&](size_t i, float def) { return i < st.args.size() ? std::stof(st.args[i]) : def; };
    if (st.action == "move") {
        mx = arg(0, mx), my = arg(1, my);
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = wid;
        e.motion.x = mx, e.motion.y = my;
    } else if (st.action == "down" || st.action == "up") {
        e.type = st.action == "down" ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.windowID = wid;
        e.button.button = Uint8(arg(0, 1));
        e.button.down = st.action == "down";
        e.button.clicks = 1;
        e.button.x = mx, e.button.y = my;
    } else if (st.action == "wheel") {
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.windowID = wid;
        e.wheel.y = arg(0, 1);
        e.wheel.mouse_x = mx, e.wheel.mouse_y = my;
    } else if (st.action == "key" && !st.args.empty()) {
        std::string k = st.args[0];
        SDL_Keymod mod = 0;
        for (auto [prefix, m] : {std::pair{"ctrl+", SDL_KMOD_GUI}, {"shift+", SDL_KMOD_SHIFT}})
            if (k.rfind(prefix, 0) == 0) k = k.substr(std::strlen(prefix)), mod |= m;
        SDL_Keycode key = SDL_GetKeyFromName(k.c_str());
        SDL_Event press{};
        auto send = [&](Uint32 type) {
            press.type = type;
            press.key.windowID = wid;
            press.key.key = key;
            press.key.scancode = SDL_GetScancodeFromKey(key, nullptr);
            press.key.mod = mod;
            press.key.down = type == SDL_EVENT_KEY_DOWN;
            SDL_PushEvent(&press);
        };
        // Modifiers go down first, as a real keyboard sends them.
        if (mod & SDL_KMOD_GUI) {
            SDL_Event m{};
            m.type = SDL_EVENT_KEY_DOWN, m.key.windowID = wid, m.key.key = SDLK_LGUI, m.key.scancode = SDL_SCANCODE_LGUI,
            m.key.mod = mod, m.key.down = true;
            SDL_PushEvent(&m);
        }
        send(SDL_EVENT_KEY_DOWN);
        send(SDL_EVENT_KEY_UP);
        if (mod & SDL_KMOD_GUI) {
            SDL_Event m{};
            m.type = SDL_EVENT_KEY_UP, m.key.windowID = wid, m.key.key = SDLK_LGUI, m.key.scancode = SDL_SCANCODE_LGUI;
            SDL_PushEvent(&m);
        }
        return;
    } else {
        return;
    }
    SDL_PushEvent(&e);
}

}  // namespace

void App::select(const ElementPath& r, const ElementPath& v, const ElementPath& o) {
    room = r, view = v, object = o;
    if (v.empty() && !r.empty())  // a room opens at its first node
        if (model::Element* re = p.find(r))
            for (size_t i = 0; i < re->children.size(); ++i)
                if (re->children[i].kind() == "SC_NODE") {
                    view = r;
                    view.index.push_back(int(i));
                    break;
                }
    createMode = false;
}

bool App::jumpTo(const std::string& whereIn) {
    std::string where = whereIn;
    if (auto arrow = where.find(" <- "); arrow != std::string::npos) where = where.substr(0, arrow);
    if (where.rfind("room ", 0) == 0) where = where.substr(5);
    std::vector<std::string> parts;
    for (size_t at = 0;;) {
        size_t slash = where.find('/', at);
        parts.push_back(where.substr(at, slash - at));
        if (slash == std::string::npos) break;
        at = slash + 1;
    }
    for (auto& r : p.rooms) {
        if (r.e->name() != parts[0]) continue;
        if (parts.size() == 1) {
            select(r.path);
            center = Center::View;
            return true;
        }
        for (size_t i = 0; i < r.e->children.size(); ++i) {
            auto& v = r.e->children[i];
            if ((v.kind() != "SC_NODE" && v.kind() != "SC_ZOOM") || v.name() != parts[1]) continue;
            ElementPath vp = r.path, op;
            vp.index.push_back(int(i));
            if (parts.size() > 2)
                for (size_t j = 0; j < v.children.size(); ++j)
                    if (v.children[j].kind() == "SC_OBJECT" && v.children[j].name() == parts[2]) {
                        op = vp;
                        op.index.push_back(int(j));
                        break;
                    }
            select(r.path, vp, op);
            center = Center::View;
            return true;
        }
    }
    // A zoom by name, in whichever room has it first (as scSwitch finds them).
    for (auto& r : p.rooms)
        for (size_t i = 0; i < r.e->children.size(); ++i)
            if (r.e->children[i].kind() == "SC_ZOOM" && r.e->children[i].name() == parts[0]) {
                ElementPath vp = r.path;
                vp.index.push_back(int(i));
                select(r.path, vp);
                center = Center::View;
                return true;
            }
    status = "can't find " + whereIn;
    return false;
}

const RendererTexture* App::texture(const std::string& name) { return renderer.texture(p.res, name); }

ImTextureID App::imTexture(const std::string& name) {
    const RendererTexture* t = texture(name);
    return t ? ImTextureID(intptr_t(t->id)) : ImTextureID(0);
}

std::filesystem::path App::writeTemp() {
    auto dir = tempDir();
    auto path = dir / p.scriptPath.filename();
    std::ofstream f(path, std::ios::binary);
    f << model::writeDocument(p.doc, p.builders);
    return f ? path : std::filesystem::path();
}

namespace {

// The engine's command line for the current script.
std::vector<std::string> engineArgs(App& a, const std::filesystem::path& script) {
    std::vector<std::string> args{a.enginePath.string()};
    for (auto& d : a.p.dataDirs) args.push_back("--data"), args.push_back(d.string());
    args.push_back("--data"), args.push_back(script.parent_path().string());
    args.push_back("--script"), args.push_back(script.filename().string());
    args.push_back("--save-dir"), args.push_back((script.parent_path() / "saves").string());
    return args;
}

SDL_Process* spawn(const std::vector<std::string>& args, bool pipe) {
    std::vector<const char*> argv;
    for (auto& s : args) argv.push_back(s.c_str());
    argv.push_back(nullptr);
    return SDL_CreateProcess(argv.data(), pipe);
}

}  // namespace

void App::runCheck() {
    problems.clear();
    checked = true;
    auto script = writeTemp();
    if (script.empty()) {
        status = "can't write a temporary script";
        return;
    }
    auto args = engineArgs(*this, script);
    args.push_back("--check");
    SDL_Process* proc = spawn(args, true);
    if (!proc) {
        status = std::string("can't run the engine: ") + SDL_GetError();
        return;
    }
    size_t size = 0;
    int code = 0;
    char* out = static_cast<char*>(SDL_ReadProcess(proc, &size, &code));
    SDL_DestroyProcess(proc);
    std::istringstream lines(out ? std::string(out, size) : std::string());
    SDL_free(out);
    std::string line, summary;
    while (std::getline(lines, line)) {
        if (line.rfind("checked ", 0) == 0) {
            summary = line;
            continue;
        }
        auto colon = line.find(": ");
        if (colon == std::string::npos) continue;
        problems.push_back({line.substr(0, colon), line.substr(colon + 2)});
    }
    status = summary.empty() ? "the check didn't run (exit " + std::to_string(code) + ")" : summary;
}

void App::play() {
    auto script = writeTemp();
    if (script.empty()) return;
    auto args = engineArgs(*this, script);
    args.push_back("--windowed");
    model::Element* r = roomEl();
    model::Element* v = viewEl();
    if (r) {
        std::string start = r->name();
        if (v && v->kind() == "SC_NODE") start += ":" + v->name();
        args.push_back("--start"), args.push_back(start);
    }
    static SDL_Process* last = nullptr;
    if (last) SDL_DestroyProcess(last);  // forgets it; the game keeps running
    last = spawn(args, false);
    status = last ? "playing " + (r ? r->name() : std::string("from the start")) : std::string("can't run the engine");
}

int runApp(const AppOptions& opts) {
    App a;
    a.opts = opts;
    if (const char* base = SDL_GetBasePath()) a.enginePath = std::filesystem::path(base) / "scream";
    auto script = findScript(opts);
    if (script.empty()) a.p.error = "no script: pass SCRIPT.lua or a --data directory with one";
    else a.p.load(script, opts.dataDirs);
    a.cam.fovY = 75;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window* window = SDL_CreateWindow("SCream Editor", opts.width, opts.height,
                                          SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    SDL_GLContext gl = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!gl) {
        std::fprintf(stderr, "window: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_MakeCurrent(window, gl);
    SDL_GL_SetSwapInterval(1);
    if (const char* missing = gl::load()) {
        std::fprintf(stderr, "OpenGL: no %s\n", missing);
        return 1;
    }
    a.renderer.init();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui::GetStyle().WindowRounding = 0;
    ImGui_ImplSDL3_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    if (!opts.open.empty()) a.jumpTo(opts.open);
    if (opts.center == "map") a.center = App::Center::Map;
    else if (opts.center.rfind("code:", 0) == 0) a.center = App::Center::Handler, a.handler = opts.center.substr(5);
    else if (opts.center.rfind("asset:", 0) == 0) a.center = App::Center::Asset, a.asset = opts.center.substr(6);
    if (opts.yaw) a.cam.yaw = opts.yaw;

    bool running = true;
    int frame = 0;
    auto input = parseInput(opts.input);
    float inputX = 0, inputY = 0;
    std::string title;
    while (running) {
        for (auto& st : input)
            if (st.frame == frame) pushInput(window, st, inputX, inputY);
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
        }
        std::string t = "SCream Editor - " + a.p.scriptPath.filename().string() + (a.p.modified() ? " *" : "");
        if (t != title) SDL_SetWindowTitle(window, (title = t).c_str());

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        // Shortcuts (Ctrl is Cmd on macOS).
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal))
            a.status = a.p.save() ? "saved " + a.p.scriptPath.string() : a.p.error;
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal) && !ImGui::GetIO().WantTextInput)
            a.p.undo();
        if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal) &&
            !ImGui::GetIO().WantTextInput)
            a.p.redo();
        if (ImGui::IsKeyPressed(ImGuiKey_F5)) a.play();
        if (ImGui::IsKeyPressed(ImGuiKey_F6)) a.runCheck();

        float menuH = 0;
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Save", "Ctrl+S")) a.status = a.p.save() ? "saved" : a.p.error;
                if (ImGui::MenuItem("Quit")) running = false;
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Undo", "Ctrl+Z", false, a.p.canUndo())) a.p.undo();
                if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, a.p.canRedo())) a.p.redo();
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Run")) {
                if (ImGui::MenuItem("Play from here", "F5")) a.play();
                if (ImGui::MenuItem("Check", "F6")) a.runCheck();
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("View", nullptr, a.center == App::Center::View)) a.center = App::Center::View;
            if (ImGui::MenuItem("Map", nullptr, a.center == App::Center::Map)) a.center = App::Center::Map;
            ImGui::Separator();
            ImGui::TextDisabled("%s", (a.p.error.empty() ? a.status : a.p.error).c_str());
            menuH = ImGui::GetWindowSize().y;
            ImGui::EndMainMenuBar();
        }

        // A fixed layout: outline | view over problems | inspector.
        ImVec2 ds = io.DisplaySize;
        const float left = 340, right = 400, bottom = 190;
        auto place = [&](float x, float y, float w, float h) {
            ImGui::SetNextWindowPos({x, y});
            ImGui::SetNextWindowSize({w, h});
        };
        place(0, menuH, left, ds.y - menuH);
        drawOutline(a);
        place(ds.x - right, menuH, right, ds.y - menuH);
        drawInspector(a);
        place(left, ds.y - bottom, ds.x - left - right, bottom);
        drawProblems(a);
        place(left, menuH, ds.x - left - right, ds.y - menuH - bottom);
        Viewport vp;
        switch (a.center) {
        case App::Center::View: vp = drawView(a); break;
        case App::Center::Map: drawMap(a); break;
        case App::Center::Handler: drawHandler(a); break;
        case App::Center::Asset: drawAsset(a); break;
        case App::Center::Text: break;
        }
        ImGui::Render();

        int w, h;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        a.renderer.beginFrame(w, h);
        gl::ClearColor(0.08f, 0.08f, 0.1f, 1);
        gl::Clear(gl::COLOR_BUFFER_BIT);
        if (vp.w > 0) {
            for (auto& f : a.scene.view.faces) a.texture(f);  // load before drawing
            a.renderer.drawNode(a.scene.view, a.cam, vp);
        }
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (++frame == opts.quitAt && !opts.screenshot.empty()) {
            a.renderer.capture(opts.screenshot.c_str());
            running = false;
        }
        SDL_GL_SwapWindow(window);
    }

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}

}  // namespace editor
