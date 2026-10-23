#include "project.hpp"

#include <algorithm>
#include <fstream>
#include <regex>
#include <sstream>

namespace editor {

namespace {

constexpr size_t kUndoDepth = 64;

std::vector<doc::Call> calls(const doc::Item& it) {
    std::vector<doc::Call> out;
    for (const auto& st : it.body) out.push_back(*st.call);
    return out;
}

}  // namespace

bool Project::load(const std::filesystem::path& script, const std::vector<std::filesystem::path>& dirs) {
    scriptPath = script;
    dataDirs = dirs;
    mount();
    std::ifstream f(script, std::ios::binary);
    if (!f) {
        error = "can't read " + script.string();
        return false;
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    undo_.clear(), redo_.clear();
    if (!parse(ss.str())) return false;
    savedText_ = text_;
    return true;
}

void Project::mount() {
    res = Resources();
    // Per directory: scream.res (the engine's own files) first, then the
    // game's archives, then loose files; later directories win.
    for (const auto& dir : dataDirs) {
        std::vector<std::filesystem::path> archives;
        std::error_code ec;
        for (const auto& e : std::filesystem::directory_iterator(dir, ec))
            if (e.path().extension() == ".res") archives.push_back(e.path());
        std::sort(archives.begin(), archives.end(), [](const auto& a, const auto& b) {
            bool sa = a.filename() == "scream.res", sb = b.filename() == "scream.res";
            return sa != sb ? sa : a < b;
        });
        for (const auto& a : archives)
            if (auto arc = ResArchive::open(a)) res.mount(std::move(arc));
        res.mountDirectory(dir);
    }
}

bool Project::save() {
    std::ofstream f(scriptPath, std::ios::binary);
    f << text_;
    if (!f) {
        error = "can't write " + scriptPath.string();
        return false;
    }
    savedText_ = text_;
    return true;
}

bool Project::parse(const std::string& text) {
    doc::Document d;
    if (!d.parse(text)) {
        error = d.error();
        return false;
    }
    doc = std::move(d);
    text_ = text;
    ++version;
    error.clear();
    builders.clear();
    for (const auto& it : doc.items)
        if (it.kind == doc::Item::Kind::Function && it.builder) builders.push_back(model::Builder::fold(it.name, calls(it)));
    reindex();
    return true;
}

void Project::reindex() {
    rooms.clear(), items.clear(), scripts.clear(), choices.clear(), tracks.clear(), overlays.clear();
    texts.clear(), handlers.clear();
    for (const auto& it : doc.items) {
        if (it.kind == doc::Item::Kind::Assign && it.text) texts[it.name] = *it.text;
        if (it.kind == doc::Item::Kind::Function && !it.builder) handlers.push_back(it.name);
    }
    std::sort(handlers.begin(), handlers.end());
    for (auto& b : builders) {
        // Rooms, items, dialogues, ... at a builder's top level, or anywhere for items.
        for (size_t i = 0; i < b.body.size(); ++i) {
            auto& e = b.body[i];
            Ref r{&e, {b.name, {int(i)}}};
            std::string k = e.kind();
            if (k == "SC_ROOM") rooms.push_back(r);
            else if (k == "SC_ITEM") items.push_back(r);
            else if (k == "SC_SCRIPT") scripts.push_back(r);
            else if (k == "SC_CHOICE") choices.push_back(r);
            else if (k == "SC_TRACK") tracks.push_back(r);
            else if (k == "SC_OVERLAY") overlays.push_back(r);
        }
    }
}

const std::map<std::string, std::vector<Project::Move>>& Project::moves() {
    if (movesVersion_ == version) return moves_;
    movesVersion_ = version;
    moves_.clear();
    static const std::regex call(R"(\b([A-Za-z_]\w*)\s*\()");
    static const std::regex any(R"re(scSwitch\(\s*(SC_ROOM|SC_NODE|SC_ZOOM)\s*,\s*"([^"]+)"|scSetLastVisited\(\s*"([^"]+)")re");
    std::map<std::string, std::vector<Move>> direct;
    std::map<std::string, std::vector<std::string>> calls;
    for (const auto& it : doc.items) {
        if (it.kind != doc::Item::Kind::Function || it.builder) continue;
        std::string src = doc.text(it.begin, it.end);
        // In order, so a scSetLastVisited applies to the room switch after it.
        std::string pending;
        for (std::sregex_iterator m(src.begin(), src.end(), any), e; m != e; ++m) {
            if ((*m)[3].matched) {
                pending = (*m)[3];
                continue;
            }
            Move mv{(*m)[1], (*m)[2], (*m)[1] == "SC_ROOM" ? pending : std::string()};
            if (mv.kind == "SC_ROOM") pending.clear();
            direct[it.name].push_back(mv);
        }
        for (std::sregex_iterator m(src.begin(), src.end(), call), e; m != e; ++m)
            if ((*m)[1] != it.name) calls[it.name].push_back((*m)[1]);
    }
    for (const auto& h : handlers) {
        auto& v = moves_[h];
        if (auto d = direct.find(h); d != direct.end()) v = d->second;
        if (auto c = calls.find(h); c != calls.end())
            for (const auto& callee : c->second)
                if (auto d = direct.find(callee); d != direct.end()) v.insert(v.end(), d->second.begin(), d->second.end());
    }
    return moves_;
}

model::Builder* Project::builder(const std::string& name) {
    for (auto& b : builders)
        if (b.name == name) return &b;
    return nullptr;
}

model::Element* Project::find(const ElementPath& p) {
    model::Builder* b = builder(p.builder);
    if (!b || p.index.empty()) return nullptr;
    std::vector<model::Element>* list = &b->body;
    model::Element* e = nullptr;
    for (int i : p.index) {
        if (i < 0 || size_t(i) >= list->size()) return nullptr;
        e = &(*list)[size_t(i)];
        list = &e->children;
    }
    return e;
}

std::string Project::resolve(const doc::Expr& e) const {
    if (e.kind == doc::Expr::Kind::String) return e.str;
    if (e.kind == doc::Expr::Kind::Name) {
        auto it = texts.find(e.src);
        if (it != texts.end()) return it->second;
    }
    return e.src;
}

void Project::touch(const ElementPath& p) {
    if (model::Builder* b = builder(p.builder)) b->dirty = true;
}

bool Project::setText(const std::string& next) {
    if (next == text_) {
        for (auto& b : builders) b.dirty = false;
        return true;
    }
    std::string old = text_;
    if (!parse(next)) {
        std::string why = error;
        parse(old);  // back to the last good state
        error = why;
        return false;
    }
    undo_.push_back(std::move(old));
    if (undo_.size() > kUndoDepth) undo_.erase(undo_.begin());
    redo_.clear();
    return true;
}

bool Project::commit() { return setText(model::writeDocument(doc, builders)); }

bool Project::replaceItem(const std::string& name, doc::Item::Kind kind, const std::string& text) {
    // Builders may be waiting to be regenerated: find the item in the text as it would be saved.
    std::string all = model::writeDocument(doc, builders);
    doc::Document d;
    if (!d.parse(all)) return false;
    for (const auto& it : d.items)
        if (it.kind == kind && it.name == name) return setText(all.substr(0, it.begin) + text + all.substr(it.end));
    return false;
}

bool Project::insertBeforeBoot(const std::string& text) {
    std::string all = model::writeDocument(doc, builders);
    doc::Document d;
    if (!d.parse(all)) return false;
    // After the last function: the boot code follows the functions.
    size_t at = all.size();
    for (const auto& it : d.items)
        if (it.kind == doc::Item::Kind::Function) at = it.end;
    return setText(all.substr(0, at) + "\n\n" + text + all.substr(at));
}

bool Project::addText(const std::string& name, const std::string& text) {
    std::string all = model::writeDocument(doc, builders);
    doc::Document d;
    if (!d.parse(all)) return false;
    size_t at = 0;
    for (const auto& it : d.items)
        if (it.kind == doc::Item::Kind::Assign && it.text) at = it.end;
    std::string line = name + " = " + doc::quote(text);
    if (!at) return setText(line + "\n\n" + all);  // no text table yet: start one at the top
    return setText(all.substr(0, at) + "\n" + line + all.substr(at));
}

std::string Project::nextTextName() const {
    int best = 0;
    size_t width = 4;
    for (const auto& [name, _] : texts)
        if (name.size() > 3 && name.compare(0, 3, "msg") == 0 && name.find_first_not_of("0123456789", 3) == std::string::npos) {
            best = std::max(best, std::stoi(name.substr(3)));
            width = std::max(width, name.size() - 3);
        }
    std::string n = std::to_string(best + 1);
    return "msg" + std::string(width > n.size() ? width - n.size() : 0, '0') + n;
}

bool Project::insertBootCall(const std::string& statement) {
    std::string all = model::writeDocument(doc, builders);
    doc::Document d;
    if (!d.parse(all)) return false;
    for (const auto& it : d.items)
        if (it.kind == doc::Item::Kind::Call && it.call && it.call->fn == "scRun")
            return setText(all.substr(0, it.begin) + statement + "\n" + all.substr(it.begin));
    return setText(all + "\n" + statement + "\n");
}

bool Project::undo() {
    if (undo_.empty()) return false;
    std::string cur = text_;
    if (!parse(undo_.back())) return false;
    undo_.pop_back();
    redo_.push_back(std::move(cur));
    return true;
}

bool Project::redo() {
    if (redo_.empty()) return false;
    std::string cur = text_;
    if (!parse(redo_.back())) return false;
    redo_.pop_back();
    undo_.push_back(std::move(cur));
    return true;
}

}  // namespace editor
