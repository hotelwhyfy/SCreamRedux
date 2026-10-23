// --check: validate the world the game script built against itself, the Lua
// globals and the data files, without running the game. Every click target,
// handler and asset a script declared must exist.

#include <cstdio>
#include <fstream>
#include <set>
#include <vector>
#include <algorithm>

#include "constants.hpp"
#include "engine.hpp"

namespace {

struct Checker {
    Engine& e;
    int problems = 0;
    std::set<std::string> reported;

    void report(const std::string& where, const std::string& what) {
        if (reported.insert(where + ": " + what).second) {
            std::printf("%s: %s\n", where.c_str(), what.c_str());
            ++problems;
        }
    }

    bool function(const std::string& name) {
        lua_State* L = e.script.state();
        lua_getfield(L, LUA_GLOBALSINDEX, name.c_str());
        bool ok = lua_isfunction(L, -1);
        lua_pop(L, 1);
        return ok;
    }

    void file(const std::string& where, const std::string& name) {
        if (!name.empty() && !e.res.exists(name) && e.findData(name).empty() && name != "black.jpg")
            report(where, "missing file " + name);
    }

    void handler(const std::string& where, const std::string& name) {
        if (!name.empty() && !function(name))
            report(where, "no Lua function " + name);
    }

    bool anyZoom(const std::string& name) {
        if (name == "black")
            return true;
        for (auto& [_, r] : e.world.rooms)
            if (r->zoom(name))
                return true;
        return false;
    }

    // `room` is the room the action is triggered from (null for item reactions).
    void action(const std::string& where, const Action& a, Room* room) {
        switch (a.kind) {
        case sc::NODE:
            if (room ? !room->node(a.arg) : true) {
                bool somewhere = false;
                for (auto& [_, r] : e.world.rooms) somewhere = somewhere || r->node(a.arg);
                if (room || !somewhere)
                    report(where, "no node " + a.arg + (room ? " in room " + room->name : ""));
            }
            break;
        case sc::ROOM: {
            Room* r = e.world.room(a.arg);
            if (!r || r->nodes.empty()) report(where, "no room " + a.arg);
            break;
        }
        case sc::ZOOM:
            if (!(room && room->zoom(a.arg)) && !anyZoom(a.arg)) report(where, "no zoom " + a.arg);
            break;
        case sc::OVERLAY:
            if (!e.world.overlays.count(a.arg)) report(where, "no overlay " + a.arg);
            break;
        case sc::CUSTOM: handler(where, a.arg); break;
        case sc::FEEDBACK_EX: file(where, a.arg2); break;
        default: break;
        }
    }

    void reactions(const std::string& where, const std::vector<Reaction>& rs, Room* room) {
        for (const auto& r : rs) {
            if (!e.world.items.count(r.item))
                report(where, "reaction to unknown item " + r.item);
            action(where + " <- " + r.item, r.action, room);
        }
    }

    void run() {
        World& w = e.world;
        for (auto& [rn, r] : w.rooms) {
            handler("room " + rn, r->onEnter);
            if (!r->lastVisited.empty() && !r->node(r->lastVisited))
                report("room " + rn, "last visited node " + r->lastVisited + " missing");
            auto views = [&](auto& list) {
                for (auto& v : list) {
                    std::string where = rn + "/" + v->name;
                    if (v->isZoom) {
                        file(where, v->image);
                        for (auto& [_, f] : v->audio) file(where, f);
                    } else {
                        for (auto& f : v->faces) file(where, f);
                        file(where, v->footstep);
                    }
                    for (auto& o : v->objects) {
                        action(where + "/" + o->name, o->click, r.get());
                        reactions(where + "/" + o->name, o->reactions, r.get());
                    }
                }
            };
            views(r->nodes);
            views(r->zooms);
        }
        for (auto& [n, it] : w.items) {
            file("item " + n, it->icon);
            file("item " + n, it->voice);
            reactions("item " + n, it->reactions, nullptr);
        }
        for (auto& [n, d] : w.dialogues)
            for (auto& l : d->lines) file("script " + n, l.voice);
        for (auto& [n, c] : w.choices)
            for (auto& [text, fn] : c->options) handler("choice " + n, fn);
        for (auto& [n, t] : w.tracks)
            for (size_t i = 0; i < (t->surround ? 3u : 1u); ++i) file("track " + n, t->files[i]);
        for (auto& [n, o] : w.overlays) {
            file("overlay " + n, o->args[0]);
            file("overlay " + n, o->args[1]);
            file("overlay " + n, o->args[2]);
        }
        for (auto& img : e.menu.images) file("menu", img);
        for (auto& fn : e.menu.newGame) handler("menu", fn);
        for (auto& [_, f] : e.cursors) file("cursor", f);
        std::printf("checked %zu rooms, %zu items, %zu scripts, %zu choices, %zu tracks: %d problem%s\n",
                    w.rooms.size(), w.items.size(), w.dialogues.size(), w.choices.size(), w.tracks.size(), problems,
                    problems == 1 ? "" : "s");
    }
};

}  // namespace

int Engine::checkWorld() {
    Checker c{*this, 0, {}};
    c.run();
    return c.problems;
}

// --dump-world: everything the script built, one fact per line in creation
// order, so two scripts that build the same world give the same file (the
// editor's round-trip test). Lua's string and number globals follow, sorted.
bool Engine::dumpWorld(const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out)
        return false;
    auto action = [](const Action& a) {
        return std::to_string(a.kind) + " [" + a.arg + "]" + (a.arg2.empty() ? "" : " [" + a.arg2 + "]");
    };
    auto reactions = [&](const std::string& indent, const std::vector<Reaction>& rs) {
        for (const auto& r : rs)
            out << indent << "reaction " << r.item << " " << action(r.action) << "\n";
    };
    auto view = [&](const View& v) {
        out << "  " << (v.isZoom ? "zoom " : "node ") << v.index << " " << v.name << "\n";
        if (v.isZoom)
            out << "    image " << v.image << "\n";
        for (size_t f = 0; f < v.faces.size(); ++f)
            if (!v.faces[f].empty()) out << "    face " << f + 1 << " " << v.faces[f] << "\n";
        if (!v.footstep.empty()) out << "    footstep " << v.footstep << "\n";
        for (const auto& [k, f] : v.audio) out << "    audio " << k << " " << f << "\n";
        for (const auto& o : v.objects) {
            out << "    object " << o->index << " " << o->name << " face " << o->face << " rect " << o->rect.x << " "
                << o->rect.y << " " << o->rect.w << " " << o->rect.h << " cursor " << o->cursor << " click "
                << action(o->click) << (o->enabled ? "" : " disabled") << "\n";
            reactions("      ", o->reactions);
        }
    };
    for (Room* r : world.roomList) {
        if (!r) continue;
        out << "room " << r->index << " " << r->name << " enter [" << r->onEnter << "] exit [" << r->onExit
            << "] last [" << r->lastVisited << "]\n";
        for (const auto& t : r->timers) out << "  timer " << t.id << " " << t.seconds << " " << t.fn << "\n";
        for (const auto& v : r->nodes) view(*v);
        for (const auto& v : r->zooms) view(*v);
    }
    for (Item* it : world.itemList) {
        if (!it) continue;
        out << "item " << it->index << " " << it->name << " [" << it->icon << "] [" << it->label << "] [" << it->text
            << "] [" << it->voice << "]\n";
        reactions("  ", it->reactions);
    }
    for (const auto& [n, d] : world.dialogues) {
        out << "script " << n << "\n";
        for (const auto& l : d->lines) out << "  line " << l.flag << " [" << l.voice << "] [" << l.text << "]\n";
    }
    for (const auto& [n, c] : world.choices) {
        out << "choice " << n << "\n";
        for (const auto& [text, fn] : c->options) out << "  option [" << text << "] " << fn << "\n";
    }
    for (Track* t : world.trackList) {
        if (!t) continue;
        out << "track " << t->index << " " << t->name << " [" << t->files[0] << "] [" << t->files[1] << "] ["
            << t->files[2] << "] surround " << t->surround << " loop " << t->loop << " volume " << t->volume
            << " fade " << t->fadeIn << " " << t->fadeOut << "\n";
    }
    for (const auto& [n, o] : world.overlays)
        out << "overlay " << n << " [" << o->args[0] << "] [" << o->args[1] << "] [" << o->args[2] << "]\n";
    for (const auto& img : menu.images) out << "menu image " << img << "\n";
    for (const auto& fn : menu.newGame) out << "menu game " << fn << "\n";
    for (const auto& [k, v] : menu.extra) out << "menu extra " << k << " " << v << "\n";
    for (const auto& [k, f] : cursors) out << "cursor " << k << " " << f << "\n";
    for (const auto& [k, v] : hints) out << "hint " << k << " [" << v << "]\n";
    out << "font " << font << "\n";
    for (const auto& t : globalTimers_) out << "global timer " << t.id << " " << t.seconds << " " << t.fn << "\n";

    lua_State* L = script.state();
    std::vector<std::pair<std::string, std::string>> globals;
    lua_pushnil(L);
    while (lua_next(L, LUA_GLOBALSINDEX)) {
        if (lua_type(L, -2) == LUA_TSTRING && (lua_type(L, -1) == LUA_TSTRING || lua_type(L, -1) == LUA_TNUMBER)) {
            lua_pushvalue(L, -1);  // lua_tostring converts in place: work on a copy
            globals.emplace_back(lua_tostring(L, -3), std::string(lua_type(L, -2) == LUA_TNUMBER ? "n " : "s ") +
                                                          lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    std::sort(globals.begin(), globals.end());
    for (const auto& [k, v] : globals) out << "global " << k << " " << v << "\n";
    return bool(out);
}
