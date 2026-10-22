// --explore: an automated player for checking that a campaign can be played
// through. It only does what a player can: walk by clicking navigation
// hotspots in the current view, click the other hotspots, use inventory items
// where the scripts define a reaction, and pick dialogue options. Actions are
// tried once per game state ("version": the state that counts as progress);
// when the state changes, everything is worth trying again. When nothing
// untried is left, it reports where it got stuck.
//
// Shortcuts (not player actions): when a zoom has no zoom-out hotspot the
// explorer leaves it as if one existed, and it does not turn the camera
// (hotspot clicks don't need the hotspot on screen).
//
// Puzzles whose answer a player reads from a clue (combination locks, phone
// numbers) can't be found by trying things. --explore-hints FILE supplies
// them; the file is kept out of the repository (it holds game content):
//
//   scratch NAME...       globals that aren't progress (what's been dialed so
//                         far); left out of the state so keypads aren't brute-forced
//   once VIEW/OBJECT...   hotspots clicked or used at most once per game: script
//                         quirks a player wouldn't repeat (grabbing an item
//                         the script lets you grab again)
//   ROOM/VIEW: LUA        a solution, tried like a hotspot click when VIEW is
//                         reachable (ROOM * matches any room): Lua run as a
//                         handler, normally setting up the input and calling
//                         the game's own handler
//
// Lines starting with # are comments.

#include <cstdio>
#include <fstream>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <SDL3/SDL.h>

#include "constants.hpp"
#include "engine.hpp"

struct Explorer {
    // One step on the way somewhere: click `object` of `view`, or use `item` on it.
    struct Step {
        View* view;
        Object* object;
        Item* item = nullptr;
    };
    std::map<size_t, std::set<std::string>> tried;  // version -> action keys
    std::set<size_t> versions;
    size_t version = 0;
    int actions = 0, frames = 0, idleFrames = 0;
    std::string lastAction;
    std::deque<Step> path;      // navigation still to do before `pending`
    std::string pendingKey;      // the action at the end of the path
    std::function<void()> pending;
    View* pendingView = nullptr; // where `pending` must run (null: anywhere)
    // Scripted doors and stairs (SC_CUSTOM handlers that only move the player,
    // on a click or an item used on the hotspot, like lighting the passage's
    // candelabra), learned by watching where an action leads.
    struct Place { Room* room; View* view; };
    using Door = std::pair<Object*, Item*>;  // item null: a click
    std::map<Door, Place> learned;
    Door watching{};             // last action's hotspot (and item)
    size_t watchVersion = 0;
    Place watchFrom{};
    Door stepped{};              // the learned door just taken on a path, if any
    Place stepFrom{};            // where it was taken from
    Place here{};                // set by exploreStep before relearn()
    void relearn() {
        if (stepped.first) {
            if (here.room == stepFrom.room && here.view == stepFrom.view) learned.erase(stepped);
            else learned[stepped] = here;
        }
        stepped = {};
    }
    std::map<std::string, std::string> lastGlobals;  // for logging what changed between states
    std::set<std::string> lastLines;

    int lastActionFrame = 0;
    // Backtracking: which action led from which state to which. A state
    // left with actions untried stays "open"; when nothing is left here, the
    // explorer replays actions (by key) towards the nearest open state.
    std::map<size_t, std::map<std::string, size_t>> edges;
    std::set<size_t> closed;      // states with nothing left to try
    bool edgePending = false;     // an action ran; record where it led
    std::string forced;           // replay this action even though it was tried
    std::set<std::string> ever;   // every action done at least once, in any state
    std::map<std::string, int> count;  // times each action was done, to report loops
    std::set<std::string> doorKeys;    // actions that turned out to be scripted doors
    struct Hint { std::string room, view, code; };
    std::vector<Hint> hints;
    std::set<std::string> scratch;  // globals left out of the state
    std::set<std::string> once, onceDone;  // "VIEW/OBJECT" 
    static constexpr int kMaxActions = 200000;
    static constexpr int kStallFrames = 100000;  // frames without a new action: looping somewhere
};

namespace {

bool isNavigation(int kind) {
    return kind == sc::NODE || kind == sc::ROOM || kind == sc::ZOOM || kind == sc::ZOOMOUT || kind == sc::ZOOMBACK;
}

}  // namespace

// Left out: location and navigation (last visited nodes), presentation
// (textures, effect flags such as SC_SKY, ambience) and audio. Progress lives
// in script globals, enabled hotspots, inventory, journal entries and choices.
const std::set<std::string> kSkip = {"loc",  "zoom", "cam",  "timer", "music", "loop", "jrnpos", "jrn", "font",
                                     "motion", "room", "face", "img",  "flag",  "amb"};

// Hash of the game state that counts as progress (see kSkip).
static size_t progressHash(const std::string& state, const std::set<std::string>& scratch) {
    std::istringstream in(state);
    std::string line, kept;

    while (std::getline(in, line)) {
        std::string tag = line.substr(0, line.find(' '));
        if (kSkip.count(tag)) continue;
        if (tag == "G" && scratch.count(line.substr(2, line.find(' ', 2) - 2))) continue;
        if (tag == "choice") {  // "choice NAME LASTSELECTED COUNT": which option was picked last isn't progress
            std::istringstream f(line);
            std::string name, last, count;
            f >> tag >> name >> last >> count;
            line = tag + " " + name + " " + count;
        }
        kept += line + "\n";
    }
    return std::hash<std::string>()(kept);
}

bool Engine::exploreStep() {
    if (!explorer_) {
        explorer_ = std::make_shared<Explorer>();
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
        script.setWatchdog(200);
        if (!opts.exploreHints.empty()) {
            std::ifstream in(opts.exploreHints);
            if (!in) std::printf("explore: can't read %s\n", opts.exploreHints.c_str());
            std::string line;
            while (std::getline(in, line)) {
                if (line.empty() || line[0] == '#') continue;
                if (line.rfind("once ", 0) == 0) {
                    std::istringstream names(line.substr(5));
                    for (std::string n; names >> n;) explorer_->once.insert(n);
                    continue;
                }
                if (line.rfind("scratch ", 0) == 0) {
                    std::istringstream names(line.substr(8));
                    for (std::string n; names >> n;) explorer_->scratch.insert(n);
                    continue;
                }
                size_t colon = line.find(':'), slash = line.find('/');
                if (colon == std::string::npos || slash > colon) {
                    std::printf("explore: bad hint line: %s\n", line.c_str());
                    continue;
                }
                explorer_->hints.push_back({line.substr(0, slash), line.substr(slash + 1, colon - slash - 1),
                                            line.substr(line.find_first_not_of(' ', colon + 1))});
            }
            std::printf("explore: %zu hints, %zu scratch globals\n", explorer_->hints.size(), explorer_->scratch.size());
        }
    }
    Explorer& x = *explorer_;
    ++x.frames;
    if (x.frames - x.lastActionFrame > Explorer::kStallFrames) {
        std::printf("explore: stalled (no new action in %d frames) after %d actions at %s/%s; last action %s\n",
                    Explorer::kStallFrames, x.actions, room ? room->name.c_str() : "?",
                    view() ? view()->name.c_str() : "?", x.lastAction.c_str());
        return false;
    }

    // Wait for scripts, camera moves and cutscenes to finish.
    if (script.busy() || camAnim_.active || video_ || menuPending_) {
        x.idleFrames = 0;
        return true;
    }
    if (aborted_) leaveAbortedGame();
    if (!gameRunning_ || menuOpen()) {
        std::printf("explore: the game ended (menu shown) after %d actions, %zu states\n", x.actions, x.versions.size());
        return false;
    }
    if (++x.idleFrames < 2) return true;  // let a frame pass so timers can start handlers
    x.here = {room, view()};

    // Dialogue choices: the first option not yet picked in this state.
    if (choice_) {
        x.version = progressHash(serializeState(), x.scratch);
        x.versions.insert(x.version);
        auto& done = x.tried[x.version];
        size_t pick = 0;
        for (size_t i = 0; i < choice_->options.size(); ++i)
            if (!done.count("choice:" + choice_->name + ":" + std::to_string(i))) { pick = i; break; }
        done.insert("choice:" + choice_->name + ":" + std::to_string(pick));
        // Options left: the action that opened the choice is worth doing again.
        for (size_t i = 0; i < choice_->options.size(); ++i)
            if (!done.count("choice:" + choice_->name + ":" + std::to_string(i))) { done.erase(x.lastAction); break; }
        std::string fn = choice_->options[pick].second;
        std::printf("  choice %s -> %s\n", choice_->name.c_str(), choice_->options[pick].first.c_str());
        choice_->lastSelected = int(pick);
        choice_ = nullptr;
        script.start(fn);
        return true;
    }
    if (invOpen_) closeInventory();
    if (journalOpen_) closeJournal();
    if (overlay) { overlay = nullptr; return true; }
    if (locked) {
        if (x.idleFrames > 200) {
            std::printf("explore: stuck locked (scLock with no script running) at %s/%s\n", room ? room->name.c_str() : "?",
                        view() ? view()->name.c_str() : "?");
            return false;
        }
        return true;
    }

    // Walking somewhere: one hotspot click per frame.
    if (!x.path.empty()) {
        Explorer::Step s = x.path.front();
        x.path.pop_front();
        bool holding = !s.item || std::find(inventory.begin(), inventory.end(), s.item) != inventory.end();
        if (s.view != view() || !s.object->enabled || !holding) {
            // Replan. A learned scripted door that now leads elsewhere is
            // updated; one that no longer moves the player is forgotten.
            x.relearn();
            x.path.clear();
            x.pending = nullptr;
            return true;
        }
        x.stepped = s.item || s.object->click.kind == sc::CUSTOM ? Explorer::Door{s.object, s.item} : Explorer::Door{};
        x.stepFrom = {room, view()};
        if (s.item) {
            held = s.item;
            useHeldOn(s.object);
        } else
            activate(s.object);
        return true;
    }
    if (x.pending && x.pendingView && x.pendingView != view()) {
        x.relearn();
        x.pending = nullptr;
        return true;
    }
    x.stepped = {};
    if (x.pending) {
        auto act = std::move(x.pending);
        x.pending = nullptr;
        x.tried[x.version].insert(x.pendingKey);
        x.watchVersion = x.version;
        x.edgePending = true;
        x.ever.insert(x.pendingKey);
        // Doors are taken again in every state; anything else done this often is
        // likely a script that keeps re-enabling it (e.g. an item that can be re-taken).
        if (++x.count[x.pendingKey] == 1000 && !x.doorKeys.count(x.pendingKey))
            std::printf("explore: looping? %s done 1000 times (a script that re-enables it?)\n", x.pendingKey.c_str());
        if (x.pendingKey == x.forced) x.forced.clear();
        x.watchFrom = {room, view()};
        ++x.actions;
        x.lastActionFrame = x.frames;
        x.lastAction = x.pendingKey;
        std::printf("[%d] %s\n", x.actions, x.pendingKey.c_str());
        act();
        if (x.actions >= Explorer::kMaxActions) {
            std::printf("explore: gave up after %d actions, %zu states\n", x.actions, x.versions.size());
            return false;
        }
        return true;
    }

    // Plan: the nearest untried action from here, by breadth-first search over
    // the views reachable through navigation hotspots.
    x.version = progressHash(serializeState(), x.scratch);
    if (x.edgePending && x.watchVersion != x.version) x.edges[x.watchVersion][x.lastAction] = x.version;
    x.edgePending = false;
    if (x.watching.first) {
        if (x.watchFrom.room != room || x.watchFrom.view != view()) {
            x.learned[x.watching] = {room, view()};
            x.doorKeys.insert(x.lastAction);
        }
        x.watching = {};
    }
    if (x.versions.insert(x.version).second) {
        std::printf("  state #%zu (%s/%s)", x.versions.size(), room ? room->name.c_str() : "?",
                    node ? node->name.c_str() : "?");
        std::istringstream in(serializeState());
        std::string line;
        std::map<std::string, std::string> now;
        while (std::getline(in, line))
            if (line.rfind("G ", 0) == 0) {
                size_t sp = line.find(' ', 2);
                now[line.substr(2, sp - 2)] = line.substr(sp + 1);
            }
        int shown = 0;
        for (auto& [k, v] : now)
            if (!x.lastGlobals.empty() && (!x.lastGlobals.count(k) || x.lastGlobals[k] != v) && shown++ < 8)
                std::printf(" %s=%s", k.c_str(), v.c_str());
        // Other changes (world, inventory, ...): the first few changed lines.
        std::set<std::string> lines;
        {
            std::istringstream all(serializeState());
            while (std::getline(all, line))
                if (line.rfind("G ", 0) != 0 && !kSkip.count(line.substr(0, line.find(' ')))) lines.insert(line);
        }
        for (auto& l : lines)
            if (!x.lastLines.empty() && !x.lastLines.count(l) && shown++ < 8) std::printf(" ~%s", l.substr(0, 60).c_str());
        x.lastLines = std::move(lines);
        std::printf("\n");
        x.lastGlobals = std::move(now);
    }
    auto& done = x.tried[x.version];
    auto tried = [&](const std::string& key) { return done.count(key) && key != x.forced; };

    // In a zoom: actions here first, else leave it.
    // `zooms`/`node`: the zoom stack and node a zoom-out returns through there.
    struct Visit { Room* room; View* view; std::vector<Explorer::Step> path; std::vector<View*> zooms; View* node; };
    std::deque<Visit> queue;
    std::set<View*> seen;
    std::vector<std::pair<Room*, View*>> reached;  // for the stuck report
    queue.push_back({room, view(), {}, zooms, node});
    seen.insert(view());
    // Objects can share a name within a view (two "hose" hotspots running
    // different handlers): those get their index.
    auto keyOf = [&](Room* r, View* v, Object* o, const std::string& item) {
        std::string name = o->name;
        int same = 0, index = 0;
        for (size_t i = 0; i < v->objects.size(); ++i) {
            const Object& other = *v->objects[i];
            if (other.name != o->name) continue;
            ++same;
            if (&other == o) index = int(i);
        }
        if (same > 1) name += "#" + std::to_string(index);
        return r->name + "/" + v->name + "/" + name + (item.empty() ? "" : " <- " + item);
    };
    // The nearest action never done anywhere wins; else the nearest one not
    // done in this state (toggles nearby would otherwise keep the explorer
    // redoing what's close and never reach what's new further away).
    struct Candidate {
        std::vector<Explorer::Step> path;
        std::string key;
        View* view;
        std::function<void()> act;
    };
    std::optional<Candidate> novel, fallback;
    auto offer = [&](const Visit* at, const std::string& key, std::function<void()> act) {
        if (tried(key)) return;
        Candidate c{at ? at->path : std::vector<Explorer::Step>{}, key, at ? at->view : nullptr, std::move(act)};
        if (!x.ever.count(key) && !novel) novel = c;
        if (!fallback) fallback = std::move(c);
    };
    while (!queue.empty() && !novel) {
        Visit cur = queue.front();
        queue.pop_front();
        reached.push_back({cur.room, cur.view});
        // Candidate actions in this view: hints first, then hotspots.
        for (size_t i = 0; i < x.hints.size(); ++i) {
            const Explorer::Hint& h = x.hints[i];
            if ((h.room != "*" && h.room != cur.room->name) || h.view != cur.view->name) continue;
            std::string key = "hint " + h.room + "/" + h.view + " #" + std::to_string(i);
            offer(&cur, key, [this, code = h.code] { script.startCode(code, "hint"); });
        }
        for (auto& op : cur.view->objects) {
            Object* o = op.get();
            if (!o->enabled) continue;
            if (!o->click.empty() && !isNavigation(o->click.kind) && o->click.kind != sc::FEEDBACK &&
                o->click.kind != sc::FEEDBACK_EX) {
                std::string key = keyOf(cur.room, cur.view, o, "");
                std::string once = cur.view->name + "/" + o->name;
                if (!x.onceDone.count(once))
                    offer(&cur, key, [this, o, once] {
                        if (explorer_->once.count(once)) explorer_->onceDone.insert(once);
                        explorer_->watching = {o, nullptr};
                        activate(o);
                    });
            }
            for (auto& re : o->reactions) {
                Item* it = world.items.count(re.item) ? world.items[re.item].get() : nullptr;
                if (!it || std::find(inventory.begin(), inventory.end(), it) == inventory.end()) continue;
                std::string once = cur.view->name + "/" + o->name;
                if (!x.onceDone.count(once))
                    offer(&cur, keyOf(cur.room, cur.view, o, re.item), [this, o, it, once] {
                        if (explorer_->once.count(once)) explorer_->onceDone.insert(once);
                        explorer_->watching = {o, it};
                        held = it;
                        useHeldOn(o);
                    });
            }
        }
        // Where we can go from here (only within reach of the current room's
        // views, plus room doors; the next plan continues from the new room).
        for (auto& op : cur.view->objects) {
            Object* o = op.get();
            if (!o->enabled || o->click.empty()) continue;
            View* to = nullptr;
            Room* toRoom = cur.room;
            std::vector<View*> stack;  // the zoom stack after the click
            View* toNode = cur.node;
            if (o->click.kind == sc::NODE) toNode = to = cur.room->node(o->click.arg);
            else if (o->click.kind == sc::ZOOM) {
                to = world.zoom(o->click.arg);  // global, as switchZoom
                stack = cur.zooms;
                stack.push_back(to);
            } else if (o->click.kind == sc::ZOOMOUT && !cur.zooms.empty()) {
                stack.assign(cur.zooms.begin(), cur.zooms.end() - 1);
                to = stack.empty() ? cur.node : stack.back();
            } else if (o->click.kind == sc::ZOOMBACK && !cur.zooms.empty())
                to = cur.node;
            else if (o->click.kind == sc::CUSTOM && x.learned.count({o, nullptr})) {
                toRoom = x.learned[{o, nullptr}].room;
                to = x.learned[{o, nullptr}].view;
                if (to && to->isZoom) {  // a scripted zoom-in, presumably from here
                    stack = cur.zooms;
                    stack.push_back(to);
                } else
                    toNode = to;
            } else if (o->click.kind == sc::ROOM) {
                toRoom = world.room(o->click.arg);
                if (toRoom && !toRoom->nodes.empty()) {
                    View* n = toRoom->lastVisited.empty() ? nullptr : toRoom->node(toRoom->lastVisited);
                    toNode = to = n ? n : toRoom->nodes.front().get();
                }
            }
            if (!to || seen.count(to)) continue;
            seen.insert(to);
            Visit next{toRoom, to, cur.path, std::move(stack), toNode};
            next.path.push_back({cur.view, o});
            queue.push_back(std::move(next));
        }
        // Learned moves made by using an item on a hotspot.
        for (auto& op : cur.view->objects) {
            Object* o = op.get();
            if (!o->enabled) continue;
            for (auto& re : o->reactions) {
                Item* it = world.items.count(re.item) ? world.items[re.item].get() : nullptr;
                auto door = x.learned.find({o, it});
                if (!it || door == x.learned.end() ||
                    std::find(inventory.begin(), inventory.end(), it) == inventory.end())
                    continue;
                View* to = door->second.view;
                if (!to || seen.count(to)) continue;
                seen.insert(to);
                std::vector<View*> stack;
                if (to->isZoom) {
                    stack = cur.zooms;
                    stack.push_back(to);
                }
                Visit next{door->second.room, to, cur.path, std::move(stack), to->isZoom ? cur.node : to};
                next.path.push_back({cur.view, o, it});
                queue.push_back(std::move(next));
            }
        }
    }
    // Item on item (inventory reactions) need no walking.
    for (Item* target : inventory)
        for (auto& re : target->reactions) {
            Item* tool = world.items.count(re.item) ? world.items[re.item].get() : nullptr;
            if (!tool || std::find(inventory.begin(), inventory.end(), tool) == inventory.end()) continue;
            offer(nullptr, "item " + re.item + " -> " + target->name,
                  [this, target, re] { world.bound.item = target; perform(re.action); });
        }
    if (novel || fallback) {
        Candidate& c = novel ? *novel : *fallback;
        x.path.assign(c.path.begin(), c.path.end());
        x.pendingKey = c.key;
        x.pendingView = c.view;
        x.pending = std::move(c.act);
        return true;
    }
    // A zoom with no way out: leave it as if it had a zoom-out hotspot.
    if (!zooms.empty() && !done.count("leave " + view()->name)) {
        done.insert("leave " + view()->name);
        zoomOut();
        return true;
    }
    // Nothing left in this state: head back to the nearest open one.
    x.closed.insert(x.version);
    if (!x.forced.empty()) {
        // That way back isn't available from here any more: forget it, try another.
        std::printf("  backtrack: %s not reachable\n", x.forced.c_str());
        x.edges[x.version].erase(x.forced);
        x.forced.clear();
    }
    {
        std::map<size_t, std::string> first;  // state -> first action on the way there
        std::deque<size_t> open{x.version};
        first[x.version] = "";
        while (!open.empty()) {
            size_t v = open.front();
            open.pop_front();
            if (!x.closed.count(v)) {
                x.forced = first[v];
                std::printf("  backtrack via %s\n", x.forced.c_str());
                return true;
            }
            for (auto& [key, to] : x.edges[v])
                if (!first.count(to)) {
                    first[to] = first[v].empty() ? key : first[v];
                    open.push_back(to);
                }
        }
    }
    std::printf("explore: stuck after %d actions, %zu states, at %s/%s; last action %s\n", x.actions,
                x.versions.size(), room ? room->name.c_str() : "?", view() ? view()->name.c_str() : "?",
                x.lastAction.c_str());
    std::printf("  reachable:");
    for (auto& [r, v] : reached) std::printf(" %s/%s", r->name.c_str(), v->name.c_str());
    std::printf("\n  hotspots here:");
    for (auto& o : view()->objects)
        if (o->enabled) std::printf(" %s(%d:%s)", o->name.c_str(), o->click.kind, o->click.arg.c_str());
    std::printf("\n  inventory:");
    for (Item* it : inventory) std::printf(" %s", it->name.c_str());
    std::printf("\n  globals:");
    for (auto& [k, v] : x.lastGlobals) std::printf(" %s=%s", k.c_str(), v.c_str());
    std::printf("\n");
    return false;
}
