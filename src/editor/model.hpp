#pragma once

// The world as the builder functions declare it, at the level of the source:
// each scCreate becomes an Element holding the calls that configure it, and
// scBegin ... scEnd nest its children. Arguments stay as written (the global
// msg0123, not its text), so the model can be written back as the same source.

#include <string>
#include <vector>

#include "luadoc.hpp"

namespace model {

struct Element {
    doc::Call head;                 // scCreate(SC_KIND, "name"), or a lone call
    std::vector<doc::Call> props;   // calls on the created thing (scPlace, scOnClick, ...)
    bool scoped = false;            // has scBegin ... scEnd
    doc::Call begin, end;
    std::vector<Element> children;  // inside scBegin ... scEnd: loose calls and elements

    bool isCreate() const { return head.fn == "scCreate" && head.args.size() >= 2; }
    std::string kind() const { return isCreate() ? head.args[0].src : std::string(); }  // "SC_ROOM", ...
    std::string name() const { return isCreate() ? head.args[1].str : std::string(); }

    // The first call to `fn` among the props, or among the loose calls of the
    // children (a node's scTexture calls come after its scBegin).
    // With `arg0`, only a call whose first argument is spelled that way (SC_FRONT).
    doc::Call* find(const std::string& fn, const std::string& arg0 = {});
    const doc::Call* find(const std::string& fn) const;
    std::vector<doc::Call*> findAll(const std::string& fn);
    // Elements among the children of the given kind ("SC_OBJECT", ...).
    std::vector<Element*> elements(const std::string& kind);
};

// One builder function.
struct Builder {
    std::string name;
    std::vector<Element> body;
    bool dirty = false;  // regenerate on save

    static Builder fold(const std::string& name, const std::vector<doc::Call>& calls);
    std::vector<doc::Call> flatten() const;
};

std::string callSource(const doc::Call& c);

// Text for a builder in the original's layout (from the shipped bytecode's
// line info: a blank line before every scCreate, scOnHover and scOnClick on
// one line, the textures of a node in one block, ...).
std::string writeBuilder(const Builder& b);

// The whole script with the dirty builders regenerated (all of them with
// `all`) and every other byte as it was.
std::string writeDocument(const doc::Document& d, const std::vector<Builder>& builders, bool all = false);

}  // namespace model
