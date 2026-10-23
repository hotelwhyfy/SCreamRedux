#include "luadoc.hpp"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <stdexcept>

namespace doc {

namespace {

struct Token {
    enum Type { Name, Keyword, Number, String, Op, Eof } type = Eof;
    size_t begin = 0, end = 0;
    int line = 0;
    std::string text;  // name, keyword or op spelling; decoded value for strings
};

bool isKeyword(const std::string& s) {
    static const char* const kw[] = {"and", "break", "do", "else", "elseif", "end", "false", "for",
                                     "function", "if", "in", "local", "nil", "not", "or", "repeat",
                                     "return", "then", "true", "until", "while"};
    for (const char* k : kw)
        if (s == k) return true;
    return false;
}

struct SyntaxError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Lexer {
public:
    explicit Lexer(const std::string& s) : s_(s) {}

    std::vector<Token> run() {
        std::vector<Token> out;
        for (;;) {
            skipSpace();
            Token t;
            t.begin = i_;
            t.line = line_;
            if (i_ >= s_.size()) {
                t.type = Token::Eof;
                t.end = i_;
                out.push_back(t);
                return out;
            }
            char c = s_[i_];
            if (std::isalpha((unsigned char)c) || c == '_') {
                while (i_ < s_.size() && (std::isalnum((unsigned char)s_[i_]) || s_[i_] == '_')) ++i_;
                t.text = s_.substr(t.begin, i_ - t.begin);
                t.type = isKeyword(t.text) ? Token::Keyword : Token::Name;
            } else if (std::isdigit((unsigned char)c) || (c == '.' && i_ + 1 < s_.size() && std::isdigit((unsigned char)s_[i_ + 1]))) {
                number();
                t.type = Token::Number;
                t.text = s_.substr(t.begin, i_ - t.begin);
            } else if (c == '"' || c == '\'') {
                t.type = Token::String;
                t.text = shortString(c);
            } else if (c == '[' && longBracketLevel() >= 0) {
                t.type = Token::String;
                t.text = longString();
            } else {
                t.type = Token::Op;
                static const char* const ops3[] = {"..."};
                static const char* const ops2[] = {"..", "==", "~=", "<=", ">="};
                size_t n = 1;
                for (const char* o : ops3)
                    if (s_.compare(i_, 3, o) == 0) n = 3;
                if (n == 1)
                    for (const char* o : ops2)
                        if (s_.compare(i_, 2, o) == 0) n = 2;
                t.text = s_.substr(i_, n);
                i_ += n;
            }
            t.end = i_;
            out.push_back(t);
        }
    }

private:
    void skipSpace() {
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (c == '\n') {
                ++line_, ++i_;
            } else if (std::isspace((unsigned char)c)) {
                ++i_;
            } else if (c == '-' && i_ + 1 < s_.size() && s_[i_ + 1] == '-') {
                i_ += 2;
                if (i_ < s_.size() && s_[i_] == '[' && longBracketLevel() >= 0) {
                    longString();
                } else {
                    while (i_ < s_.size() && s_[i_] != '\n') ++i_;
                }
            } else {
                break;
            }
        }
    }

    // At '[': the level of a long bracket opening here ([[ = 0, [=[ = 1, ...), or -1.
    int longBracketLevel() const {
        size_t j = i_ + 1;
        int level = 0;
        while (j < s_.size() && s_[j] == '=') ++j, ++level;
        return j < s_.size() && s_[j] == '[' ? level : -1;
    }

    std::string longString() {
        int level = longBracketLevel();
        i_ += size_t(level) + 2;
        if (i_ < s_.size() && s_[i_] == '\r') ++i_;
        if (i_ < s_.size() && s_[i_] == '\n') ++line_, ++i_;
        std::string close = "]" + std::string(size_t(level), '=') + "]";
        size_t e = s_.find(close, i_);
        if (e == std::string::npos) throw SyntaxError("unfinished long string");
        std::string v = s_.substr(i_, e - i_);
        for (char ch : v)
            if (ch == '\n') ++line_;
        i_ = e + close.size();
        return v;
    }

    std::string shortString(char q) {
        ++i_;
        std::string v;
        while (i_ < s_.size() && s_[i_] != q) {
            char c = s_[i_++];
            if (c == '\n') throw SyntaxError("unfinished string");
            if (c != '\\') {
                v += c;
                continue;
            }
            if (i_ >= s_.size()) break;
            char e = s_[i_++];
            switch (e) {
            case 'a': v += '\a'; break;
            case 'b': v += '\b'; break;
            case 'f': v += '\f'; break;
            case 'n': v += '\n'; break;
            case 'r': v += '\r'; break;
            case 't': v += '\t'; break;
            case 'v': v += '\v'; break;
            case '\n': v += '\n', ++line_; break;
            default:
                if (std::isdigit((unsigned char)e)) {
                    int n = e - '0';
                    for (int k = 0; k < 2 && i_ < s_.size() && std::isdigit((unsigned char)s_[i_]); ++k)
                        n = n * 10 + (s_[i_++] - '0');
                    v += char(n);
                } else {
                    v += e;
                }
            }
        }
        if (i_ >= s_.size()) throw SyntaxError("unfinished string");
        ++i_;
        return v;
    }

    void number() {
        if (s_[i_] == '0' && i_ + 1 < s_.size() && (s_[i_ + 1] == 'x' || s_[i_ + 1] == 'X')) {
            i_ += 2;
            while (i_ < s_.size() && std::isxdigit((unsigned char)s_[i_])) ++i_;
            return;
        }
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (std::isalnum((unsigned char)c) || c == '.') {
                ++i_;
                if ((c == 'e' || c == 'E') && i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
            } else {
                break;
            }
        }
    }

    const std::string& s_;
    size_t i_ = 0;
    int line_ = 1;
};

// Recursive descent over Lua 5.1's grammar. It only records spans: statements
// of function bodies, top-level items, and calls whose arguments it can name.
class Parser {
public:
    Parser(const std::string& src, std::vector<Token> toks) : src_(src), t_(std::move(toks)) {}

    std::vector<Item> chunk() {
        std::vector<Item> items;
        while (!at(Token::Eof)) {
            if (isOp(";")) {
                ++p_;
                continue;
            }
            items.push_back(topItem());
        }
        return items;
    }

private:
    const Token& cur() const { return t_[p_]; }
    bool at(Token::Type ty) const { return cur().type == ty; }
    bool isOp(const char* o) const { return cur().type == Token::Op && cur().text == o; }
    bool isKw(const char* k) const { return cur().type == Token::Keyword && cur().text == k; }
    [[noreturn]] void fail(const std::string& what) const {
        throw SyntaxError("line " + std::to_string(cur().line) + ": " + what + " near '" +
                          src_.substr(cur().begin, cur().end - cur().begin) + "'");
    }
    void expectOp(const char* o) {
        if (!isOp(o)) fail(std::string("'") + o + "' expected");
        ++p_;
    }
    void expectKw(const char* k) {
        if (!isKw(k)) fail(std::string("'") + k + "' expected");
        ++p_;
    }
    std::string expectName() {
        if (!at(Token::Name)) fail("name expected");
        return t_[p_++].text;
    }
    size_t prevEnd() const { return t_[p_ - 1].end; }
    int prevLine() const {
        // Line of the last character of the previous token (long strings span lines).
        const Token& t = t_[p_ - 1];
        int l = t.line;
        for (size_t i = t.begin; i < t.end; ++i)
            if (src_[i] == '\n') ++l;
        return l;
    }

    Item topItem() {
        Item it;
        it.begin = cur().begin;
        it.line = cur().line;
        if (isKw("function")) {
            ++p_;
            it.kind = Item::Kind::Function;
            it.name = expectName();
            while (isOp(".") || isOp(":")) {
                it.name += t_[p_].text;
                ++p_;
                it.name += expectName();
            }
            funcBody(&it);
        } else {
            size_t s = p_;
            std::optional<Call> call;
            statement(&call);
            // name = "text" (one string literal) is the text table.
            if (s + 3 == p_ && t_[s].type == Token::Name && t_[s + 1].type == Token::Op && t_[s + 1].text == "=" &&
                t_[s + 2].type == Token::String) {
                it.kind = Item::Kind::Assign;
                it.name = t_[s].text;
                it.text = t_[s + 2].text;
            } else if (s + 1 < p_ && t_[s].type == Token::Name && t_[s + 1].type == Token::Op && t_[s + 1].text == "=") {
                it.kind = Item::Kind::Assign;
                it.name = t_[s].text;
            } else if (call) {
                it.kind = Item::Kind::Call;
                it.call = call;
            }
        }
        it.end = prevEnd();
        it.lastLine = prevLine();
        return it;
    }

    // ( params ) block end; with `item`, the body's statements are recorded.
    void funcBody(Item* item) {
        expectOp("(");
        size_t ps = cur().begin;
        while (!isOp(")")) {
            if (isOp("...")) ++p_;
            else expectName();
            if (isOp(",")) ++p_;
            else if (!isOp(")")) fail("')' expected");
        }
        if (item) item->params = src_.substr(ps, cur().begin - ps);
        ++p_;
        block(item ? &item->body : nullptr);
        expectKw("end");
        if (item) {
            item->builder = item->params.empty() && !item->body.empty();  // a candidate; see markBuilders
            for (const Stmt& st : item->body)
                if (!st.call) item->builder = false;
        }
    }

    bool blockEnd() const {
        return at(Token::Eof) || isKw("end") || isKw("else") || isKw("elseif") || isKw("until");
    }

    void block(std::vector<Stmt>* out) {
        while (!blockEnd()) {
            if (isOp(";")) {
                ++p_;
                continue;
            }
            Stmt st;
            st.begin = cur().begin;
            st.line = cur().line;
            bool last = isKw("return") || isKw("break");
            statement(out ? &st.call : nullptr);
            st.end = prevEnd();
            if (out) out->push_back(st);
            if (last) {
                if (isOp(";")) ++p_;
                break;
            }
        }
    }

    void statement(std::optional<Call>* call) {
        if (isKw("if")) {
            ++p_;
            expr();
            expectKw("then");
            block(nullptr);
            while (isKw("elseif")) {
                ++p_;
                expr();
                expectKw("then");
                block(nullptr);
            }
            if (isKw("else")) {
                ++p_;
                block(nullptr);
            }
            expectKw("end");
        } else if (isKw("while")) {
            ++p_;
            expr();
            expectKw("do");
            block(nullptr);
            expectKw("end");
        } else if (isKw("do")) {
            ++p_;
            block(nullptr);
            expectKw("end");
        } else if (isKw("for")) {
            ++p_;
            expectName();
            if (isOp("=")) {
                ++p_;
                exprList();
            } else {
                while (isOp(",")) ++p_, expectName();
                expectKw("in");
                exprList();
            }
            expectKw("do");
            block(nullptr);
            expectKw("end");
        } else if (isKw("repeat")) {
            ++p_;
            block(nullptr);
            expectKw("until");
            expr();
        } else if (isKw("function")) {
            ++p_;
            expectName();
            while (isOp(".") || isOp(":")) ++p_, expectName();
            funcBody(nullptr);
        } else if (isKw("local")) {
            ++p_;
            if (isKw("function")) {
                ++p_;
                expectName();
                funcBody(nullptr);
            } else {
                expectName();
                while (isOp(",")) ++p_, expectName();
                if (isOp("=")) ++p_, exprList();
            }
        } else if (isKw("return")) {
            ++p_;
            if (!blockEnd() && !isOp(";")) exprList();
        } else if (isKw("break")) {
            ++p_;
        } else {
            exprStatement(call);
        }
    }

    void exprStatement(std::optional<Call>* call) {
        size_t s = p_;
        bool lastWasCall = false;
        suffixedExpr(&lastWasCall);
        if (isOp("=") || isOp(",")) {
            while (isOp(",")) ++p_, suffixedExpr(nullptr);
            expectOp("=");
            exprList();
            return;
        }
        if (!lastWasCall) fail("syntax error");
        // name ( args ) with nothing in between: a call the editor can model.
        if (call && t_[s].type == Token::Name && t_[s + 1].type == Token::Op && t_[s + 1].text == "(" &&
            t_[p_ - 1].text == ")") {
            Call c;
            c.fn = t_[s].text;
            size_t q = s + 2;
            int depth = 0;
            size_t argStart = q;
            bool ok = true;
            auto flush = [&](size_t endTok) {
                if (endTok == argStart) return;
                c.args.push_back(classify(argStart, endTok));
            };
            for (; q < p_ - 1; ++q) {
                const Token& k = t_[q];
                if (k.type == Token::Op && (k.text == "(" || k.text == "{" || k.text == "[")) ++depth;
                else if (k.type == Token::Op && (k.text == ")" || k.text == "}" || k.text == "]")) {
                    if (--depth < 0) ok = false;  // f(a)(b): not one call
                }
                else if (k.type == Token::Keyword && k.text == "function") ok = false;
                else if (depth == 0 && k.type == Token::Op && k.text == ",") {
                    flush(q);
                    argStart = q + 1;
                }
            }
            if (depth != 0) ok = false;
            if (ok && q == p_ - 1) {
                flush(q);
                *call = std::move(c);
            }
        }
    }

    Expr classify(size_t a, size_t b) const {
        Expr e;
        e.src = src_.substr(t_[a].begin, t_[b - 1].end - t_[a].begin);
        if (b == a + 1) {
            const Token& k = t_[a];
            switch (k.type) {
            case Token::Number: e.kind = Expr::Kind::Number; break;
            case Token::String:
                e.kind = Expr::Kind::String;
                e.str = k.text;
                break;
            case Token::Name: e.kind = k.text.rfind("SC_", 0) == 0 ? Expr::Kind::Const : Expr::Kind::Name; break;
            case Token::Keyword:
                if (k.text == "nil") e.kind = Expr::Kind::Nil;
                break;
            default: break;
            }
        } else if (b == a + 2 && t_[a].type == Token::Op && t_[a].text == "-" && t_[a + 1].type == Token::Number) {
            e.kind = Expr::Kind::Number;
        }
        return e;
    }

    void exprList() {
        expr();
        while (isOp(",")) ++p_, expr();
    }

    bool binop() const {
        if (cur().type == Token::Keyword) return cur().text == "and" || cur().text == "or";
        if (cur().type != Token::Op) return false;
        static const char* const ops[] = {"+", "-", "*", "/", "%", "^", "..", "==", "~=", "<", "<=", ">", ">="};
        for (const char* o : ops)
            if (cur().text == o) return true;
        return false;
    }

    void expr() {
        while (isKw("not") || isOp("-") || isOp("#")) ++p_;
        simpleExpr();
        while (binop()) {
            ++p_;
            while (isKw("not") || isOp("-") || isOp("#")) ++p_;
            simpleExpr();
        }
    }

    void simpleExpr() {
        if (at(Token::Number) || at(Token::String) || isKw("nil") || isKw("true") || isKw("false") || isOp("...")) {
            ++p_;
        } else if (isOp("{")) {
            table();
        } else if (isKw("function")) {
            ++p_;
            funcBody(nullptr);
        } else {
            suffixedExpr(nullptr);
        }
    }

    void table() {
        expectOp("{");
        while (!isOp("}")) {
            if (isOp("[")) {
                ++p_;
                expr();
                expectOp("]");
                expectOp("=");
                expr();
            } else if (at(Token::Name) && t_[p_ + 1].type == Token::Op && t_[p_ + 1].text == "=") {
                p_ += 2;
                expr();
            } else {
                expr();
            }
            if (isOp(",") || isOp(";")) ++p_;
            else if (!isOp("}")) fail("'}' expected");
        }
        ++p_;
    }

    void suffixedExpr(bool* lastWasCall) {
        if (at(Token::Name)) {
            ++p_;
        } else if (isOp("(")) {
            ++p_;
            expr();
            expectOp(")");
        } else {
            fail("unexpected symbol");
        }
        bool called = false;
        for (;;) {
            if (isOp(".")) {
                ++p_, expectName();
                called = false;
            } else if (isOp("[")) {
                ++p_, expr(), expectOp("]");
                called = false;
            } else if (isOp(":")) {
                ++p_, expectName(), args();
                called = true;
            } else if (isOp("(") || isOp("{") || at(Token::String)) {
                args();
                called = true;
            } else {
                break;
            }
        }
        if (lastWasCall) *lastWasCall = called;
    }

    void args() {
        if (at(Token::String)) {
            ++p_;
        } else if (isOp("{")) {
            table();
        } else {
            expectOp("(");
            if (!isOp(")")) exprList();
            expectOp(")");
        }
    }

    const std::string& src_;
    std::vector<Token> t_;
    size_t p_ = 0;
};

}  // namespace

Expr Expr::number(int v) {
    Expr e;
    e.kind = Kind::Number;
    e.src = std::to_string(v);
    return e;
}

Expr Expr::string(const std::string& s) {
    Expr e;
    e.kind = Kind::String;
    e.str = s;
    e.src = quote(s);
    return e;
}

Expr Expr::name(const std::string& n) {
    Expr e;
    e.kind = n.rfind("SC_", 0) == 0 ? Kind::Const : Kind::Name;
    e.src = n;
    return e;
}

double Expr::num() const { return std::strtod(src.c_str(), nullptr); }

bool Document::parse(std::string source) {
    src_ = std::move(source);
    error_.clear();
    items.clear();
    try {
        Parser p(src_, Lexer(src_).run());
        items = p.chunk();
        markBuilders();
    } catch (const SyntaxError& e) {
        error_ = e.what();
        items.clear();
        return false;
    }
    return true;
}

// Builders are the functions the boot code runs to build the world: called
// from the top level, or from another builder, and made only of calls.
void Document::markBuilders() {
    std::map<std::string, Item*> candidates;
    for (Item& it : items)
        if (it.kind == Item::Kind::Function && it.builder) candidates[it.name] = &it, it.builder = false;
    std::vector<std::string> todo;
    for (const Item& it : items)
        if (it.kind == Item::Kind::Call) todo.push_back(it.call->fn);
    while (!todo.empty()) {
        auto f = candidates.find(todo.back());
        todo.pop_back();
        if (f == candidates.end() || f->second->builder) continue;
        f->second->builder = true;
        for (const Stmt& st : f->second->body) todo.push_back(st.call->fn);
    }
}

const Item* Document::function(const std::string& name) const {
    for (const Item& it : items)
        if (it.kind == Item::Kind::Function && it.name == name) return &it;
    return nullptr;
}

std::string quote(const std::string& s) {
    std::string out = "\"";
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\0': out += "\\0"; break;
        default:
            if (c < 32 || c == 127) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\%d", c);
                out += buf;
            } else {
                out += char(c);
            }
        }
    }
    return out + "\"";
}

}  // namespace doc
