// Lua 5.0 -> 5.1 bytecode translation, for the 2006 release's scripts.
//
// The two virtual machines are close enough that most instructions map one to
// one, so jump offsets stay valid. What changes:
// - the chunk layout: 5.0 stores debug info before the constants and the code
//   last; 5.1 stores code, constants, nested functions, then debug info, and
//   adds lastlinedefined;
// - instruction fields: 5.0 is OP(6) C(9) B(9) A(8) from bit 0, 5.1 is OP(6)
//   A(8) C(9) B(9); constants in RK operands are >= 250 (MAXSTACK) in 5.0 and
//   have bit 8 set in 5.1;
// - opcode numbers (5.1 adds MOD, LEN, TESTSET, FORPREP, VARARG and drops
//   TFORPREP, SETLISTO);
// - 5.0 TEST is 5.1 TESTSET (same semantics); 5.0 numeric for loops are SUB +
//   JMP + FORLOOP, which 5.1 runs as is, except that its FORLOOP also writes
//   the visible variable at A+3, so the frame needs room for it;
// - vararg functions: 5.0's implicit `arg` table is 5.1's compatibility mode
//   (VARARG_HASARG | VARARG_ISVARARG | VARARG_NEEDSARG), same register.
// Generic for loops (TFORPREP/TFORLOOP) and table constructors with more than
// 32 array items have no direct equivalent; the shipped scripts use neither.

#include "lua50.hpp"

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace lua50 {

namespace {

// Lua 5.0 opcodes, in order.
enum Op50 {
    MOVE, LOADK, LOADBOOL, LOADNIL, GETUPVAL, GETGLOBAL, GETTABLE, SETGLOBAL, SETUPVAL, SETTABLE,
    NEWTABLE, SELF, ADD, SUB, MUL, DIV, POW, UNM, NOT, CONCAT, JMP, EQ, LT, LE, TEST, CALL,
    TAILCALL, RETURN, FORLOOP, TFORLOOP, TFORPREP, SETLIST, SETLISTO, CLOSE, CLOSURE, NUM_OPS50
};

// The 5.1 opcode for each 5.0 one (-1: not translatable).
constexpr int kOp51[NUM_OPS50] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,   // MOVE .. SETTABLE
    10, 11, 12, 13, 14, 15,                  // NEWTABLE SELF ADD SUB MUL DIV
    17, 18, 19, 21, 22, 23, 24, 25,          // POW UNM NOT CONCAT JMP EQ LT LE
    27, 28, 29, 30, 31,                      // TEST(->TESTSET) CALL TAILCALL RETURN FORLOOP
    -1, -1, 34, 34, 35, 36                   // TFORLOOP TFORPREP SETLIST SETLISTO CLOSE CLOSURE
};

constexpr int kMaxStack50 = 250;   // RK operands at or above it are constants (5.0)
constexpr int kBitRK51 = 1 << 8;   // ... marked by this bit in 5.1
constexpr int kFieldsPerFlush50 = 32;

enum Mode { ABC, ABx, AsBx };
Mode mode(int op) {
    switch (op) {
    case LOADK: case GETGLOBAL: case SETGLOBAL: case CLOSURE: case SETLIST: case SETLISTO: return ABx;
    case JMP: case FORLOOP: case TFORPREP: return AsBx;
    default: return ABC;
    }
}

// Which of B and C are RK operands.
void rkOperands(int op, bool& b, bool& c) {
    b = c = false;
    switch (op) {
    case GETTABLE: case SELF: c = true; break;
    case SETTABLE: case ADD: case SUB: case MUL: case DIV: case POW: case EQ: case LT: case LE: b = c = true; break;
    default: break;
    }
}

int fb2int50(int x) { return (x & 7) << (x >> 3); }

int int2fb51(unsigned x) {  // luaO_int2fb
    int e = 0;
    while (x >= 16) { x = (x + 1) >> 1; ++e; }
    return x < 8 ? int(x) : ((e + 1) << 3) | (int(x) - 8);
}

struct Reader {
    const std::string& d;
    size_t p = 0;
    size_t sizeT = 4, numberSize = 8;
    void need(size_t n) const {
        if (p + n > d.size()) throw std::runtime_error("truncated Lua 5.0 chunk");
    }
    uint8_t byte() { need(1); return uint8_t(d[p++]); }
    int32_t i32() { need(4); int32_t v; std::memcpy(&v, d.data() + p, 4); p += 4; return v; }
    std::string raw(size_t n) { need(n); std::string s = d.substr(p, n); p += n; return s; }
    std::string sized() {  // a dumped string: size_t length (with the NUL), then the bytes
        uint64_t n = 0;
        need(sizeT);
        std::memcpy(&n, d.data() + p, sizeT);
        p += sizeT;
        return raw(size_t(n));
    }
};

struct Writer {
    std::string out;
    void byte(int v) { out.push_back(char(v)); }
    void i32(int32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); }
    void u32(uint32_t v) { out.append(reinterpret_cast<const char*>(&v), 4); }
    void sized(const std::string& s) {  // keeps the source's raw length + bytes
        u32(uint32_t(s.size()));
        out += s;
    }
};

uint32_t translateInstruction(uint32_t ins, int& maxStack) {
    const int op = int(ins & 63);
    if (op >= NUM_OPS50 || kOp51[op] < 0)
        throw std::runtime_error("Lua 5.0 opcode " + std::to_string(op) + " has no 5.1 translation");
    const uint32_t a = ins >> 24, b = (ins >> 15) & 511, c = (ins >> 6) & 511, bx = (ins >> 6) & 0x3ffff;
    const uint32_t op51 = uint32_t(kOp51[op]);
    switch (mode(op)) {
    case ABx:
    case AsBx:
        if (op == SETLIST || op == SETLISTO) {
            // R(A)[Bx - Bx%32 + i] := R(A+i), i = 1..Bx%32+1 (SETLISTO: up to the top).
            if (bx >= uint32_t(kFieldsPerFlush50))
                throw std::runtime_error("table constructor with more than 32 array items");
            const uint32_t count = op == SETLISTO ? 0 : bx % kFieldsPerFlush50 + 1;
            return op51 | a << 6 | 1u << 14 | count << 23;  // B = count, C = 1: from index 1
        }
        if (op == FORLOOP && int(a) + 4 > maxStack) maxStack = int(a) + 4;  // 5.1 writes R(A+3)
        return op51 | a << 6 | bx << 14;  // same bias for sBx
    case ABC: break;
    }
    uint32_t b51 = b, c51 = c;
    bool rkB, rkC;
    rkOperands(op, rkB, rkC);
    auto rk = [](uint32_t v) {
        if (v < uint32_t(kMaxStack50)) return v;
        const uint32_t k = v - kMaxStack50;
        if (k >= uint32_t(kBitRK51)) throw std::runtime_error("constant index too large for a 5.1 RK operand");
        return k | kBitRK51;
    };
    if (rkB) b51 = rk(b);
    if (rkC) c51 = rk(c);
    if (op == NEWTABLE) {  // size hints: B array size (5.0 fb encoding), C log2 of the hash size + 1
        b51 = uint32_t(int2fb51(unsigned(fb2int50(int(b)))));
        c51 = uint32_t(int2fb51(c == 0 ? 0u : 1u << (c - 1)));
    }
    return op51 | a << 6 | c51 << 14 | b51 << 23;
}

void function(Reader& r, Writer& w) {
    const std::string source = r.sized();
    const int32_t lineDefined = r.i32();
    const int nups = r.byte(), numParams = r.byte(), isVararg = r.byte();
    int maxStack = r.byte();

    std::vector<int32_t> lines(size_t(r.i32()));
    for (auto& l : lines) l = r.i32();
    struct Local { std::string name; int32_t start, end; };
    std::vector<Local> locals(size_t(r.i32()));
    for (auto& l : locals) { l.name = r.sized(); l.start = r.i32(); l.end = r.i32(); }
    std::vector<std::string> upvalues(size_t(r.i32()));
    for (auto& u : upvalues) u = r.sized();

    // Constants: nil, number, string (5.0 has no boolean constants); then nested functions.
    Writer consts;
    const int32_t nk = r.i32();
    consts.i32(nk);
    for (int32_t i = 0; i < nk; ++i) {
        const int t = r.byte();
        consts.byte(t);
        if (t == 3) consts.out += r.raw(r.numberSize);
        else if (t == 4) consts.sized(r.sized());
        else if (t != 0) throw std::runtime_error("unknown Lua 5.0 constant type " + std::to_string(t));
    }
    Writer nested;
    const int32_t np = r.i32();
    nested.i32(np);
    for (int32_t i = 0; i < np; ++i) function(r, nested);

    std::vector<uint32_t> code(size_t(r.i32()));
    for (auto& ins : code) ins = translateInstruction(uint32_t(r.i32()), maxStack);
    if (maxStack > 250) throw std::runtime_error("function needs more than 250 registers");

    w.sized(source);
    w.i32(lineDefined);
    w.i32(0);  // lastlinedefined: not in 5.0
    w.byte(nups);
    w.byte(numParams);
    w.byte(isVararg ? 1 | 2 | 4 : 0);  // VARARG_HASARG | VARARG_ISVARARG | VARARG_NEEDSARG
    w.byte(maxStack);
    w.i32(int32_t(code.size()));
    for (uint32_t ins : code) w.u32(ins);
    w.out += consts.out;
    w.out += nested.out;
    w.i32(int32_t(lines.size()));
    for (int32_t l : lines) w.i32(l);
    w.i32(int32_t(locals.size()));
    for (const auto& l : locals) { w.sized(l.name); w.i32(l.start); w.i32(l.end); }
    w.i32(int32_t(upvalues.size()));
    for (const auto& u : upvalues) w.sized(u);
}

}  // namespace

bool isChunk(const std::string& bytes) { return bytes.size() > 5 && bytes.compare(0, 4, "\x1bLua") == 0 && uint8_t(bytes[4]) == 0x50; }

std::string translate(const std::string& bytes) {
    Reader r{bytes};
    if (!isChunk(bytes)) throw std::runtime_error("not a Lua 5.0 chunk");
    r.p = 5;
    // endianness, sizeof(int), sizeof(size_t), sizeof(Instruction), SIZE_OP/A/B/C, sizeof(lua_Number)
    const int little = r.byte(), intSize = r.byte(), sizeT = r.byte(), instrSize = r.byte();
    const int sOp = r.byte(), sA = r.byte(), sB = r.byte(), sC = r.byte(), numberSize = r.byte();
    if (little != 1 || intSize != 4 || (sizeT != 4 && sizeT != 8) || instrSize != 4 || sOp != 6 || sA != 8 ||
        sB != 9 || sC != 9 || numberSize != 8)
        throw std::runtime_error("unsupported Lua 5.0 chunk format");
    r.sizeT = size_t(sizeT);
    r.numberSize = size_t(numberSize);
    r.raw(size_t(numberSize));  // test number

    Writer w;
    w.out = "\x1bLua";
    w.byte(0x51);
    w.byte(0);           // official format
    w.byte(1);           // little endian
    w.byte(4);           // sizeof(int)
    w.byte(4);           // sizeof(size_t): strings are written with 32-bit lengths
    w.byte(4);           // sizeof(Instruction)
    w.byte(8);           // sizeof(lua_Number)
    w.byte(0);           // floating point numbers
    function(r, w);
    if (r.p != bytes.size()) throw std::runtime_error("trailing bytes after the Lua 5.0 chunk");
    return w.out;
}

}  // namespace lua50
