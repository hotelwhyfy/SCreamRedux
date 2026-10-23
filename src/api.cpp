// The sc* scripting API: all 87 functions registered by the original scream.exe.
//
// Argument handling mirrors what the shipped scripts rely on: arguments are
// read loosely, and nil (e.g. the misspelled SC_HITNS or the undefined
// SC_STEREO_PATCH) is accepted rather than raising an error.

#include "api.hpp"

#include <SDL3/SDL_log.h>

#include <memory>
#include <string>

#include "constants.hpp"
#include "engine.hpp"

namespace {

Engine* E = nullptr;

// Wait mode (0x412590 / 0x4125b0) only takes effect when SC_WAIT is enabled.
void setWaitMode(bool on) { E->waitMode = on && E->flags.count(sc::WAIT); }

int argInt(lua_State* L, int i, int def = 0) { return lua_isnumber(L, i) ? int(lua_tonumber(L, i)) : def; }
double argNum(lua_State* L, int i, double def = 0) { return lua_isnumber(L, i) ? lua_tonumber(L, i) : def; }
std::string argStr(lua_State* L, int i) {
    size_t n;
    const char* s = lua_isstring(L, i) ? lua_tolstring(L, i, &n) : nullptr;
    return s ? std::string(s, n) : std::string();
}

std::string describeArgs(lua_State* L) {
    std::string out;
    for (int i = 1, n = lua_gettop(L); i <= n; ++i) {
        if (i > 1) out += ", ";
        switch (lua_type(L, i)) {
        case LUA_TNUMBER: out += std::to_string(lua_tonumber(L, i)); break;
        case LUA_TSTRING: out += '"' + argStr(L, i).substr(0, 60) + '"'; break;
        case LUA_TNIL: out += "nil"; break;
        default: out += lua_typename(L, lua_type(L, i));
        }
    }
    return out;
}

void warn(const char* fn, const std::string& what) {
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s: %s", fn, what.c_str());
}

// Parse (kind, arg[, arg2]) as used by scOnClick, scReaction and friends.
Action readAction(lua_State* L, int first) {
    Action a;
    a.kind = argInt(L, first);
    a.arg = argStr(L, first + 1);
    a.arg2 = argStr(L, first + 2);
    return a;
}

// ---------------------------------------------------------------- engine / settings

int scInit(lua_State* L) { E->init(); return 0; }
int scRun(lua_State* L) { E->run(); return 0; }
int scTerminate(lua_State* L) { return 0; }

int scSet(lua_State* L) {
    int key = argInt(L, 1);
    if (lua_type(L, 2) == LUA_TSTRING) E->settings[key] = argStr(L, 2);
    else E->settings[key] = argNum(L, 2);
    // The speeds take SC_LO/SC_MED/SC_HI as 25/50/75, or a value (0x40fde0).
    if (key == sc::TXT_SPEED || key == sc::CAM_SPEED) {
        double v = argNum(L, 2);
        if (v == sc::LO) E->settings[key] = 25.0;
        else if (v == sc::MED) E->settings[key] = 50.0;
        else if (v == sc::HI) E->settings[key] = 75.0;
    }
    return 0;
}

// scGet (0x405330): strings for SC_LANGUAGE and the current room and node
// (0x4125d0); numbers for SC_TXT_SPEED, SC_MOV_SPEED and SC_HINTS (0x412610);
// 0 for every other key, SC_ZOOM and the other settings included.
int scGet(lua_State* L) {
    int key = argInt(L, 1);
    auto number = [&](int k) {
        auto it = E->settings.find(k);
        auto* d = it == E->settings.end() ? nullptr : std::get_if<double>(&it->second);
        return d ? *d : 0.0;
    };
    switch (key) {
    case sc::ROOM: lua_pushstring(L, E->room ? E->room->name.c_str() : ""); return 1;
    case sc::NODE: lua_pushstring(L, E->node ? E->node->name.c_str() : ""); return 1;
    case sc::LANGUAGE: {
        auto it = E->settings.find(key);
        auto* s = it == E->settings.end() ? nullptr : std::get_if<std::string>(&it->second);
        lua_pushstring(L, s ? s->c_str() : "");
        return 1;
    }
    case sc::TXT_SPEED: case sc::MOV_SPEED: case sc::HINTS: lua_pushnumber(L, number(key)); return 1;
    default:
        if (E->release2006) return 0;  // the 2006 engine returns nothing for other keys (P 0x410380)
        lua_pushnumber(L, 0);
        return 1;
    }
}

// scEnable/scDisable(SC_OBJECT) toggle the bound object (only that one: 0x40fa60
// sets +0x34 of the single bound object); other keys are
// effect flags (SC_SKY, SC_RAIN, SC_CANDLE, SC_WAIT, ...).
int setEnabled(lua_State* L, bool on) {
    int key = argInt(L, 1);
    if (key == sc::OBJECT) {
        if (Object* o = E->world.bound.object) o->enabled = on;
        else warn(on ? "scEnable" : "scDisable", "target not bound (fatal in scream.exe)");
        return 0;
    }
    if (on) E->flags.insert(key);
    else E->flags.erase(key);
    // These keys share their variable with the setting of the same name (0x40fa60).
    if (key == sc::HINTS || key == sc::SHOW_HOTSPOTS || key == sc::SURROUND || key == sc::FULLSCREEN ||
        key == sc::DEBUG)
        E->settings[key] = on ? 1.0 : 0.0;
    if (key == sc::LOG)
        E->setLogging(on);
    return 0;
}
int scEnable(lua_State* L) { return setEnabled(L, true); }
int scDisable(lua_State* L) { return setEnabled(L, false); }

// scream.cfg's parser (0x40f4a0); the mode keys only matter before scInit.
int scLoadSettings(lua_State* L) { E->loadSettings(argStr(L, 1)); return 0; }

// Presets for SC_SKY_COLOR / SC_CLOUD_COLOR / SC_SCREEN_COLOR; a third argument of 1
// switches at once, 0 fades slowly (0x40ca50).
int scSetAmbience(lua_State* L) {
    E->ambience[argInt(L, 1)] = {argInt(L, 2), argInt(L, 3)};
    if (argInt(L, 3) != 0) E->snapAmbience(argInt(L, 1));
    return 0;
}

// ---------------------------------------------------------------- world building

int scCreate(lua_State* L) {
    int kind = argInt(L, 1);
    std::string name = argStr(L, 2);
    if (!E->world.create(kind, name)) warn("scCreate", "no parent scope for " + name);
    return 0;
}

int scBind(lua_State* L) {
    int kind = argInt(L, 1);
    std::string name = argStr(L, 2);
    if (!E->world.bind(kind, name)) warn("scBind", "nothing named " + name + " (kind " + std::to_string(kind) + ")");
    return 0;
}

int scBegin(lua_State* L) { E->world.begin(argInt(L, 1)); return 0; }
int scEnd(lua_State* L) { E->world.end(argInt(L, 1)); return 0; }

int scTexture(lua_State* L) {
    int face = argInt(L, 1);
    std::string file = argStr(L, 2);
    auto& b = E->world.bound;
    if (face == sc::ZOOM && b.zoom) b.zoom->image = file;
    else if (face >= sc::FRONT && face <= sc::DOWN && b.node) b.node->faces[face - 1] = file;
    else warn("scTexture", "nothing bound for " + file);
    return 0;
}

// Third argument: 1 when the change is visible right now. Textures load
// lazily, so the new name simply takes effect on the next frame.
int scTextureUpdate(lua_State* L) { return scTexture(L); }

int scPlace(lua_State* L) {
    Rect r{argInt(L, 1), argInt(L, 2), argInt(L, 3), argInt(L, 4)};
    int face = argInt(L, 5);
    if (Object* o = E->world.bound.object) {
        o->rect = r;
        o->face = face;
    }
    return 0;
}

int scOnHover(lua_State* L) {
    if (Object* o = E->world.bound.object) o->cursor = argInt(L, 1);
    return 0;
}

int scOnClick(lua_State* L) {
    Action a = readAction(L, 1);
    if (Object* o = E->world.bound.object) o->click = a;
    return 0;
}

int scReaction(lua_State* L) {
    Reaction r{argStr(L, 1), readAction(L, 2)};
    if (Object* o = E->world.bound.object) o->reactions.push_back(r);
    return 0;
}

int scFootstep(lua_State* L) {
    if (E->world.bound.node) E->world.bound.node->footstep = argStr(L, 1);
    return 0;
}

int scAudio(lua_State* L) {
    if (E->world.bound.zoom) E->world.bound.zoom->audio.emplace_back(argInt(L, 1), argStr(L, 2));
    return 0;
}

int scSetLastVisited(lua_State* L) {
    if (E->world.bound.room) E->world.bound.room->lastVisited = argStr(L, 1);
    return 0;
}

// ---------------------------------------------------------------- movement / view

int scSwitch(lua_State* L) {
    int kind = argInt(L, 1);
    std::string name = argStr(L, 2);
    switch (kind) {
    case sc::ROOM: E->switchRoom(name); break;
    case sc::NODE: E->switchNode(name); break;
    case sc::ZOOM: E->switchZoom(name); break;
    case sc::OVERLAY: E->switchOverlay(name); break;
    case sc::ZOOMOUT: E->zoomOut(); break;
    case sc::ZOOMBACK: E->zoomBack(); break;
    default: warn("scSwitch", "unknown kind " + std::to_string(kind));
    }
    return 0;
}

int scLookAt(lua_State* L) {
    bool smooth = argInt(L, 2) == sc::SMOOTH;
    E->lookAt(argInt(L, 1), smooth);
    if (!smooth) return 0;
    return E->script.wait(L, [] { return !E->cameraMoving(); });
}

// As in scream.exe (0x4124a0): camera input is off while sleeping and wait
// mode is on; afterwards motion is re-enabled unconditionally.
// scSleep (0x4124a0): the camera is held (the 2006 engine leaves it free,
// P 0x410230) and the wait cursor shows. In the DC, Esc ends the sleep and
// stops every playing channel (0x419b80), which skips the logo sequences.
int scSleep(lua_State* L) {
    double until = E->now() + argNum(L, 1);
    if (!E->release2006) E->motion = false;
    setWaitMode(true);
    ++E->sleeping_;
    return E->script.wait(L, [until] {
        const bool skipped = E->sleepSkip_;
        if (!skipped && E->now() < until) return false;
        if (skipped) E->sound.stopChannels();
        if (--E->sleeping_ == 0) E->sleepSkip_ = false;
        E->motion = true;
        setWaitMode(false);
        return true;
    });
}

int scLock(lua_State* L) { E->locked = true; return 0; }
int scUnlock(lua_State* L) { E->locked = false; return 0; }
int scWait(lua_State* L) { setWaitMode(true); return 0; }
int scGo(lua_State* L) { setWaitMode(false); return 0; }
int scMotion(lua_State* L) { E->motion = true; return 0; }
int scStill(lua_State* L) { E->motion = false; return 0; }
int scAbort(lua_State* L) { E->abortGame(); return 0; }

// Room hooks (0x415f60 / 0x415fa0) on the bound room. scream.exe calls the
// enter hook after scSwitch(SC_ROOM); nothing ever calls the exit hook.
int scOnEnter(lua_State* L) { if (Room* r = E->world.bound.room) r->onEnter = argStr(L, 1); return 0; }
int scOnExit(lua_State* L) { if (Room* r = E->world.bound.room) r->onExit = argStr(L, 1); return 0; }

int scOverlay(lua_State* L) {
    if (Overlay* o = E->world.bound.overlay) o->args = {argStr(L, 1), argStr(L, 2), argStr(L, 3)};
    return 0;
}

// No-ops in scream.exe too: scAttach reads its arguments and calls an empty
// function (0x414790); the scMusic* functions just return.
int scAttach(lua_State* L) { return 0; }
int scMusicPlay(lua_State* L) { return 0; }
int scMusicStop(lua_State* L) { return 0; }
int scMusicFadeIn(lua_State* L) { return 0; }
int scMusicFadeOut(lua_State* L) { return 0; }

int scCursor(lua_State* L) { E->cursors[argInt(L, 1)] = argStr(L, 2); return 0; }
int scFont(lua_State* L) { E->font = argStr(L, 1); return 0; }

// ---------------------------------------------------------------- feedback / text

int scFeedback(lua_State* L) { E->feedback(argStr(L, 1)); return 0; }
int scFeedbackEx(lua_State* L) { E->feedback(argStr(L, 1), argStr(L, 2)); return 0; }

int scLoadSubtitle(lua_State* L) { E->loadSubtitles(argStr(L, 1)); return 0; }
int scUnloadSubtitle(lua_State* L) { E->unloadSubtitles(); return 0; }

// ---------------------------------------------------------------- inventory

int scInventorySet(lua_State* L) {
    if (Item* it = E->world.bound.item) {
        it->icon = argStr(L, 1);
        it->label = argStr(L, 2);
        it->text = argStr(L, 3);
        it->voice = argStr(L, 4);
    }
    return 0;
}

int scInventoryReaction(lua_State* L) {
    if (Item* it = E->world.bound.item) it->reactions.push_back({argStr(L, 1), readAction(L, 2)});
    return 0;
}

int scInventoryAdd(lua_State* L) {
    Item* it = E->world.bound.item;
    if (it && std::find(E->inventory.begin(), E->inventory.end(), it) == E->inventory.end())
        E->inventory.push_back(it);
    return 0;
}

int scInventoryRemove(lua_State* L) {
    std::erase(E->inventory, E->world.bound.item);
    if (E->held == E->world.bound.item) E->held = nullptr;
    return 0;
}

// ---------------------------------------------------------------- journal / hints / menu

int scJournalAdd(lua_State* L) { E->journalAdd(argInt(L, 1), argStr(L, 2)); return 0; }
int scJournalClear(lua_State* L) { E->journalClear(); return 0; }
int scJournalSet(lua_State* L) {
    E->journalConfig = {argStr(L, 1), argStr(L, 2), argStr(L, 3), argStr(L, 4), argStr(L, 5), E->world.bound.item};
    return 0;
}
int scJournalShow(lua_State* L) { E->openJournal(); return 0; }

int scHintsSet(lua_State* L) { E->hints[argInt(L, 1)] = argStr(L, 2); return 0; }
int scHints(lua_State* L) { E->hintState = argInt(L, 1); return 0; }

int scMenuSet(lua_State* L) {
    E->menu.images.clear();
    for (int i = 1; i <= 7; ++i) E->menu.images.push_back(argStr(L, i));
    E->menu.newGame.clear();
    for (int i = 8, n = lua_gettop(L); i <= n; ++i) E->menu.newGame.push_back(argStr(L, i));
    return 0;
}
int scMenuSetEx(lua_State* L) { E->menu.extra[argInt(L, 1)] = argStr(L, 2); return 0; }
int scMenuSetSaveFunc(lua_State* L) { E->menu.saveFunc = true; return 0; }
int scMenuShow(lua_State* L) { E->openMenu(); return 0; }

// ---------------------------------------------------------------- dialogue

int scScriptAdd(lua_State* L) {
    if (Dialogue* d = E->world.bound.dialogue) d->lines.push_back({argStr(L, 1), argStr(L, 2), argInt(L, 3)});
    return 0;
}

// As 0x4156a0: the music is turned down by 0.25 for the whole dialogue; each
// line plays its voice and shows its text, and the next line starts when the
// voice ends. Lines added with flag 1 are hints, played only while SC_HINTS is on.
int scScriptGo(lua_State* L) {
    auto it = E->world.dialogues.find(argStr(L, 1));
    if (it == E->world.dialogues.end()) { warn("scScriptGo", "no script " + argStr(L, 1)); return 0; }
    const Dialogue* d = it->second.get();
    E->sound.duckMusic(-0.25f);
    auto next = std::make_shared<size_t>(0);
    auto finished = [d, next] {
        if (E->sound.voicePlaying()) return false;
        while (*next < d->lines.size()) {
            const DialogueLine& line = d->lines[(*next)++];
            if (line.flag != 0 && !E->hintsOn()) continue;
            E->feedback(line.text, line.voice);
            return false;
        }
        E->sound.duckMusic(0.25f);
        E->feedback("");
        return true;
    };
    if (finished()) return 0;
    return E->script.wait(L, finished);
}

int scChoiceAdd(lua_State* L) {
    Choice* c = E->world.bound.choice;
    if (c && c->options.size() < 16)  // the original holds 16 options (0x403e30)
        c->options.emplace_back(argStr(L, 1), argStr(L, 2));
    return 0;
}
int scChoiceGo(lua_State* L) { E->showChoice(argStr(L, 1)); return 0; }
int scChoiceClear(lua_State* L) { if (Choice* c = E->world.bound.choice) c->options.clear(); return 0; }
// Removes the option the player picked last (0x403ec0), so asked questions disappear.
int scChoiceRemove(lua_State* L) {
    Choice* c = E->world.bound.choice;
    if (c && c->lastSelected >= 0 && c->lastSelected < int(c->options.size())) {
        c->options.erase(c->options.begin() + c->lastSelected);
        c->lastSelected = -1;
    }
    return 0;
}

// ---------------------------------------------------------------- timers

int scTimerCreate(lua_State* L) { E->addTimer(argInt(L, 1), argInt(L, 2), argStr(L, 3), false); return 0; }
int scTimerCreateGlobal(lua_State* L) { E->addTimer(argInt(L, 1), argInt(L, 2), argStr(L, 3), true); return 0; }

// ---------------------------------------------------------------- tracks (music)

int scTrackSet(lua_State* L) {
    if (Track* t = E->world.bound.track) { t->files = {argStr(L, 1), "", ""}; t->surround = false; t->loop = argInt(L, 2); }
    return 0;
}
int scTrackSet3D(lua_State* L) {
    if (Track* t = E->world.bound.track) { t->files = {argStr(L, 1), argStr(L, 2), argStr(L, 3)}; t->surround = true; t->loop = argInt(L, 4); }
    return 0;
}
int scTrackSetEx(lua_State* L) {
    if (Track* t = E->world.bound.track) {
        int v = argInt(L, 2);
        switch (argInt(L, 1)) {
        case sc::VOLUME: t->volume = v; break;
        case sc::FADEIN: t->fadeIn = v; break;
        case sc::FADEOUT: t->fadeOut = v; break;
        }
    }
    return 0;
}
int scTrackPlay(lua_State* L) {
    auto it = E->world.tracks.find(argStr(L, 1));
    if (it == E->world.tracks.end()) { warn("scTrackPlay", "no track " + argStr(L, 1)); return 0; }
    E->sound.playTrack(*it->second);
    return 0;
}
int scTrackStop(lua_State*) { E->sound.stopTrack(); return 0; }
int scTrackFadeOut(lua_State* L) { E->sound.fadeOutTrack(argInt(L, 1)); return 0; }

// ---------------------------------------------------------------- sound effects

// scFXPlayEx(channel, file, wait, loop). The channel may be nil (SC_STEREO_PATCH
// is never defined), which then simply acts as channel 0.
int scFXPlayEx(lua_State* L) {
    int channel = argInt(L, 1);
    E->sound.playChannel(channel, argStr(L, 2), argInt(L, 4) != 0);
    if (argInt(L, 3) == 0) return 0;
    return E->script.wait(L, [channel] { return !E->sound.channelPlaying(channel); });
}

// Two calls in the shipped script pass a channel first, scFXPlayEx-style.
int scFXPlay(lua_State* L) {
    if (lua_type(L, 1) == LUA_TNUMBER) return scFXPlayEx(L);
    E->sound.playFx(argStr(L, 1));
    return 0;
}

int scFXSet(lua_State* L) { E->sound.setChannel(argInt(L, 1), argInt(L, 2), argNum(L, 3)); return 0; }
int scFXStop(lua_State* L) { E->sound.stopChannel(argInt(L, 1)); return 0; }

// ---------------------------------------------------------------- cutscenes

int scCutscene(lua_State* L) {
    if (!E->playCutscene(argStr(L, 1))) return 0;
    return E->script.wait(L, [] { return !E->cutscenePlaying(); });
}

// ---------------------------------------------------------------- everything below is not implemented yet

int scPlaylistClear(lua_State* L) { E->sound.playlistClear(); return 0; }
int scPlaylistQueue(lua_State* L) {
    auto it = E->world.tracks.find(argStr(L, 1));
    if (it == E->world.tracks.end()) { warn("scPlaylistQueue", "no track " + argStr(L, 1)); return 0; }
    E->sound.playlistQueue(it->second.get());
    return 0;
}
int scPlaylistStart(lua_State* L) { E->sound.playlistStart(); return 0; }
int scPlaylistStop(lua_State* L) {
    const int f = argInt(L, 1);  // nil reads as 0
    E->sound.playlistStop(f, f < 0);
    return 0;
}
int scPlaylistNext(lua_State* L) { E->sound.playlistNext(); return 0; }
int scPlaylistPrevious(lua_State* L) { E->sound.playlistPrevious(); return 0; }
int scPlaylistFirst(lua_State* L) { E->sound.playlistFirst(); return 0; }
int scPlaylistLast(lua_State* L) { E->sound.playlistLast(); return 0; }

// ---------------------------------------------------------------- registry

struct Entry {
    api::Info info;
    lua_CFunction fn;
};

using S = api::Status;
#define FN(name, status, note) {{#name, status, note}, name}

const Entry kEntries[] = {
    FN(scAbort, S::Done, "ends the game after input (main menu not built yet)"),
    FN(scAttach, S::Done, "no-op in scream.exe as well"),
    FN(scAudio, S::Done, "played on zoom enter/leave"),
    FN(scBegin, S::Done, ""),
    FN(scBind, S::Done, "binds every same-named object"),
    FN(scChoiceAdd, S::Done, ""),
    FN(scChoiceClear, S::Done, ""),
    FN(scChoiceGo, S::Done, "non-blocking; selection starts the callback"),
    FN(scChoiceRemove, S::Done, "removes the last-picked option"),
    FN(scCreate, S::Done, ""),
    FN(scCursor, S::Done, ""),
    FN(scCutscene, S::Done, "click/Esc/Space skips"),
    FN(scDisable, S::Done, "objects, sky/fog/rain/night/candle/lamp/dream; SC_GLOW unused in the original too"),
    FN(scEnable, S::Done, "objects, sky/fog/rain/night/candle/lamp/dream; SC_GLOW unused in the original too"),
    FN(scEnd, S::Done, ""),
    FN(scFXPlay, S::Done, "string form or scFXPlayEx-style"),
    FN(scFXPlayEx, S::Done, "arg3 = wait, arg4 = loop"),
    FN(scFXSet, S::Done, "volume 0..10; fades don't stop"),
    FN(scFXStop, S::Done, ""),
    FN(scFeedback, S::Done, ""),
    FN(scFeedbackEx, S::Done, ""),
    FN(scFont, S::Done, ""),
    FN(scFootstep, S::Done, "played when walking to the node"),
    FN(scGet, S::Done, "room/node/language strings; text speed, move speed, hints; 0 otherwise"),
    FN(scGo, S::Done, "leaves wait mode"),
    FN(scHints, S::Done, "inventory tutorial hints"),
    FN(scHintsSet, S::Done, ""),
    FN(scInit, S::Done, ""),
    FN(scInventoryAdd, S::Done, ""),
    FN(scInventoryReaction, S::Done, ""),
    FN(scInventoryRemove, S::Done, ""),
    FN(scInventorySet, S::Done, ""),
    FN(scJournalAdd, S::Done, ""),
    FN(scJournalClear, S::Done, ""),
    FN(scJournalSet, S::Done, "book opens from the bound item"),
    FN(scJournalShow, S::Done, ""),
    FN(scLoadSettings, S::Done, "scream.cfg parser; mode keys apply at scInit"),
    FN(scLoadSubtitle, S::Done, ".srt cues timed as 0x4180b0"),
    FN(scLock, S::Done, "boolean, as in scream.exe"),
    FN(scLookAt, S::Done, ""),
    FN(scMenuSet, S::Done, ""),
    FN(scMenuSetEx, S::Done, "intro/outro handlers, music, background"),
    FN(scMenuSetSaveFunc, S::Done, "save() names the slot; save format is our own"),
    FN(scMenuShow, S::Done, ""),
    FN(scMotion, S::Done, ""),
    FN(scMusicFadeIn, S::Done, "no-op in scream.exe as well"),
    FN(scMusicFadeOut, S::Done, "no-op in scream.exe as well"),
    FN(scMusicPlay, S::Done, "no-op in scream.exe as well"),
    FN(scMusicStop, S::Done, "no-op in scream.exe as well"),
    FN(scOnClick, S::Done, ""),
    FN(scOnEnter, S::Done, "runs after entering the room"),
    FN(scOnExit, S::Done, "stored; scream.exe never calls it either"),
    FN(scOnHover, S::Done, ""),
    FN(scOverlay, S::Done, "image, enter sound, leave sound (0x412a00)"),
    FN(scPlace, S::Done, "rects clipped to the face"),
    FN(scPlaylistClear, S::Done, ""),
    FN(scPlaylistFirst, S::Done, ""),
    FN(scPlaylistLast, S::Done, ""),
    FN(scPlaylistNext, S::Done, ""),
    FN(scPlaylistPrevious, S::Done, ""),
    FN(scPlaylistQueue, S::Done, ""),
    FN(scPlaylistStart, S::Done, ""),
    FN(scPlaylistStop, S::Done, "fade F = F seconds from full volume (audio tick taken as 2.5 ms)"),
    FN(scReaction, S::Done, ""),
    FN(scRun, S::Done, "main loop: intro, menu, game"),
    FN(scScriptAdd, S::Done, ""),
    FN(scScriptGo, S::Done, ""),
    FN(scSet, S::Done, ""),
    FN(scSetAmbience, S::Done, "sky, cloud and screen colours; the flag sets that colour at once"),
    FN(scSetLastVisited, S::Done, ""),
    FN(scSleep, S::Done, "freezes camera input while sleeping"),
    FN(scStill, S::Done, ""),
    FN(scSwitch, S::Done, "ZOOM pushes, ZOOMOUT pops one, ZOOMBACK clears"),
    FN(scTerminate, S::Done, ""),
    FN(scTexture, S::Done, ""),
    FN(scTextureUpdate, S::Done, ""),
    FN(scTimerCreate, S::Done, ""),
    FN(scTimerCreateGlobal, S::Done, ""),
    FN(scTrackFadeOut, S::Done, ""),
    FN(scTrackPlay, S::Done, ""),
    FN(scTrackSet, S::Done, ""),
    FN(scTrackSet3D, S::Done, ""),
    FN(scTrackSetEx, S::Done, ""),
    FN(scTrackStop, S::Done, ""),
    FN(scUnloadSubtitle, S::Done, ""),
    FN(scUnlock, S::Done, "boolean, as in scream.exe"),
    FN(scWait, S::Done, "wait cursor while locked, if SC_WAIT enabled"),
};
#undef FN

constexpr int kCount = int(sizeof kEntries / sizeof kEntries[0]);
static_assert(kCount == 87, "scream.exe registers 87 sc* functions");

// Every call goes through here so --trace can log it and stubs report themselves once.
int dispatch(lua_State* L) {
    const Entry& e = kEntries[int(lua_tointeger(L, lua_upvalueindex(1)))];
    static bool reported[kCount] = {};
    int idx = int(&e - kEntries);
    if (E->opts.trace)
        SDL_Log("[sc] %s(%s)", e.info.name, describeArgs(L).c_str());
    else if (e.info.status == S::Stub && !reported[idx]) {
        reported[idx] = true;
        SDL_Log("[stub] %s(%s) not implemented yet", e.info.name, describeArgs(L).c_str());
    }
    return e.fn(L);
}

// Debug helper for --exec, not part of the original API.
int debugClick(lua_State* L) {
    std::string name = argStr(L, 1);
    Object* o = E->findInView(name);
    if (!o) {
        warn("debugClick", "no enabled object " + name + " in current view");
        return 0;
    }
    if (E->held) E->debugUseHeld(o);
    else E->activate(o);
    return 0;
}

}  // namespace

void api::registerAll(Engine& engine) {
    E = &engine;
    lua_State* L = engine.script.state();
    for (int i = 0; i < kCount; ++i) {
        lua_pushinteger(L, i);
        lua_pushcclosure(L, dispatch, 1);
        lua_setfield(L, LUA_GLOBALSINDEX, kEntries[i].info.name);
    }
    lua_register(L, "debugClick", debugClick);
    lua_register(L, "debugMouse", [](lua_State* L) { E->debugMouse(float(argNum(L, 1)), float(argNum(L, 2))); return 0; });
    lua_register(L, "debugPress", [](lua_State* L) { E->debugPress(argInt(L, 1)); return 0; });
    lua_register(L, "debugMenu", [](lua_State* L) { E->debugMenu(argInt(L, 1)); return 0; });
    lua_register(L, "debugKey", [](lua_State* L) { E->hotkey(argInt(L, 1)); return 0; });
}

const api::Info* api::table(int& count) {
    static api::Info infos[kCount];
    for (int i = 0; i < kCount; ++i) infos[i] = kEntries[i].info;
    count = kCount;
    return infos;
}
