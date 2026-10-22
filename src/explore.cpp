// --explore: an automated player for checking that a campaign can be played
// through. It only does what a player can: walk by clicking navigation
// hotspots in the current view, click the other hotspots, use inventory items
// where the scripts define a reaction, and pick dialogue options. Actions are
// tried once per game state ("version": everything but location, camera,
// timers and audio); when the state changes, everything is worth trying again.
// When nothing untried is left, it reports where it got stuck.
//
// Shortcuts (not player actions): when a zoom has no zoom-out hotspot the
// explorer leaves it as if one existed, and it does not turn the camera
// (hotspot clicks don't need the hotspot on screen).

#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <SDL3/SDL.h>

#include "constants.hpp"
#include "engine.hpp"

struct Explorer {
    // One step on the way somewhere: click `object` of `view` in `room`.
    struct Step {
        View* view;
        Object* object;
    };
    std::map<size_t, std::set<std::string>> tried;  // version -> action keys
    std::set<size_t> versions;
    size_t version = 0;
    int actions = 0, frames = 0, idleFrames = 0;
    std::string lastAction;
    std::deque<Step> path;      // navigation still to do before `pending`
    std::string pendingKey;      // the action at the end of the path
    std::function<void()> pending;
    // Scripted doors and stairs (SC_CUSTOM handlers that only move the player),
    // learned by watching where an action leads when the state doesn't change.
    struct Place { Room* room; View* view; };
    std::map<Object*, Place> learned;
    Object* watching = nullptr;  // last action's hotspot, if a click
    size_t watchVersion = 0;
    Place watchFrom{};
    std::map<std::string, std::string> lastGlobals;  // for logging what changed between states

    static constexpr int kMaxActions = 200000;
};

namespace {

bool isNavigation(int kind) {
    return kind == sc::NODE || kind == sc::ROOM || kind == sc::ZOOM || kind == sc::ZOOMOUT || kind == sc::ZOOMBACK;
}

}  // namespace

// Hash of the game state without location, camera, timers and audio.
static size_t progressHash(const std::string& state) {
    std::istringstream in(state);
    std::string line, kept;
    static const std::set<std::string> skip = {"loc", "zoom", "cam", "timer", "music", "loop", "jrnpos", "jrn",
                                               "font", "motion"};
    while (std::getline(in, line)) {
        std::string tag = line.substr(0, line.find(' '));
        if (!skip.count(tag)) kept += line + "\n";
    }
    return std::hash<std::string>()(kept);
}

bool Engine::exploreStep() {
    if (!explorer_) {
        explorer_ = std::make_shared<Explorer>();
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
        script.setWatchdog(200);
    }
    Explorer& x = *explorer_;
    ++x.frames;

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

    // Dialogue choices: the first option not yet picked in this state.
    if (choice_) {
        x.version = progressHash(serializeState());
        x.versions.insert(x.version);
        auto& done = x.tried[x.version];
        size_t pick = 0;
        for (size_t i = 0; i < choice_->options.size(); ++i)
            if (!done.count("choice:" + choice_->name + ":" + std::to_string(i))) { pick = i; break; }
        done.insert("choice:" + choice_->name + ":" + std::to_string(pick));
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
        if (s.view != view() || !s.object->enabled) { x.path.clear(); x.pending = nullptr; return true; }  // replan
        activate(s.object);
        return true;
    }
    if (x.pending) {
        auto act = std::move(x.pending);
        x.pending = nullptr;
        x.tried[x.version].insert(x.pendingKey);
        x.watchVersion = x.version;
        x.watchFrom = {room, view()};
        ++x.actions;
        x.lastAction = x.pendingKey;
        std::printf("[%d] %s\n", x.actions, x.pendingKey.c_str());
        act();
        return x.actions < Explorer::kMaxActions;
    }

    // Plan: the nearest untried action from here, by breadth-first search over
    // the views reachable through navigation hotspots.
    x.version = progressHash(serializeState());
    if (x.watching) {
        if (x.watchFrom.room != room || x.watchFrom.view != view())
            x.learned[x.watching] = {room, view()};
        x.watching = nullptr;
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
            if (x.lastGlobals.count(k) && x.lastGlobals[k] != v && shown++ < 8)
                std::printf(" %s=%s", k.c_str(), v.c_str());
        std::printf("\n");
        x.lastGlobals = std::move(now);
    }
    auto& done = x.tried[x.version];

    // In a zoom: actions here first, else leave it.
    struct Visit { Room* room; View* view; std::vector<Explorer::Step> path; };
    std::deque<Visit> queue;
    std::set<View*> seen;
    queue.push_back({room, view(), {}});
    seen.insert(view());
    auto keyOf = [&](Room* r, View* v, Object* o, const std::string& item) {
        return r->name + "/" + v->name + "/" + o->name + (item.empty() ? "" : " <- " + item);
    };
    while (!queue.empty()) {
        Visit cur = queue.front();
        queue.pop_front();
        // Candidate actions in this view.
        for (auto& op : cur.view->objects) {
            Object* o = op.get();
            if (!o->enabled) continue;
            if (!o->click.empty() && !isNavigation(o->click.kind) && o->click.kind != sc::FEEDBACK &&
                o->click.kind != sc::FEEDBACK_EX) {
                std::string key = keyOf(cur.room, cur.view, o, "");
                if (!done.count(key)) {
                    x.path.assign(cur.path.begin(), cur.path.end());
                    x.pendingKey = key;
                    x.pending = [this, o] { explorer_->watching = o; activate(o); };
                    return true;
                }
            }
            for (auto& re : o->reactions) {
                Item* it = world.items.count(re.item) ? world.items[re.item].get() : nullptr;
                if (!it || std::find(inventory.begin(), inventory.end(), it) == inventory.end()) continue;
                std::string key = keyOf(cur.room, cur.view, o, re.item);
                if (!done.count(key)) {
                    x.path.assign(cur.path.begin(), cur.path.end());
                    x.pendingKey = key;
                    x.pending = [this, o, it] { held = it; useHeldOn(o); };
                    return true;
                }
            }
        }
        // Where we can go from here (only within reach of the current room's
        // views, plus room doors; the next plan continues from the new room).
        for (auto& op : cur.view->objects) {
            Object* o = op.get();
            if (!o->enabled || o->click.empty()) continue;
            View* to = nullptr;
            Room* toRoom = cur.room;
            if (o->click.kind == sc::NODE) to = cur.room->node(o->click.arg);
            else if (o->click.kind == sc::ZOOM) to = cur.room->zoom(o->click.arg);
            else if ((o->click.kind == sc::ZOOMOUT || o->click.kind == sc::ZOOMBACK) && cur.view == view() && view()->isZoom)
                to = zooms.size() > 1 && o->click.kind == sc::ZOOMOUT ? zooms[zooms.size() - 2] : node;
            else if (o->click.kind == sc::CUSTOM && x.learned.count(o)) {
                toRoom = x.learned[o].room;
                to = x.learned[o].view;
            } else if (o->click.kind == sc::ROOM) {
                toRoom = world.room(o->click.arg);
                if (toRoom && !toRoom->nodes.empty()) {
                    View* n = toRoom->lastVisited.empty() ? nullptr : toRoom->node(toRoom->lastVisited);
                    to = n ? n : toRoom->nodes.front().get();
                }
            }
            if (!to || seen.count(to)) continue;
            seen.insert(to);
            Visit next{toRoom, to, cur.path};
            next.path.push_back({cur.view, o});
            queue.push_back(std::move(next));
        }
    }
    // Item on item (inventory reactions) need no walking.
    for (Item* target : inventory)
        for (auto& re : target->reactions) {
            Item* tool = world.items.count(re.item) ? world.items[re.item].get() : nullptr;
            if (!tool || std::find(inventory.begin(), inventory.end(), tool) == inventory.end()) continue;
            std::string key = "item " + re.item + " -> " + target->name;
            if (!done.count(key)) {
                x.pendingKey = key;
                x.pending = [this, target, re] { world.bound.item = target; perform(re.action); };
                return true;
            }
        }
    // A zoom with no way out: leave it as if it had a zoom-out hotspot.
    if (!zooms.empty() && !done.count("leave " + view()->name)) {
        done.insert("leave " + view()->name);
        zoomOut();
        return true;
    }
    std::printf("explore: stuck after %d actions, %zu states, at %s/%s; last action %s\n", x.actions,
                x.versions.size(), room ? room->name.c_str() : "?", view() ? view()->name.c_str() : "?",
                x.lastAction.c_str());
    std::printf("  reachable:");
    for (View* v : seen) std::printf(" %s", v->name.c_str());
    std::printf("\n  hotspots here:");
    for (auto& o : view()->objects)
        if (o->enabled) std::printf(" %s(%d:%s)", o->name.c_str(), o->click.kind, o->click.arg.c_str());
    std::printf("\n  inventory:");
    for (Item* it : inventory) std::printf(" %s", it->name.c_str());
    std::printf("\n");
    return false;
}
