#include "world.hpp"

#include "constants.hpp"

const char* sc::faceName(int face) {
    static const char* names[] = {"?", "front", "back", "left", "right", "up", "down"};
    return face >= 1 && face <= 6 ? names[face] : names[0];
}

std::vector<Object*> View::find(const std::string& objectName) {
    std::vector<Object*> out;
    for (auto& o : objects)
        if (o->name == objectName)
            out.push_back(o.get());
    return out;
}

static View* findView(std::vector<std::unique_ptr<View>>& views, const std::string& n) {
    for (auto& v : views)
        if (v->name == n)
            return v.get();
    return nullptr;
}

View* Room::node(const std::string& n) { return findView(nodes, n); }
View* Room::zoom(const std::string& n) { return findView(zooms, n); }

Room* World::room(const std::string& name) {
    auto it = rooms.find(name);
    return it == rooms.end() ? nullptr : it->second.get();
}

template <typename T>
static T* createNamed(std::map<std::string, std::unique_ptr<T>>& m, const std::string& name) {
    auto& slot = m[name];
    if (!slot) {
        slot = std::make_unique<T>();
        slot->name = name;
    }
    return slot.get();
}

bool World::create(int kind, const std::string& name) {
    switch (kind) {
    case sc::ROOM:
        bound.room = createNamed(rooms, name);
        bound.node = bound.zoom = bound.view = nullptr;
        bound.object = nullptr;
        return true;
    case sc::NODE:
    case sc::ZOOM: {
        if (!bound.room)
            return false;
        auto& list = kind == sc::NODE ? bound.room->nodes : bound.room->zooms;
        auto v = std::make_unique<View>();
        v->name = name;
        v->isZoom = kind == sc::ZOOM;
        View* p = v.get();
        list.push_back(std::move(v));
        (kind == sc::NODE ? bound.node : bound.zoom) = p;
        bound.view = p;
        bound.object = nullptr;
        if (kind == sc::ZOOM)
            zoomOrder.push_back(p);
        return true;
    }
    case sc::OBJECT: {
        if (!bound.view)
            return false;
        auto o = std::make_unique<Object>();
        o->name = name;
        bound.object = o.get();
        bound.view->objects.push_back(std::move(o));
        return true;
    }
    case sc::ITEM: bound.item = createNamed(items, name); return true;
    case sc::SCRIPT: bound.dialogue = createNamed(dialogues, name); return true;
    case sc::CHOICE: bound.choice = createNamed(choices, name); return true;
    case sc::TRACK: bound.track = createNamed(tracks, name); return true;
    case sc::OVERLAY: bound.overlay = createNamed(overlays, name); return true;
    }
    return false;
}

bool World::bind(int kind, const std::string& name) {
    switch (kind) {
    case sc::ROOM:
        if (Room* r = room(name)) {
            bound.room = r;  // the node and zoom slots keep their binding
            return true;
        }
        return false;
    case sc::ZOOM:
        if (View* z = zoom(name)) {
            bound.zoom = bound.view = z;
            return true;
        }
        return false;
    case sc::NODE: {
        if (!bound.room)
            return false;
        View* v = bound.room->node(name);
        if (!v)
            return false;
        (kind == sc::NODE ? bound.node : bound.zoom) = v;
        bound.view = v;
        return true;
    }
    case sc::OBJECT: {
        Scope scope = objectScope ? objectScope() : Scope::Build;
        View* v = scope == Scope::Node ? bound.node : scope == Scope::Zoom ? bound.zoom : scope == Scope::Build ? bound.view : nullptr;
        // Not found binds nothing (0x4148a0 binds the search result, 0); using
        // it then is scream.exe's fatal "GetBind: target not bound".
        bound.object = nullptr;
        if (!v)
            return false;
        // The first object of that name in the view (0x403fd0 scans the global
        // object table, which is in creation order, for one in the view).
        auto found = v->find(name);
        bound.object = found.empty() ? nullptr : found.front();
        return bound.object != nullptr;
    }
    case sc::ITEM:
        if (auto it = items.find(name); it != items.end()) { bound.item = it->second.get(); return true; }
        return false;
    case sc::SCRIPT:
        if (auto it = dialogues.find(name); it != dialogues.end()) { bound.dialogue = it->second.get(); return true; }
        return false;
    case sc::CHOICE:
        if (auto it = choices.find(name); it != choices.end()) { bound.choice = it->second.get(); return true; }
        return false;
    case sc::TRACK:
        if (auto it = tracks.find(name); it != tracks.end()) { bound.track = it->second.get(); return true; }
        return false;
    case sc::OVERLAY:
        if (auto it = overlays.find(name); it != overlays.end()) { bound.overlay = it->second.get(); return true; }
        return false;
    }
    return false;
}

void World::begin(int kind) { scopes_.push_back(kind); }

void World::end(int kind) {
    if (!scopes_.empty())
        scopes_.pop_back();
    // Leaving a zoom returns object creation to the enclosing node.
    if (kind == sc::ZOOM) {
        bound.zoom = nullptr;
        bound.view = bound.node;
    } else if (kind == sc::NODE) {
        bound.node = nullptr;
        bound.view = nullptr;
    }
    bound.object = nullptr;
}

void World::bindClicked(Room* room, View* view, Object* object) {
    bound.room = room;
    bound.view = view;
    (view && view->isZoom ? bound.zoom : bound.node) = view;
    bound.object = view ? object : nullptr;  // the clicked object itself (0x414343)
}

View* World::zoom(const std::string& name) const {
    for (View* z : zoomOrder)
        if (z->name == name)
            return z;
    return nullptr;
}
