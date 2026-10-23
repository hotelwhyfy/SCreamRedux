#include "model.hpp"

#include <map>

namespace model {

doc::Call* Element::find(const std::string& fn, const std::string& arg0) {
    auto match = [&](doc::Call& c) {
        return c.fn == fn && (arg0.empty() || (!c.args.empty() && c.args[0].src == arg0));
    };
    for (auto& c : props)
        if (match(c)) return &c;
    for (auto& e : children)
        if (!e.isCreate() && match(e.head)) return &e.head;
    return nullptr;
}

const doc::Call* Element::find(const std::string& fn) const { return const_cast<Element*>(this)->find(fn); }

std::vector<doc::Call*> Element::findAll(const std::string& fn) {
    std::vector<doc::Call*> out;
    for (auto& c : props)
        if (c.fn == fn) out.push_back(&c);
    for (auto& e : children)
        if (!e.isCreate() && e.head.fn == fn) out.push_back(&e.head);
    return out;
}

std::vector<Element*> Element::elements(const std::string& k) {
    std::vector<Element*> out;
    for (auto& e : children)
        if (e.isCreate() && e.kind() == k) out.push_back(&e);
    return out;
}

Builder Builder::fold(const std::string& name, const std::vector<doc::Call>& calls) {
    Builder b;
    b.name = name;
    std::vector<std::vector<Element>*> lists{&b.body};
    std::vector<Element*> owners{nullptr};
    for (const auto& c : calls) {
        auto& list = *lists.back();
        Element* last = list.empty() ? nullptr : &list.back();
        if (c.fn == "scCreate" && c.args.size() >= 2) {
            Element e;
            e.head = c;
            list.push_back(std::move(e));
        } else if (c.fn == "scBegin" && last && last->isCreate() && !last->scoped) {
            last->scoped = true;
            last->begin = c;
            lists.push_back(&last->children);
            owners.push_back(last);
        } else if (c.fn == "scEnd" && owners.back()) {
            owners.back()->end = c;
            owners.pop_back();
            lists.pop_back();
        } else if (last && last->isCreate() && !last->scoped) {
            last->props.push_back(c);
        } else {
            Element e;
            e.head = c;
            list.push_back(std::move(e));
        }
    }
    return b;
}

namespace {

void flattenInto(const std::vector<Element>& list, std::vector<doc::Call>& out) {
    for (const auto& e : list) {
        out.push_back(e.head);
        for (const auto& p : e.props) out.push_back(p);
        if (e.scoped) {
            out.push_back(e.begin);
            flattenInto(e.children, out);
            out.push_back(e.end);
        }
    }
}

// Lines from call `a` to call `b` (0: same line, 2: a blank line between).
int gap(const std::string& a, const std::string& b) {
    if (a == "scOnHover" && b == "scOnClick") return 0;
    if (b == "scCreate") return a == "scInventoryReaction" || a == "scInventorySet" ? 1 : 2;
    if (b == "scEnd") return a == "scEnd" ? 2 : 1;
    if (a == "scTexture" && b != "scTexture") return 2;
    return 1;
}

}  // namespace

std::vector<doc::Call> Builder::flatten() const {
    std::vector<doc::Call> out;
    flattenInto(body, out);
    return out;
}

std::string callSource(const doc::Call& c) {
    std::string s = c.fn + "(";
    for (size_t i = 0; i < c.args.size(); ++i) {
        if (i) s += ", ";
        s += c.args[i].src;
    }
    return s + ")";
}

std::string writeBuilder(const Builder& b) {
    std::string s = "function " + b.name + "()";
    std::string prev;
    for (const auto& c : b.flatten()) {
        int g = prev.empty() ? 1 : gap(prev, c.fn);
        if (g == 0) {
            s += " ";
        } else {
            s += std::string(size_t(g), '\n');
            s += "  ";
        }
        s += callSource(c);
        prev = c.fn;
    }
    return s + "\nend";
}

std::string writeDocument(const doc::Document& d, const std::vector<Builder>& builders, bool all) {
    std::map<std::string, const Builder*> byName;
    for (const auto& b : builders) byName[b.name] = &b;
    std::string out;
    size_t at = 0;
    for (const auto& it : d.items) {
        if (it.kind != doc::Item::Kind::Function || !it.builder) continue;
        auto f = byName.find(it.name);
        if (f == byName.end() || !(all || f->second->dirty)) continue;
        out += d.text(at, it.begin);
        out += writeBuilder(*f->second);
        at = it.end;
    }
    out += d.text(at, d.source().size());
    return out;
}

}  // namespace model
