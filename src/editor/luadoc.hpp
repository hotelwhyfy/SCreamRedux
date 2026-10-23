#pragma once

// A game script as source text, split the way the editor needs it: top-level
// items (text globals, functions, boot statements) with their exact spans, and
// the bodies of builder functions parsed into sc* calls. Everything the editor
// doesn't model is kept as the original text, so saving an unchanged script
// reproduces it byte for byte.

#include <optional>
#include <string>
#include <vector>

namespace doc {

// One argument of a call, as written.
struct Expr {
    enum class Kind { Const, Number, String, Name, Nil, Other };
    Kind kind = Kind::Other;
    std::string src;  // source spelling ("SC_FRONT", "765", "\"n2\"", "globalMusicVolume - 2")
    std::string str;  // Kind::String: the decoded value

    static Expr number(int v);
    static Expr string(const std::string& s);
    static Expr name(const std::string& n);  // SC_* names become Kind::Const
    bool isNumber() const { return kind == Kind::Number; }
    double num() const;
};

struct Call {
    std::string fn;
    std::vector<Expr> args;
};

// A statement inside a function body.
struct Stmt {
    size_t begin = 0, end = 0;  // byte span in the source
    int line = 0;               // 1-based line of `begin`
    std::optional<Call> call;   // set when the statement is fn(simple args...)
};

struct Item {
    enum class Kind { Function, Assign, Call, Other };
    Kind kind = Kind::Other;
    size_t begin = 0, end = 0;  // byte span (no surrounding whitespace)
    int line = 0, lastLine = 0;
    std::string name;            // function name, or the assigned global
    // Function: the body's statements; params as written.
    std::string params;
    std::vector<Stmt> body;
    // Assign of a single string literal (the text table).
    std::optional<std::string> text;
    // Call: the parsed call, when its arguments are simple.
    std::optional<Call> call;
    // A function with no parameters whose body is only calls, run by the boot
    // code (directly or from another builder): the declarative world-building
    // functions (create*).
    bool builder = false;
};

class Document {
public:
    // Parses `source`; on a syntax error returns false and sets error().
    bool parse(std::string source);
    const std::string& source() const { return src_; }
    const std::string& error() const { return error_; }
    std::vector<Item> items;

    std::string text(size_t begin, size_t end) const { return src_.substr(begin, end - begin); }
    const Item* function(const std::string& name) const;

private:
    void markBuilders();
    std::string src_, error_;
};

// Lua string literal for `s` in double quotes, escaping as Lua's %q would.
std::string quote(const std::string& s);

}  // namespace doc
