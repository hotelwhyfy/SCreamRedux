#include "app.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "gl.hpp"
#include "luadoc.hpp"
#include "model.hpp"

namespace editor {

namespace {

struct Project {
    std::filesystem::path scriptPath;
    doc::Document doc;
    std::vector<model::Builder> builders;
    std::string error;

    bool load(const std::filesystem::path& path) {
        scriptPath = path;
        std::ifstream f(path, std::ios::binary);
        if (!f) {
            error = "can't read " + path.string();
            return false;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        if (!doc.parse(ss.str())) {
            error = path.string() + ": " + doc.error();
            return false;
        }
        builders.clear();
        for (const auto& it : doc.items) {
            if (it.kind != doc::Item::Kind::Function || !it.builder) continue;
            std::vector<doc::Call> calls;
            for (const auto& st : it.body) calls.push_back(*st.call);
            builders.push_back(model::Builder::fold(it.name, calls));
        }
        return true;
    }
};

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

void outlineElement(model::Element& e) {
    if (!e.isCreate()) return;
    std::string label = e.kind().substr(3) + "  " + e.name();
    ImGuiTreeNodeFlags flags = e.scoped ? 0 : ImGuiTreeNodeFlags_Leaf;
    if (ImGui::TreeNodeEx(&e, flags, "%s", label.c_str())) {
        for (auto& c : e.children) outlineElement(c);
        ImGui::TreePop();
    }
}

void outline(Project& p) {
    ImGui::Begin("Outline");
    if (!p.error.empty()) ImGui::TextWrapped("%s", p.error.c_str());
    for (auto& b : p.builders) {
        if (ImGui::TreeNode(b.name.c_str())) {
            for (auto& e : b.body) outlineElement(e);
            ImGui::TreePop();
        }
    }
    ImGui::End();
}

}  // namespace

int runApp(const AppOptions& opts) {
    Project project;
    auto script = findScript(opts);
    if (script.empty()) project.error = "no script: pass SCRIPT.lua or a --data directory with one";
    else project.load(script);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_Window* window = SDL_CreateWindow("SCream Editor", 1400, 900,
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

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL3_InitForOpenGL(window, gl);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT || e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) running = false;
        }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        outline(project);
        ImGui::Render();
        int w, h;
        SDL_GetWindowSizeInPixels(window, &w, &h);
        gl::Viewport(0, 0, w, h);
        gl::ClearColor(0.1f, 0.1f, 0.12f, 1);
        gl::Clear(gl::COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
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
