// Saved games.
//
// The world is built once at boot, so a save only needs what play can change:
// the script's global variables (stored as a diff against their values when
// the game started) plus the engine's mutable state. A new game is the same
// mechanism: restore the start-of-game baseline, then run the new-game
// callback. The format is line based; free text always comes last on a line.
//
// That snapshot is also how this port's first saves (slotN.sav) were
// written; saved games are now in scream.exe's own format (below).

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstring>
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
    auto timers = [&](const std::string& owner, const std::vector<GameTimer>& list) {
        for (const auto& t : list)
            o << "timer " << owner << " " << t.id << " " << t.seconds << " " << t.elapsed << " " << t.active << " "
              << t.fn << "\n";
    };
    timers("*", globalTimers_);
    for (const auto& [rname, r] : world.rooms) timers(rname, r->timers);
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
    escWaits_ = 0;  // the cancelled waits won't count down
    escSkip_ = false;
    sound.stopAll();
    video_.reset();
    choice_ = nullptr;
    invOpen_ = journalOpen_ = false;
    held = nullptr;
    clearFeedback();
    unloadSubtitles();
    locked = waitMode = false;
    globalTimers_.clear();
    for (auto& [rname, r] : world.rooms) r->timers.clear();
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
            std::string owner;
            GameTimer t;
            ls >> owner >> t.id >> t.seconds >> t.elapsed >> t.active >> t.fn;
            if (owner == "*") globalTimers_.push_back(t);
            else if (Room* r = world.room(owner)) r->timers.push_back(t);
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

// ---------------------------------------------------------------- scream.0NN
//
// Saved games in scream.exe's own format (writer 0x411370, reader 0x4107d0),
// so the original's saves load here and ours load there: "scream." + 3 digits
// (001-010 from the menu, 011 the quicksave), a little-endian dump of the
// engine's tables. Tables are 1-based and written from record 0 (unused) to
// the last; rooms, nodes, zooms, objects and items are matched by creation
// order. Fields this port doesn't model are written with scream.exe's
// defaults (0x40f260) and ignored on reading.

namespace {

struct Out {
    std::string b;
    void i32(int32_t v) { b.append(reinterpret_cast<const char*>(&v), 4); }
    void f32(float v) { b.append(reinterpret_cast<const char*>(&v), 4); }
    void str(const std::string& s, size_t n) {  // NUL-padded; cut to n bytes
        std::string f = s.substr(0, n);
        f.resize(n, '\0');
        b += f;
    }
};

struct In {
    const std::string& d;
    size_t p = 0;
    bool ok = true;
    bool need(size_t n) {
        if (p + n > d.size()) ok = false;
        return ok;
    }
    int32_t i32() {
        int32_t v = 0;
        if (need(4)) std::memcpy(&v, d.data() + p, 4), p += 4;
        return v;
    }
    float f32() {
        float v = 0;
        if (need(4)) std::memcpy(&v, d.data() + p, 4), p += 4;
        return v;
    }
    std::string str(size_t n) {
        if (!need(n)) return {};
        std::string s(d.data() + p, n);
        p += n;
        return s.substr(0, s.find('\0'));
    }
    void skip(size_t n) {
        if (need(n)) p += n;
    }
};

// The 700-byte block of settings at 0x4df8b64, as offsets into it.
namespace set {
constexpr size_t WIDTH = 0x0, HEIGHT = 0x4, DEPTH = 0x8, RATIOX = 0xc, RATIOY = 0x10, VIEWX = 0x14, VIEWY = 0x18,
                 QUALITY = 0x1c, FRAME = 0x20, GAMMA = 0x24, FOV = 0x28, LOG = 0x2c, MEDIA = 0x30, FULLSCREEN = 0x34,
                 RESOURCE = 0x38, VSYNC = 0x3c, RATE = 0x40, AUDIO_DEVICE = 0x44, BUFFER_SIZE = 0x48,
                 NUM_BUFFERS = 0x4c, RES_PATH = 0x50, RES_FILE = 0x150, SURROUND = 0x1a0, CAM_SPEED = 0x1a4,
                 TXT_SPEED = 0x1a8, MOV_SPEED = 0x1ac, SKY_SPEED = 0x1b0, SHOW_HOTSPOTS = 0x1b4, TEX_SIZE = 0x1b8,
                 SIZES = 0x1bc, SHOWMENU = 0x1d0, HINTS = 0x1d4, SPLASH = 0x1d8, SCRIPT = 0x228, SLIDESHOW = 0x278,
                 GLOWTEXT = 0x27c, WAIT = 0x280, SKY = 0x284, NIGHT = 0x288, GLOW = 0x28c, RAIN = 0x290, FOG = 0x294,
                 DREAM = 0x298, CANDLE = 0x29c, LAMP = 0x2a0, DEBUG = 0x2a4, LANGUAGE = 0x2a8, FPS_STYLE = 0x2ac,
                 SKY_COLOR = 0x2b0, CLOUD_COLOR = 0x2b4, SCREEN_COLOR = 0x2b8, SIZE = 0x2bc;
}

struct Block {
    std::string b = std::string(set::SIZE, '\0');
    void i32(size_t at, int32_t v) { std::memcpy(&b[at], &v, 4); }
    void f32(size_t at, float v) { std::memcpy(&b[at], &v, 4); }
    void str(size_t at, const std::string& s, size_t n) { std::memcpy(&b[at], s.data(), std::min(s.size(), n - 1)); }
    int32_t i32(size_t at) const { int32_t v; std::memcpy(&v, &b[at], 4); return v; }
    float f32(size_t at) const { float v; std::memcpy(&v, &b[at], 4); return v; }
};

constexpr int kStateNode = 4000, kStateZoom = 0xfa2, kStateInventory = 0xfa3, kStateBook = 0xfa8;

// What differs in the 2006 engine's files (patched exe; writer P 0x40f1f0):
// no journal or font, the pitch as an angle, a 368-entry zoom stack, items of
// 612 bytes without a voice, a 172-byte playlist block, and a settings block
// that is the DC's without the int at +0x1c4, DEBUG, alternate control and
// the ambience presets (so LANGUAGE moves to +0x2a0).
struct Layout {
    bool v2006;
    int32_t timerCallback;  // the C function that calls Lua by name, saved in each timer
    size_t zoomStack, itemText, playlist, playCounter;
};
constexpr Layout kLayoutDC{false, 0x404d80, 0x5e4 / 4, 0x190, 0xc4, 0xb8};
constexpr Layout kLayout2006{true, 0x404c20, 0x5c0 / 4, 0x118, 0xac, 0xa0};

std::string toBlock2006(const std::string& dc) {
    return dc.substr(0, 0x1c4) + dc.substr(0x1c8, 0x2a4 - 0x1c8) + dc.substr(0x2a8, 4);
}
std::string fromBlock2006(const std::string& b) {
    return b.substr(0, 0x1c4) + std::string(4, '\0') + b.substr(0x1c4, 0x2a0 - 0x1c4) + std::string(4, '\0') +
           b.substr(0x2a0, 4) + std::string(set::SIZE - 0x2ac, '\0');
}

std::string maskName(const std::string& image) {
    std::string base = image.substr(0, image.find('.'));  // strtok(name, ".")
    return image.empty() ? std::string() : base + "_msk.tga";
}

}  // namespace

std::filesystem::path Engine::slotPath(int slot) const {
    char name[16];
    std::snprintf(name, sizeof name, "scream.%03d", slot);
    return saveDir() / name;
}

// This port's earlier text saves (slotN.sav) still load.
std::filesystem::path Engine::legacySlotPath(int slot) const {
    return saveDir() / ("slot" + std::to_string(slot) + ".sav");
}

// The menu shows a save's first 80 bytes: the description (0x412d20).
std::string Engine::slotDescription(int slot) const {
    std::ifstream in(slotPath(slot), std::ios::binary);
    if (in) {
        char desc[80] = {};
        in.read(desc, sizeof desc);
        return std::string(desc, strnlen(desc, sizeof desc));
    }
    std::ifstream legacy(legacySlotPath(slot));
    std::string magic, desc;
    if (!std::getline(legacy, magic) || !std::getline(legacy, desc))
        return {};
    return unescape(desc);
}

bool Engine::slotUsed(int slot) const {
    return std::filesystem::exists(slotPath(slot)) || std::filesystem::exists(legacySlotPath(slot));
}

std::string Engine::saveDescription(int slot) {
    if (slot == kQuickSlot)
        return "quicksave";  // F5 (0x417aa5)
    // The menu asks the script's save() (scMenuSetSaveFunc, 0x404e30).
    std::string desc;
    lua_State* L = script.state();
    lua_getfield(L, LUA_GLOBALSINDEX, "save");
    if (lua_isfunction(L, -1) && lua_pcall(L, 0, 1, 0) == 0 && lua_isstring(L, -1))
        desc = lua_tostring(L, -1);
    lua_settop(L, 0);
    return desc;
}

bool Engine::saveSlot(int slot) {
    const Layout& lay = release2006 ? kLayout2006 : kLayoutDC;
    const size_t kZoomStack = lay.zoomStack;
    Out o;
    o.str(saveDescription(slot), 80);

    // Every global whose value is a number (or a numeric string), as key=value.
    lua_State* L = script.state();
    std::vector<std::string> lines;
    lua_pushnil(L);
    while (lua_next(L, LUA_GLOBALSINDEX) != 0) {
        if (lua_type(L, -2) == LUA_TSTRING && lua_isnumber(L, -1)) {
            lua_pushvalue(L, -1);
            lines.push_back(std::string(lua_tostring(L, -3)) + "=" + lua_tostring(L, -1));
            lua_pop(L, 1);
        }
        lua_pop(L, 1);
    }
    o.i32(int32_t(lines.size()));
    for (const auto& l : lines) {
        o.i32(int32_t(l.size() + 1));
        o.b += l;
        o.b += '\0';
    }

    // Settings.
    auto num = [&](int key, double def) {
        auto it = settings.find(key);
        auto* v = it == settings.end() ? nullptr : std::get_if<double>(&it->second);
        return v ? *v : def;
    };
    auto text = [&](int key, const std::string& def) {
        auto it = settings.find(key);
        auto* v = it == settings.end() ? nullptr : std::get_if<std::string>(&it->second);
        return v ? *v : def;
    };
    auto ambienceOf = [&](int key) {
        auto it = ambience.find(key);
        return it == ambience.end() ? sc::MORNING : it->second.first;
    };
    Block st;
    st.i32(set::WIDTH, int32_t(num(sc::WIDTH, 1024)));
    st.i32(set::HEIGHT, int32_t(num(sc::HEIGHT, 768)));
    st.i32(set::DEPTH, int32_t(num(sc::DEPTH, 32)));
    st.i32(set::RATIOX, release2006 ? 30 : 1);
    st.i32(set::RATIOY, release2006 ? 40 : 1);
    st.i32(set::VIEWX, 16);
    st.i32(set::VIEWY, 9);
    st.i32(set::QUALITY, int32_t(num(sc::QUALITY, 3)));
    st.f32(set::FRAME, 1);
    st.i32(set::GAMMA, gamma_);
    st.f32(set::FOV, fx_.fov);
    st.i32(set::LOG, int32_t(num(sc::LOG, 0)));
    st.i32(set::MEDIA, 1);
    st.i32(set::FULLSCREEN, int32_t(num(sc::FULLSCREEN, 1)));
    st.i32(set::RESOURCE, 1);
    st.i32(set::VSYNC, config.vsync);
    st.i32(set::RATE, config.rate);
    st.i32(set::AUDIO_DEVICE, config.audioDevice);
    st.i32(set::BUFFER_SIZE, config.bufferSize);
    st.i32(set::NUM_BUFFERS, config.numBuffers);
    st.str(set::RES_PATH, text(sc::RES_PATH, config.path), 0x100);
    st.str(set::RES_FILE, text(sc::RES_FILE, config.resource), 0x50);
    st.i32(set::SURROUND, int32_t(num(sc::SURROUND, 1)));
    st.i32(set::CAM_SPEED, int32_t(num(sc::CAM_SPEED, 50)));
    st.i32(set::TXT_SPEED, int32_t(num(sc::TXT_SPEED, 50)));
    st.i32(set::MOV_SPEED, int32_t(num(sc::MOV_SPEED, 25)));
    st.i32(set::SKY_SPEED, int32_t(num(sc::SKY_SPEED, release2006 ? 25 : 15)));
    st.i32(set::SHOW_HOTSPOTS, int32_t(num(sc::SHOW_HOTSPOTS, 0)));
    st.i32(set::TEX_SIZE, int32_t(num(sc::TEX_SIZE, release2006 ? 1024 : 1280)));
    const int32_t sizes[] = {int32_t(num(sc::MSK_SIZE, release2006 ? 512 : 1024)),
                             int32_t(num(sc::SKY_SIZE, release2006 ? 256 : 512)), int32_t(num(sc::IMG_SIZE, 512)),
                             int32_t(num(sc::INV_SIZE, 64)), int32_t(num(sc::CUR_SIZE, 64))};
    for (size_t i = 0; i < 5; ++i) st.i32(set::SIZES + 4 * i, sizes[i]);
    st.i32(set::SHOWMENU, menuShown_ ? 1 : 0);
    st.i32(set::HINTS, hintsOn() ? 1 : 0);
    st.str(set::SPLASH, text(sc::SPLASH, "sc_logo.tga"), 0x50);
    st.str(set::SCRIPT, config.script, 0x50);
    st.i32(set::SLIDESHOW, slideshow_ ? 1 : 0);
    st.i32(set::GLOWTEXT, 0);  // never set by anything (dead in both exes)
    const std::pair<size_t, int> flagFields[] = {{set::WAIT, sc::WAIT}, {set::SKY, sc::SKY}, {set::NIGHT, sc::NIGHT},
                                                 {set::GLOW, sc::GLOW}, {set::RAIN, sc::RAIN}, {set::FOG, sc::FOG},
                                                 {set::DREAM, sc::DREAM}, {set::CANDLE, sc::CANDLE},
                                                 {set::LAMP, sc::LAMP}};
    for (const auto& [at, f] : flagFields) st.i32(at, flag(f) ? 1 : 0);
    st.i32(set::DEBUG, int32_t(num(sc::DEBUG, 0)));
    st.str(set::LANGUAGE, text(sc::LANGUAGE, release2006 ? "gr" : "en"), 4);
    st.i32(set::FPS_STYLE, fpsStyle() ? 1 : 0);
    st.i32(set::SKY_COLOR, ambienceOf(sc::SKY_COLOR));
    st.i32(set::CLOUD_COLOR, ambienceOf(sc::CLOUD_COLOR));
    st.i32(set::SCREEN_COLOR, ambienceOf(sc::SCREEN_COLOR));
    o.b += lay.v2006 ? toBlock2006(st.b) : st.b;

    if (!lay.v2006) {
        // Journal: last, first, the added flags, then 1000 lines of 40 bytes.
        o.i32(journal.last);
        o.i32(journal.first);
        for (int i = 0; i < 1000; ++i) o.i32(journal.added.count(i) ? 1 : 0);
        for (size_t i = 0; i < 1000; ++i) o.str(i < journal.lines.size() ? journal.lines[i] : std::string(), 40);
        o.str(font, 48);
    }
    float yaw, y;
    getLook(yaw, y);
    o.f32(yaw);
    o.f32(y);

    View* v = view();
    const int state = invOpen_ ? kStateInventory : journalOpen_ ? kStateBook : v && v->isZoom ? kStateZoom : kStateNode;
    o.i32(state);
    o.i32(kStateNode);  // the previous state
    o.i32(0);           // scBegin kind
    o.i32(room ? room->index : 0);

    auto timers = [&](const std::vector<GameTimer>& list) {
        o.i32(int32_t(list.size()));
        o.b += std::string(0x44, '\0');  // record 0
        for (const GameTimer& t : list) {
            o.i32(t.id);
            o.i32(t.seconds);
            o.i32(t.elapsed);
            o.i32(t.active ? 1 : 0);
            o.str(t.fn, 0x30);
            o.i32(lay.timerCallback);
        }
    };
    timers(globalTimers_);

    // Rooms: current node, entry node, zoom stack, local timers.
    auto nodeIndex = [&](Room* r, const std::string& name) {
        View* n = name.empty() ? nullptr : r->node(name);
        return n ? n->index : 0;
    };
    o.i32(int32_t(world.roomList.size() - 1));
    for (size_t i = 0; i < world.roomList.size(); ++i) {
        Room* r = world.roomList[i];
        o.i32(int32_t(i));
        if (!r) {
            o.b += std::string(12 + kZoomStack * 4, '\0');
            o.i32(0);
            o.b += std::string(0x44, '\0');
            continue;
        }
        o.i32(r == room && node ? node->index : nodeIndex(r, r->lastVisited));
        o.i32(nodeIndex(r, r->lastVisited));
        const bool here = r == room;
        o.i32(here ? int32_t(zooms.size()) : 0);
        std::vector<int32_t> stack(kZoomStack, 0);
        for (size_t k = 0; here && k < zooms.size() && k + 1 < kZoomStack; ++k)
            stack[k + 1] = zooms[k]->index;
        for (int32_t z : stack) o.i32(z);
        timers(r->timers);
    }

    // Nodes: faces and their masks; zooms: image and mask.
    o.i32(int32_t(world.nodeList.size() - 1));
    for (size_t i = 0; i < world.nodeList.size(); ++i) {
        View* n = world.nodeList[i];
        o.i32(int32_t(i));
        for (int f = 0; f < 6; ++f) o.str(n ? n->faces[size_t(f)] : std::string(), 0x30);
        for (int f = 0; f < 6; ++f) o.str(n ? maskName(n->faces[size_t(f)]) : std::string(), 0x30);
    }
    o.i32(int32_t(world.zoomList.size() - 1));
    for (size_t i = 0; i < world.zoomList.size(); ++i) {
        View* z = world.zoomList[i];
        const std::string image = z ? z->image : i == 1 ? std::string("black.jpg") : std::string();
        o.i32(int32_t(i));
        o.str(image, 0x30);
        o.str(maskName(image), 0x30);
    }
    o.i32(int32_t(world.objectList.size() - 1));
    for (size_t i = 0; i < world.objectList.size(); ++i) {
        o.i32(int32_t(i));
        o.i32(world.objectList[i] && world.objectList[i]->enabled ? 1 : 0);
    }
    o.i32(int32_t(world.itemList.size() - 1));
    for (size_t i = 0; i < world.itemList.size(); ++i) {
        Item* it = world.itemList[i];
        o.i32(int32_t(i));
        o.str(it ? it->icon : std::string(), 0x30);
        o.str(it ? it->label : std::string(), lay.itemText);
        o.str(it ? it->text : std::string(), lay.itemText);
        if (!lay.v2006) o.str(it ? it->voice : std::string(), 0x30);
    }
    // Inventory: its size, then slots 0..(item count), slot 0 unused.
    o.i32(int32_t(inventory.size()));
    for (size_t i = 0; i < world.itemList.size(); ++i)
        o.i32(i >= 1 && i <= inventory.size() ? inventory[i - 1]->index : 0);

    // Sound channels 1-17.
    const auto records = sound.channelRecords();
    for (int ch = 1; ch <= 17; ++ch) {
        const Sound::ChannelRecord& r = records[size_t(ch)];
        o.str(r.file, 32);
        o.i32(r.loaded);
        o.i32(r.playing);
        o.f32(r.volume);   // +0x340
        o.f32(r.current);  // +0x33c
        o.i32(r.fadeIn);
        o.i32(r.fadeOut);
        o.i32(r.loop);
    }
    // The playlist block (0x4df5b48); no shipped script uses the playlist.
    Out pl;
    pl.b = std::string(lay.playlist, '\0');
    int32_t one = 1;
    std::memcpy(&pl.b[lay.playCounter], &one, 4);  // play counter
    o.b += pl.b;

    std::ofstream out(slotPath(slot), std::ios::binary | std::ios::trunc);
    out.write(o.b.data(), std::streamsize(o.b.size()));
    if (!out) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not save game! (%s)", slotPath(slot).string().c_str());
        return false;
    }
    std::filesystem::remove(legacySlotPath(slot));
    return true;
}

bool Engine::loadSlot(int slot) {
    std::ifstream file(slotPath(slot), std::ios::binary);
    if (!file)
        return loadLegacySlot(slot);
    const std::string data((std::istreambuf_iterator<char>(file)), {});
    const Layout& lay = release2006 ? kLayout2006 : kLayoutDC;
    const size_t kZoomStack = lay.zoomStack;
    In in{data};
    in.skip(80);

    // Back to the start of the game, then what the file holds. (scream.exe
    // leaves whatever the file doesn't cover as it was in memory.)
    if (!restoreState(baseline_))
        return false;
    lua_State* L = script.state();
    const int globals = in.i32();
    for (int i = 0; in.ok && i < globals; ++i) {
        const int len = in.i32();
        if (len < 0 || !in.need(size_t(len)))
            break;
        std::string line(data.data() + in.p, size_t(len));
        in.p += size_t(len);
        line = line.substr(0, line.find('\0'));
        if (luaL_loadbuffer(L, line.data(), line.size(), "line") == 0)  // each line runs as Lua
            lua_pcall(L, 0, 0, 0);
        lua_settop(L, 0);
    }

    Block st;
    const size_t blockSize = lay.v2006 ? 0x2a4 : set::SIZE;
    if (in.need(blockSize)) {
        st.b = data.substr(in.p, blockSize);
        if (lay.v2006) st.b = fromBlock2006(st.b);
        in.p += blockSize;
    }
    if (!in.ok)
        return false;
    gamma_ = std::clamp(int(st.i32(set::GAMMA)), 0, 3);
    fx_.fov = st.f32(set::FOV);
    settings[sc::SURROUND] = double(st.i32(set::SURROUND));
    settings[sc::CAM_SPEED] = double(st.i32(set::CAM_SPEED));
    settings[sc::TXT_SPEED] = double(st.i32(set::TXT_SPEED));
    settings[sc::MOV_SPEED] = double(st.i32(set::MOV_SPEED));
    settings[sc::SKY_SPEED] = double(st.i32(set::SKY_SPEED));
    settings[sc::HINTS] = double(st.i32(set::HINTS));
    if (!lay.v2006) settings[sc::FPS_STYLE] = double(st.i32(set::FPS_STYLE));
    menuShown_ = st.i32(set::SHOWMENU) != 0;
    slideshow_ = st.i32(set::SLIDESHOW) != 0;
    const std::pair<size_t, int> flagFields[] = {{set::WAIT, sc::WAIT}, {set::SKY, sc::SKY}, {set::NIGHT, sc::NIGHT},
                                                 {set::GLOW, sc::GLOW}, {set::RAIN, sc::RAIN}, {set::FOG, sc::FOG},
                                                 {set::DREAM, sc::DREAM}, {set::CANDLE, sc::CANDLE},
                                                 {set::LAMP, sc::LAMP}};
    for (const auto& [at, f] : flagFields) {
        if (st.i32(at)) flags.insert(f);
        else flags.erase(f);
    }
    if (!lay.v2006) {
        ambience[sc::SKY_COLOR] = {st.i32(set::SKY_COLOR), 1};
        ambience[sc::CLOUD_COLOR] = {st.i32(set::CLOUD_COLOR), 1};
        ambience[sc::SCREEN_COLOR] = {st.i32(set::SCREEN_COLOR), 1};

        journal.last = in.i32();
        journal.first = in.i32();
        journal.added.clear();
        for (int i = 0; i < 1000; ++i)
            if (in.i32()) journal.added.insert(i);
        journal.lines.assign(1000, {});
        for (size_t i = 0; i < 1000; ++i) journal.lines[i] = in.str(40);
        journal.lines.resize(size_t(std::clamp(journal.last + 1, 0, 1000)));
        font = in.str(48);
    }
    const float yaw = in.f32(), y = in.f32();
    const int state = in.i32();
    in.i32();  // previous state
    in.i32();  // scBegin kind
    const int roomIndex = in.i32();

    auto timers = [&](std::vector<GameTimer>& list) {
        const int count = in.i32();
        list.clear();
        in.skip(0x44);
        for (int i = 1; in.ok && i <= count; ++i) {
            GameTimer t;
            t.id = in.i32();
            t.seconds = in.i32();
            t.elapsed = in.i32();
            t.active = in.i32() != 0;
            t.fn = in.str(0x30);
            in.i32();  // callback
            list.push_back(t);
        }
    };
    timers(globalTimers_);

    std::vector<int32_t> savedStack;
    const int rooms = in.i32();
    for (int i = 0; in.ok && i <= rooms; ++i) {
        const int index = in.i32();
        in.i32();  // current node
        const int entry = in.i32();
        const int depth = in.i32();
        std::vector<int32_t> stack(kZoomStack);
        for (auto& z : stack) z = in.i32();
        Room* r = index >= 1 && size_t(index) < world.roomList.size() ? world.roomList[size_t(index)] : nullptr;
        std::vector<GameTimer> local;
        timers(local);
        if (!r)
            continue;
        r->timers = local;
        if (entry >= 1 && size_t(entry) < world.nodeList.size() && world.nodeList[size_t(entry)])
            r->lastVisited = world.nodeList[size_t(entry)]->name;
        if (index == roomIndex)
            savedStack.assign(stack.begin() + 1, stack.begin() + 1 + std::clamp(depth, 0, int(kZoomStack) - 1));
    }
    const int nodes = in.i32();
    for (int i = 0; in.ok && i <= nodes; ++i) {
        const int index = in.i32();
        std::array<std::string, 6> faces;
        for (auto& f : faces) f = in.str(0x30);
        in.skip(6 * 0x30);  // mask names follow from the faces
        if (index >= 1 && size_t(index) < world.nodeList.size() && world.nodeList[size_t(index)])
            world.nodeList[size_t(index)]->faces = faces;
    }
    const int zoomCount = in.i32();
    for (int i = 0; in.ok && i <= zoomCount; ++i) {
        const int index = in.i32();
        std::string image = in.str(0x30);
        in.skip(0x30);
        if (index >= 2 && size_t(index) < world.zoomList.size() && world.zoomList[size_t(index)])
            world.zoomList[size_t(index)]->image = image;
    }
    const int objects = in.i32();
    for (int i = 0; in.ok && i <= objects; ++i) {
        const int index = in.i32();
        const bool enabled = in.i32() != 0;
        if (index >= 1 && size_t(index) < world.objectList.size() && world.objectList[size_t(index)])
            world.objectList[size_t(index)]->enabled = enabled;
    }
    // Items: read in order over our own item count (as 0x4107d0 does).
    in.i32();
    for (size_t i = 0; in.ok && i < world.itemList.size(); ++i) {
        in.i32();
        std::string icon = in.str(0x30), label = in.str(lay.itemText), text = in.str(lay.itemText);
        std::string voice = lay.v2006 ? std::string() : in.str(0x30);
        if (Item* it = world.itemList[i]) it->icon = icon, it->label = label, it->text = text, it->voice = voice;
    }
    const int invCount = in.i32();
    std::vector<int32_t> slots(world.itemList.size());
    for (auto& s : slots) s = in.i32();
    inventory.clear();
    for (int i = 1; i <= invCount && size_t(i) < slots.size(); ++i)
        if (slots[size_t(i)] >= 1 && size_t(slots[size_t(i)]) < world.itemList.size() && world.itemList[size_t(slots[size_t(i)])])
            inventory.push_back(world.itemList[size_t(slots[size_t(i)])]);

    std::array<Sound::ChannelRecord, 18> records{};
    for (int ch = 1; ch <= 17; ++ch) {
        Sound::ChannelRecord& r = records[size_t(ch)];
        r.file = in.str(32);
        r.loaded = in.i32() != 0;
        r.playing = in.i32() != 0;
        r.volume = in.f32();
        r.current = in.f32();
        r.fadeIn = in.i32();
        r.fadeOut = in.i32();
        r.loop = in.i32() != 0;
    }
    in.skip(lay.playlist);  // the playlist block
    if (!in.ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Could not load game! (%s is short)", slotPath(slot).string().c_str());
        return false;
    }

    // Then (0x4111aa): into the saved room (its entry node and onEnter), the
    // zooms back if it was saved in one, the view, the ambience at once.
    Room* r = roomIndex >= 1 && size_t(roomIndex) < world.roomList.size() ? world.roomList[size_t(roomIndex)] : nullptr;
    if (!r || !switchRoom(r->name))
        return false;
    transition_.active = false;
    if (state == kStateZoom)
        for (int32_t z : savedStack)
            if (z >= 2 && size_t(z) < world.zoomList.size() && world.zoomList[size_t(z)])
                zooms.push_back(world.zoomList[size_t(z)]);
    if (!zooms.empty())
        world.bound.zoom = world.bound.view = zooms.back();
    setLook(yaw, y);
    fx_.snap = true;
    sound.restoreChannels(records);
    return true;
}

bool Engine::loadLegacySlot(int slot) {
    std::ifstream in(legacySlotPath(slot));
    std::string magic, desc, gameLine;
    if (!std::getline(in, magic) || magic != kMagic || !std::getline(in, desc) || !std::getline(in, gameLine))
        return false;
    std::string state((std::istreambuf_iterator<char>(in)), {});
    if (!restoreState(baseline_) || !restoreState(state) || !room || !node) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "could not restore %s", legacySlotPath(slot).string().c_str());
        return false;
    }
    return true;
}
