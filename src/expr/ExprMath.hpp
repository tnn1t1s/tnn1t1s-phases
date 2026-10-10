#pragma once
// ExprMath.hpp -- an equation typeset the way LaTeX sets it, as boxes.
//
// Lays out the tree EXPR's compiler builds into glyph runs, rules and
// delimiter shapes, following TeX's rules for text-style math closely
// enough to read like the videos: fractions with a rule on the math axis,
// raised exponents at script size, floor brackets and parentheses that
// grow with what they enclose, a brace for a piecewise, and TeX's spacing
// between ordinary atoms, operators, relations and function names.
//
// The layout is dependency-free: text widths come from a callback, so the
// display measures with its font and a test can measure with anything.
// Letters are mapped to the Unicode mathematical italic block, so one font
// (Latin Modern Math) draws both upright and italic.
//
// Coordinates are in px at the size given to layout(), origin on the
// baseline at the left edge, y growing downward: a box spans -h to d.

#include "ExprCompiler.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Lives {
namespace Expr {
namespace Math {

struct Run { std::string text; float x, y, size; };          // baseline at y
struct Rule { float x0, y0, x1, y1, width; };

enum ShapeKind {
    S_PAREN_L, S_PAREN_R, S_FLOOR_L, S_FLOOR_R, S_CEIL_L, S_CEIL_R,
    S_BAR, S_BRACKET_L, S_BRACKET_R, S_BRACE_L, S_BRACE_R, S_RADICAL
};
struct Shape { ShapeKind kind; float x, top, bottom, w, em; };

/// TeX's atom classes, as far as spacing needs them.
enum Atom { A_ORD, A_OP, A_BIN, A_REL, A_INNER };

struct Box {
    float w = 0, h = 0, d = 0;
    Atom left = A_ORD, right = A_ORD;      // the class at each edge, for spacing
    std::vector<Run> runs;
    std::vector<Rule> rules;
    std::vector<Shape> shapes;

    void shift(float dx, float dy) {
        for (Run& r : runs) { r.x += dx; r.y += dy; }
        for (Rule& r : rules) { r.x0 += dx; r.x1 += dx; r.y0 += dy; r.y1 += dy; }
        for (Shape& s : shapes) { s.x += dx; s.top += dy; s.bottom += dy; }
    }
    /// Put b after this box, gap px between them, b's baseline moved by dy.
    void append(Box b, float gap, float dy = 0.f) {
        b.shift(w + gap, dy);
        if (runs.empty() && shapes.empty() && rules.empty()) left = b.left;
        right = b.right;
        h = std::max(h, b.h - dy);
        d = std::max(d, b.d + dy);
        w += gap + b.w;
        runs.insert(runs.end(), b.runs.begin(), b.runs.end());
        rules.insert(rules.end(), b.rules.begin(), b.rules.end());
        shapes.insert(shapes.end(), b.shapes.begin(), b.shapes.end());
    }
};

/// Width in px of `utf8` set in the math font at `size` px.
using Measure = std::function<float(const std::string& utf8, float size)>;

namespace detail_math {

inline std::string utf8(uint32_t cp) {
    std::string s;
    if (cp < 0x80) s += char(cp);
    else if (cp < 0x800) { s += char(0xC0 | (cp >> 6)); s += char(0x80 | (cp & 0x3F)); }
    else if (cp < 0x10000) {
        s += char(0xE0 | (cp >> 12)); s += char(0x80 | ((cp >> 6) & 0x3F)); s += char(0x80 | (cp & 0x3F));
    } else {
        s += char(0xF0 | (cp >> 18)); s += char(0x80 | ((cp >> 12) & 0x3F));
        s += char(0x80 | ((cp >> 6) & 0x3F)); s += char(0x80 | (cp & 0x3F));
    }
    return s;
}

/// A Latin letter in the mathematical italic block (h has its own code point).
inline std::string italic(char c) {
    if (c == 'h') return utf8(0x210E);
    if (c >= 'a' && c <= 'z') return utf8(0x1D44E + uint32_t(c - 'a'));
    if (c >= 'A' && c <= 'Z') return utf8(0x1D434 + uint32_t(c - 'A'));
    return std::string(1, c);
}

}  // namespace detail_math

// Font metrics of Latin Modern Math, in em (values from its MATH table and
// glyph bounds, rounded): the axis the fraction rule and delimiters centre
// on, the rule thickness, and the height and depth assumed for a run of
// text, since the callback measures only widths.
static const float kAxis = 0.25f;
static const float kRule = 0.04f;
static const float kTextH = 0.69f;     // digits and ascenders
static const float kXHeight = 0.43f;
static const float kDescent = 0.19f;   // g, p, y
// TeX's spacing, in em: 3, 4 and 5 mu.
static const float kThin = 3.f / 18.f, kMed = 4.f / 18.f, kThick = 5.f / 18.f;
// Script and scriptscript sizes relative to text, as in TeX's 10/7/5 pt.
static const float kScript = 0.7f, kScriptScript = 0.5f;

struct Layout {
    using Compiler = detail::Compiler;
    using Node = detail::Node;
    const Compiler& c;
    Measure measure;
    float base;
    std::map<std::pair<int, int>, const detail::Token*> tokAt;

    Layout(const Compiler& comp, Measure m, float size) : c(comp), measure(std::move(m)), base(size) {
        for (const detail::Token& t : c.toks) tokAt[{t.line, t.col}] = &t;
    }

    const Node& nd(int i) const { return c.nodes[i]; }
    bool script(float s) const { return s < base * 0.9f; }
    float smaller(float s) const { return script(s) ? base * kScriptScript : base * kScript; }

    // ---- atoms -----------------------------------------------------------

    Box text(const std::string& shown, const std::string& letters, float s, Atom a = A_ORD) {
        Box b;
        b.w = measure(shown, s);
        bool tall = false, deep = false;
        for (char ch : letters) {
            if (std::isdigit((unsigned char) ch) || std::isupper((unsigned char) ch)
                || std::strchr("bdfhklt", ch)) tall = true;
            if (std::strchr("fgjpqy", ch)) deep = true;
        }
        b.h = (tall || letters.empty() ? kTextH : kXHeight) * s;
        b.d = deep ? kDescent * s : 0.f;
        b.left = b.right = a;
        b.runs.push_back({shown, 0.f, 0.f, s});
        return b;
    }
    Box roman(const std::string& t, float s, Atom a = A_ORD) { return text(t, t, s, a); }
    Box letter(const std::string& name, float s) {
        if (name == "theta") return text(detail_math::utf8(0x1D703), "", s);
        Box b = text(detail_math::italic(name[0]), name.substr(0, 1), s);
        const size_t u = name.find('_');
        if (u != std::string::npos) {
            Box sub = roman(name.substr(u + 1), smaller(s));
            b.append(sub, 0.f, 0.15f * s);
        }
        return b;
    }
    Box symbol(uint32_t cp, float s, Atom a) {
        Box b = roman(detail_math::utf8(cp), s, a);
        b.h = (a == A_REL ? 0.55f : 0.58f) * s;   // relations and operators sit on the axis
        return b;
    }

    Box number(const Node& n, float s) {
        auto it = tokAt.find({n.line, n.col});
        if (it != tokAt.end()) {
            const detail::Token& t = *it->second;
            if (t.kind == detail::T_NUMBER && t.num == n.num) return roman(t.text, s);
            if (t.kind == detail::T_IDENT && t.text == "pi") return text(detail_math::utf8(0x1D70B), "", s);
            if (t.kind == detail::T_IDENT && t.text == "tau") return text(detail_math::utf8(0x1D70F), "", s);
            if (t.kind == detail::T_IDENT && t.text == "e") return letter("e", s);
        }
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", n.num);
        return roman(buf, s);
    }

    // ---- spacing ---------------------------------------------------------

    /// TeX's space between two adjacent atoms, in px (Ord, Op, Inner only;
    /// binary operators and relations add their own).
    float gap(Atom a, Atom b, float s) const {
        if (a == A_OP || b == A_OP) return kThin * s;
        if ((a == A_INNER || b == A_INNER) && !script(s)) return kThin * s;
        return 0.f;
    }
    void joinOp(Box& out, Box op, Box rhs, float space, float s) {
        const float g = script(s) ? 0.f : space * s;
        out.append(op, g);
        out.append(rhs, g);
    }

    // ---- delimiters ------------------------------------------------------

    /// Wrap `body` in delimiters sized the way \left ... \right sizes them:
    /// symmetric about the axis, at least 90.1 % of the body's extent and
    /// no more than 0.5 em short of it.
    Box fence(Box body, ShapeKind l, ShapeKind r, float s, float lw = 0.39f, float rw = 0.39f) {
        const float a = kAxis * s;
        const float half = std::max(body.h - a, body.d + a);
        const float H = std::max({1.0f * s, 2.f * half * 0.901f, 2.f * half - 0.5f * s});
        const float top = -a - H / 2, bottom = -a + H / 2;
        Box out;
        out.left = out.right = A_INNER;
        out.w = lw * s;
        out.shapes.push_back({l, 0.f, top, bottom, lw * s, s});
        out.h = -top; out.d = bottom;
        out.append(body, 0.f);
        const float x = out.w;
        out.shapes.push_back({r, x, top, bottom, rw * s, s});
        out.w += rw * s;
        out.h = std::max(out.h, -top); out.d = std::max(out.d, bottom);
        out.left = out.right = A_INNER;
        return out;
    }
    Box parens(Box b, float s) { return fence(std::move(b), S_PAREN_L, S_PAREN_R, s); }

    // ---- precedence ------------------------------------------------------

    enum Prec { P_COND, P_SUM, P_MOD, P_PRODUCT, P_NEG, P_POWER, P_ATOM };
    static bool isMod(const Node& n) { return n.kind == detail::N_CALL && n.name == "mod" && n.kids.size() == 2; }
    static bool isCompare(const Node& n) {
        return n.kind == detail::N_BINARY && (n.name == "<" || n.name == "<=" || n.name == ">"
                                              || n.name == ">=" || n.name == "==");
    }
    Prec prec(int i) const {
        const Node& n = nd(i);
        switch (n.kind) {
        case detail::N_BINARY:
            if (n.name == "+" || n.name == "-") return P_SUM;
            if (n.name == "*") return P_PRODUCT;
            if (n.name == "/") return P_ATOM;
            if (n.name == "^") return P_POWER;
            return P_COND;
        case detail::N_UNARY: return P_NEG;
        case detail::N_CALL: return isMod(n) ? P_MOD : P_ATOM;
        case detail::N_AND: return P_COND;
        case detail::N_IF: return n.name == "or" ? P_COND : P_ATOM;
        default: return P_ATOM;
        }
    }
    Box at(int i, Prec min, float s) { return prec(i) < min ? parens(lay(i, s), s) : lay(i, s); }

    // ---- constructions ---------------------------------------------------

    Box fraction(int num, int den, float s) {
        const float t = smaller(s);
        Box n = lay(num, t), d = lay(den, t);
        const float a = kAxis * s, rule = kRule * s, clear = 0.1f * s;
        const float up = std::max(0.394f * s, a + rule / 2 + clear + n.d);
        const float down = std::max(0.345f * s, d.h - (a - rule / 2) + clear);
        const float pad = 0.12f * s;
        const float w = std::max(n.w, d.w) + 2 * pad;
        Box out;
        out.left = out.right = A_INNER;
        out.w = 0;
        n.shift((w - n.w) / 2, -up);
        d.shift((w - d.w) / 2, down);
        out.runs = n.runs; out.rules = n.rules; out.shapes = n.shapes;
        out.runs.insert(out.runs.end(), d.runs.begin(), d.runs.end());
        out.rules.insert(out.rules.end(), d.rules.begin(), d.rules.end());
        out.shapes.insert(out.shapes.end(), d.shapes.begin(), d.shapes.end());
        out.rules.push_back({pad * 0.5f, -a, w - pad * 0.5f, -a, rule});
        out.w = w;
        out.h = up + n.h;
        out.d = down + d.d;
        return out;
    }

    Box power(int baseNode, int ex, float s) {
        const Node& b = nd(baseNode);
        const bool atomic = b.kind == detail::N_NUM || b.kind == detail::N_SWEPT || b.kind == detail::N_KNOB
                         || (b.kind == detail::N_CALL && !isMod(b) && b.name != "sqrt");
        Box out = atomic && b.kind != detail::N_CALL ? lay(baseNode, s) : parens(lay(baseNode, s), s);
        Box e = lay(ex, smaller(s));
        const float up = std::max(0.363f * s, out.h - 0.386f * s);
        out.append(e, 0.05f * s, -up);
        out.right = A_ORD;
        return out;
    }

    Box radical(int body, float s) {
        Box b = lay(body, s);
        const float rule = kRule * s, clear = 0.12f * s;
        const float top = -(b.h + clear + rule);
        const float bottom = std::max(b.d, 0.1f * s) + 0.05f * s;
        const float rw = 0.8f * s;
        Box out;
        out.shapes.push_back({S_RADICAL, 0.f, top, bottom, rw, s});
        out.w = rw;
        out.h = -top; out.d = bottom;
        out.append(b, 0.04f * s);
        out.rules.push_back({rw, top + rule / 2, out.w + 0.04f * s, top + rule / 2, rule});
        out.w += 0.04f * s;
        out.h = -top + 0.02f * s;
        out.left = out.right = A_ORD;
        return out;
    }

    Box args(const Node& n, float s) {
        Box inner;
        for (size_t k = 0; k < n.kids.size(); k++) {
            if (k) inner.append(roman(",", s), 0.f);
            inner.append(lay(n.kids[k], s), k ? kThin * s : 0.f);
        }
        return parens(inner, s);
    }

    Box call(const Node& n, float s) {
        const std::string& f = n.name;
        if (isMod(n)) {
            Box out = at(n.kids[0], P_MOD, s);
            const float g = kThick * s;      // \bmod's kerns hold at every size
            out.append(roman("mod", s, A_BIN), g);
            out.append(at(n.kids[1], P_POWER, s), g);
            out.left = A_ORD; out.right = A_ORD;
            return out;
        }
        if (f == "floor") return fence(lay(n.kids[0], s), S_FLOOR_L, S_FLOOR_R, s, 0.44f, 0.44f);
        if (f == "ceil") return fence(lay(n.kids[0], s), S_CEIL_L, S_CEIL_R, s, 0.44f, 0.44f);
        if (f == "abs") return fence(lay(n.kids[0], s), S_BAR, S_BAR, s, 0.28f, 0.28f);
        if (f == "sqrt") return radical(n.kids[0], s);
        std::string name = f == "sgn" ? "sign" : f == "log2" ? "log" : f;
        Box out = roman(name, s, A_OP);
        if (f == "log2") out.append(roman("2", smaller(s)), 0.f, 0.2f * s);
        out.right = A_OP;
        Box a = args(n, s);
        out.append(a, gap(A_OP, A_INNER, s));
        out.left = A_OP; out.right = A_INNER;
        return out;
    }

    Box relation(const std::string& op, float s) {
        const uint32_t cp = op == "<=" ? 0x2264 : op == ">=" ? 0x2265 : op == "==" ? '=' : uint32_t(op[0]);
        return symbol(cp, s, A_REL);
    }

    /// A chain a < b ≤ c is parsed as (a < b) and (b ≤ c) sharing b.
    Box conjunction(const Node& n, float s) {
        const Node& a = nd(n.kids[0]);
        const Node& b = nd(n.kids[1]);
        if (isCompare(b)) {
            const Node* last = &a;
            while (last->kind == detail::N_AND) last = &nd(last->kids[1]);
            if (isCompare(*last) && last->kids[1] == b.kids[0]) {
                Box out = lay(n.kids[0], s);
                joinOp(out, relation(b.name, s), at(b.kids[1], P_SUM, s), kThick, s);
                return out;
            }
        }
        Box out = logical(n.kids[0], s);
        joinOp(out, symbol(0x2227, s, A_BIN), logical(n.kids[1], s), kMed, s);
        return out;
    }
    Box logical(int i, float s) {
        const Node& n = nd(i);
        return n.kind == detail::N_IF && n.name == "or" ? parens(lay(i, s), s) : lay(i, s);
    }

    bool impliedOne(int i) const {
        const Node& n = nd(i);
        if (n.kind != detail::N_NUM || n.num != 1.0) return false;
        auto it = tokAt.find({n.line, n.col});
        return it == tokAt.end() || it->second->kind != detail::T_NUMBER;
    }

    Box piecewise(int i, float s) {
        std::vector<std::pair<int, int>> branches;   // condition, value
        int tail = i;
        while (nd(tail).kind == detail::N_IF && nd(tail).name == "if") {
            branches.push_back({nd(tail).kids[0], nd(tail).kids[1]});
            tail = nd(tail).kids[2];
        }
        const bool undefinedElse = nd(tail).kind == detail::N_UNDEF;
        if (branches.size() == 1 && undefinedElse && impliedOne(branches[0].second))
            return fence(lay(branches[0].first, s), S_BRACE_L, S_BRACE_R, s, 0.5f, 0.5f);

        struct Row { Box value, cond; };
        std::vector<Row> rows;
        for (const auto& br : branches) {
            Row r;
            r.value = lay(br.second, s);
            r.cond = roman("if", s);
            r.cond.append(lay(br.first, s), 0.333f * s);
            rows.push_back(r);
        }
        if (!undefinedElse) rows.push_back({lay(tail, s), roman("otherwise", s)});

        // Rows as an array with \arraystretch 1.2: each row at least the
        // strut (1.02 em high, 0.42 em deep), \quad between the columns.
        float col1 = 0;
        for (const Row& r : rows) col1 = std::max(col1, r.value.w);
        const float strutH = 1.02f * s, strutD = 0.42f * s, quad = 1.f * s;
        std::vector<float> base(rows.size(), 0.f);
        std::vector<float> rowH(rows.size()), rowD(rows.size());
        for (size_t k = 0; k < rows.size(); k++) {
            rowH[k] = std::max({strutH, rows[k].value.h, rows[k].cond.h});
            rowD[k] = std::max({strutD, rows[k].value.d, rows[k].cond.d});
            if (k) base[k] = base[k - 1] + rowD[k - 1] + rowH[k];
        }
        const float prevD = rowD.back();
        const float firstH = rowH[0];
        const float total = firstH + base.back() + prevD;
        // Centre the block on the axis.
        const float shiftY = -kAxis * s - total / 2 + firstH;
        Box body;
        float bodyW = 0;
        for (size_t k = 0; k < rows.size(); k++) {
            Box v = rows[k].value, c2 = rows[k].cond;
            v.shift(0, base[k] + shiftY);
            c2.shift(col1 + quad, base[k] + shiftY);
            for (Box* b : {&v, &c2}) {
                body.runs.insert(body.runs.end(), b->runs.begin(), b->runs.end());
                body.rules.insert(body.rules.end(), b->rules.begin(), b->rules.end());
                body.shapes.insert(body.shapes.end(), b->shapes.begin(), b->shapes.end());
            }
            bodyW = std::max(bodyW, col1 + quad + rows[k].cond.w);
        }
        body.w = bodyW;
        body.h = firstH - shiftY;
        body.d = shiftY + base.back() + prevD;
        // The brace alone, then the body: a \right. closes nothing.
        const float a = kAxis * s;
        const float half = std::max(body.h - a, body.d + a);
        const float H = std::max(2.f * half * 0.901f, 2.f * half - 0.5f * s);
        Box out;
        out.shapes.push_back({S_BRACE_L, 0.f, -a - H / 2, -a + H / 2, 0.55f * s, s});
        out.w = 0.55f * s;
        out.h = a + H / 2; out.d = H / 2 - a;
        out.append(body, 0.1f * s);
        out.left = out.right = A_INNER;
        return out;
    }

    Box product(const Node& n, float s) {
        Box a = at(n.kids[0], P_PRODUCT, s);
        const int r = n.kids[1];
        Box b = prec(r) == P_NEG ? parens(lay(r, s), s) : at(r, P_PRODUCT, s);
        // Two numbers side by side would read as one: 2·3, not 23.
        const std::string& first = b.runs.empty() ? std::string() : b.runs.front().text;
        const bool digitFirst = b.shapes.empty() && b.rules.empty() && !first.empty()
                             && (std::isdigit((unsigned char) first[0]) || first[0] == '.');
        if (digitFirst && b.runs.front().x == 0.f) {
            joinOp(a, symbol(0x22C5, s, A_BIN), b, kMed, s);
            return a;
        }
        a.append(b, gap(a.right, b.left, s));
        return a;
    }

    Box lay(int i, float s) {
        const Node& n = nd(i);
        switch (n.kind) {
        case detail::N_NUM: return number(n, s);
        case detail::N_SWEPT:
        case detail::N_KNOB: return letter(n.name, s);
        case detail::N_UNDEF: return roman("undefined", s);
        case detail::N_UNARY: {
            Box out = symbol(0x2212, s, A_ORD);
            out.append(prec(n.kids[0]) <= P_NEG ? parens(lay(n.kids[0], s), s) : lay(n.kids[0], s), 0.f);
            return out;
        }
        case detail::N_BINARY:
            if (n.name == "+" || n.name == "-") {
                Box out = lay(n.kids[0], s);
                const int r = n.kids[1];
                const bool wrap = prec(r) == P_NEG || (n.name == "-" && prec(r) == P_SUM);
                joinOp(out, symbol(n.name == "+" ? '+' : 0x2212, s, A_BIN),
                       wrap ? parens(lay(r, s), s) : lay(r, s), kMed, s);
                return out;
            }
            if (n.name == "*") return product(n, s);
            if (n.name == "/") return fraction(n.kids[0], n.kids[1], s);
            if (n.name == "^") return power(n.kids[0], n.kids[1], s);
            {
                Box out = at(n.kids[0], P_SUM, s);
                joinOp(out, relation(n.name, s), at(n.kids[1], P_SUM, s), kThick, s);
                return out;
            }
        case detail::N_CALL: return call(n, s);
        case detail::N_AND: return conjunction(n, s);
        case detail::N_IF:
            if (n.name == "or") {
                Box out = logical(n.kids[0], s);
                joinOp(out, symbol(0x2228, s, A_BIN), logical(n.kids[2], s), kMed, s);
                return out;
            }
            return piecewise(i, s);
        case detail::N_INDEX: {
            Box list;
            for (size_t k = 1; k < n.kids.size(); k++) {
                if (k > 1) list.append(roman(",", s), 0.f);
                list.append(lay(n.kids[k], s), k > 1 ? kThin * s : 0.f);
            }
            Box out = fence(list, S_BRACKET_L, S_BRACKET_R, s, 0.28f, 0.28f);
            out.append(fence(lay(n.kids[0], s), S_BRACKET_L, S_BRACKET_R, s, 0.28f, 0.28f), 0.f);
            return out;
        }
        }
        return Box();
    }
};

/// Typeset `source` at `size` px. False if it does not compile.
inline bool layout(const std::string& source, float size, const Measure& measure, Box& out) {
    detail::Compiler c;
    if (!c.compile(source)) return false;
    // The equation's roots are the nodes no other node refers to: one for
    // y = and r =, two (x then y) for a parametric curve.
    std::vector<bool> used(c.nodes.size(), false);
    for (const detail::Node& n : c.nodes)
        for (int k : n.kids) if (k >= 0) used[k] = true;
    std::vector<int> roots;
    for (size_t i = 0; i < c.nodes.size(); i++) if (!used[i]) roots.push_back(int(i));

    Layout L(c, measure, size);
    if (c.prog.mode == MODE_PARAM && roots.size() == 2) {
        Box inner = L.lay(roots[0], size);
        inner.append(L.roman(",", size), 0.f);
        inner.append(L.lay(roots[1], size), kThin * size);
        out = L.parens(inner, size);
        return true;
    }
    if (roots.size() != 1) return false;
    out = L.letter(c.prog.mode == MODE_POLAR ? "r" : "y", size);
    L.joinOp(out, L.symbol('=', size, A_REL), L.lay(roots[0], size), kThick, size);
    return true;
}

}  // namespace Math
}  // namespace Expr
}  // namespace Lives
