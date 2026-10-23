#pragma once

// The editor's state shared by its panels (panels.cpp), views (views.cpp) and
// shell (app.cpp).

#include <imgui.h>

#include <map>
#include <string>
#include <vector>

#include "app.hpp"
#include "project.hpp"
#include "renderer.hpp"
#include "world.hpp"

namespace editor {

// The cube faces and the names scripts use for them.
inline const char* const kFaces[] = {"SC_FRONT", "SC_BACK", "SC_LEFT", "SC_RIGHT", "SC_UP", "SC_DOWN"};
int faceIndex(const std::string& name);  // SC_FRONT -> 1 ... SC_DOWN -> 6, else 0

struct Problem {
    std::string where, what;
};

struct App {
    AppOptions opts;
    Project p;
    Renderer renderer;
    std::filesystem::path enginePath;  // the scream executable next to scream-edit

    // What is open: a room, one of its nodes or zooms, and an object in it.
    ElementPath room, view, object;
    enum class Center { View, Map, Handler, Text, Asset } center = Center::View;
    std::string handler;  // Center::Handler: the function shown
    std::string asset;    // Center::Asset: the file shown
    Camera cam;
    struct {
        View view;  // the open node's faces, for the renderer
    } scene;
    std::string status;  // last message for the status bar
    std::vector<Problem> problems;
    bool checked = false;
    bool showHotspots = true;

    // Node and zoom editing (views.cpp).
    struct Drag {
        enum class Mode { None, Look, Move, Resize, Create } mode = Mode::None;
        int face = 0;
        float startX = 0, startY = 0;  // face or zoom coordinates at the press
        Rect startRect;
        int edges = 0;  // Resize: 1 left, 2 right, 4 top, 8 bottom
        ImVec2 startMouse;
        float startYaw = 0, startPitch = 0;
    } drag;
    bool createMode = false;  // the next drag in a view draws a new hotspot

    // Editing buffers for the code and text panels.
    std::string codeBuffer, codeFor;

    void select(const ElementPath& roomPath, const ElementPath& viewPath = {}, const ElementPath& objectPath = {});
    // Selects by names, as the problems report them ("room/view/object").
    bool jumpTo(const std::string& where);
    model::Element* roomEl() { return p.find(room); }
    model::Element* viewEl() { return p.find(view); }
    model::Element* objectEl() { return p.find(object); }
    const RendererTexture* texture(const std::string& name);
    ImTextureID imTexture(const std::string& name);

    // Run the engine on the current text: --check (fills problems), or play
    // from the open node.
    void runCheck();
    void play();
    std::filesystem::path writeTemp();  // the current text as a script in a temp directory

    // Change element `path` with `fn` and commit; reports a failed commit.
    template <class F>
    void edit(const ElementPath& path, F fn) {
        if (model::Element* e = p.find(path)) {
            p.touch(path);
            fn(*e);
            if (!p.commit()) status = "edit failed: " + p.error;
        }
    }
};

// Panels (panels.cpp).
void drawOutline(App& a);
void drawInspector(App& a);
void drawProblems(App& a);
void drawHandler(App& a);
void drawText(App& a);
void drawAsset(App& a);
// Views (views.cpp). The node view returns the screen rect (in pixels) the
// renderer must draw the panorama into, or an empty rect.
Viewport drawView(App& a);
void drawMap(App& a);

// Element helpers shared by the panels.
std::string clickSummary(const App& a, const doc::Call* click);
std::vector<std::string> names(model::Element* parent, const std::string& kind);
// Add the rect of an object to `out` (scPlace's x, y, w, h); false if none.
bool objectRect(model::Element& o, Rect& out, int& face);

}  // namespace editor
