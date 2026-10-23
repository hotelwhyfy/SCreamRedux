// The node view (the panorama with its hotspots on the cube faces), the zoom
// view (a close-up and its hotspots) and the map of a room's nodes.

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "ui.hpp"

namespace editor {

namespace {

constexpr float kFace = 1024;                 // scPlace space on a cube face, whatever the texture size
constexpr float kZoomW = 800, kZoomH = 472;  // scPlace space in a zoom
constexpr int kSnap = 5;                     // the developers' grid (98.9% of their numbers)

Vec3 add(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
Vec3 scale(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

int snap(float v) { return int(std::lround(v / kSnap)) * kSnap; }

// The direction of face pixel (x, y), in scPlace units.
Vec3 facePoint(int face, float x, float y) {
    const FaceBasis& b = faceBasis(face);
    return add(add(b.center, scale(b.right, 2 * x / kFace - 1)), scale(b.down, 2 * y / kFace - 1));
}

// Where a direction meets the plane of `face`, in that face's scPlace units
// (past its edges too); false when it points away from the face.
bool onFacePlane(Vec3 dir, int face, float& x, float& y) {
    const FaceBasis& b = faceBasis(face);
    float d = dot(dir, b.center);
    if (d <= 1e-4f) return false;
    Vec3 p = add(scale(dir, 1 / d), scale(b.center, -1));
    x = (dot(p, b.right) + 1) / 2 * kFace;
    y = (dot(p, b.down) + 1) / 2 * kFace;
    return true;
}

ImU32 cursorColor(const std::string& cursor, float alpha) {
    int a = int(alpha * 255);
    if (cursor == "SC_FORWARD") return IM_COL32(80, 220, 120, a);
    if (cursor == "SC_LOOK") return IM_COL32(90, 160, 255, a);
    if (cursor == "SC_USE") return IM_COL32(255, 170, 60, a);
    if (cursor == "SC_ZOOMOUT") return IM_COL32(200, 120, 255, a);
    return IM_COL32(220, 220, 220, a);
}

struct Hotspot {
    model::Element* e;
    ElementPath path;
    Rect rect;
    int face;
    std::string cursor;
    bool disabled;
};

std::vector<Hotspot> hotspots(App& a) {
    std::vector<Hotspot> out;
    model::Element* v = a.viewEl();
    if (!v) return out;
    for (size_t i = 0; i < v->children.size(); ++i) {
        auto& o = v->children[i];
        if (o.kind() != "SC_OBJECT") continue;
        Hotspot h{&o, a.view, {}, 0, {}, o.find("scDisable", "SC_OBJECT") != nullptr};
        h.path.index.push_back(int(i));
        if (!objectRect(o, h.rect, h.face)) continue;
        if (auto* c = o.find("scOnHover"); c && !c->args.empty()) h.cursor = c->args[0].src;
        out.push_back(h);
    }
    return out;
}

void setRect(model::Element& o, const Rect& r) {
    if (auto* c = o.find("scPlace"); c && c->args.size() >= 4) {
        c->args[0] = doc::Expr::number(r.x);
        c->args[1] = doc::Expr::number(r.y);
        c->args[2] = doc::Expr::number(r.w);
        c->args[3] = doc::Expr::number(r.h);
    }
}

Rect normalized(float x0, float y0, float x1, float y1) {
    int ax = snap(std::min(x0, x1)), ay = snap(std::min(y0, y1));
    int bx = snap(std::max(x0, x1)), by = snap(std::max(y0, y1));
    return {ax, ay, bx - ax, by - ay};
}

// A rect changed by dragging: moved, or some edges moved.
Rect dragged(const Rect& r, int edges, float dx, float dy) {
    float x0 = float(r.x), y0 = float(r.y), x1 = float(r.x + r.w), y1 = float(r.y + r.h);
    if (!edges) x0 += dx, x1 += dx, y0 += dy, y1 += dy;
    if (edges & 1) x0 += dx;
    if (edges & 2) x1 += dx;
    if (edges & 4) y0 += dy;
    if (edges & 8) y1 += dy;
    if (!edges) {
        int x = snap(x0), y = snap(y0);
        return {x, y, r.w, r.h};
    }
    return normalized(x0, y0, x1, y1);
}

// Which edges of `r` are within `m` units of (x, y): 0 inside (move), -1 outside.
int edgesAt(const Rect& r, float x, float y, float m) {
    if (x < r.x - m || x > r.x + r.w + m || y < r.y - m || y > r.y + r.h + m) return -1;
    int e = 0;
    if (std::fabs(x - r.x) < m) e |= 1;
    else if (std::fabs(x - (r.x + r.w)) < m) e |= 2;
    if (std::fabs(y - r.y) < m) e |= 4;
    else if (std::fabs(y - (r.y + r.h)) < m) e |= 8;
    return e;
}

model::Element newObject(const std::string& face, const Rect& r) {
    model::Element o;
    o.head = {"scCreate", {doc::Expr::name("SC_OBJECT"), doc::Expr::string("hotspot")}};
    o.props.push_back({"scPlace", {doc::Expr::number(r.x), doc::Expr::number(r.y), doc::Expr::number(r.w),
                                   doc::Expr::number(r.h), doc::Expr::name(face)}});
    o.props.push_back({"scOnHover", {doc::Expr::name("SC_LOOK")}});
    o.props.push_back({"scOnClick", {doc::Expr::name("SC_FEEDBACK"), doc::Expr::string("Nothing special.")}});
    return o;
}

// Add a hotspot to the open view and select it.
void addObject(App& a, const std::string& face, const Rect& r) {
    model::Element* v = a.viewEl();
    if (!v) return;
    ElementPath path = a.view;
    path.index.push_back(int(v->children.size()));
    a.edit(a.view, [o = newObject(face, r)](model::Element& view) { view.children.push_back(o); });
    a.object = path;
}

// Follow an object's click the way the game would: to a node, zoom or room,
// through a handler's scSwitch calls (its first move), or back out of a zoom
// to the view it was opened from. Returns false when the click goes nowhere.
bool follow(App& a, model::Element& o) {
    auto* c = o.find("scOnClick");
    if (!c || c->args.empty()) return false;
    std::string kind = c->args[0].src, target = c->args.size() > 1 ? a.p.resolve(c->args[1]) : std::string();
    std::string node;  // a room switch's node, when the handler names one
    if (kind == "SC_CUSTOM") {
        auto m = a.p.moves().find(target);
        if (m == a.p.moves().end() || m->second.empty()) {
            a.status = target + "() doesn't move the player (no scSwitch in its code)";
            return false;
        }
        kind = m->second[0].kind, target = m->second[0].target, node = m->second[0].node;
    }
    const auto here = std::make_pair(a.room, a.view);
    auto went = [&](bool ok) {
        if (ok) a.history.push_back(here);
        return ok;
    };
    if (kind == "SC_ZOOMOUT" || kind == "SC_ZOOMBACK") {
        // Back to where the zoom was opened from (ZOOMBACK: out of every zoom).
        while (!a.history.empty()) {
            auto [r, v] = a.history.back();
            a.history.pop_back();
            model::Element* ve = a.p.find(v);
            if (ve && (kind == "SC_ZOOMOUT" || ve->kind() == "SC_NODE")) {
                a.select(r, v);
                return true;
            }
        }
        a.status = "opened directly: no view to go back to";
        return false;
    }
    if (kind == "SC_NODE") {
        model::Element* room = a.roomEl();
        if (!room) return false;
        for (size_t i = 0; i < room->children.size(); ++i)
            if (room->children[i].kind() == "SC_NODE" && room->children[i].name() == target) {
                ElementPath vp = a.room;
                vp.index.push_back(int(i));
                a.select(a.room, vp);
                return went(true);
            }
        a.status = "no node " + target + " in this room";
        return false;
    }
    if (kind == "SC_ZOOM") {
        // The room's own zoom of that name first, else the first anywhere (as scSwitch).
        if (model::Element* room = a.roomEl())
            for (size_t i = 0; i < room->children.size(); ++i)
                if (room->children[i].kind() == "SC_ZOOM" && room->children[i].name() == target) {
                    ElementPath vp = a.room;
                    vp.index.push_back(int(i));
                    a.select(a.room, vp);
                    return went(true);
                }
        return went(a.jumpTo(target));
    }
    if (kind == "SC_ROOM") {
        // The game opens the room at the node last visited there; the editor at its first
        // node, or at the one the handler names (scSetLastVisited).
        return went(a.jumpTo(node.empty() ? target : target + "/" + node));
    }
    return false;
}

void tooltip(App& a, model::Element& o) {
    ImGui::BeginTooltip();
    ImGui::Text("%s", o.name().c_str());
    ImGui::TextDisabled("%s", clickSummary(a, o.find("scOnClick")).c_str());
    for (auto* r : o.findAll("scReaction"))
        if (!r->args.empty()) ImGui::TextDisabled("+ %s", a.p.resolve(r->args[0]).c_str());
    ImGui::EndTooltip();
}

Viewport nodeView(App& a, model::Element& node) {
    ImVec2 r0 = ImGui::GetCursorScreenPos(), size = ImGui::GetContentRegionAvail();
    if (size.x < 32 || size.y < 32) return {};
    ImGui::InvisibleButton("canvas", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const int w = int(size.x), h = int(size.y);
    auto project = [&](int face, float x, float y, ImVec2& out) {
        float px, py;
        if (!projectPoint(a.cam, facePoint(face, x, y), w, h, px, py)) return false;
        out = {r0.x + px, r0.y + py};
        return true;
    };
    // The face and face coordinates under the pointer.
    ImVec2 m = io.MousePos;
    Vec3 ray = viewRay(a.cam, m.x - r0.x, m.y - r0.y, w, h);
    int hoverFace = 0;
    float hx = 0, hy = 0;
    bool inside = ImGui::IsItemHovered();
    if (inside) pickFace(ray, hoverFace, hx, hy, int(kFace));

    auto spots = hotspots(a);
    Hotspot* hovered = nullptr;
    for (auto& s : spots)
        if (inside && s.face == hoverFace && s.rect.contains(int(hx), int(hy))) hovered = &s;

    // Input.
    auto& d = a.drag;
    if (ImGui::IsItemActivated()) {
        d.startMouse = m;
        d.startYaw = a.cam.yaw, d.startPitch = a.cam.pitch;
        d.mode = App::Drag::Mode::Look;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && hoverFace) {
            Hotspot* sel = nullptr;
            for (auto& s : spots)
                if (s.path == a.object) sel = &s;
            const float margin = 12 * kFace / float(w) * a.cam.fovY / 60;
            int e = sel && sel->face == hoverFace ? edgesAt(sel->rect, hx, hy, margin) : -1;
            if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !a.createMode) {
                d.mode = App::Drag::Mode::None;  // the first click selected it; this one follows it
                ElementPath target = hovered->path;
                a.later([&a, target] {
                    if (model::Element* o = a.p.find(target)) follow(a, *o);
                });
            } else if (a.createMode) {
                d.mode = App::Drag::Mode::Create;
                d.face = hoverFace;
            } else if (e >= 0) {
                d.mode = e ? App::Drag::Mode::Resize : App::Drag::Mode::Move;
                d.edges = e;
                d.face = sel->face;
                d.startRect = sel->rect;
                a.p.touch(a.object);
            } else if (hovered) {
                a.object = hovered->path;
                d.mode = App::Drag::Mode::None;
            } else {
                a.object = {};
            }
            d.startX = hx, d.startY = hy;
        }
    }
    float fx = 0, fy = 0;
    bool onPlane = d.face && onFacePlane(ray, d.face, fx, fy);
    if (ImGui::IsItemActive()) {
        switch (d.mode) {
        case App::Drag::Mode::Look: {
            float k = a.cam.fovY / float(h);
            a.cam.yaw = d.startYaw - (m.x - d.startMouse.x) * k;
            a.cam.pitch = std::clamp(d.startPitch + (m.y - d.startMouse.y) * k, -89.0f, 89.0f);
            break;
        }
        case App::Drag::Mode::Move:
        case App::Drag::Mode::Resize:
            if (onPlane)
                if (model::Element* o = a.objectEl()) setRect(*o, dragged(d.startRect, d.edges, fx - d.startX, fy - d.startY));
            break;
        default: break;
        }
    }
    if (ImGui::IsItemDeactivated()) {
        if (d.mode == App::Drag::Mode::Move || d.mode == App::Drag::Mode::Resize) {
            a.commitLater();
        } else if (d.mode == App::Drag::Mode::Create && onPlane) {
            Rect r = normalized(d.startX, d.startY, fx, fy);
            if (r.w >= kSnap && r.h >= kSnap) addObject(a, kFaces[d.face - 1], r);
            a.createMode = false;
        }
        d.mode = App::Drag::Mode::None;
        d.face = 0;
    }
    if (inside && io.MouseWheel != 0) a.cam.fovY = std::clamp(a.cam.fovY - io.MouseWheel * 3, 20.0f, 110.0f);

    // Hotspots, drawn as the engine clips them: to their face.
    dl->PushClipRect(r0, {r0.x + size.x, r0.y + size.y}, true);
    auto drawRect = [&](int face, Rect r, ImU32 fill, ImU32 line, float thick) {
        float x0 = std::max(0.0f, float(r.x)), y0 = std::max(0.0f, float(r.y));
        float x1 = std::min(kFace, float(r.x + r.w)), y1 = std::min(kFace, float(r.y + r.h));
        if (x1 <= x0 || y1 <= y0) return;
        constexpr int N = 8;  // straight in 3D, but it may cross behind the camera
        ImVec2 grid[N + 1][N + 1];
        bool ok[N + 1][N + 1];
        for (int i = 0; i <= N; ++i)
            for (int j = 0; j <= N; ++j)
                ok[i][j] = project(face, x0 + (x1 - x0) * i / N, y0 + (y1 - y0) * j / N, grid[i][j]);
        for (int i = 0; i < N; ++i)
            for (int j = 0; j < N; ++j)
                if (ok[i][j] && ok[i + 1][j] && ok[i + 1][j + 1] && ok[i][j + 1])
                    dl->AddQuadFilled(grid[i][j], grid[i + 1][j], grid[i + 1][j + 1], grid[i][j + 1], fill);
        for (int k = 0; k < N; ++k) {
            auto seg = [&](int i0, int j0, int i1, int j1) {
                if (ok[i0][j0] && ok[i1][j1]) dl->AddLine(grid[i0][j0], grid[i1][j1], line, thick);
            };
            seg(k, 0, k + 1, 0), seg(k, N, k + 1, N), seg(0, k, 0, k + 1), seg(N, k, N, k + 1);
        }
    };
    if (a.showHotspots)
        for (auto& s : spots) {
            bool sel = s.path == a.object, hov = hovered == &s;
            float alpha = s.disabled ? 0.12f : sel ? 0.35f : hov ? 0.3f : 0.18f;
            ImU32 line = sel ? IM_COL32(255, 230, 80, 255) : cursorColor(s.cursor, s.disabled ? 0.4f : 0.9f);
            drawRect(s.face, s.rect, cursorColor(s.cursor, alpha), line, sel ? 2.5f : 1.2f);
            ImVec2 c;
            if ((sel || hov) && project(s.face, s.rect.x + s.rect.w / 2.0f, s.rect.y + s.rect.h / 2.0f, c)) {
                ImVec2 ts = ImGui::CalcTextSize(s.e->name().c_str());
                dl->AddRectFilled({c.x - ts.x / 2 - 3, c.y - ts.y / 2 - 1}, {c.x + ts.x / 2 + 3, c.y + ts.y / 2 + 1},
                                  IM_COL32(0, 0, 0, 160), 3);
                dl->AddText({c.x - ts.x / 2, c.y - ts.y / 2}, IM_COL32_WHITE, s.e->name().c_str());
            }
        }
    if (d.mode == App::Drag::Mode::Create && ImGui::IsItemActive() && onPlane)
        drawRect(d.face, normalized(d.startX, d.startY, fx, fy), IM_COL32(255, 255, 255, 50), IM_COL32_WHITE, 2);
    // Pointer readout: face and scPlace coordinates, as the developers measured them.
    if (inside && hoverFace) {
        char buf[96];
        std::snprintf(buf, sizeof buf, "%s  %d, %d%s", kFaces[hoverFace - 1] + 3, snap(hx), snap(hy),
                      a.createMode ? "   drag to draw a hotspot" : "");
        dl->AddText({r0.x + 8, r0.y + size.y - 22}, IM_COL32(255, 255, 255, 200), buf);
    }
    dl->PopClipRect();
    if (hovered && d.mode == App::Drag::Mode::None) tooltip(a, *hovered->e);

    // The panorama itself is drawn by the renderer under this window.
    a.scene.view.isZoom = false;
    for (int f = 1; f <= 6; ++f) {
        auto* t = node.find("scTexture", kFaces[f - 1]);
        a.scene.view.faces[size_t(f - 1)] = t && t->args.size() >= 2 ? a.p.resolve(t->args[1]) : std::string();
    }
    float fb = io.DisplayFramebufferScale.x;
    return {int(r0.x * fb), int(r0.y * fb), int(size.x * fb), int(size.y * fb)};
}

void zoomView(App& a, model::Element& zoom) {
    auto* t = zoom.find("scTexture", "SC_ZOOM");
    std::string image = t && t->args.size() >= 2 ? a.p.resolve(t->args[1]) : std::string();
    const RendererTexture* tex = image.empty() ? nullptr : a.texture(image);
    ImVec2 avail = ImGui::GetContentRegionAvail();
    // The image at its own aspect (the DC's HD close-ups are 16:10); hotspots
    // span the whole image in 800 x 472 units.
    float aspect = tex && tex->height ? float(tex->width) / float(tex->height) : kZoomW / kZoomH;
    float w = avail.x, h = w / aspect;
    if (h > avail.y) h = avail.y, w = h * aspect;
    if (w < 32 || h < 32) return;
    ImVec2 r0 = ImGui::GetCursorScreenPos();
    if (tex) ImGui::GetWindowDrawList()->AddImage(ImTextureID(intptr_t(tex->id)), r0, {r0.x + w, r0.y + h});
    else ImGui::GetWindowDrawList()->AddRectFilled(r0, {r0.x + w, r0.y + h}, IM_COL32(40, 0, 40, 255));
    ImGui::InvisibleButton("zoom", {w, h});
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiIO& io = ImGui::GetIO();
    const float sx = w / kZoomW, sy = h / kZoomH;
    float zx = (io.MousePos.x - r0.x) / sx, zy = (io.MousePos.y - r0.y) / sy;
    bool inside = ImGui::IsItemHovered();
    auto spots = hotspots(a);
    Hotspot* hovered = nullptr;
    for (auto& s : spots)
        if (inside && s.rect.contains(int(zx), int(zy))) hovered = &s;
    auto& d = a.drag;
    if (ImGui::IsItemActivated()) {
        d.mode = App::Drag::Mode::None;
        Hotspot* sel = nullptr;
        for (auto& s : spots)
            if (s.path == a.object) sel = &s;
        int e = sel ? edgesAt(sel->rect, zx, zy, 8 / sx) : -1;
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !a.createMode) {
            ElementPath target = hovered->path;
            a.later([&a, target] {
                if (model::Element* o = a.p.find(target)) follow(a, *o);
            });
        } else if (a.createMode) {
            d.mode = App::Drag::Mode::Create;
        } else if (e >= 0) {
            d.mode = e ? App::Drag::Mode::Resize : App::Drag::Mode::Move;
            d.edges = e;
            d.startRect = sel->rect;
            a.p.touch(a.object);
        } else if (hovered) {
            a.object = hovered->path;
        } else {
            a.object = {};
        }
        d.startX = zx, d.startY = zy;
    }
    if (ImGui::IsItemActive() && (d.mode == App::Drag::Mode::Move || d.mode == App::Drag::Mode::Resize))
        if (model::Element* o = a.objectEl()) setRect(*o, dragged(d.startRect, d.edges, zx - d.startX, zy - d.startY));
    if (ImGui::IsItemDeactivated()) {
        if (d.mode == App::Drag::Mode::Move || d.mode == App::Drag::Mode::Resize) {
            a.commitLater();
        } else if (d.mode == App::Drag::Mode::Create) {
            Rect r = normalized(d.startX, d.startY, zx, zy);
            if (r.w >= kSnap && r.h >= kSnap) addObject(a, "SC_ZOOM", r);
            a.createMode = false;
        }
        d.mode = App::Drag::Mode::None;
    }
    auto box = [&](const Rect& r) {
        return std::pair<ImVec2, ImVec2>{{r0.x + r.x * sx, r0.y + r.y * sy}, {r0.x + (r.x + r.w) * sx, r0.y + (r.y + r.h) * sy}};
    };
    dl->PushClipRect(r0, {r0.x + w, r0.y + h}, true);
    if (a.showHotspots)
        for (auto& s : spots) {
            bool sel = s.path == a.object, hov = hovered == &s;
            auto [p0, p1] = box(s.rect);
            float alpha = s.disabled ? 0.12f : sel ? 0.35f : hov ? 0.3f : 0.18f;
            dl->AddRectFilled(p0, p1, cursorColor(s.cursor, alpha));
            dl->AddRect(p0, p1, sel ? IM_COL32(255, 230, 80, 255) : cursorColor(s.cursor, s.disabled ? 0.4f : 0.9f), 0, 0,
                        sel ? 2.5f : 1.2f);
            if (sel || hov) dl->AddText({p0.x + 4, p0.y + 2}, IM_COL32_WHITE, s.e->name().c_str());
        }
    if (d.mode == App::Drag::Mode::Create && ImGui::IsItemActive()) {
        auto [p0, p1] = box(normalized(d.startX, d.startY, zx, zy));
        dl->AddRect(p0, p1, IM_COL32_WHITE, 0, 0, 2);
    }
    if (inside) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "%d, %d", snap(zx), snap(zy));
        dl->AddText({r0.x + 8, r0.y + h - 22}, IM_COL32(255, 255, 255, 220), buf);
    }
    dl->PopClipRect();
    if (hovered && d.mode == App::Drag::Mode::None) tooltip(a, *hovered->e);
}

}  // namespace

int faceIndex(const std::string& name) {
    for (int i = 0; i < 6; ++i)
        if (name == kFaces[i]) return i + 1;
    return 0;
}

bool objectRect(model::Element& o, Rect& out, int& face) {
    auto* c = o.find("scPlace");
    if (!c || c->args.size() < 4) return false;
    for (int i = 0; i < 4; ++i)
        if (!c->args[size_t(i)].isNumber()) return false;
    out = {int(c->args[0].num()), int(c->args[1].num()), int(c->args[2].num()), int(c->args[3].num())};
    face = c->args.size() >= 5 ? faceIndex(c->args[4].src) : 0;
    return true;
}

Viewport drawView(App& a) {
    Viewport vp;
    model::Element* v = a.viewEl();
    std::string title = v ? (v->kind() == "SC_NODE" ? "Node " : "Zoom ") + v->name() : std::string("View");
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    bool node = v && v->kind() == "SC_NODE";
    if (node) flags |= ImGuiWindowFlags_NoBackground;
    ImGui::Begin((title + "###view").c_str(), nullptr, flags);
    if (!v) {
        ImGui::TextDisabled("Open a node or zoom from the outline.");
    } else {
        ImGui::Checkbox("Hotspots", &a.showHotspots);
        ImGui::SameLine();
        if (ImGui::Button(a.createMode ? "Drawing... (Esc)" : "New hotspot")) a.createMode = !a.createMode;
        if (a.createMode && ImGui::IsKeyPressed(ImGuiKey_Escape)) a.createMode = false;
        ImGui::SameLine();
        ImGui::TextDisabled(node ? "drag: look   wheel: zoom   double-click: follow" : "double-click: follow");
        if (node) vp = nodeView(a, *v);
        else zoomView(a, *v);
    }
    ImGui::End();
    return vp;
}

namespace {

// A graph laid out in columns: each column one step further from the roots
// (over links between main boxes); a child box (a zoom) sits indented under
// the box that opens it. Boxes the roots don't reach come last.
struct GraphBox {
    std::string name;
    bool child = false;
    ImU32 fill = IM_COL32(30, 70, 45, 255);
    bool reached = true;  // set by layoutGraph for main boxes
    ImVec2 b0, b1;        // set by layoutGraph
};
struct GraphLink {
    size_t from, to;
};

ImVec2 layoutGraph(std::vector<GraphBox>& boxes, const std::vector<GraphLink>& links, const std::vector<size_t>& roots,
                   ImVec2 origin) {
    std::vector<int> depth(boxes.size(), -1), parent(boxes.size(), -1);
    std::vector<size_t> queue;
    for (size_t r : roots)
        if (r < boxes.size() && depth[r] < 0) depth[r] = 0, queue.push_back(r);
    int maxDepth = 0;
    for (size_t q = 0; q < queue.size(); ++q)
        for (auto& l : links)
            if (l.from == queue[q] && !boxes[l.to].child && depth[l.to] < 0) {
                depth[l.to] = depth[queue[q]] + 1;
                maxDepth = std::max(maxDepth, depth[l.to]);
                queue.push_back(l.to);
            }
    bool unreached = false;
    for (size_t i = 0; i < boxes.size(); ++i) {
        boxes[i].reached = boxes[i].child || depth[i] >= 0;
        if (!boxes[i].child && depth[i] < 0) depth[i] = maxDepth + 1, unreached = true;
    }
    for (auto& l : links)
        if (boxes[l.to].child && parent[l.to] < 0 && l.from != l.to) parent[l.to] = int(l.from);
    const int lastCol = maxDepth + (unreached ? 2 : 1);
    std::vector<std::vector<std::pair<size_t, int>>> cols(size_t(lastCol) + 1);  // box, indent
    std::vector<bool> placed(boxes.size(), false);
    auto group = [&](auto& self, size_t b, int indent, std::vector<std::pair<size_t, int>>& out) -> void {
        if (placed[b]) return;
        placed[b] = true;
        out.push_back({b, indent});
        for (size_t z = 0; z < boxes.size(); ++z)
            if (boxes[z].child && parent[z] == int(b)) self(self, z, indent + 1, out);
    };
    for (int d = 0; d < lastCol; ++d)
        for (size_t i = 0; i < boxes.size(); ++i)
            if (!boxes[i].child && depth[i] == d) group(group, i, 0, cols[size_t(d)]);
    for (size_t i = 0; i < boxes.size(); ++i)
        if (!placed[i]) group(group, i, 0, cols[size_t(lastCol)]);
    const float rowH = ImGui::GetTextLineHeight() + 12, indentW = 18, gapX = 56;
    float x = origin.x + 8, bottom = origin.y;
    for (auto& col : cols) {
        if (col.empty()) continue;
        float y = origin.y + 8, colW = 0;
        for (auto& [b, indent] : col) {
            ImVec2 ts = ImGui::CalcTextSize(boxes[b].name.c_str());
            boxes[b].b0 = {x + indent * indentW, y};
            boxes[b].b1 = {boxes[b].b0.x + ts.x + 12, y + ts.y + 6};
            colW = std::max(colW, boxes[b].b1.x - x);
            y += rowH;
        }
        bottom = std::max(bottom, y);
        x += colW + gapX;
    }
    return {x - origin.x, bottom - origin.y + 8};
}

// Draws the laid-out graph; returns the box clicked (double: double-clicked), or -1.
int drawGraph(const std::vector<GraphBox>& boxes, const std::vector<GraphLink>& links, int selected,
              const std::vector<std::string>& tips, bool& doubleClicked) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    for (auto& l : links) {
        const GraphBox &f = boxes[l.from], &t = boxes[l.to];
        ImU32 col = t.child ? IM_COL32(200, 120, 255, 110) : IM_COL32(80, 220, 120, 150);
        if (l.from == l.to) continue;
        if (t.child && t.b0.x > f.b0.x && t.b0.y > f.b0.y && t.b0.x - f.b0.x < 200) {  // under its parent
            ImVec2 p0{f.b0.x + 6, f.b1.y}, p1{t.b0.x, (t.b0.y + t.b1.y) / 2};
            dl->AddLine(p0, {p0.x, p1.y}, col, 1.2f);
            dl->AddLine({p0.x, p1.y}, p1, col, 1.2f);
            continue;
        }
        bool forward = t.b0.x > f.b1.x;
        ImVec2 p0{forward ? f.b1.x : f.b0.x, (f.b0.y + f.b1.y) / 2};
        ImVec2 p1{forward ? t.b0.x : t.b1.x, (t.b0.y + t.b1.y) / 2};
        float bend = std::max(30.0f, std::fabs(p1.x - p0.x) * 0.4f) * (forward ? 1 : -1);
        if (!forward && std::fabs(p0.x - p1.x) < 1) p0.x = f.b1.x, p1.x = t.b1.x, bend = -40;
        dl->AddBezierCubic(p0, {p0.x + bend, p0.y}, {p1.x - bend, p1.y}, p1, col, 1.3f);
        dl->AddCircleFilled(p1, 2.5f, col);
    }
    int clicked = -1;
    doubleClicked = false;
    for (size_t i = 0; i < boxes.size(); ++i) {
        const GraphBox& b = boxes[i];
        bool open = int(i) == selected;
        float round = b.child ? 0.0f : 6.0f;
        dl->AddRectFilled(b.b0, b.b1, b.fill, round);
        dl->AddRect(b.b0, b.b1, open ? IM_COL32(255, 230, 80, 255) : b.reached ? IM_COL32(200, 200, 200, 255) : IM_COL32(255, 80, 80, 255),
                    round, 0, open ? 2.5f : 1);
        dl->AddText({b.b0.x + 6, b.b0.y + 3}, IM_COL32_WHITE, b.name.c_str());
        ImGui::SetCursorScreenPos(b.b0);
        ImGui::PushID(int(i));
        if (ImGui::InvisibleButton("box", {b.b1.x - b.b0.x, b.b1.y - b.b0.y})) clicked = int(i);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) clicked = int(i), doubleClicked = true;
        if (ImGui::IsItemHovered() && i < tips.size() && !tips[i].empty()) ImGui::SetTooltip("%s", tips[i].c_str());
        ImGui::PopID();
    }
    return clicked;
}

// The whole game: rooms, linked by hotspots that change room (SC_ROOM, or a
// handler that calls scSwitch(SC_ROOM)), from the room the new-game handlers enter.
void worldMap(App& a) {
    const auto& moves = a.p.moves();
    std::vector<GraphBox> boxes;
    std::map<std::string, size_t> index;
    for (auto& r : a.p.rooms) {
        index[r.e->name()] = boxes.size();
        boxes.emplace_back().name = r.e->name();
    }
    std::vector<GraphLink> links;
    std::vector<std::string> tips(boxes.size());
    std::set<std::pair<size_t, size_t>> seen;
    auto link = [&](size_t from, const std::string& to, const std::string& how) {
        auto t = index.find(to);
        if (t == index.end()) return;
        tips[from] += "\n" + how + " -> " + to;
        if (t->second != from && seen.insert({from, t->second}).second) links.push_back({from, t->second});
    };
    auto action = [&](size_t from, const std::string& where, const doc::Call& c, size_t k) {
        if (c.args.size() < k + 2) return;
        std::string kind = c.args[k].src, target = a.p.resolve(c.args[k + 1]);
        if (kind == "SC_ROOM") link(from, target, where);
        if (kind == "SC_CUSTOM")
            if (auto m = moves.find(target); m != moves.end())
                for (const auto& mv : m->second)
                    if (mv.kind == "SC_ROOM") link(from, mv.target, where + " (" + target + ")");
    };
    for (size_t ri = 0; ri < a.p.rooms.size(); ++ri) {
        model::Element& room = *a.p.rooms[ri].e;
        tips[ri] = "room " + room.name() + ", built by " + a.p.rooms[ri].path.builder + "()";
        for (auto& v : room.children) {
            if (v.kind() != "SC_NODE" && v.kind() != "SC_ZOOM") continue;
            for (auto& o : v.children) {
                if (o.kind() != "SC_OBJECT") continue;
                std::string where = v.name() + "/" + o.name();
                if (auto* c = o.find("scOnClick")) action(ri, where, *c, 0);
                for (auto* r : o.findAll("scReaction")) action(ri, where + " <- " + a.p.resolve(r->args[0]), *r, 1);
            }
        }
    }
    // Where a new game starts: the rooms the menu's new-game handlers switch to.
    std::vector<size_t> roots;
    for (const auto& it : a.p.doc.items)
        if (it.kind == doc::Item::Kind::Call && it.call && it.call->fn == "scMenuSet")
            for (size_t k = 7; k < it.call->args.size(); ++k)
                if (auto m = moves.find(a.p.resolve(it.call->args[k])); m != moves.end())
                    for (const auto& mv : m->second)
                        if (auto i = index.find(mv.target); mv.kind == "SC_ROOM" && i != index.end()) roots.push_back(i->second);
    if (roots.empty() && !boxes.empty()) roots.push_back(0);
    ImGui::TextDisabled("columns: steps from where a new game starts (%s)   double-click: open the room   red: no "
                        "hotspot leads there (a handler, a cutscene or another game may)",
                        roots.empty() ? "-" : boxes[roots[0]].name.c_str());
    ImVec2 r0 = ImGui::GetCursorScreenPos();
    ImVec2 extent = layoutGraph(boxes, links, roots, r0);
    int selected = -1;
    for (size_t i = 0; i < a.p.rooms.size(); ++i)
        if (a.p.rooms[i].path == a.room) selected = int(i);
    bool dbl;
    int hit = drawGraph(boxes, links, selected, tips, dbl);
    if (hit >= 0) {
        a.select(a.p.rooms[size_t(hit)].path);
        if (dbl) a.worldMap = false;
    }
    ImGui::SetCursorScreenPos(r0);
    ImGui::Dummy(extent);
}

// One room: its nodes and zooms by what leads where.
void roomMap(App& a, model::Element& room) {
    std::vector<GraphBox> boxes;
    std::vector<size_t> child;  // room->children index of each box
    std::map<std::string, size_t> byName;
    for (size_t i = 0; i < room.children.size(); ++i) {
        auto& c = room.children[i];
        if (c.kind() != "SC_NODE" && c.kind() != "SC_ZOOM") continue;
        bool zoom = c.kind() == "SC_ZOOM";
        byName[c.kind() + c.name()] = boxes.size();
        GraphBox& box = boxes.emplace_back();
        box.name = c.name();
        box.child = zoom;
        if (zoom) box.fill = IM_COL32(70, 40, 90, 255);
        child.push_back(i);
    }
    std::vector<GraphLink> links;
    std::vector<std::string> exits, tips(boxes.size());
    for (size_t b = 0; b < boxes.size(); ++b) {
        tips[b] = std::string(boxes[b].child ? "zoom " : "node ") + boxes[b].name;
        for (auto& o : room.children[child[b]].children) {
            if (o.kind() != "SC_OBJECT") continue;
            auto* c = o.find("scOnClick");
            if (!c || c->args.size() < 2) continue;
            std::string k = c->args[0].src, t = a.p.resolve(c->args[1]);
            auto to = [&](const std::string& kind, const std::string& target, const std::string& how) {
                auto it = byName.find(kind + target);
                if (it == byName.end() || it->second == b) return;
                links.push_back({b, it->second});
                tips[b] += "\n" + o.name() + how + " -> " + target;
            };
            if (k == "SC_NODE" || k == "SC_ZOOM") to(k, t, "");
            if (k == "SC_ROOM") exits.push_back(boxes[b].name + "/" + o.name() + " -> " + t);
            // Steps, ladders and doors that animate first: a handler that calls scSwitch.
            if (k == "SC_CUSTOM")
                if (auto m = a.p.moves().find(t); m != a.p.moves().end())
                    for (const auto& mv : m->second) {
                        if (mv.kind == "SC_ROOM") exits.push_back(boxes[b].name + "/" + o.name() + " (" + t + ") -> " + mv.target);
                        else to(mv.kind, mv.target, " (" + t + ")");
                    }
        }
    }
    std::vector<size_t> roots;
    for (size_t i = 0; i < boxes.size(); ++i)
        if (!boxes[i].child) {
            roots.push_back(i);
            break;
        }
    ImGui::TextDisabled("columns: steps from %s   green: nodes   purple: zooms   red: a node nothing here leads to",
                        roots.empty() ? "-" : boxes[roots[0]].name.c_str());
    for (auto& e : exits) ImGui::TextDisabled("exit %s", e.c_str());
    ImVec2 r0 = ImGui::GetCursorScreenPos();
    ImVec2 extent = layoutGraph(boxes, links, roots, r0);
    for (size_t i = 0; i < boxes.size(); ++i)
        if (!boxes[i].reached)
            tips[i] += "\nno hotspot in this room leads here, nor a handler one runs (other code may: scSwitch)";
    int selected = -1;
    for (size_t i = 0; i < boxes.size(); ++i) {
        ElementPath vp = a.room;
        vp.index.push_back(int(child[i]));
        if (vp == a.view) selected = int(i);
    }
    bool dbl;
    int hit = drawGraph(boxes, links, selected, tips, dbl);
    if (hit >= 0) {
        ElementPath vp = a.room;
        vp.index.push_back(int(child[size_t(hit)]));
        a.select(a.room, vp);
        a.center = App::Center::View;
    }
    ImGui::SetCursorScreenPos(r0);
    ImGui::Dummy(extent);
}

}  // namespace

void drawMap(App& a) {
    ImGui::Begin("Map###view", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                            ImGuiWindowFlags_HorizontalScrollbar);
    model::Element* room = a.roomEl();
    if (ImGui::RadioButton("whole game", a.worldMap)) a.worldMap = true;
    ImGui::SameLine();
    if (ImGui::RadioButton(room ? ("room " + room->name()).c_str() : "room", !a.worldMap)) a.worldMap = false;
    if (a.worldMap) worldMap(a);
    else if (room) roomMap(a, *room);
    else ImGui::TextDisabled("Open a room from the outline, or look at the whole game.");
    ImGui::End();
}

}  // namespace editor
