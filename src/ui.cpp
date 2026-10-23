// Inventory and journal screens.
//
// Controls follow the game's own tutorial hints (msg01110..msg01117):
// right-click opens and closes the inventory, left-click grabs an item, and a
// held item is used with a left-click on a scene object (scReaction) or on
// another inventory item (scInventoryReaction). Without a matching reaction
// the held item's failure line plays (the 3rd/4th arguments of scInventorySet).
// The inventory is drawn over the frozen view tinted sepia (0.9, 0.75, 0.5),
// with icons in rows of ten, as scream.exe does (0x409680).

#include <algorithm>

#include "constants.hpp"
#include "engine.hpp"

namespace {

// 10 icons per row in the DC (70 slots), 8 in the 2006 engine (P 0x408fa0).
int columns(bool release2006) { return release2006 ? 8 : 10; }
int slotCount(bool release2006) { return release2006 ? 64 : 70; }
constexpr int kJournalPage = 76;  // lines per two-page spread

const Reaction* findReaction(const std::vector<Reaction>& reactions, const Item* item) {
    for (const auto& r : reactions)
        if (r.item == item->name)
            return &r;
    return nullptr;
}

}  // namespace

bool Engine::hintsOn() const {
    auto it = settings.find(sc::HINTS);
    return it != settings.end() && std::holds_alternative<double>(it->second) && std::get<double>(it->second) == 1;
}

bool Engine::hintsActive() const { return hintsOn() && hintState == sc::HINTS_ACTIVE; }

bool Engine::canOpenInventory() const {
    return !locked && !scriptBusy() && !video_ && !choice_ && !journalOpen_ && view();
}

// Opening and closing crossfade like a view switch (0x4174d8, 0x417503).
void Engine::openInventory() {
    beginTransition();  // the capture (0x4174e7) is the inventory's backdrop
    invOpen_ = true;
    invFade_ = 0;
    held = nullptr;
    if (hintsActive())
        feedback(hints[sc::HINTS_OPEN]);
}

void Engine::closeInventory() {
    beginTransition();
    invOpen_ = false;
    if (held && hintsActive() && held->name == hints[sc::HINTS_ITEM])
        feedback(hints[sc::HINTS_CLOSE]);
}

void Engine::rightClick() {
    if (aborted_ || video_ || menuOpen())
        return;
    if (journalOpen_) {
        closeJournal();
        return;
    }
    if (invOpen_)
        closeInventory();
    else if (canOpenInventory())
        openInventory();
}

Engine::Box Engine::slotBox(int slot, bool draw) const {
    double size = 64;
    auto it = settings.find(sc::INV_SIZE);
    if (it != settings.end() && std::holds_alternative<double>(it->second)) size = std::get<double>(it->second);
    int col = slot % columns(release2006), row = slot / columns(release2006);
    float x = float(32 + col * (32 + size)) * gMode.px;
    // Hit rows (0x417c90, state 0xfa3) start at y 64 (2006: 96); the icons are
    // drawn 2 pixels lower (2006: 8 higher), as compiled.
    float y = float((release2006 ? 96 : 64) + row * 96 + (draw ? (release2006 ? -8 : 2) : 0)) * gMode.px;
    return {x, y, float(size) * gMode.px, float(size) * gMode.px};
}

int Engine::slotAt(float x, float y) const {
    for (int i = 0; i < int(inventory.size()) && i < slotCount(release2006); ++i) {
        Box b = slotBox(i);
        if (x >= b.x && y >= b.y && x < b.x + b.w && y < b.y + b.h)
            return i;
    }
    return -1;
}

void Engine::inventoryClick() {
    float x, y;
    canvasMouse(x, y);
    int i = slotAt(x, y);
    if (i < 0) {
        held = nullptr;
        return;
    }
    Item* item = inventory[size_t(i)];
    if (!held) {
        if (item == journalConfig.item) {
            openJournal();
            return;
        }
        held = item;
        if (hintsActive() && item->name == hints[sc::HINTS_ITEM])
            feedback(hints[sc::HINTS_GRAB]);
        return;
    }
    if (held == item) {
        held = nullptr;
        return;
    }
    // Held item used on another item: the target's scInventoryReaction for the held one.
    Item* tool = held;
    held = nullptr;
    world.bound.item = item;
    if (const Reaction* r = findReaction(item->reactions, tool))
        perform(r->action);
    else
        feedback(tool->text, tool->voice);
}

void Engine::useHeldOn(Object* o) {
    Item* tool = held;
    held = nullptr;
    world.bindClicked(room, view(), o);
    world.bound.item = tool;
    if (const Reaction* r = findReaction(o->reactions, tool))
        perform(r->action);
    else
        feedback(tool->text, tool->voice);
}

void Engine::drawInventoryBackdrop() {
    const Transition& t = transition_;
    if (t.capture.id)
        renderer.drawImageUV(t.capture, t.x0, gMode.bar, t.x1 - t.x0, sc::SCREEN_H - 2 * gMode.bar, t.u0, t.v0, t.u1, t.v1,
                             1 - invFade_, 0.9f, 0.75f, 0.5f);
}

void Engine::drawInventory() {
    for (int i = 0; i < int(inventory.size()) && i < slotCount(release2006); ++i) {
        Item* item = inventory[size_t(i)];
        if (item == held)
            continue;  // it is on the cursor
        if (auto* t = renderer.texture(res, item->icon)) {
            Box b = slotBox(i, true);
            renderer.drawImage(*t, b.x, b.y, b.w, b.h, std::min(1.0f, invFade_ * 2));
        }
    }
    if (capturing_)
        return;
    double speed = 25;
    if (auto* v = std::get_if<double>(&settings[sc::MOV_SPEED])) speed = *v;
    if (invFade_ < 0.5f)
        invFade_ += float(speed * 0.001 * frameDt_ * 60);
}

// The held item as the cursor (0x40b400, a 64 px quad). With `glow` (cursor
// mode 0xcb) it is drawn twice more, added (SRC_ALPHA, ONE), the second time a
// pixel larger all round: brighter over something it can be tried on.
void Engine::drawHeldCursor(bool glow) {
    if (auto* t = renderer.texture(res, held->icon)) {
        float x, y;
        canvasMouse(x, y);
        if (fpsActive()) {
            x = sc::SCREEN_W / 2.0f;
            y = sc::SCREEN_H / 2.0f;
        }
        const Box b = {0, 0, 64 * gMode.px, 64 * gMode.px};  // a 64 pixel quad, centred
        renderer.drawImage(*t, x - b.w / 2, y - b.h / 2, b.w, b.h);
        for (int i = 0; glow && i < 2; ++i) {
            const float grow = float(i) * gMode.px;
            renderer.drawImagePass(*t, x - b.w / 2 - grow, y - b.h / 2 - grow, b.w + 2 * grow, b.h + 2 * grow,
                                   Renderer::Pass{1, 1, 1, 1, Blend::AddAlpha});
        }
    }
}

// ---------------------------------------------------------------- journal

void Engine::openJournal() {
    if (journalConfig.image.empty())
        return;
    if (!journalFont_.loaded() && !journalFont_.load(renderer, res, journalConfig.font))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "cannot load journal font %s", journalConfig.font.c_str());
    clearFeedback();
    beginTransition();
    journalOpen_ = true;  // at the page last shown (0x418580)
    invOpen_ = false;
    sound.playFx(journalConfig.openSound);
}

void Engine::closeJournal() {
    beginTransition();
    journalOpen_ = false;
    if (fpsStyle()) recenterPointer(true);  // 0x413ff1
    sound.playFx(journalConfig.closeSound);
}

// scJournalAdd (0x418550, 0x40e380): entries after the first are preceded by
// an empty line; a line breaks at the first space once it is over 30
// characters, and '|' is a non-breaking space. As in the original, the last
// line is only stored when the entry starts a new line (so a one-line first
// entry is lost).
void Engine::journalAdd(int index, const std::string& text) {
    Journal& j = journal;
    if (!j.added.insert(index).second)
        return;
    auto line = [&](int i) -> std::string& {
        if (j.lines.size() <= size_t(i)) j.lines.resize(size_t(i) + 1);
        return j.lines[size_t(i)];
    };
    int n = 0;
    if (j.last >= 1) {
        line(j.last).clear();
        n = j.last + 1;
        if (n > 1000)
            return;
    }
    std::string buf = text;
    size_t from = 0;
    int run = 0;
    for (size_t i = 0; i < buf.size(); ++i, ++run) {
        if (buf[i] == ' ' && run > 30) {
            line(n++) = buf.substr(from, i - from);
            from = i + 1;
            ++i;  // the character after the break is taken without a check
            run = 0;
        } else if (buf[i] == '|') {
            buf[i] = ' ';
        }
    }
    if (j.last < n)
        line(n) = buf.substr(std::min(from, buf.size()));
    j.last = n + 1;
}

// Clicks above y = 605 (1024x768 pixels) turn back on the left half and
// forward on the right half; the turn sound plays when the page changes.
// Clicks below close the book (0x413f80).
void Engine::journalClick() {
    float x, y;
    canvasMouse(x, y);
    const float px = gMode.px, ox = x / px, oy = y / px;
    if (oy >= 605) {
        closeJournal();
        return;
    }
    auto turn = [&](int by) {  // 0x40e490
        Journal& j = journal;
        j.first += by;
        if (j.first < 0) {
            j.first = 0;
            return false;
        }
        if (j.first <= j.last)
            return true;
        j.first = j.last / kJournalPage * kJournalPage;
        return false;
    };
    if (ox < 512 && turn(-kJournalPage))
        sound.playFx(journalConfig.turnSound);
    if (ox > 512 && turn(kJournalPage))
        sound.playFx(journalConfig.turnSound);
}

// The book image, then 76 lines in two columns of 38 (0x40b0a0): black text
// at x = 32 and 512, from y = 100 in 16 pixel steps.
void Engine::drawJournal() {
    const auto* book = renderer.texture(res, journalConfig.image);
    if (!book)
        return;
    // Like a zoom image, over the view between the 64 pixel bars (0x4083c0).
    renderer.drawImage(*book, 0, 64 * gMode.px, sc::SCREEN_W, sc::SCREEN_H - 128 * gMode.px);
    if (!journalFont_.loaded())
        return;
    const float px = gMode.px;
    int x = 32, y = 100;
    for (int r = 0, i = journal.first; r < kJournalPage; ++r, ++i) {
        if (i <= journal.last && size_t(i) < journal.lines.size())
            journalFont_.drawMono(renderer, journal.lines[size_t(i)], float(x) * px, float(y) * px, px, 1, {0, 0, 0});
        y += 16;
        if (y > 703) {
            y = 100;
            x += 480;
        }
    }
}
