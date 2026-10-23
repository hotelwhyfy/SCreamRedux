// scream-edit: an editor for SCream game scripts. It reads a game script as
// source, models the world its builder functions declare, and writes it back
// in the original's own layout.

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "app.hpp"
#include "luadoc.hpp"
#include "model.hpp"

namespace {

void usage() {
    std::puts(
        "usage: scream-edit [--data DIR ...] [SCRIPT.lua]\n"
        "\n"
        "  SCRIPT.lua         the game script to edit (default: the data's SCRIPT.lua)\n"
        "  --data DIR         data directories for images and sounds (repeatable)\n"
        "  --stats SCRIPT     print what the parser finds in a script\n"
        "  --roundtrip IN OUT regenerate every builder function of IN into OUT\n");
}

bool readFile(const std::string& path, std::string& out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

bool load(const std::string& path, doc::Document& d) {
    std::string src;
    if (!readFile(path, src)) {
        std::fprintf(stderr, "can't read %s\n", path.c_str());
        return false;
    }
    if (!d.parse(std::move(src))) {
        std::fprintf(stderr, "%s: %s\n", path.c_str(), d.error().c_str());
        return false;
    }
    return true;
}

std::vector<model::Builder> builders(const doc::Document& d) {
    std::vector<model::Builder> out;
    for (const auto& it : d.items) {
        if (it.kind != doc::Item::Kind::Function || !it.builder) continue;
        std::vector<doc::Call> calls;
        for (const auto& st : it.body) calls.push_back(*st.call);
        out.push_back(model::Builder::fold(it.name, calls));
    }
    return out;
}

int stats(const std::string& path) {
    doc::Document d;
    if (!load(path, d)) return 1;
    int functions = 0, builderFns = 0, texts = 0, assigns = 0, calls = 0, other = 0;
    for (const auto& it : d.items) {
        switch (it.kind) {
        case doc::Item::Kind::Function: ++functions, builderFns += it.builder; break;
        case doc::Item::Kind::Assign: it.text ? ++texts : ++assigns; break;
        case doc::Item::Kind::Call: ++calls; break;
        case doc::Item::Kind::Other: ++other; break;
        }
    }
    std::printf("%s: %zu items: %d functions (%d builders), %d texts, %d other assignments, %d calls, %d other\n",
                path.c_str(), d.items.size(), functions, builderFns, texts, assigns, calls, other);
    for (const auto& b : builders(d)) {
        int created = 0;
        for (const auto& c : b.flatten()) created += c.fn == "scCreate";
        std::printf("  %s: %d created\n", b.name.c_str(), created);
    }
    return 0;
}

int roundtrip(const std::string& in, const std::string& out) {
    doc::Document d;
    if (!load(in, d)) return 1;
    auto bs = builders(d);
    std::string text = model::writeDocument(d, bs, true);
    std::ofstream f(out, std::ios::binary);
    f << text;
    if (!f) {
        std::fprintf(stderr, "can't write %s\n", out.c_str());
        return 1;
    }
    // The output must parse back into the same calls.
    doc::Document again;
    if (!again.parse(text)) {
        std::fprintf(stderr, "%s doesn't parse: %s\n", out.c_str(), again.error().c_str());
        return 1;
    }
    auto bs2 = builders(again);
    size_t same = 0;
    for (size_t i = 0; i < bs.size() && i < bs2.size(); ++i)
        same += model::writeBuilder(bs[i]) == model::writeBuilder(bs2[i]);
    std::printf("regenerated %zu builders (%zu stable on a second pass)\n", bs.size(), same);
    return same == bs.size() && bs.size() == bs2.size() ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    editor::AppOptions opts;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", a.c_str());
                std::exit(2);
            }
            return argv[++i];
        };
        if (a == "--stats") return stats(next());
        if (a == "--roundtrip") {
            std::string in = next();
            return roundtrip(in, next());
        }
        if (a == "--data") opts.dataDirs.emplace_back(next());
        else if (a == "-h" || a == "--help") return usage(), 0;
        else if (!a.empty() && a[0] != '-') opts.script = a;
        else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            usage();
            return 2;
        }
    }
    return editor::runApp(opts);
}
