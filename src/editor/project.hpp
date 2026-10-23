#pragma once

// The script being edited, the data it refers to, and an index of what the
// builders declare. Every edit ends in commit(): the script is written out
// (dirty builders regenerated, everything else as it was) and parsed again,
// so the text is always the one source of truth and undo is a stack of texts.

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include "luadoc.hpp"
#include "model.hpp"
#include "resources.hpp"

namespace editor {

// Where an element sits: the builder, then child indices from its body down.
struct ElementPath {
    std::string builder;
    std::vector<int> index;
    bool empty() const { return builder.empty(); }
    bool operator==(const ElementPath&) const = default;
};

class Project {
public:
    bool load(const std::filesystem::path& script, const std::vector<std::filesystem::path>& dataDirs);
    bool save();
    bool modified() const { return text_ != savedText_; }

    std::filesystem::path scriptPath;
    std::vector<std::filesystem::path> dataDirs;
    doc::Document doc;
    std::vector<model::Builder> builders;
    std::string error;
    int version = 0;  // counts parses, for caches of what the text says
    Resources res;

    // The index, rebuilt by every commit.
    struct Ref {
        model::Element* e = nullptr;
        ElementPath path;
    };
    std::vector<Ref> rooms, items, scripts, choices, tracks, overlays;
    std::map<std::string, std::string> texts;  // the text table: global -> text
    std::vector<std::string> handlers;         // functions that aren't builders

    // Where a handler takes the player: its scSwitch(SC_ROOM / SC_NODE / SC_ZOOM,
    // "name") calls, and those of the handlers it calls (one level: door
    // handlers are short). `node` is a scSetLastVisited("n") made before a
    // room switch: the node the room opens at.
    struct Move {
        std::string kind, target, node;
    };
    const std::map<std::string, std::vector<Move>>& moves();

    model::Element* find(const ElementPath& p);
    model::Builder* builder(const std::string& name);
    // A string argument's text, or the text global it names.
    std::string resolve(const doc::Expr& e) const;

    // Mark the builder holding `p` for regeneration (call before changing it).
    void touch(const ElementPath& p);
    // Write the edits into the text and re-parse; false (and nothing changed) if
    // the result doesn't parse.
    bool commit();
    // Replace a top-level item's source (a handler, a text) and commit.
    bool replaceItem(const std::string& name, doc::Item::Kind kind, const std::string& text);
    // Append source at the end of the item list (before the boot code) and commit.
    bool insertBeforeBoot(const std::string& text);
    // Add a global to the text table (after its last entry, as the scripts keep it at the top).
    bool addText(const std::string& name, const std::string& text);
    // A free name for a new text global, after the highest msgNNNN.
    std::string nextTextName() const;
    // Add a statement to the boot code, before scRun() (new builders must be called there).
    bool insertBootCall(const std::string& statement);

    bool undo();
    bool redo();
    bool canUndo() const { return !undo_.empty(); }
    bool canRedo() const { return !redo_.empty(); }

private:
    bool parse(const std::string& text);
    bool setText(const std::string& next);  // parse it as the new state, keeping the old for undo
    void reindex();
    void mount();
    std::string text_, savedText_;
    std::vector<std::string> undo_, redo_;
    std::map<std::string, std::vector<Move>> moves_;
    int movesVersion_ = -1;
};

}  // namespace editor
