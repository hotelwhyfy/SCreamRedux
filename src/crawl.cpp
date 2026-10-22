// --crawl: an automated sweep through the game. Each task starts from the
// start-of-game state, enters a room, goes to a view and then does one thing
// there: click an object, use an item on it (every scReaction), use an item
// on another (every scInventoryReaction), pick a choice option, or play a
// dialogue script. Scripts run in turbo mode until idle; any warning or Lua
// error logged meanwhile is reported against the task.
//
// Errors can come from running a handler out of its story order (state the
// game would have set up first); the point is to catch what the engine does
// wrong, which shows up as API warnings, missing files and crashes.

#include <cstdio>
#include <SDL3/SDL.h>

#include "constants.hpp"
#include "engine.hpp"

struct Crawler {
    enum class Kind { Click, Use, ItemUse, Choice, Dialogue };
    struct Task {
        Kind kind;
        std::string room, view;  // view empty: the room's entry node
        size_t object = 0;       // index into the view's objects
        std::string item;        // Use: held item; ItemUse: the tool
        std::string target;      // ItemUse: the item used on; Choice: choice name; Dialogue: script name
        size_t option = 0;       // Choice
        std::string label;
    };
    enum class Phase { Start, Enter, Act, Run, Done };

    std::vector<Task> tasks;
    size_t next = 0;
    Phase phase = Phase::Start;
    int frames = 0;
    std::vector<std::string> messages;  // warnings/errors logged during the current task
    int withProblems = 0, skipped = 0, timeouts = 0;
    SDL_LogOutputFunction defaultLog = nullptr;
    void* defaultLogData = nullptr;

    static constexpr int kBudget = 4000;  // frames (x 0.5 game s in turbo) before a task times out
};

namespace {

Crawler* gCrawler = nullptr;

void captureLog(void* userdata, int category, SDL_LogPriority priority, const char* message) {
    Crawler* c = gCrawler;
    if (c && priority >= SDL_LOG_PRIORITY_WARN && c->messages.size() < 20)
        c->messages.emplace_back(message);
    if (c && c->defaultLog && priority >= SDL_LOG_PRIORITY_WARN)
        c->defaultLog(c->defaultLogData, category, priority, message);
}

std::string firstLine(const std::string& s) { return s.substr(0, s.find('\n')); }

}  // namespace

bool Engine::crawlStep() {
    if (!crawler_) {
        crawler_ = std::make_shared<Crawler>();
        Crawler& c = *crawler_;
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
        script.setWatchdog(200);
        gCrawler = &c;
        SDL_GetLogOutputFunction(&c.defaultLog, &c.defaultLogData);
        SDL_SetLogOutputFunction(captureLog, nullptr);
        using K = Crawler::Kind;
        for (auto& [rn, r] : world.rooms) {
            if (r->nodes.empty()) continue;
            auto add = [&](auto& views) {
                for (auto& v : views)
                    for (size_t i = 0; i < v->objects.size(); ++i) {
                        Object& o = *v->objects[i];
                        std::string where = rn + "/" + v->name + "/" + o.name;
                        if (!o.click.empty()) c.tasks.push_back({K::Click, rn, v->name, i, "", "", 0, where});
                        for (auto& re : o.reactions)
                            c.tasks.push_back({K::Use, rn, v->name, i, re.item, "", 0, where + " <- " + re.item});
                    }
            };
            add(r->nodes);
            add(r->zooms);
        }
        std::string home = opts.start.empty() ? "" : opts.start.substr(0, opts.start.find(':'));
        if (home.empty())
            for (auto& [rn, r] : world.rooms)
                if (!r->nodes.empty()) { home = rn; break; }
        for (auto& [n, it] : world.items)
            for (auto& re : it->reactions)
                c.tasks.push_back({K::ItemUse, home, "", 0, re.item, n, 0, "item " + re.item + " -> " + n});
        for (auto& [n, ch] : world.choices)
            for (size_t i = 0; i < ch->options.size(); ++i)
                c.tasks.push_back({K::Choice, home, "", 0, "", n, i, "choice " + n + " #" + std::to_string(i)});
        for (auto& [n, d] : world.dialogues)
            c.tasks.push_back({K::Dialogue, home, "", 0, "", n, 0, "script " + n});
        std::printf("crawl: %zu tasks\n", c.tasks.size());
    }
    Crawler& c = *crawler_;
    using P = Crawler::Phase;
    using K = Crawler::Kind;
    if (c.next >= c.tasks.size()) {
        std::printf("crawl: %zu tasks, %d with problems, %d timed out, %d skipped (object disabled)\n", c.tasks.size(),
                    c.withProblems, c.timeouts, c.skipped);
        SDL_SetLogOutputFunction(c.defaultLog, c.defaultLogData);
        gCrawler = nullptr;
        return false;
    }
    const Crawler::Task& t = c.tasks[c.next];
    auto finish = [&](const char* note = nullptr) {
        if (note) c.messages.emplace_back(note);
        if (!c.messages.empty()) {
            ++c.withProblems;
            std::printf("[%zu] %s\n", c.next, t.label.c_str());
            for (auto& m : c.messages) std::printf("    %s\n", firstLine(m).c_str());
        }
        c.messages.clear();
        c.phase = P::Start;
        ++c.next;
        crawlRender_ = true;  // also loads the textures of where we ended up
    };
    auto idle = [&] { return !script.busy() && !camAnim_.active; };

    switch (c.phase) {
    case P::Start:
        restoreState(baseline_);
        closeMenu();
        aborted_ = false;
        gameRunning_ = true;
        c.messages.clear();
        if (!switchRoom(t.room)) { finish(); return true; }
        c.phase = P::Enter;
        c.frames = 0;
        return true;
    case P::Enter:
        if (!idle() && ++c.frames < Crawler::kBudget) return true;
        script.cancelAll();
        if (!t.view.empty()) {
            Room* r = world.room(t.room);
            if (r->node(t.view)) {
                switchNode(t.view);
            } else {
                // Reach a zoom as a player would: from a node, through any parent
                // zooms, so the bindings are what the handlers expect.
                std::vector<View*> chain;
                std::string want = t.view;
                for (int depth = 0; depth < 8 && !want.empty(); ++depth) {
                    View* parent = nullptr;
                    auto search = [&](auto& views) {
                        for (auto& v : views)
                            for (auto& o : v->objects)
                                if (!parent && o->click.kind == sc::ZOOM && o->click.arg == want) parent = v.get();
                    };
                    search(r->nodes);
                    search(r->zooms);
                    if (!parent) break;
                    chain.push_back(parent);
                    want = parent->isZoom ? parent->name : "";
                }
                for (auto it = chain.rbegin(); it != chain.rend(); ++it)
                    if ((*it)->isZoom) switchZoom((*it)->name);
                    else switchNode((*it)->name);
                switchZoom(t.view);
            }
        }
        c.phase = P::Act;
        return true;
    case P::Act: {
        Object* o = nullptr;
        if (t.kind == K::Click || t.kind == K::Use) {
            View* v = view();
            if (v && v->name == t.view && t.object < v->objects.size()) o = v->objects[t.object].get();
            if (!o) { finish("could not reach the view"); return true; }
            if (!o->enabled) { ++c.skipped; c.messages.clear(); c.phase = P::Start; ++c.next; return true; }
        }
        switch (t.kind) {
        case K::Click: activate(o); break;
        case K::Use:
            held = world.items.count(t.item) ? world.items[t.item].get() : nullptr;
            if (held) useHeldOn(o);
            break;
        case K::ItemUse: {
            Item* target = world.items[t.target].get();
            world.bound.item = target;
            for (auto& re : target->reactions)
                if (re.item == t.item) { perform(re.action); break; }
            break;
        }
        case K::Choice: {
            Choice* ch = world.choices[t.target].get();
            world.bound.choice = ch;
            ch->lastSelected = int(t.option);
            script.start(ch->options[t.option].second);
            break;
        }
        case K::Dialogue: script.startCode("scScriptGo(\"" + t.target + "\")", "crawl"); break;
        }
        c.phase = P::Run;
        c.frames = 0;
        return true;
    }
    case P::Run:
        if (!idle() && ++c.frames < Crawler::kBudget) return true;
        if (!idle()) {
            ++c.timeouts;
            finish("timed out: a handler is still running");
            return true;
        }
        finish();
        return true;
    case P::Done: return false;
    }
    return true;
}
