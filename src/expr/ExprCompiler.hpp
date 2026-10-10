#pragma once

// ExprCompiler.hpp -- one Desmos-style equation to an immutable Program.
//
// The language is what Desmos accepts as one equation
// (doc/decisions/0008-desmos-is-the-language.md):
//
//     y = a sin(bx)                      a curve; a and b become knobs
//     r = 1 + cos(5θ/3)                  a polar curve
//     (cos(3t), sin(2t))                 a parametric curve
//     y = [0,3,5,7,10][floor(5x/2π)+1]   a list, indexed from 1
//     y = {x < π: sin(x), -1}            piecewise, lazy
//
// A restriction at the very end whose bounds are numbers, `{-8 ≤ x ≤ 8}`
// or `{0 ≤ t ≤ π}`, is the domain: the range one lap sweeps. Any other
// condition at the end is an ordinary Desmos restriction, a gap outside it.
//
// `f(x) =` and `f(θ) =` are accepted for `y =` and `r =`, and a bare
// expression is `y = ...`. Multiplication can be implied (2x, πx, 2(x+1),
// x sin x), |...| is the absolute value, and a function written without
// parentheses takes the product that follows it (sin 2x is sin(2x)).
// π, θ and τ may be typed as symbols or as pi, theta and tau.
//
// A letter that is not x, y, r, θ, t or e is a knob, assigned to slots in
// order of first appearance; a seventh is an error. Angles are radians.
//
// There is no state: a program is a function of its swept variable and its
// knobs, and nothing else.
//
// Compilation is a UI-thread activity. It allocates freely, reports the
// first error with a source position, and never touches a running
// Instance: a failed compile leaves the previous program exactly as it was.

#include "expr/ExprProgram.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace Lives {
namespace Expr {

struct CompileError {
    bool ok = true;
    int  line = 0, col = 0;
    std::string message;
};

// Knobs default to whole numbers from 0 to 10, starting at 1 (the module
// rounds unless a knob is set to fine steps). On the default 0..2π domain a
// whole-number frequency always closes the curve at the end of the lap, so
// sin(bx) never jumps at the wrap. A negative frequency only mirrors the
// curve, so the range starts at 0. Default, chosen 2026-10-07 to be tried
// by ear (DR-0008, "Letters are knobs").
static const double kKnobDefault = 1.0;
static const double kKnobMin     = 0.0;
static const double kKnobMax     = 10.0;

namespace detail {

enum TokKind { T_END, T_NUMBER, T_IDENT, T_FUNC, T_OP };

struct Token {
    TokKind kind;
    std::string text;
    double num;
    int line, col;
};

struct FnInfo {
    const char* name;
    Op   op;
    int  minArgs, maxArgs;   // maxArgs < 0: any number (min, max)
};

/// Desmos's names. sec/csc/cot are emitted as 1/cos etc.; arctan(y, x)
/// is the two-argument form.
inline const FnInfo* lookupFn(const std::string& n) {
    static const FnInfo table[] = {
        {"sin", OP_SIN, 1, 1},       {"cos", OP_COS, 1, 1},
        {"tan", OP_TAN, 1, 1},       {"sec", OP_COS, 1, 1},
        {"csc", OP_SIN, 1, 1},       {"cot", OP_TAN, 1, 1},
        {"arcsin", OP_ASIN, 1, 1},   {"arccos", OP_ACOS, 1, 1},
        {"arctan", OP_ATAN, 1, 2},
        {"sinh", OP_SINH, 1, 1},     {"cosh", OP_COSH, 1, 1},
        {"tanh", OP_TANH, 1, 1},
        {"abs", OP_ABS, 1, 1},       {"sign", OP_SIGN, 1, 1},
        {"sgn", OP_SIGN, 1, 1},
        {"floor", OP_FLOOR, 1, 1},   {"ceil", OP_CEIL, 1, 1},
        {"round", OP_ROUND, 1, 1},
        {"mod", OP_MOD, 2, 2},
        {"min", OP_MIN, 1, -1},      {"max", OP_MAX, 1, -1},
        {"exp", OP_EXP, 1, 1},       {"ln", OP_LOG, 1, 1},
        {"log", OP_LOG10, 1, 1},     {"sqrt", OP_SQRT, 1, 1},
        {"log2", OP_LOG2, 1, 1},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++)
        if (n == table[i].name) return &table[i];
    return 0;
}

/// Multi-letter names the lexer recognises inside a run of letters.
inline bool isWordName(const std::string& n) {
    return lookupFn(n) || n == "pi" || n == "theta" || n == "tau";
}

struct Lexer {
    const std::string& src;
    size_t pos = 0;
    int line = 1, col = 1;
    std::vector<Token> out;
    CompileError err;

    explicit Lexer(const std::string& s) : src(s) {}

    void fail(const std::string& m, int l, int c) {
        if (!err.ok) return;
        err.ok = false; err.message = m; err.line = l; err.col = c;
    }

    void push(TokKind k, const std::string& t, double n, int l, int c) {
        Token tk; tk.kind = k; tk.text = t; tk.num = n; tk.line = l; tk.col = c;
        out.push_back(tk);
    }

    /// The UTF-8 symbols people paste, as the ASCII they stand for.
    const char* symbol(size_t& len) const {
        struct S { const char* utf8; const char* ascii; };
        static const S table[] = {
            {"\xCF\x80", "pi"}, {"\xCE\xB8", "theta"}, {"\xCF\x84", "tau"},
            {"\xE2\x89\xA4", "<="}, {"\xE2\x89\xA5", ">="},
            {"\xC2\xB7", "*"}, {"\xE2\x8B\x85", "*"}, {"\xC3\x97", "*"},
            {"\xE2\x88\x92", "-"},
            {"\xE2\x88\xA7", "and"}, {"\xE2\x88\xA8", "or"},
        };
        for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
            const size_t n = std::strlen(table[i].utf8);
            if (src.compare(pos, n, table[i].utf8) == 0) { len = n; return table[i].ascii; }
        }
        return 0;
    }

    static bool isLogical(const std::string& w) { return w == "and" || w == "or"; }

    /// `log2`, `log_2`, `log_{2}` and `log_10`: a base written after `log`.
    /// Base 10 is plain log; base 2 is log2. Any other base is an error
    /// rather than a guess, because without this `log2(8)` reads as
    /// log(2*8) and compiles to the wrong number.
    bool logBase(int l) {
        size_t q = pos;
        const bool sub = q < src.size() && src[q] == '_';
        if (sub) q++;
        const bool brace = sub && q < src.size() && src[q] == '{';
        if (brace) q++;
        const size_t d0 = q;
        while (q < src.size() && std::isdigit((unsigned char) src[q])) q++;
        if (q == d0) {
            if (sub) { fail("log_ needs a base: log_2 or log_10", l, col); return false; }
            return true;
        }
        if (brace) {
            if (q >= src.size() || src[q] != '}') { fail("expected '}' after the log base", l, col); return false; }
            q++;
        }
        const std::string base = src.substr(d0, q - d0 - (brace ? 1 : 0));
        if (base == "2") out.back().text = "log2";
        else if (base != "10") {
            fail("log base " + base + " is not supported; write ln(x)/ln(" + base + ")", l, col);
            return false;
        }
        col += int(q - pos);
        pos = q;
        return true;
    }

    /// A run of letters is split the way Desmos reads it: the longest
    /// function or constant name at each point, otherwise one letter, so
    /// `ab` is a·b and `sinx` is sin x.
    void letters(const std::string& run, int l, int c) {
        size_t i = 0;
        while (i < run.size()) {
            size_t best = 0;
            for (size_t n = run.size() - i; n >= 2; n--)
                if (isWordName(run.substr(i, n))) { best = n; break; }
            if (best) {
                const std::string w = run.substr(i, best);
                push(lookupFn(w) ? T_FUNC : T_IDENT, w, 0, l, c + int(i));
                i += best;
            } else {
                push(T_IDENT, run.substr(i, 1), 0, l, c + int(i));
                i++;
            }
        }
    }

    bool run() {
        while (pos < src.size()) {
            const char ch = src[pos];
            if (ch == '\n') { pos++; line++; col = 1; continue; }
            if (ch == '\r' || ch == ' ' || ch == '\t') { pos++; col++; continue; }
            const int l = line, c = col;
            // Floor and ceiling brackets are the function and its parentheses.
            if (src.compare(pos, 3, "\xE2\x8C\x8A") == 0 || src.compare(pos, 3, "\xE2\x8C\x88") == 0) {
                push(T_FUNC, src[pos + 2] == '\x8A' ? "floor" : "ceil", 0, l, c);
                push(T_OP, "(", 0, l, c);
                pos += 3; col++;
                continue;
            }
            if (src.compare(pos, 3, "\xE2\x8C\x8B") == 0 || src.compare(pos, 3, "\xE2\x8C\x89") == 0) {
                push(T_OP, ")", 0, l, c);
                pos += 3; col++;
                continue;
            }
            size_t slen = 0;
            if (const char* s = symbol(slen)) {
                // "and"/"or" are operators even though they spell like names.
                if (std::isalpha((unsigned char) s[0]) && !isLogical(s)) push(T_IDENT, s, 0, l, c);
                else push(T_OP, s, 0, l, c);
                pos += slen; col++;
                continue;
            }
            if (ch == '\\') {
                fail("this looks like LaTeX copied from Desmos; type the equation as text", l, c);
                return false;
            }
            if (std::isdigit((unsigned char) ch) || (ch == '.' && pos + 1 < src.size() && std::isdigit((unsigned char) src[pos + 1]))) {
                // No exponent notation: in Desmos `2e3` is 2·e·3.
                const size_t start = pos;
                bool dot = false;
                while (pos < src.size() && (std::isdigit((unsigned char) src[pos]) || (src[pos] == '.' && !dot))) {
                    if (src[pos] == '.') dot = true;
                    pos++;
                }
                const std::string text = src.substr(start, pos - start);
                col += int(pos - start);
                push(T_NUMBER, text, std::strtod(text.c_str(), 0), l, c);
                continue;
            }
            if (std::isalpha((unsigned char) ch)) {
                const size_t start = pos;
                while (pos < src.size() && std::isalpha((unsigned char) src[pos])) pos++;
                col += int(pos - start);
                const std::string run = src.substr(start, pos - start);
                if (isLogical(run)) { push(T_OP, run, 0, l, c); continue; }
                letters(run, l, c);
                if (out.back().kind == T_FUNC && out.back().text == "log" && !logBase(l)) return false;
                // A subscript belongs to the single letter before it: a_1.
                if (pos + 1 < src.size() && src[pos] == '_' && std::isalnum((unsigned char) src[pos + 1])) {
                    Token& last = out.back();
                    if (last.kind != T_IDENT || last.text.size() != 1) {
                        fail("only a single letter can take a subscript", l, col);
                        return false;
                    }
                    const size_t s0 = pos++;
                    while (pos < src.size() && std::isalnum((unsigned char) src[pos])) pos++;
                    last.text += src.substr(s0, pos - s0);
                    col += int(pos - s0);
                }
                continue;
            }
            static const char* two[] = {"<=", ">=", 0};
            bool matched = false;
            for (int i = 0; two[i]; i++) {
                if (src.compare(pos, 2, two[i]) == 0) {
                    push(T_OP, two[i], 0, l, c); pos += 2; col += 2; matched = true; break;
                }
            }
            if (matched) continue;
            if (std::strchr("+-*/^()[]{},:|=<>", ch)) {
                push(T_OP, std::string(1, ch), 0, l, c); pos++; col++;
                continue;
            }
            fail(std::string("unexpected character '") + ch + "'", l, c);
            return false;
        }
        push(T_END, "", 0, line, col);
        return true;
    }
};

enum NodeKind {
    N_NUM, N_SWEPT, N_KNOB, N_UNARY, N_BINARY, N_CALL, N_AND, N_IF,
    N_UNDEF, N_INDEX
};

struct Node {
    NodeKind kind;
    std::string name;      // function name or operator text
    double num = 0.0;      // N_NUM value, N_KNOB slot, N_UNDEF reason
    std::vector<int> kids; // N_INDEX: index first, then the elements
    int line = 0, col = 0;
};

struct Compiler {
    std::vector<Token> toks;
    size_t p = 0;
    std::vector<Node> nodes;
    CompileError err;
    Program prog;
    int depth = 0;
    int absDepth = 0;      // open |...| at this bracket level
    int nextReg = 0;

    void fail(const std::string& m, int l, int c) {
        if (!err.ok) return;
        err.ok = false; err.message = m; err.line = l; err.col = c;
    }
    void fail(const std::string& m) { fail(m, cur().line, cur().col); }

    const Token& cur() const { return toks[p]; }
    const Token& at(size_t i) const { return toks[i < toks.size() ? i : toks.size() - 1]; }
    bool atOp(const char* s) const { return cur().kind == T_OP && cur().text == s; }
    bool accept(const char* s) { if (atOp(s)) { p++; return true; } return false; }
    bool expect(const char* s) {
        if (accept(s)) return true;
        fail(std::string("expected '") + s + "'" + (cur().kind == T_END ? " before the end" : ""));
        return false;
    }

    int node(NodeKind k, const std::string& name, int line, int col) {
        Node n; n.kind = k; n.name = name; n.line = line; n.col = col;
        nodes.push_back(n);
        return int(nodes.size()) - 1;
    }
    int num(double v, int line, int col) {
        int n = node(N_NUM, "", line, col);
        nodes[n].num = v;
        return n;
    }
    int binary(const std::string& op, int a, int b, int line, int col) {
        int n = node(N_BINARY, op, line, col);
        nodes[n].kids.push_back(a); nodes[n].kids.push_back(b);
        return n;
    }

    // ---- names ---------------------------------------------------------

    /// x, θ or t, whichever the mode sweeps; anything else is an error
    /// that names the form the user probably meant.
    int swept(const std::string& name, int l, int c) {
        const char* want = prog.mode == MODE_X ? "x" : prog.mode == MODE_POLAR ? "theta" : "t";
        if (name == want) {
            prog.usesX = true;
            return node(N_SWEPT, name, l, c);
        }
        if (name == "x") fail("a polar or parametric curve is not a function of x", l, c);
        else if (name == "theta") fail("θ is for polar curves: write r = ...", l, c);
        else fail("t is for parametric curves: write (x(t), y(t))", l, c);
        return -1;
    }

    int knob(const std::string& name, int l, int c) {
        for (size_t i = 0; i < prog.params.size(); i++)
            if (prog.params[i].name == name) {
                int n = node(N_KNOB, name, l, c);
                nodes[n].num = prog.params[i].slot;
                return n;
            }
        if (int(prog.params.size()) >= kMaxSlots) {
            fail("too many knobs: '" + name + "' would be letter " + std::to_string(kMaxSlots + 1) +
                 ", and there are " + std::to_string(kMaxSlots), l, c);
            return -1;
        }
        ParamDecl d;
        d.name = name;
        d.slot = int(prog.params.size());
        d.def = kKnobDefault;
        d.min = kKnobMin;
        d.max = kKnobMax;
        d.cvScale = (d.max - d.min) / 10.0;   // 10 V sweeps the range: 1 per volt
        prog.params.push_back(d);
        int n = node(N_KNOB, name, l, c);
        nodes[n].num = d.slot;
        return n;
    }

    int name(const Token& t) {
        const std::string& n = t.text;
        if (n == "pi")  return num(3.14159265358979323846, t.line, t.col);
        if (n == "tau") return num(6.28318530717958647692, t.line, t.col);
        if (n == "e")   return num(2.71828182845904523536, t.line, t.col);
        if (n == "x" || n == "theta" || n == "t") return swept(n, t.line, t.col);
        if (n == "y" || n == "r") {
            fail(n + " belongs on the left: " + n + " = ...", t.line, t.col);
            return -1;
        }
        return knob(n, t.line, t.col);
    }

    // ---- expressions ---------------------------------------------------

    int parseExpr() {
        if (++depth > kMaxNesting) { fail("expression nested too deeply"); depth--; return -1; }
        int r = parseAdd();
        depth--;
        return r;
    }

    int parseAdd() {
        int lhs = parseMul();
        while (err.ok && (atOp("+") || atOp("-"))) {
            const int l = cur().line, c = cur().col;
            const std::string op = cur().text; p++;
            int rhs = parseMul();
            lhs = binary(op, lhs, rhs, l, c);
        }
        return lhs;
    }

    /// Can the current token begin a factor that multiplies the previous
    /// one with no operator written?
    bool startsImpliedFactor() const {
        const Token& t = cur();
        if (t.kind == T_NUMBER || t.kind == T_IDENT || t.kind == T_FUNC) return true;
        if (t.kind != T_OP) return false;
        if (t.text == "(" || t.text == "[" || t.text == "{") return true;
        // Inside |...| a bar closes; outside, it opens a new factor: 2|x|.
        return t.text == "|" && absDepth == 0;
    }

    /// After a complete factor, `mod` is the operator `a mod b` unless it
    /// is followed by a two-argument call, `mod(a, b)`.
    bool atInfixMod() const {
        if (cur().kind != T_FUNC || cur().text != "mod") return false;
        if (!(at(p + 1).kind == T_OP && at(p + 1).text == "(")) return true;
        int level = 0;
        for (size_t i = p + 1; i < toks.size(); i++) {
            const Token& t = toks[i];
            if (t.kind == T_END) break;
            if (t.kind != T_OP) continue;
            if (t.text == "(" || t.text == "[" || t.text == "{") level++;
            else if (t.text == ")" || t.text == "]" || t.text == "}") { if (--level == 0) break; }
            else if (t.text == "," && level == 1) return false;
        }
        return true;
    }

    int parseMul() {
        int lhs = parseUnary();
        while (err.ok) {
            const int l = cur().line, c = cur().col;
            if (atInfixMod()) {
                p++;
                int rhs = parseUnary();
                int n = node(N_CALL, "mod", l, c);
                nodes[n].kids.push_back(lhs); nodes[n].kids.push_back(rhs);
                lhs = n;
            } else if (atOp("*") || atOp("/")) {
                const std::string op = cur().text; p++;
                int rhs = parseUnary();
                lhs = binary(op, lhs, rhs, l, c);
            } else if (startsImpliedFactor()) {
                int rhs = parsePower();
                lhs = binary("*", lhs, rhs, l, c);
            } else {
                break;
            }
        }
        return lhs;
    }

    int parseUnary() {
        if (atOp("-")) {
            const int l = cur().line, c = cur().col; p++;
            if (++depth > kMaxNesting) { fail("expression nested too deeply"); depth--; return -1; }
            int k = parseUnary();
            depth--;
            int n = node(N_UNARY, "-", l, c);
            nodes[n].kids.push_back(k);
            return n;
        }
        if (atOp("+")) { p++; return parseUnary(); }
        return parsePower();
    }

    int parsePower() {
        int base = parsePostfix();
        if (err.ok && atOp("^")) {
            const int l = cur().line, c = cur().col; p++;
            int ex = parseUnary();   // right-associative, binds -x^2 as -(x^2)
            return binary("^", base, ex, l, c);
        }
        return base;
    }

    int parsePostfix() {
        int e = parsePrimary();
        if (err.ok && e >= 0 && nodes[e].kind == N_INDEX && nodes[e].kids.empty() == false
            && nodes[e].kids[0] < 0) {
            // A list literal: it needs its index now.
            const int l = cur().line, c = cur().col;
            if (!accept("[")) {
                fail("a list needs an index here, as in [1,2,3][2]", nodes[e].line, nodes[e].col);
                return -1;
            }
            int idx = bracketed("]");
            if (!err.ok) return -1;
            nodes[e].kids[0] = idx;
            nodes[e].line = l; nodes[e].col = c;
        }
        return e;
    }

    /// Parse an expression up to `close` with its own |...| level.
    int bracketed(const char* close) {
        const int saved = absDepth;
        absDepth = 0;
        int e = parseExpr();
        absDepth = saved;
        if (!err.ok) return -1;
        if (!expect(close)) return -1;
        return e;
    }

    int parsePrimary() {
        const Token t = cur();
        if (t.kind == T_NUMBER) { p++; return num(t.num, t.line, t.col); }
        if (t.kind == T_IDENT) { p++; return name(t); }
        if (t.kind == T_FUNC) { p++; return parseCall(t); }
        if (accept("(")) return bracketed(")");
        if (accept("|")) {
            const int saved = absDepth;
            absDepth = 1;
            int e = parseExpr();
            absDepth = saved;
            if (!err.ok) return -1;
            if (!expect("|")) return -1;
            int n = node(N_CALL, "abs", t.line, t.col);
            nodes[n].kids.push_back(e);
            return n;
        }
        if (accept("[")) return parseList(t);
        if (accept("{")) return parsePiecewise(t);
        if (t.kind == T_END) fail("unexpected end of equation", t.line, t.col);
        else if (t.kind == T_OP && t.text == "=")
            fail("one equation, with y, r or f(x) on the left", t.line, t.col);
        else fail("unexpected '" + t.text + "'", t.line, t.col);
        return -1;
    }

    /// `sin(x)`, or `sin 2x`: without parentheses the argument is the
    /// product that follows, up to an operator or another function.
    int parseCall(const Token& t) {
        const FnInfo* fi = lookupFn(t.text);
        int n = node(N_CALL, t.text, t.line, t.col);
        if (accept("(")) {
            const int saved = absDepth;
            absDepth = 0;
            for (;;) {
                int a = parseExpr();
                if (!err.ok) return -1;
                nodes[n].kids.push_back(a);
                if (accept(",")) continue;
                break;
            }
            absDepth = saved;
            if (!expect(")")) return -1;
        } else {
            if (!startsImpliedFactor() || cur().kind == T_FUNC) {
                fail(t.text + " needs an argument", t.line, t.col);
                return -1;
            }
            int a = parsePower();
            while (err.ok && startsImpliedFactor() && cur().kind != T_FUNC) {
                const int l = cur().line, c = cur().col;
                a = binary("*", a, parsePower(), l, c);
            }
            nodes[n].kids.push_back(a);
        }
        const int got = int(nodes[n].kids.size());
        if (got < fi->minArgs || (fi->maxArgs >= 0 && got > fi->maxArgs)) {
            std::string want = fi->maxArgs < 0 ? "at least " + std::to_string(fi->minArgs)
                             : fi->minArgs == fi->maxArgs ? std::to_string(fi->minArgs)
                             : std::to_string(fi->minArgs) + " or " + std::to_string(fi->maxArgs);
            fail(t.text + "() takes " + want + " argument" + (want == "1" ? "" : "s"), t.line, t.col);
            return -1;
        }
        return n;
    }

    /// `[a, b, c]` -- the index follows in parsePostfix. kids[0] is the
    /// index slot, -1 until it is parsed.
    int parseList(const Token& t) {
        int n = node(N_INDEX, "", t.line, t.col);
        nodes[n].kids.push_back(-1);
        const int saved = absDepth;
        absDepth = 0;
        if (atOp("]")) { fail("an empty list", t.line, t.col); return -1; }
        for (;;) {
            int e = parseExpr();
            if (!err.ok) return -1;
            nodes[n].kids.push_back(e);
            if (accept(",")) continue;
            break;
        }
        absDepth = saved;
        if (!expect("]")) return -1;
        return n;
    }

    bool atCompare() const {
        return atOp("<") || atOp("<=") || atOp(">") || atOp(">=") || atOp("=");
    }

    /// `a < b <= c`: a chain of comparisons, all of which must hold.
    int parseCondition(int lhs) {
        int result = -1;
        while (err.ok && atCompare()) {
            const int l = cur().line, c = cur().col;
            std::string op = cur().text; p++;
            if (op == "=") op = "==";
            int rhs = parseExpr();
            if (!err.ok) return -1;
            int cmp = binary(op, lhs, rhs, l, c);
            if (result < 0) result = cmp;
            else {
                int a = node(N_AND, "and", l, c);
                nodes[a].kids.push_back(result); nodes[a].kids.push_back(cmp);
                result = a;
            }
            lhs = rhs;
        }
        return result;
    }

    struct Saved { size_t p, nodes, params; bool usesX; int absDepth, depth; };
    Saved save() const { return {p, nodes.size(), prog.params.size(), prog.usesX, absDepth, depth}; }
    void restore(const Saved& s) {
        p = s.p; nodes.resize(s.nodes); prog.params.resize(s.params);
        prog.usesX = s.usesX; absDepth = s.absDepth; depth = s.depth; err = CompileError();
    }

    /// `c1 or c2`, where each side is a chain of `and`. Desmos itself has no
    /// `or`; the creators' videos write it, and it means what it says.
    int parseLogical() {
        int a = parseConjunction();
        while (err.ok && atOp("or")) {
            const int l = cur().line, c = cur().col; p++;
            int b = parseConjunction();
            if (!err.ok) return -1;
            int n = node(N_IF, "or", l, c);
            nodes[n].kids.push_back(a);
            nodes[n].kids.push_back(num(1.0, l, c));
            nodes[n].kids.push_back(b);
            a = n;
        }
        return a;
    }

    int parseConjunction() {
        int a = parseComparison();
        while (err.ok && atOp("and")) {
            const int l = cur().line, c = cur().col; p++;
            int b = parseComparison();
            if (!err.ok) return -1;
            int n = node(N_AND, "and", l, c);
            nodes[n].kids.push_back(a); nodes[n].kids.push_back(b);
            a = n;
        }
        return a;
    }

    /// `a < b`, a chain `a < b <= c`, or a parenthesised condition. `(x+1) < 2`
    /// also starts with a bracket, so the bracketed reading is tried first
    /// and abandoned if it is not a condition closed by `)`.
    int parseComparison() {
        if (atOp("(")) {
            const Saved before = save();
            p++;
            int inner = parseLogical();
            if (err.ok && accept(")") && !atCompare()) return inner;
            restore(before);
        }
        const int l = cur().line, c = cur().col;
        int e = parseExpr();
        if (!err.ok) return -1;
        if (!atCompare()) { fail("expected a comparison, as in x < 1", l, c); return -1; }
        return parseCondition(e);
    }

    /// `{c1: a, c2: b, else}`. A condition without a value is 1 where it
    /// holds; with no else, the value is undefined outside every condition.
    int parsePiecewise(const Token& t) {
        const int saved = absDepth;
        absDepth = 0;
        struct Branch { int cond, value; };
        std::vector<Branch> branches;
        int otherwise = -1;
        for (;;) {
            const int l = cur().line, c = cur().col;
            // A branch starts with a condition; if what follows does not
            // parse as one, it is the value that applies otherwise.
            const Saved before = save();
            int cond = parseLogical();
            if (err.ok) {
                Branch b;
                b.cond = cond;
                b.value = accept(":") ? parseExpr() : num(1.0, l, c);
                if (!err.ok) return -1;
                branches.push_back(b);
            } else {
                const CompileError condErr = err;
                restore(before);
                if (branches.empty()) { err = condErr; return -1; }
                otherwise = parseExpr();
                if (err.ok && atOp(",")) { fail("the value without a condition must come last", l, c); return -1; }
                if (!err.ok || !atOp("}")) {
                    // Neither reading works: the condition's error is the useful one.
                    err = condErr;
                    return -1;
                }
            }
            if (accept(",")) continue;
            break;
        }
        absDepth = saved;
        if (!expect("}")) return -1;
        int tail = otherwise;
        if (tail < 0) {
            tail = node(N_UNDEF, "", t.line, t.col);
            nodes[tail].num = UNDEF_NO_BRANCH;
        }
        for (size_t i = branches.size(); i-- > 0;) {
            int n = node(N_IF, "if", t.line, t.col);
            nodes[n].kids.push_back(branches[i].cond);
            nodes[n].kids.push_back(branches[i].value);
            nodes[n].kids.push_back(tail);
            tail = n;
        }
        return tail;
    }

    // ---- code generation ---------------------------------------------

    int allocReg(int line, int col) {
        if (nextReg >= kMaxRegisters) {
            fail("too many registers (limit " + std::to_string(kMaxRegisters) + ")", line, col);
            return 0;
        }
        return nextReg++;
    }

    int emit(Op op, int dst, int a, int b, int c, int imm, int line, int col) {
        if (int(prog.code.size()) >= kMaxInstructions) {
            fail("equation too long (limit " + std::to_string(kMaxInstructions) + " instructions)", line, col);
            return 0;
        }
        Instruction in;
        in.op = op; in.dst = uint16_t(dst); in.a = uint16_t(a); in.b = uint16_t(b);
        in.c = uint16_t(c); in.imm = imm; in.line = uint16_t(line); in.col = uint16_t(col);
        prog.code.push_back(in);
        return int(prog.code.size()) - 1;
    }

    int constIndex(double v) {
        for (size_t i = 0; i < prog.constants.size(); i++)
            if (prog.constants[i] == v) return int(i);
        prog.constants.push_back(v);
        return int(prog.constants.size()) - 1;
    }

    /// Emit code computing node n into register dst.
    void gen(int n, int dst) {
        if (!err.ok || n < 0) return;
        const Node nd = nodes[n];
        const int L = nd.line, C = nd.col;
        switch (nd.kind) {
        case N_NUM:
            emit(OP_CONST, dst, 0, 0, 0, constIndex(nd.num), L, C);
            return;
        case N_SWEPT:
            emit(OP_INPUT, dst, 0, 0, 0, IN_X, L, C);
            return;
        case N_KNOB:
            emit(OP_PARAM, dst, 0, 0, 0, int(nd.num), L, C);
            return;
        case N_UNDEF:
            emit(OP_UNDEF, dst, 0, 0, 0, int(nd.num), L, C);
            return;
        case N_UNARY: {
            int r = allocReg(L, C);
            gen(nd.kids[0], r);
            emit(OP_NEG, dst, r, 0, 0, 0, L, C);
            nextReg = r;
            return;
        }
        case N_BINARY: {
            int ra = allocReg(L, C);
            gen(nd.kids[0], ra);
            int rb = allocReg(L, C);
            gen(nd.kids[1], rb);
            Op op;
            const std::string& s = nd.name;
            if      (s == "+")  op = OP_ADD;
            else if (s == "-")  op = OP_SUB;
            else if (s == "*")  op = OP_MUL;
            else if (s == "/")  op = OP_DIV;
            else if (s == "^")  op = OP_POW;
            else if (s == "<")  op = OP_LT;
            else if (s == "<=") op = OP_LE;
            else if (s == ">")  op = OP_GT;
            else if (s == ">=") op = OP_GE;
            else                op = OP_EQ;
            emit(op, dst, ra, rb, 0, 0, L, C);
            nextReg = ra;
            return;
        }
        case N_AND: {
            gen(nd.kids[0], dst);
            int jz = emit(OP_JZ, 0, dst, 0, 0, 0, L, C);
            gen(nd.kids[1], dst);
            if (err.ok) prog.code[jz].imm = int(prog.code.size());
            return;
        }
        case N_IF: {
            int rc = allocReg(L, C);
            gen(nd.kids[0], rc);
            int jz = emit(OP_JZ, 0, rc, 0, 0, 0, L, C);
            nextReg = rc;
            gen(nd.kids[1], dst);
            int jmp = emit(OP_JMP, 0, 0, 0, 0, 0, L, C);
            if (err.ok) prog.code[jz].imm = int(prog.code.size());
            gen(nd.kids[2], dst);
            if (err.ok) prog.code[jmp].imm = int(prog.code.size());
            return;
        }
        case N_INDEX: {
            // Element k (1-based) where the index equals k exactly; any
            // other index is undefined, as a fractional one is in Desmos.
            int ri = allocReg(L, C);
            gen(nd.kids[0], ri);
            int rk = allocReg(L, C);
            std::vector<int> ends;
            for (size_t k = 1; k < nd.kids.size(); k++) {
                emit(OP_CONST, rk, 0, 0, 0, constIndex(double(k)), L, C);
                emit(OP_EQ, rk, ri, rk, 0, 0, L, C);
                int jz = emit(OP_JZ, 0, rk, 0, 0, 0, L, C);
                const int base = nextReg;
                gen(nd.kids[k], dst);
                nextReg = base;
                ends.push_back(emit(OP_JMP, 0, 0, 0, 0, 0, L, C));
                if (err.ok) prog.code[jz].imm = int(prog.code.size());
            }
            emit(OP_UNDEF, dst, 0, 0, 0, UNDEF_INDEX, L, C);
            if (err.ok)
                for (size_t i = 0; i < ends.size(); i++) prog.code[ends[i]].imm = int(prog.code.size());
            nextReg = ri;
            return;
        }
        case N_CALL:
            genCall(nd, dst);
            return;
        }
    }

    void genCall(const Node& nd, int dst) {
        const int L = nd.line, C = nd.col;
        const FnInfo* fi = lookupFn(nd.name);
        const int base = nextReg;
        const int argc = int(nd.kids.size());
        if (nd.name == "min" || nd.name == "max") {
            gen(nd.kids[0], dst);
            int r = allocReg(L, C);
            for (int i = 1; i < argc; i++) {
                gen(nd.kids[i], r);
                emit(fi->op, dst, dst, r, 0, 0, L, C);
            }
            nextReg = base;
            return;
        }
        int regs[2] = {0, 0};
        for (int i = 0; i < argc; i++) {
            regs[i] = allocReg(L, C);
            gen(nd.kids[i], regs[i]);
        }
        Op op = fi->op;
        if (nd.name == "arctan" && argc == 2) op = OP_ATAN2;
        if (nd.name == "sec" || nd.name == "csc" || nd.name == "cot") {
            emit(op, regs[0], regs[0], 0, 0, 0, L, C);
            int one = allocReg(L, C);
            emit(OP_CONST, one, 0, 0, 0, constIndex(1.0), L, C);
            emit(OP_DIV, dst, one, regs[0], 0, 0, L, C);
        } else {
            emit(op, dst, regs[0], regs[1], 0, 0, L, C);
        }
        nextReg = base;
    }

    // ---- the equation ------------------------------------------------

    bool isOp(size_t i, const char* s) const { return at(i).kind == T_OP && at(i).text == s; }

    /// `y =`, `r =`, `f(x) =` or `f(θ) =` at the start: consume it and set
    /// the mode. Anything else leaves p alone.
    void parseLeft() {
        const Token& t = cur();
        if (t.kind != T_IDENT) return;
        if ((t.text == "y" || t.text == "r") && isOp(p + 1, "=")) {
            prog.mode = t.text == "y" ? MODE_X : MODE_POLAR;
            p += 2;
            return;
        }
        if (t.text == "x" && isOp(p + 1, "=")) {
            fail("x = ... is a curve in y; write y = f(x)", t.line, t.col);
            return;
        }
        if (t.text.size() == 1 && isOp(p + 1, "(") && at(p + 2).kind == T_IDENT
            && isOp(p + 3, ")") && isOp(p + 4, "=")) {
            const std::string& v = at(p + 2).text;
            if (v == "x") prog.mode = MODE_X;
            else if (v == "theta") prog.mode = MODE_POLAR;
            else { fail("a function here takes x or θ", at(p + 2).line, at(p + 2).col); return; }
            p += 5;
        }
    }

    /// `(x(t), y(t))` as the whole equation.
    bool isParametric() const {
        if (!isOp(0, "(")) return false;
        int level = 0;
        bool comma = false;
        for (size_t i = 0; i < toks.size(); i++) {
            const Token& t = toks[i];
            if (t.kind != T_OP) continue;
            if (t.text == "(" || t.text == "[" || t.text == "{") level++;
            else if (t.text == ")" || t.text == "]" || t.text == "}") {
                if (--level == 0) return comma && i + 1 < toks.size() && toks[i + 1].kind == T_END;
            } else if (t.text == "," && level == 1) comma = true;
        }
        return false;
    }

    // ---- the domain ----------------------------------------------------

    /// The value of a bound: numbers, π, τ, e and arithmetic on them.
    bool constValue(int n, double& out) {
        if (n < 0) return false;
        const Node& nd = nodes[n];
        if (nd.kind == N_NUM) { out = nd.num; return true; }
        if (nd.kind == N_UNARY && nd.name == "-" && nd.kids.size() == 1) {
            if (!constValue(nd.kids[0], out)) return false;
            out = -out;
            return true;
        }
        if (nd.kind == N_BINARY && nd.kids.size() == 2) {
            double a, b;
            if (!constValue(nd.kids[0], a) || !constValue(nd.kids[1], b)) return false;
            if (nd.name == "+") out = a + b;
            else if (nd.name == "-") out = a - b;
            else if (nd.name == "*") out = a * b;
            else if (nd.name == "/") out = a / b;
            else if (nd.name == "^") out = std::pow(a, b);
            else return false;
            return std::isfinite(out);
        }
        return false;
    }

    /// Parse toks[from, to) as a bound. Knobs and the swept variable are
    /// not allowed: a domain that moved would not be a domain.
    bool parseBound(size_t from, size_t to, double& out) {
        Compiler sub;
        sub.toks.assign(toks.begin() + from, toks.begin() + to);
        sub.toks.push_back(toks.back());           // T_END
        const int e = sub.finish(sub.parseExpr());
        if (!sub.err.ok || e < 0 || !sub.prog.params.empty() || sub.prog.usesX
            || !sub.constValue(e, out)) {
            fail("a domain's ends are numbers, like -8 or 2π", toks[from].line, toks[from].col);
            return false;
        }
        return true;
    }

    /// A trailing `{lo ≤ v ≤ hi}` (or ≥ both ways), v the swept variable:
    /// record it as the domain and cut it off the token list. Anything else
    /// at the end is left for the parser.
    std::string domainVar;
    void takeDomain() {
        const size_t end = toks.size() - 1;               // T_END
        if (end < 2 || !isOp(end - 1, "}")) return;
        int level = 0;
        size_t open = 0;
        for (size_t i = end; i-- > 0;) {
            const Token& t = toks[i];
            if (t.kind != T_OP) continue;
            if (t.text == ")" || t.text == "]" || t.text == "}") level++;
            else if (t.text == "(" || t.text == "[" || t.text == "{") {
                if (--level == 0) { open = i; break; }
            }
        }
        if (level != 0 || open == 0 || toks[open].text != "{") return;
        // The braces must follow a complete factor to restrict it; after
        // `=` or an operator they are a piecewise value, `y = {0 ≤ x ≤ 1}`.
        const Token& before = toks[open - 1];
        if (before.kind == T_OP && before.text != ")" && before.text != "]"
            && before.text != "}" && before.text != "|") return;
        // Two comparisons at the brace's own level, one name between them.
        std::vector<size_t> cmp;
        level = 0;
        for (size_t i = open + 1; i < end - 1; i++) {
            const Token& t = toks[i];
            if (t.kind != T_OP) continue;
            if (t.text == "(" || t.text == "[" || t.text == "{") level++;
            else if (t.text == ")" || t.text == "]" || t.text == "}") level--;
            else if (level == 0 && (t.text == "<" || t.text == "<=" || t.text == ">"
                                    || t.text == ">=" || t.text == ":" || t.text == ","))
                cmp.push_back(i);
        }
        if (cmp.size() != 2 || cmp[1] != cmp[0] + 2) return;
        const Token& v = toks[cmp[0] + 1];
        if (v.kind != T_IDENT || (v.text != "x" && v.text != "theta" && v.text != "t")) return;
        const std::string a = toks[cmp[0]].text, b = toks[cmp[1]].text;
        const bool up = (a == "<" || a == "<=") && (b == "<" || b == "<=");
        const bool down = (a == ">" || a == ">=") && (b == ">" || b == ">=");
        if (!up && !down) return;
        double lo, hi;
        if (!parseBound(open + 1, cmp[0], lo) || !parseBound(cmp[1] + 1, end - 1, hi)) return;
        if (down) std::swap(lo, hi);
        if (!(hi > lo)) {
            fail("a domain needs its low end first and two different ends", toks[open].line, toks[open].col);
            return;
        }
        prog.hasDomain = true;
        prog.domainLo = lo;
        prog.domainHi = hi;
        domainVar = v.text;
        Token stop = toks[end];
        toks.resize(open);
        toks.push_back(stop);
    }

    int finish(int e) {
        if (!err.ok) return -1;
        if (cur().kind != T_END) {
            if (atOp("=")) fail("one equation, with y, r or f(x) on the left");
            else if (atCompare()) fail("a comparison belongs in a piecewise: {x < 1: ...}");
            else fail("unexpected '" + cur().text + "'");
            return -1;
        }
        return e;
    }

    bool compile(const std::string& source) {
        prog.source = source;
        if (source.size() > kMaxSourceBytes) {
            fail("source is longer than " + std::to_string(kMaxSourceBytes) + " bytes", 1, 1);
            return false;
        }
        Lexer lx(source);
        if (!lx.run()) { err = lx.err; return false; }
        toks = lx.out;
        if (cur().kind == T_END) { fail("empty equation", 1, 1); return false; }
        takeDomain();
        if (!err.ok) return false;
        if (cur().kind == T_END) { fail("a domain needs an equation before it", 1, 1); return false; }

        int ex = -1, ey = -1;
        if (isParametric()) {
            prog.mode = MODE_PARAM;
            p++;
            ex = parseExpr();
            if (!err.ok) return false;
            if (!expect(",")) return false;
            ey = parseExpr();
            if (!err.ok) return false;
            if (!expect(")")) return false;
            if (finish(0) < 0) return false;
        } else {
            parseLeft();
            if (!err.ok) return false;
            ey = finish(parseExpr());
            if (!err.ok) return false;
        }

        if (prog.hasDomain) {
            const char* want = prog.mode == MODE_X ? "x" : prog.mode == MODE_POLAR ? "theta" : "t";
            if (domainVar != want) {
                fail(std::string("this equation sweeps ") + (prog.mode == MODE_POLAR ? "θ" : want)
                     + ", so its domain is written in it", 1, 1);
                return false;
            }
        }

        const int L = 1, C = 1;
        if (prog.mode == MODE_X) {
            int rx = allocReg(L, C), ry = allocReg(L, C);
            emit(OP_INPUT, rx, 0, 0, 0, IN_X, L, C);
            gen(ey, ry);
            emit(OP_RET, 0, rx, ry, 0, 0, L, C);
        } else if (prog.mode == MODE_POLAR) {
            int rr = allocReg(L, C), rt = allocReg(L, C);
            int rx = allocReg(L, C), ry = allocReg(L, C);
            gen(ey, rr);
            emit(OP_INPUT, rt, 0, 0, 0, IN_X, L, C);
            emit(OP_COS, rx, rt, 0, 0, 0, L, C);
            emit(OP_MUL, rx, rr, rx, 0, 0, L, C);
            emit(OP_SIN, ry, rt, 0, 0, 0, L, C);
            emit(OP_MUL, ry, rr, ry, 0, 0, L, C);
            emit(OP_RET, 0, rx, ry, 0, 0, L, C);
        } else {
            int rx = allocReg(L, C), ry = allocReg(L, C);
            gen(ex, rx);
            gen(ey, ry);
            emit(OP_RET, 0, rx, ry, 0, 0, L, C);
        }
        if (!err.ok) return false;
        prog.numRegisters = nextReg;
        prog.name = defaultName(source);
        return true;
    }

    /// The equation itself, trimmed to fit the display.
    static std::string defaultName(const std::string& src) {
        std::string s;
        for (size_t i = 0; i < src.size(); i++) {
            const char ch = src[i] == '\n' || src[i] == '\t' || src[i] == '\r' ? ' ' : src[i];
            if (ch == ' ' && (s.empty() || s[s.size() - 1] == ' ')) continue;
            s += ch;
        }
        while (!s.empty() && s[s.size() - 1] == ' ') s.erase(s.size() - 1);
        if (s.size() > 24) {
            size_t cut = 23;   // never inside a UTF-8 sequence (θ, π)
            while (cut > 0 && (s[cut] & 0xC0) == 0x80) cut--;
            s = s.substr(0, cut) + "~";
        }
        return s.empty() ? "expr" : s;
    }
};

}  // namespace detail

/// Compile `source`. On success `out` is complete and err.ok is true; on
/// failure `out` is unspecified and err carries the first error with its
/// line and column (1-based).
inline bool compile(const std::string& source, Program& out, CompileError& err) {
    detail::Compiler c;
    bool ok = c.compile(source);
    err = c.err;
    if (ok) {
        out = c.prog;
        return true;
    }
    return false;
}

}  // namespace Expr
}  // namespace Lives
