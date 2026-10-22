// Saved games.
//
// The world is built once at boot, so a save only needs what play can change:
// the script's global variables (stored as a diff against their values when
// the game started) plus the engine's mutable state. A new game is the same
// mechanism: restore the start-of-game baseline, then run the new-game
// callback. The format is line based; free text always comes last on a line.
//
// The original save format is unknown, so these files are this port's own.

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>

#include <ctime>
#include <fstream>
#include <sstream>

#include "constants.hpp"
#include "engine.hpp"

namespace {

constexpr const char* kMagic = "SCREAMREDUX-SAVE 1";

std::string escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}

std::string unescape(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            ++i;
            out += s[i] == 'n' ? '\n' : s[i];
        } else {
            out += s[i];
        }
    }
    return out;
}

// Rest of the line after the stream's current position, minus one separator.
std::string rest(std::istringstream& in) {
    std::string r;
    std::getline(in, r);
    if (!r.empty() && r[0] == ' ')
        r.erase(0, 1);
    return unescape(r);
}

}  // namespace

std::map<std::string, Engine::LuaValue> Engine::luaGlobals() {
    std::map<std::string, LuaValue> out;
    lua_State* L = script.state();
    lua_pushnil(L);
    while (lua_next(L, LUA_GLOBALSINDEX) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING) {
            std::string key = lua_tostring(L, -2);
            switch (lua_type(L, -1)) {
            case LUA_TNUMBER: out[key] = {'n', lua_tonumber(L, -1), {}}; break;
            case LUA_TBOOLEAN: out[key] = {'b', double(lua_toboolean(L, -1)), {}}; break;
            case LUA_TSTRING: out[key] = {'s', 0, lua_tostring(L, -1)}; break;
            }
        }
        lua_pop(L, 1);
    }
    return out;
}

void Engine::setLuaGlobal(const std::string& name, const LuaValue* v) {
    lua_State* L = script.state();
    if (!v) lua_pushnil(L);
    else if (v->type == 'n') lua_pushnumber(L, v->number);
    else if (v->type == 'b') lua_pushboolean(L, v->number != 0);
    else lua_pushlstring(L, v->text.data(), v->text.size());
    lua_setfield(L, LUA_GLOBALSINDEX, name.c_str());
}

std::string Engine::serializeState() {
    std::ostringstream o;
    o.precision(17);
    o << kMagic << "\n";

    // Script globals that differ from the start of the game.
    for (const auto& [name, v] : luaGlobals()) {
        auto b = luaBaseline_.find(name);
        if (b != luaBaseline_.end() && b->second.type == v.type && b->second.number == v.number && b->second.text == v.text)
            continue;
        if (v.type == 's') o << "G " << name << " s " << escape(v.text) << "\n";
        else o << "G " << name << " " << v.type << " " << v.number << "\n";
    }

    // World.
    for (const auto& [rname, room] : world.rooms) {
        o << "room " << rname << " " << escape(room->lastVisited) << "\n";
        auto views = [&](const char* kind, const auto& list) {
            for (size_t i = 0; i < list.size(); ++i) {
                const View& v = *list[i];
                o << "view " << kind << " " << i;
                std::string bits;
                for (const auto& obj : v.objects) bits += obj->enabled ? '1' : '0';
                o << " " << (bits.empty() ? "-" : bits) << "\n";
                if (v.isZoom) o << "img " << escape(v.image) << "\n";
                else for (int f = 0; f < 6; ++f) o << "face " << f << " " << escape(v.faces[size_t(f)]) << "\n";
            }
        };
        views("n", room->nodes);
        views("z", room->zooms);
    }
    for (const auto& [name, c] : world.choices) {
        o << "choice " << name << " " << c->lastSelected << " " << c->options.size() << "\n";
        for (const auto& [text, fn] : c->options) o << "opt " << fn << " " << escape(text) << "\n";
    }

    // Engine.
    for (Item* it : inventory) o << "inv " << it->name << "\n";
    o << "jrnpos " << journal.first << " " << journal.last << "\n";
    for (int idx : journal.added) o << "jrnadded " << idx << "\n";
    for (const auto& line : journal.lines) o << "jrn " << escape(line) << "\n";
    for (int f : flags) o << "flag " << f << "\n";
    for (const auto& [key, v] : ambience) o << "amb " << key << " " << v.first << " " << v.second << "\n";
    o << "hint " << hintState << "\n";
    o << "font " << escape(font) << "\n";
    o << "motion " << motion << "\n";
    if (room) o << "loc " << room->name << " " << (node ? node->name : "") << "\n";
    for (View* z : zooms) {
        // Zooms are found by name, so store the owning room when there is one.
        std::string owner = "*";
        for (const auto& [rname, r] : world.rooms)
            for (const auto& v : r->zooms)
                if (v.get() == z) owner = rname;
        o << "zoom " << owner << " " << z->name << "\n";
    }
    o << "cam " << cam.yaw << " " << cam.pitch << "\n";
    for (const auto& t : timers_) o << "timer " << (t.at - now()) << " " << t.global << " " << t.fn << "\n";
    std::string track = sound.currentTrack();
    if (!track.empty()) o << "music " << track << "\n";
    for (const auto& l : sound.loopingChannels()) o << "loop " << l.channel << " " << l.volume << " " << l.file << "\n";
    return o.str();
}

bool Engine::restoreState(const std::string& data) {
    std::istringstream in(data);
    std::string line;
    if (!std::getline(in, line) || line != kMagic)
        return false;

    script.cancelAll();
    sound.stopAll();
    video_.reset();
    choice_ = nullptr;
    invOpen_ = journalOpen_ = false;
    held = nullptr;
    clearFeedback();
    unloadSubtitles();
    locked = waitMode = false;
    timers_.clear();
    camAnim_.active = false;

    // Script globals: back to the start-of-game values, then the saved diff.
    for (const auto& [name, v] : luaGlobals())
        if (!luaBaseline_.count(name))
            setLuaGlobal(name, nullptr);
    for (const auto& [name, v] : luaBaseline_)
        setLuaGlobal(name, &v);

    inventory.clear();
    journal = {};
    flags.clear();
    ambience.clear();
    zooms.clear();
    overlay = nullptr;
    room = nullptr;
    node = nullptr;

    Room* r = nullptr;
    std::vector<std::unique_ptr<View>>* list = nullptr;
    View* v = nullptr;
    Choice* c = nullptr;
    std::string music;
    std::vector<Sound::Loop> loops;
    while (std::getline(in, line)) {
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "G") {
            std::string name;
            char type;
            ls >> name >> type;
            LuaValue val{type, 0, {}};
            if (type == 's') val.text = rest(ls);
            else ls >> val.number;
            setLuaGlobal(name, &val);
        } else if (tag == "room") {
            std::string name;
            ls >> name;
            r = world.room(name);
            if (r) r->lastVisited = rest(ls);
        } else if (tag == "view" && r) {
            std::string kind, bits;
            size_t i;
            ls >> kind >> i >> bits;
            list = kind == "n" ? &r->nodes : &r->zooms;
            v = i < list->size() ? (*list)[i].get() : nullptr;
            if (v && bits != "-")
                for (size_t k = 0; k < v->objects.size() && k < bits.size(); ++k)
                    v->objects[k]->enabled = bits[k] == '1';
        } else if (tag == "img" && v) {
            v->image = rest(ls);
        } else if (tag == "face" && v) {
            int f;
            ls >> f;
            if (f >= 0 && f < 6) v->faces[size_t(f)] = rest(ls);
        } else if (tag == "choice") {
            std::string name;
            ls >> name;
            auto it = world.choices.find(name);
            c = it == world.choices.end() ? nullptr : it->second.get();
            if (c) {
                ls >> c->lastSelected;
                c->options.clear();
            }
        } else if (tag == "opt" && c) {
            std::string fn;
            ls >> fn;
            c->options.emplace_back(rest(ls), fn);
        } else if (tag == "inv") {
            std::string name;
            ls >> name;
            if (auto it = world.items.find(name); it != world.items.end()) inventory.push_back(it->second.get());
        } else if (tag == "jrnpos") {
            ls >> journal.first >> journal.last;
        } else if (tag == "jrnadded") {
            int idx;
            ls >> idx;
            journal.added.insert(idx);
        } else if (tag == "jrn") {
            journal.lines.push_back(rest(ls));
        } else if (tag == "flag") {
            int f;
            ls >> f;
            flags.insert(f);
        } else if (tag == "amb") {
            int key, preset, flag;
            ls >> key >> preset >> flag;
            ambience[key] = {preset, flag};
        } else if (tag == "hint") {
            ls >> hintState;
        } else if (tag == "font") {
            font = rest(ls);
        } else if (tag == "motion") {
            ls >> motion;
        } else if (tag == "loc") {
            std::string rname, nname;
            ls >> rname >> nname;
            room = world.room(rname);
            node = room ? room->node(nname) : nullptr;
        } else if (tag == "zoom") {
            std::string owner, name;
            ls >> owner >> name;
            Room* zr = world.room(owner);
            if (View* z = zr ? zr->zoom(name) : nullptr) zooms.push_back(z);
        } else if (tag == "cam") {
            ls >> cam.yaw >> cam.pitch;
        } else if (tag == "timer") {
            double remaining;
            bool global;
            std::string fn;
            ls >> remaining >> global >> fn;
            timers_.push_back({now() + remaining, fn, global});
        } else if (tag == "music") {
            ls >> music;
        } else if (tag == "loop") {
            Sound::Loop l{};
            ls >> l.channel >> l.volume >> l.file;
            loops.push_back(l);
        }
    }
    fx_.snap = true;  // restored ambience applies at once
    if (!music.empty())
        if (auto it = world.tracks.find(music); it != world.tracks.end())
            sound.playTrack(*it->second);
    for (const auto& l : loops) {
        sound.setChannel(l.channel, sc::VOLUME, l.volume * 10);
        sound.playChannel(l.channel, l.file, true);
    }
    return true;
}

void Engine::captureBaseline() {
    luaBaseline_ = luaGlobals();
    baseline_ = serializeState();
}

std::filesystem::path Engine::saveDir() const {
    if (!opts.saveDir.empty()) {
        std::filesystem::create_directories(opts.saveDir);
        return opts.saveDir;
    }
    char* pref = SDL_GetPrefPath("SCreamRedux", "Scratches");
    std::filesystem::path dir = pref ? pref : ".";
    SDL_free(pref);
    return dir;
}

std::filesystem::path Engine::slotPath(int slot) const {
    return saveDir() / ("slot" + std::to_string(slot) + ".sav");
}

// First lines of a save: the description and timestamp shown in the menu.
std::string Engine::slotDescription(int slot) const {
    std::ifstream in(slotPath(slot));
    std::string magic, desc;
    if (!std::getline(in, magic) || !std::getline(in, desc))
        return {};
    return unescape(desc);
}

bool Engine::saveSlot(int slot) {
    // The game's own save() returns the slot description ("the last visit, <room>").
    std::string desc = "Saved game";
    lua_State* L = script.state();
    lua_getfield(L, LUA_GLOBALSINDEX, "save");
    if (lua_isfunction(L, -1) && lua_pcall(L, 0, 1, 0) == 0 && lua_isstring(L, -1))
        desc = lua_tostring(L, -1);
    lua_settop(L, 0);
    char when[32];
    std::time_t t = std::time(nullptr);
    std::strftime(when, sizeof when, "%Y-%m-%d %H:%M", std::localtime(&t));

    std::string state = serializeState();
    std::ofstream out(slotPath(slot), std::ios::trunc);
    // Header: magic, then a description line, then the state (which repeats the magic).
    out << kMagic << "\n" << escape(std::string(when) + "  " + desc) << "\n"
        << "game " << currentGame_ << "\n" << state;
    return bool(out);
}

bool Engine::loadSlot(int slot) {
    std::ifstream in(slotPath(slot));
    std::string magic, desc, gameLine;
    if (!std::getline(in, magic) || magic != kMagic || !std::getline(in, desc) || !std::getline(in, gameLine))
        return false;
    std::istringstream gl(gameLine);
    std::string tag;
    gl >> tag >> currentGame_;
    std::string state((std::istreambuf_iterator<char>(in)), {});
    if (!restoreState(baseline_) || !restoreState(state) || !room || !node) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "could not restore %s", slotPath(slot).string().c_str());
        return false;
    }
    return true;
}
