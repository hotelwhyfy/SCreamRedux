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

constexpr float kVirtual = 800.0f / 1024.0f;  // scream.exe lays the grid out in 1024-wide units
constexpr int kColumns = 10;
constexpr int kSlots = 70;
constexpr float kLabelScale = 0.75f;
constexpr float kJournalScale = 0.55f;
constexpr TextColor kInk{0.16f, 0.11f, 0.07f};  // journal text is drawn dark on the page

const Reaction* findReaction(const std::vector<Reaction>& reactions, const Item* item) {
    for (const auto& r : reactions)
        if (r.item == item->name)
            return &r;
    return nullptr;
}

}  // namespace

bool Engine::hintsActive() const {
    auto it = settings.find(sc::HINTS);
    bool enabled = it != settings.end() && std::holds_alternative<double>(it->second) && std::get<double>(it->second) == 1;
    return enabled && hintState == sc::HINTS_ACTIVE;
}

bool Engine::canOpenInventory() const {
    return !locked && !scriptBusy() && !video_ && !choice_ && !journalOpen_ && view();
}

void Engine::openInventory() {
    invOpen_ = true;
    held = nullptr;
    if (hintsActive())
        feedback(hints[sc::HINTS_OPEN]);
}

void Engine::closeInventory() {
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

Engine::Box Engine::slotBox(int slot) const {
    double size = 64;
    auto it = settings.find(sc::INV_SIZE);
    if (it != settings.end() && std::holds_alternative<double>(it->second)) size = std::get<double>(it->second);
    int col = slot % kColumns, row = slot / kColumns;
    float x = float(32 + col * (32 + size)) * kVirtual;
    float y = 64 + 40 + float(row * 96) * kVirtual;
    return {x, y, float(size) * kVirtual, float(size) * kVirtual};
}

int Engine::slotAt(float x, float y) const {
    for (int i = 0; i < int(inventory.size()) && i < kSlots; ++i) {
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

void Engine::drawInventory() {
    float mx, my;
    canvasMouse(mx, my);
    int hover = slotAt(mx, my);
    for (int i = 0; i < int(inventory.size()) && i < kSlots; ++i) {
        Item* item = inventory[size_t(i)];
        if (item == held)
            continue;  // it is on the cursor
        if (auto* t = renderer.texture(res, item->icon)) {
            Box b = slotBox(i);
            renderer.drawImage(*t, b.x, b.y, b.w, b.h, i == hover ? 1.0f : 0.85f);
        }
    }
    // Label of the item under the cursor, in the bottom bar.
    if (hover >= 0 && feedback_.empty() && font_.loaded()) {
        const std::string& label = inventory[size_t(hover)]->label;
        float w = font_.width(label, kLabelScale);
        font_.draw(renderer, label, (sc::SCREEN_W - w) / 2, sc::SCREEN_H - 52, kLabelScale);
    }
}

void Engine::drawHeldCursor() {
    if (auto* t = renderer.texture(res, held->icon)) {
        float x, y;
        canvasMouse(x, y);
        Box b = slotBox(0);
        renderer.drawImage(*t, x - b.w / 2, y - b.h / 2, b.w, b.h);
    }
}

// ---------------------------------------------------------------- journal

void Engine::openJournal() {
    if (journalConfig.image.empty())
        return;
    if (!journalFont_.loaded() && !journalFont_.load(renderer, res, journalConfig.font))
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "cannot load journal font %s", journalConfig.font.c_str());
    layoutJournal();
    // Open at the spread holding the newest entry.
    journalSpread_ = journalPages_.empty() ? 0 : (journalPages_.size() - 1) / 2;
    journalOpen_ = true;
    invOpen_ = false;
    sound.playFx(journalConfig.openSound);
}

void Engine::closeJournal() {
    journalOpen_ = false;
    sound.playFx(journalConfig.closeSound);
}

// Entries in index order, wrapped to the page width and flowed across pages.
void Engine::layoutJournal() {
    journalPages_.clear();
    if (!journalFont_.loaded())
        return;
    const float pageWidth = 280, lineHeight = journalFont_.lineHeight(kJournalScale);
    const size_t linesPerPage = size_t(380 / lineHeight);
    std::vector<std::string> page;
    auto push = [&](const std::string& line) {
        if (page.size() >= linesPerPage) {
            journalPages_.push_back(std::move(page));
            page.clear();
        }
        if (!(page.empty() && line.empty()))
            page.push_back(line);
    };
    for (const auto& [index, text] : journal) {
        for (const auto& line : journalFont_.wrap(text, pageWidth, kJournalScale))
            push(line);
        push("");
    }
    if (!page.empty())
        journalPages_.push_back(std::move(page));
}

void Engine::journalClick() {
    float x, y;
    canvasMouse(x, y);
    size_t spreads = (journalPages_.size() + 1) / 2;
    // Left half turns back, right half turns forward.
    if (x < sc::SCREEN_W / 2.0f && journalSpread_ > 0) {
        --journalSpread_;
        sound.playFx(journalConfig.turnSound);
    } else if (x >= sc::SCREEN_W / 2.0f && journalSpread_ + 1 < spreads) {
        ++journalSpread_;
        sound.playFx(journalConfig.turnSound);
    }
}

void Engine::drawJournal() {
    const auto* book = renderer.texture(res, journalConfig.image);
    if (!book)
        return;
    float h = sc::SCREEN_W * float(book->height) / float(book->width);
    float top = (sc::SCREEN_H - h) / 2;
    renderer.drawImage(*book, 0, top, sc::SCREEN_W, h);
    if (!journalFont_.loaded())
        return;
    const float lh = journalFont_.lineHeight(kJournalScale);
    const float pageX[2] = {40, 410};
    for (int side = 0; side < 2; ++side) {
        size_t p = journalSpread_ * 2 + size_t(side);
        if (p >= journalPages_.size())
            break;
        for (size_t i = 0; i < journalPages_[p].size(); ++i)
            journalFont_.draw(renderer, journalPages_[p][i], pageX[side], top + 40 + lh * float(i), kJournalScale, 1, kInk);
    }
}
