// The editor's panels: the outline (world, items, dialogue, text, code,
// assets), the inspector of the selected element, the problems list, the
// handler editor and the asset preview.

#include <algorithm>
#include <cstring>
#include <map>

#include "ui.hpp"

namespace editor {

namespace {

int resizeCallback(ImGuiInputTextCallbackData* data) {
    if (data->EventFlag == ImGuiInputTextFlags_CallbackResize) {
        auto* s = static_cast<std::string*>(data->UserData);
        s->resize(size_t(data->BufTextLen));
        data->Buf = s->data();
    }
    return 0;
}

bool inputString(const char* label, std::string& s, ImGuiInputTextFlags flags = 0, ImVec2 size = {}, bool multiline = false) {
    flags |= ImGuiInputTextFlags_CallbackResize;
    if (multiline) return ImGui::InputTextMultiline(label, s.data(), s.capacity() + 1, size, flags, resizeCallback, &s);
    return ImGui::InputText(label, s.data(), s.capacity() + 1, flags, resizeCallback, &s);
}

// A text field showing `value`; true (with `out` set) when an edit was finished.
// The text being typed is kept here until then, so nothing is committed per key.
bool editString(const char* label, const std::string& value, std::string& out, bool multiline = false,
                ImVec2 size = {}) {
    static std::map<ImGuiID, std::string> buffers;
    ImGuiID id = ImGui::GetID(label);
    auto it = buffers.find(id);
    if (it == buffers.end()) it = buffers.emplace(id, value).first;
    inputString(label, it->second, 0, size, multiline);
    bool done = ImGui::IsItemDeactivatedAfterEdit();
    if (done) out = it->second;
    if (!ImGui::IsItemActive()) buffers.erase(id);
    return done && out != value;
}

// An argument written as Lua source ("\"n2\"", "msg0123", "SC_FRONT", "765").
std::optional<doc::Expr> parseExpr(const std::string& src) {
    doc::Document d;
    if (!d.parse("f(" + src + ")") || d.items.size() != 1 || !d.items[0].call || d.items[0].call->args.size() != 1)
        return std::nullopt;
    return d.items[0].call->args[0];
}

const char* const kCursors[] = {"SC_FORWARD", "SC_LOOK", "SC_USE", "SC_ZOOMOUT", "SC_NORMAL",
                                "SC_LEFT", "SC_RIGHT", "SC_UP", "SC_DOWN"};
const char* const kActions[] = {"SC_NODE", "SC_ROOM", "SC_ZOOM", "SC_ZOOMOUT", "SC_ZOOMBACK",
                                "SC_CUSTOM", "SC_FEEDBACK", "SC_FEEDBACK_EX"};

// Labels for the arguments of the builder calls, by function.
const std::map<std::string, std::vector<const char*>>& signatures() {
    static const std::map<std::string, std::vector<const char*>> s = {
        {"scCreate", {"kind", "name"}},
        {"scPlace", {"x", "y", "width", "height", "face"}},
        {"scTexture", {"face", "image"}},
        {"scOnHover", {"cursor"}},
        {"scOnClick", {"action", "target", "voice"}},
        {"scReaction", {"item", "action", "target", "voice"}},
        {"scInventoryReaction", {"item", "action", "target", "voice"}},
        {"scFootstep", {"sound"}},
        {"scAudio", {"when", "sound"}},
        {"scInventorySet", {"icon", "label", "fail text", "fail voice"}},
        {"scScriptAdd", {"voice", "text", "hint only"}},
        {"scChoiceAdd", {"text", "handler"}},
        {"scTrackSet", {"file", "plays"}},
        {"scTrackSet3D", {"front", "rear left", "rear right", "plays"}},
        {"scTrackSetEx", {"option", "value"}},
        {"scSetLastVisited", {"node"}},
        {"scOnEnter", {"handler"}},
        {"scOnExit", {"handler"}},
        {"scDisable", {"what"}},
        {"scJournalSet", {"image", "font", "open sound", "turn sound", "close sound"}},
        {"scOverlay", {"image", "enter sound", "leave sound"}},
    };
    return s;
}

// A combo over `options` for an argument spelled as a name; true when changed.
bool nameCombo(const char* label, doc::Expr& e, const char* const* options, size_t n) {
    bool changed = false;
    if (ImGui::BeginCombo(label, e.src.c_str())) {
        for (size_t i = 0; i < n; ++i)
            if (ImGui::Selectable(options[i], e.src == options[i])) e = doc::Expr::name(options[i]), changed = true;
        ImGui::EndCombo();
    }
    return changed;
}

bool stringCombo(const char* label, doc::Expr& e, const App& a, const std::vector<std::string>& options) {
    bool changed = false;
    std::string cur = a.p.resolve(e);
    if (ImGui::BeginCombo(label, cur.c_str())) {
        for (const auto& o : options)
            if (ImGui::Selectable(o.c_str(), o == cur)) e = doc::Expr::string(o), changed = true;
        ImGui::EndCombo();
    }
    return changed;
}

// Accepts an asset dragged from the assets list onto the last item.
bool assetDrop(doc::Expr& e) {
    bool changed = false;
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("ASSET")) {
            e = doc::Expr::string(std::string(static_cast<const char*>(pl->Data), size_t(pl->DataSize)));
            changed = true;
        }
        ImGui::EndDragDropTarget();
    }
    return changed;
}

// One argument as Lua source, plus the text it stands for when it names a text global.
bool argField(App& a, const char* label, doc::Expr& e) {
    bool changed = false;
    std::string out;
    ImGui::SetNextItemWidth(-90);
    if (editString(label, e.src, out)) {
        if (auto x = parseExpr(out)) e = *x, changed = true;
        else a.status = "not a Lua value: " + out;
    }
    changed |= assetDrop(e);
    if (e.kind == doc::Expr::Kind::Name) {
        auto it = a.p.texts.find(e.src);
        if (it != a.p.texts.end()) ImGui::TextWrapped("  \"%s\"", it->second.c_str());
    }
    return changed;
}

// A popup listing the text table; true (with `e` naming the global) when one was picked.
bool pickText(App& a, doc::Expr& e) {
    bool picked = false;
    if (ImGui::SmallButton("pick text")) ImGui::OpenPopup("picktext");
    if (ImGui::BeginPopup("picktext")) {
        static std::string filter;
        ImGui::SetNextItemWidth(400);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        inputString("##f", filter);
        ImGui::BeginChild("texts", {400, 300});
        std::vector<const std::pair<const std::string, std::string>*> rows;
        for (auto& kv : a.p.texts)
            if (filter.empty() || kv.first.find(filter) != std::string::npos || kv.second.find(filter) != std::string::npos)
                rows.push_back(&kv);
        ImGuiListClipper clip;
        clip.Begin(int(rows.size()));
        while (clip.Step())
            for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i)
                if (ImGui::Selectable((rows[size_t(i)]->first + "  " + rows[size_t(i)]->second).c_str())) {
                    e = doc::Expr::name(rows[size_t(i)]->first);
                    picked = true;
                    ImGui::CloseCurrentPopup();
                }
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    return picked;
}

// A text argument (feedback, a label, a dialogue line): typed as plain text.
// A literal is edited in place; a text-table global is edited in the table
// (where other calls may share it); anything else is shown as Lua.
bool textArg(App& a, const char* label, doc::Expr& e) {
    std::string out;
    if (e.kind == doc::Expr::Kind::String) {
        ImGui::SetNextItemWidth(-90);
        bool changed = editString(label, e.str, out);
        if (changed) e = doc::Expr::string(out);
        changed |= assetDrop(e);
        ImGui::PushID(label);
        if (!e.str.empty() && ImGui::SmallButton("to text table")) {
            // Queued before the caller's own edit, so the global exists when the call names it.
            std::string name = a.p.nextTextName(), text = e.str;
            a.later([&a, name, text] {
                if (!a.p.addText(name, text)) a.status = a.p.error;
            });
            e = doc::Expr::name(name);
            changed = true;
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("keep this text with the others at the top of the script");
        ImGui::SameLine();
        changed |= pickText(a, e);
        ImGui::PopID();
        return changed;
    }
    auto it = e.kind == doc::Expr::Kind::Name ? a.p.texts.find(e.src) : a.p.texts.end();
    if (it == a.p.texts.end()) return argField(a, label, e);
    ImGui::SetNextItemWidth(-90);
    if (editString(label, it->second, out))
        a.later([&a, name = e.src, out] {
            if (!a.p.replaceItem(name, doc::Item::Kind::Assign, name + " = " + doc::quote(out))) a.status = a.p.error;
        });
    ImGui::TextDisabled("  %s in the text table", e.src.c_str());
    ImGui::SameLine();
    ImGui::PushID(label);
    bool picked = pickText(a, e);
    ImGui::SameLine();
    if (ImGui::SmallButton("own text")) e = doc::Expr::string(it->second), picked = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("a copy of the text for this call alone");
    ImGui::PopID();
    return picked;
}

// Which arguments of a call are text (0-based).
bool isTextArg(const std::string& fn, size_t k) {
    return (fn == "scInventorySet" && (k == 1 || k == 2)) || (fn == "scScriptAdd" && k == 1) ||
           (fn == "scChoiceAdd" && k == 0);
}

// The handler, target and text of an action (scOnClick, scReaction, ...), from argument `k`.
bool actionEditor(App& a, doc::Call& c, size_t k) {
    bool changed = false;
    while (c.args.size() < k + 2) c.args.push_back(doc::Expr::string(""));
    doc::Expr& kind = c.args[k];
    ImGui::SetNextItemWidth(-90);
    if (nameCombo("action", kind, kActions, std::size(kActions))) {
        c.args.resize(k + 2);
        c.args[k + 1] = doc::Expr::string("");
        if (kind.src == "SC_FEEDBACK_EX") c.args.push_back(doc::Expr::string(""));
        changed = true;
    }
    doc::Expr& target = c.args[k + 1];
    model::Element* room = a.roomEl();
    ImGui::SetNextItemWidth(-90);
    if (kind.src == "SC_NODE" && room) {
        changed |= stringCombo("node", target, a, names(room, "SC_NODE"));
    } else if (kind.src == "SC_ZOOM" && room) {
        changed |= stringCombo("zoom", target, a, names(room, "SC_ZOOM"));
    } else if (kind.src == "SC_ROOM") {
        std::vector<std::string> rooms;
        for (auto& r : a.p.rooms) rooms.push_back(r.e->name());
        changed |= stringCombo("room", target, a, rooms);
    } else if (kind.src == "SC_CUSTOM") {
        changed |= stringCombo("handler", target, a, a.p.handlers);
        std::string h = a.p.resolve(target);
        bool exists = std::find(a.p.handlers.begin(), a.p.handlers.end(), h) != a.p.handlers.end();
        if (!h.empty() && exists && ImGui::SmallButton("open handler")) {
            a.handler = h;
            a.center = App::Center::Handler;
        }
        if (!h.empty() && !exists && ImGui::SmallButton(("create " + h + "()").c_str())) {
            a.later([&a, h] {
                if (!a.p.insertBeforeBoot("function " + h + "()\n  scFeedback(\"...\")\nend")) a.status = a.p.error;
            });
            a.handler = h;
            a.center = App::Center::Handler;
        }
        // Where it leads, read from its code (scSwitch), as the maps do.
        if (auto m = a.p.moves().find(h); m != a.p.moves().end())
            for (const auto& mv : m->second) {
                if (mv.kind == "SC_NODE") ImGui::TextDisabled("  goes to node %s", mv.target.c_str());
                else if (mv.kind == "SC_ZOOM") ImGui::TextDisabled("  opens zoom %s", mv.target.c_str());
                else if (!mv.node.empty()) ImGui::TextDisabled("  goes to room %s, node %s", mv.target.c_str(), mv.node.c_str());
                else {
                    // No node named: the engine enters the room at the node the player
                    // was last in there, the first time at its first node.
                    std::string first;
                    for (auto& r : a.p.rooms)
                        if (r.e->name() == mv.target)
                            if (auto n = names(r.e, "SC_NODE"); !n.empty()) first = n[0];
                    ImGui::TextDisabled("  goes to room %s, at its last visited node", mv.target.c_str());
                    ImGui::TextDisabled("  (the first time: %s)", first.empty() ? "?" : first.c_str());
                }
            }
        changed |= argField(a, "name##handler", target);
    } else if (kind.src == "SC_FEEDBACK" || kind.src == "SC_FEEDBACK_EX") {
        changed |= textArg(a, "text", target);
        if (kind.src == "SC_FEEDBACK_EX") {
            while (c.args.size() < k + 3) c.args.push_back(doc::Expr::string(""));
            changed |= argField(a, "voice", c.args[k + 2]);
        }
    }
    return changed;
}

// Every call configuring an element, with an editor for each argument.
bool callsEditor(App& a, std::vector<doc::Call>& calls) {
    bool changed = false;
    int remove = -1;
    for (size_t i = 0; i < calls.size(); ++i) {
        doc::Call& c = calls[i];
        ImGui::PushID(int(i));
        bool open = ImGui::TreeNodeEx("call", ImGuiTreeNodeFlags_DefaultOpen, "%s", c.fn.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 10);
        if (ImGui::SmallButton("x")) remove = int(i);
        if (open) {
            auto sig = signatures().find(c.fn);
            if (c.fn == "scOnClick") {
                changed |= actionEditor(a, c, 0);
            } else if (c.fn == "scReaction" || c.fn == "scInventoryReaction") {
                std::vector<std::string> items;
                for (auto& it : a.p.items) items.push_back(it.e->name());
                ImGui::SetNextItemWidth(-90);
                if (!c.args.empty()) changed |= stringCombo("item", c.args[0], a, items);
                changed |= actionEditor(a, c, 1);
            } else if (c.fn == "scOnHover" && !c.args.empty()) {
                ImGui::SetNextItemWidth(-90);
                changed |= nameCombo("cursor", c.args[0], kCursors, std::size(kCursors));
            } else {
                for (size_t k = 0; k < c.args.size(); ++k) {
                    std::string label = sig != signatures().end() && k < sig->second.size() ? sig->second[k] : "arg " + std::to_string(k + 1);
                    ImGui::PushID(int(k));
                    bool faceArg = (c.fn == "scPlace" && k == 4) || (c.fn == "scTexture" && k == 0);
                    if (faceArg && c.args[k].src != "SC_ZOOM") {
                        ImGui::SetNextItemWidth(-90);
                        changed |= nameCombo(label.c_str(), c.args[k], kFaces, 6);
                    } else if (isTextArg(c.fn, k)) {
                        changed |= textArg(a, label.c_str(), c.args[k]);
                    } else {
                        changed |= argField(a, label.c_str(), c.args[k]);
                    }
                    ImGui::PopID();
                }
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (remove >= 0) calls.erase(calls.begin() + remove), changed = true;
    return changed;
}

// The calls an element of each kind usually has, for the "add" menu.
std::vector<doc::Call> callTemplates(const std::string& kind) {
    using doc::Expr;
    auto s = [](const char* v) { return Expr::string(v); };
    auto n = [](const char* v) { return Expr::name(v); };
    if (kind == "SC_OBJECT")
        return {{"scReaction", {s("item"), n("SC_FEEDBACK"), s("...")}}, {"scDisable", {n("SC_OBJECT")}},
                {"scOnHover", {n("SC_LOOK")}}, {"scOnClick", {n("SC_FEEDBACK"), s("...")}}};
    if (kind == "SC_NODE") return {{"scFootstep", {s("fx-foot.ogg")}}, {"scTexture", {n("SC_FRONT"), s("")}}};
    if (kind == "SC_ZOOM") return {{"scAudio", {n("SC_ZOOM"), s("")}}, {"scAudio", {n("SC_ZOOMOUT"), s("")}}};
    if (kind == "SC_ROOM") return {{"scSetLastVisited", {s("n1")}}, {"scOnEnter", {s("enterRoom")}}};
    if (kind == "SC_ITEM") return {{"scInventoryReaction", {s("item"), n("SC_FEEDBACK"), s("...")}}};
    if (kind == "SC_SCRIPT") return {{"scScriptAdd", {s("voice.ogg"), s("..."), Expr::number(0)}}};
    if (kind == "SC_CHOICE") return {{"scChoiceAdd", {s("..."), s("handler")}}};
    if (kind == "SC_TRACK") return {{"scTrackSetEx", {n("SC_VOLUME"), Expr::number(10)}}};
    return {};
}

void elementButtons(App& a, const ElementPath& path, model::Element& e) {
    // Add a call: the scoped kinds take theirs after scBegin (a node's textures), the rest as props.
    if (ImGui::Button("+ call")) ImGui::OpenPopup("addcall");
    if (ImGui::BeginPopup("addcall")) {
        for (auto& t : callTemplates(e.kind()))
            if (ImGui::Selectable((t.fn + " " + (t.args.empty() ? "" : t.args[0].src)).c_str())) {
                a.edit(path, [t](model::Element& el) {
                    bool inside = el.scoped && (t.fn == "scTexture" || t.fn == "scFootstep" || t.fn == "scAudio" ||
                                                t.fn == "scSetLastVisited");
                    if (!inside) {
                        el.props.push_back(t);
                        return;
                    }
                    // Loose calls go before the first child element.
                    auto at = std::find_if(el.children.begin(), el.children.end(), [](auto& c) { return c.isCreate(); });
                    model::Element call;
                    call.head = t;
                    el.children.insert(at, std::move(call));
                });
            }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Duplicate")) {
        ElementPath parent = path;
        int i = parent.index.back();
        parent.index.pop_back();
        model::Element copy = e;
        copy.head.args[1] = doc::Expr::string(e.name() + "-copy");
        if (parent.index.empty()) {
            a.later([&a, path, i, copy] {
                if (auto* b = a.p.builder(path.builder)) {
                    b->dirty = true;
                    b->body.insert(b->body.begin() + i + 1, copy);
                    if (!a.p.commit()) a.status = a.p.error;
                }
            });
        } else {
            a.edit(parent, [i, copy](model::Element& p) { p.children.insert(p.children.begin() + i + 1, copy); });
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete")) ImGui::OpenPopup("delete");
    if (ImGui::BeginPopup("delete")) {
        ImGui::Text("Delete %s %s?", e.kind().c_str() + 3, e.name().c_str());
        if (ImGui::Button("Delete")) {
            ElementPath parent = path;
            int i = parent.index.back();
            parent.index.pop_back();
            if (parent.index.empty()) {
                a.later([&a, path, i] {
                    if (auto* b = a.p.builder(path.builder)) {
                        b->dirty = true;
                        b->body.erase(b->body.begin() + i);
                        if (!a.p.commit()) a.status = a.p.error;
                    }
                });
            } else {
                a.edit(parent, [i](model::Element& p) { p.children.erase(p.children.begin() + i); });
            }
            if (a.object == path) a.object = {};
            if (a.view == path) a.view = {}, a.object = {};
            if (a.room == path) a.room = {}, a.view = {}, a.object = {};
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void inspectElement(App& a, const ElementPath& path) {
    model::Element* e = a.p.find(path);
    if (!e) return;
    ImGui::PushID(e);
    ImGui::SeparatorText((e->kind().substr(3) + "  " + e->name()).c_str());
    std::string name;
    ImGui::SetNextItemWidth(-90);
    if (editString("name", e->name(), name))
        a.edit(path, [name](model::Element& el) { el.head.args[1] = doc::Expr::string(name); });
    if (e->kind() == "SC_OBJECT") {
        Rect r;
        int face;
        if (objectRect(*e, r, face) && face && (r.x < 0 || r.y < 0 || r.x + r.w > 1024 || r.y + r.h > 1024))
            ImGui::TextColored({1, 0.7f, 0.3f, 1}, "extends past the face edge: the engine clips it");
        bool disabled = e->find("scDisable", "SC_OBJECT") != nullptr;
        if (ImGui::Checkbox("disabled at start (scDisable)", &disabled))
            a.edit(path, [disabled](model::Element& el) {
                if (disabled) el.props.push_back({"scDisable", {doc::Expr::name("SC_OBJECT")}});
                else el.props.erase(std::remove_if(el.props.begin(), el.props.end(), [](auto& c) {
                                        return c.fn == "scDisable" && !c.args.empty() && c.args[0].src == "SC_OBJECT";
                                    }), el.props.end());
            });
    }
    // Edit a copy, so a change commits once through edit().
    model::Element copy = *e;
    bool changed = callsEditor(a, copy.props);
    std::vector<doc::Call> loose;
    std::vector<size_t> where;
    for (size_t i = 0; i < copy.children.size(); ++i)
        if (!copy.children[i].isCreate()) loose.push_back(copy.children[i].head), where.push_back(i);
    if (!loose.empty()) {
        size_t before = loose.size();
        ImGui::PushID("loose");
        bool c2 = callsEditor(a, loose);
        ImGui::PopID();
        if (c2) {
            changed = true;
            if (loose.size() == before) {
                for (size_t i = 0; i < where.size(); ++i) copy.children[where[i]].head = loose[i];
            } else {  // one was removed: find which
                size_t k = 0;
                std::vector<model::Element> kids;
                for (size_t i = 0; i < copy.children.size(); ++i) {
                    if (copy.children[i].isCreate()) kids.push_back(copy.children[i]);
                    else if (k < loose.size() && copy.children[i].head.fn == loose[k].fn) {
                        kids.push_back(copy.children[i]);
                        kids.back().head = loose[k++];
                    }
                }
                copy.children = std::move(kids);
            }
        }
    }
    if (changed) a.edit(path, [copy](model::Element& el) { el = copy; });
    if (model::Element* now = a.p.find(path)) elementButtons(a, path, *now);
    ImGui::PopID();
}

// A new node (six empty faces) or zoom at the end of a room, opened in the view.
void addView(App& a, const ElementPath& room, const std::string& kind) {
    model::Element* r = a.p.find(room);
    if (!r) return;
    std::vector<std::string> taken = names(r, kind);
    std::string name;
    for (int k = 1; name.empty() || std::count(taken.begin(), taken.end(), name); ++k)
        name = (kind == "SC_NODE" ? "n" : "zoom") + std::to_string(k);
    model::Element v;
    v.head = {"scCreate", {doc::Expr::name(kind), doc::Expr::string(name)}};
    v.scoped = true;
    v.begin = {"scBegin", {doc::Expr::name(kind)}};
    v.end = {"scEnd", {doc::Expr::name(kind)}};
    auto texture = [&](const char* face) {
        model::Element t;
        t.head = {"scTexture", {doc::Expr::name(face), doc::Expr::string("")}};
        v.children.push_back(t);
    };
    if (kind == "SC_NODE")
        for (const char* f : kFaces) texture(f);
    else
        texture("SC_ZOOM");
    ElementPath path = room;
    path.index.push_back(int(r->children.size()));
    a.edit(room, [v](model::Element& el) { el.children.push_back(v); });
    a.select(room, path);
    a.center = App::Center::View;
}

// Rooms -> nodes and zooms; objects are picked in the view.
void worldTab(App& a) {
    if (ImGui::Button("New room")) ImGui::OpenPopup("newroom");
    if (ImGui::BeginPopup("newroom")) {
        static std::string name = "room";
        inputString("name", name);
        if (ImGui::Button("Create")) {
            std::string fn = "create";
            bool up = true;
            for (char c : name) {
                if (!std::isalnum(static_cast<unsigned char>(c))) {
                    up = true;
                    continue;
                }
                fn += up ? char(std::toupper(static_cast<unsigned char>(c))) : c;
                up = false;
            }
            std::string text = "function " + fn + "()\n  scCreate(SC_ROOM, " + doc::quote(name) +
                               ")\n  scBegin(SC_ROOM)\n\n  scCreate(SC_NODE, \"n1\")\n  scBegin(SC_NODE)\n" +
                               "  scTexture(SC_FRONT, \"\")\n  scTexture(SC_BACK, \"\")\n  scTexture(SC_LEFT, \"\")\n" +
                               "  scTexture(SC_RIGHT, \"\")\n  scTexture(SC_UP, \"\")\n  scTexture(SC_DOWN, \"\")\n" +
                               "  scEnd(SC_NODE)\n\n  scEnd(SC_ROOM)\nend";
            // A builder only counts once the boot code calls it.
            a.later([&a, text, fn, room = name] {
                if (!a.p.insertBeforeBoot(text) || !a.p.insertBootCall(fn + "()")) a.status = "new room: " + a.p.error;
                else a.jumpTo(room);
            });
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::SameLine();
    static std::string filter;
    ImGui::SetNextItemWidth(-1);
    inputString("##filter", filter, ImGuiInputTextFlags_None);
    for (auto& r : a.p.rooms) {
        model::Element& room = *r.e;
        if (!filter.empty() && room.name().find(filter) == std::string::npos) continue;
        bool open = r.path == a.room;
        ImGui::SetNextItemOpen(open, open ? ImGuiCond_Always : ImGuiCond_Once);
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | (open ? ImGuiTreeNodeFlags_Selected : 0);
        bool expanded = ImGui::TreeNodeEx(&room, flags, "%s", room.name().c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen()) a.select(r.path);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("built by %s()", r.path.builder.c_str());
        if (!expanded) continue;
        for (size_t i = 0; i < room.children.size(); ++i) {
            auto& v = room.children[i];
            if (v.kind() != "SC_NODE" && v.kind() != "SC_ZOOM") continue;
            ElementPath vp = r.path;
            vp.index.push_back(int(i));
            std::string label = (v.kind() == "SC_NODE" ? "node  " : "zoom  ") + v.name();
            if (ImGui::Selectable(label.c_str(), vp == a.view)) {
                a.select(r.path, vp);
                a.center = App::Center::View;
            }
        }
        if (ImGui::SmallButton("+ node")) addView(a, r.path, "SC_NODE");
        ImGui::SameLine();
        if (ImGui::SmallButton("+ zoom")) addView(a, r.path, "SC_ZOOM");
        ImGui::TreePop();
    }
}

// Items, dialogues, choices or tracks: a list, the selected one inspected below.
void listTab(App& a, std::vector<Project::Ref>& refs, ElementPath& sel) {
    ImGui::BeginChild("list", {0, ImGui::GetContentRegionAvail().y * 0.4f}, ImGuiChildFlags_Borders);
    for (auto& r : refs) {
        std::string label = r.e->name();
        if (auto* set = r.e->find("scInventorySet"); set && set->args.size() >= 2)
            label += "   " + a.p.resolve(set->args[1]);
        if (ImGui::Selectable(label.c_str(), r.path == sel)) sel = r.path;
    }
    ImGui::EndChild();
    if (!sel.empty()) inspectElement(a, sel);
}

void textTab(App& a) {
    // A new entry: the next free msgNNNN unless another name is typed.
    static std::string newName, newText;
    if (newName.empty()) newName = a.p.nextTextName();
    ImGui::SetNextItemWidth(90);
    inputString("##newname", newName);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-50);
    inputString("##newtext", newText);
    ImGui::SameLine();
    ImGui::BeginDisabled(newName.empty() || a.p.texts.count(newName));
    if (ImGui::Button("Add")) {
        a.later([&a, n = newName, t = newText] {
            if (!a.p.addText(n, t)) a.status = a.p.error;
            else a.status = "added " + n;
        });
        newName.clear(), newText.clear();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled("then use it by name, e.g. as a hotspot's feedback text");
    ImGui::Separator();
    static std::string filter;
    ImGui::SetNextItemWidth(-1);
    inputString("##textfilter", filter);
    ImGui::BeginChild("texts");
    ImGuiListClipper clip;
    std::vector<const std::pair<const std::string, std::string>*> rows;
    for (auto& kv : a.p.texts)
        if (filter.empty() || kv.first.find(filter) != std::string::npos || kv.second.find(filter) != std::string::npos)
            rows.push_back(&kv);
    clip.Begin(int(rows.size()));
    while (clip.Step())
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const auto& [name, text] = *rows[size_t(i)];
            ImGui::PushID(name.c_str());
            ImGui::TextDisabled("%s", name.c_str());
            std::string out;
            ImGui::SetNextItemWidth(-1);
            if (editString("##t", text, out)) {
                std::string n = name;  // the edit re-parses: keep the name
                a.later([&a, n, out] {
                    if (!a.p.replaceItem(n, doc::Item::Kind::Assign, n + " = " + doc::quote(out))) a.status = a.p.error;
                });
            }
            ImGui::PopID();
        }
    ImGui::EndChild();
}

void codeTab(App& a) {
    static std::string filter, newName = "newHandler";
    ImGui::SetNextItemWidth(-60);
    inputString("##newh", newName);
    ImGui::SameLine();
    if (ImGui::Button("New")) {
        a.later([&a, n = newName] {
            if (!a.p.insertBeforeBoot("function " + n + "()\nend")) a.status = a.p.error;
        });
        a.handler = newName;
        a.center = App::Center::Handler;
    }
    ImGui::SetNextItemWidth(-1);
    inputString("##codefilter", filter);
    ImGui::BeginChild("handlers");
    for (auto& h : a.p.handlers) {
        if (!filter.empty() && h.find(filter) == std::string::npos) continue;
        if (ImGui::Selectable(h.c_str(), a.center == App::Center::Handler && a.handler == h)) {
            a.handler = h;
            a.center = App::Center::Handler;
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("builders");
    for (auto& b : a.p.builders) {
        if (!filter.empty() && b.name.find(filter) == std::string::npos) continue;
        if (ImGui::Selectable(b.name.c_str(), a.center == App::Center::Handler && a.handler == b.name)) {
            a.handler = b.name;
            a.center = App::Center::Handler;
        }
    }
    ImGui::EndChild();
}

void assetsTab(App& a) {
    static std::string filter;
    static std::vector<ResArchive::Entry> entries;
    if (entries.empty()) entries = a.p.res.list();
    ImGui::SetNextItemWidth(-1);
    inputString("##assetfilter", filter);
    ImGui::TextDisabled("%zu files; drag one onto a field", entries.size());
    ImGui::BeginChild("assets");
    std::vector<const ResArchive::Entry*> rows;
    for (auto& e : entries)
        if (filter.empty() || e.name.find(filter) != std::string::npos) rows.push_back(&e);
    ImGuiListClipper clip;
    clip.Begin(int(rows.size()));
    while (clip.Step())
        for (int i = clip.DisplayStart; i < clip.DisplayEnd; ++i) {
            const auto& e = *rows[size_t(i)];
            if (ImGui::Selectable(e.name.c_str(), a.center == App::Center::Asset && a.asset == e.name)) {
                a.asset = e.name;
                a.center = App::Center::Asset;
            }
            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("ASSET", e.name.data(), e.name.size());
                ImGui::Text("%s", e.name.c_str());
                ImGui::EndDragDropSource();
            }
        }
    ImGui::EndChild();
}

}  // namespace

std::vector<std::string> names(model::Element* parent, const std::string& kind) {
    std::vector<std::string> out;
    if (parent)
        for (auto* e : parent->elements(kind)) out.push_back(e->name());
    return out;
}

std::string clickSummary(const App& a, const doc::Call* c) {
    if (!c || c->args.empty()) return "no click";
    std::string k = c->args[0].src.substr(std::min<size_t>(3, c->args[0].src.size()));
    std::string t = c->args.size() > 1 ? a.p.resolve(c->args[1]) : std::string();
    if (t.size() > 60) t = t.substr(0, 57) + "...";
    return k + (t.empty() ? "" : ": " + t);
}

void drawOutline(App& a) {
    ImGui::Begin("Outline", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    if (ImGui::BeginTabBar("tabs")) {
        static ElementPath item, script, choice, track;
        if (ImGui::BeginTabItem("World")) worldTab(a), ImGui::EndTabItem();
        if (ImGui::BeginTabItem("Items")) listTab(a, a.p.items, item), ImGui::EndTabItem();
        if (ImGui::BeginTabItem("Talk")) {
            ImGui::TextDisabled("dialogues (scScriptGo)");
            listTab(a, a.p.scripts, script);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Choices")) listTab(a, a.p.choices, choice), ImGui::EndTabItem();
        if (ImGui::BeginTabItem("Music")) listTab(a, a.p.tracks, track), ImGui::EndTabItem();
        if (ImGui::BeginTabItem("Text")) textTab(a), ImGui::EndTabItem();
        if (ImGui::BeginTabItem("Code")) codeTab(a), ImGui::EndTabItem();
        if (ImGui::BeginTabItem("Assets")) assetsTab(a), ImGui::EndTabItem();
        ImGui::EndTabBar();
    }
    ImGui::End();
}

void drawInspector(App& a) {
    ImGui::Begin("Inspector", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    if (!a.object.empty()) inspectElement(a, a.object);
    if (!a.view.empty()) inspectElement(a, a.view);
    if (!a.room.empty()) inspectElement(a, a.room);
    if (a.room.empty()) ImGui::TextDisabled("Select a room, node, zoom or hotspot.");
    ImGui::End();
}

void drawProblems(App& a) {
    ImGui::Begin("Problems", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    if (ImGui::Button("Check (F6)")) a.runCheck();
    ImGui::SameLine();
    if (!a.checked) ImGui::TextDisabled("runs the engine's --check on the script as it is now");
    else if (a.problems.empty()) ImGui::TextColored({0.4f, 1, 0.5f, 1}, "no problems");
    else ImGui::Text("%zu problem%s", a.problems.size(), a.problems.size() == 1 ? "" : "s");
    for (size_t i = 0; i < a.problems.size(); ++i) {
        auto& pr = a.problems[i];
        ImGui::PushID(int(i));
        if (ImGui::Selectable(pr.where.c_str())) a.jumpTo(pr.where);
        ImGui::SameLine(320);
        ImGui::TextUnformatted(pr.what.c_str());
        ImGui::PopID();
    }
    ImGui::End();
}

void drawHandler(App& a) {
    ImGui::Begin("Code###view", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    const doc::Item* it = a.p.doc.function(a.handler);
    if (!it) {
        ImGui::TextDisabled("no function %s", a.handler.c_str());
        ImGui::End();
        return;
    }
    std::string src = a.p.doc.text(it->begin, it->end);
    if (a.codeFor != a.handler) a.codeBuffer = src, a.codeFor = a.handler;
    bool edited = a.codeBuffer != src;
    ImGui::Text("%s()", a.handler.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled(it->builder ? "builder: builds the world at boot" : "handler");
    ImGui::SameLine();
    ImGui::BeginDisabled(!edited);
    bool apply = ImGui::Button("Apply (Ctrl+Enter)") ||
                 (edited && ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyPressed(ImGuiKey_Enter));
    ImGui::SameLine();
    if (ImGui::Button("Revert")) a.codeBuffer = src;
    ImGui::EndDisabled();
    if (apply) {
        std::string name = a.handler;
        a.later([&a, name, code = a.codeBuffer] {
            if (!a.p.replaceItem(name, doc::Item::Kind::Function, code)) a.status = "not applied: " + a.p.error;
            else a.codeFor.clear(), a.status = "applied " + name;
        });
    }
    // Who refers to this function: hotspots, reactions, choices, other code.
    std::vector<std::string> refs;
    for (auto& b : a.p.builders)
        for (auto& c : b.flatten())
            for (auto& arg : c.args)
                if (arg.kind == doc::Expr::Kind::String && arg.str == a.handler && c.fn != "scCreate")
                    refs.push_back(b.name + ": " + model::callSource(c));
    for (auto& i : a.p.doc.items)
        if (i.kind == doc::Item::Kind::Function && i.name != a.handler &&
            a.p.doc.text(i.begin, i.end).find(a.handler) != std::string::npos)
            refs.push_back("code: " + i.name + "()");
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float refsH = refs.empty() ? 0 : std::min(160.0f, 22.0f + 18.0f * float(refs.size()));
    inputString("##code", a.codeBuffer, ImGuiInputTextFlags_AllowTabInput, {avail.x, avail.y - refsH}, true);
    if (!refs.empty()) {
        ImGui::BeginChild("refs");
        ImGui::TextDisabled("used by");
        for (auto& r : refs) ImGui::TextUnformatted(r.c_str());
        ImGui::EndChild();
    }
    ImGui::End();
}

void drawAsset(App& a) {
    ImGui::Begin("Asset###view", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);
    ImGui::Text("%s", a.asset.c_str());
    std::string ext = a.asset.size() > 4 ? a.asset.substr(a.asset.size() - 4) : "";
    if (ext == ".jpg" || ext == ".tga") {
        if (const RendererTexture* t = a.texture(a.asset)) {
            ImGui::TextDisabled("%d x %d", t->width, t->height);
            ImVec2 avail = ImGui::GetContentRegionAvail();
            float s = std::min(avail.x / float(t->width), avail.y / float(t->height));
            ImGui::Image(ImTextureID(intptr_t(t->id)), {t->width * s, t->height * s});
        }
    } else {
        ImGui::TextDisabled("sound or video");
    }
    // Where the script uses it.
    for (auto& b : a.p.builders)
        for (auto& c : b.flatten())
            for (auto& arg : c.args)
                if (arg.kind == doc::Expr::Kind::String && arg.str == a.asset)
                    ImGui::BulletText("%s: %s", b.name.c_str(), model::callSource(c).c_str());
    ImGui::End();
}

}  // namespace editor
