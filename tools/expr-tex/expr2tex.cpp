// expr2tex -- an EXPR equation as LaTeX, typeset the way the videos show it.
//
// Reads the equation from stdin and prints LaTeX math (no $) to stdout. It
// parses with EXPR's own compiler and prints the tree that compiler built,
// so what renders is what the module plays. Numbers and π, τ, e keep the
// spelling the source used. With --domain the trailing {lo ≤ x ≤ hi} is
// printed too; the videos leave it out.
//
//   echo 'y = a sin(bx)' | build/expr2tex
//
// Exits 1 with "line:col message" on stderr if the equation does not compile.

#include "expr/ExprCompiler.hpp"
#include <cstdio>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <utility>

using namespace Lives::Expr;
using namespace Lives::Expr::detail;

namespace {

// Printing precedence, loosest first. A child prints in parentheses when
// its own level is looser than its position allows.
enum Prec { P_COND, P_SUM, P_MOD, P_PRODUCT, P_NEG, P_POWER, P_ATOM };

struct Printer {
    const Compiler& c;
    std::map<std::pair<int, int>, const Token*> tokAt;

    explicit Printer(const Compiler& comp) : c(comp) {
        for (const Token& t : c.toks) tokAt[{t.line, t.col}] = &t;
    }

    const Node& nd(int i) const { return c.nodes[i]; }

    static bool isCompare(const Node& n) {
        return n.kind == N_BINARY && (n.name == "<" || n.name == "<=" || n.name == ">"
                                      || n.name == ">=" || n.name == "==");
    }
    static bool isMod(const Node& n) { return n.kind == N_CALL && n.name == "mod" && n.kids.size() == 2; }

    Prec prec(int i) const {
        const Node& n = nd(i);
        switch (n.kind) {
        case N_BINARY:
            if (n.name == "+" || n.name == "-") return P_SUM;
            if (n.name == "*") return P_PRODUCT;
            if (n.name == "/") return P_ATOM;   // \frac
            if (n.name == "^") return P_POWER;
            return P_COND;
        case N_UNARY: return P_NEG;
        case N_CALL: return isMod(n) ? P_MOD : P_ATOM;
        case N_AND: return P_COND;
        case N_IF: return n.name == "or" ? P_COND : P_ATOM;
        default: return P_ATOM;
        }
    }

    std::string paren(const std::string& s) const { return "\\left(" + s + "\\right)"; }
    std::string at(int i, Prec min) const { return prec(i) < min ? paren(tex(i)) : tex(i); }

    /// A number as the source spelled it: 0.25 stays 0.25, π stays π. A
    /// number the parser made up (the 1 of a condition with no value) has
    /// no token of its own and prints by value.
    std::string number(const Node& n) const {
        auto it = tokAt.find({n.line, n.col});
        if (it != tokAt.end()) {
            const Token& t = *it->second;
            if (t.kind == T_NUMBER && t.num == n.num) return t.text;
            if (t.kind == T_IDENT && t.text == "pi") return "\\pi";
            if (t.kind == T_IDENT && t.text == "tau") return "\\tau";
            if (t.kind == T_IDENT && t.text == "e") return "e";
        }
        char buf[32];
        std::snprintf(buf, sizeof buf, "%g", n.num);
        return buf;
    }

    static std::string letter(const std::string& name) {
        if (name == "theta") return "\\theta";
        const size_t u = name.find('_');
        if (u == std::string::npos) return name;
        return name.substr(0, u) + "_{" + name.substr(u + 1) + "}";
    }

    std::string args(const Node& n) const {
        std::string s;
        for (size_t k = 0; k < n.kids.size(); k++) s += (k ? ", " : "") + tex(n.kids[k]);
        return paren(s);
    }

    std::string call(const Node& n) const {
        const std::string& f = n.name;
        if (isMod(n))
            return at(n.kids[0], P_MOD) + " \\bmod " + at(n.kids[1], P_POWER);
        if (f == "floor") return "\\left\\lfloor " + tex(n.kids[0]) + " \\right\\rfloor";
        if (f == "ceil") return "\\left\\lceil " + tex(n.kids[0]) + " \\right\\rceil";
        if (f == "abs") return "\\left| " + tex(n.kids[0]) + " \\right|";
        if (f == "sqrt") return "\\sqrt{" + tex(n.kids[0]) + "}";
        if (f == "log2") return "\\log_{2}" + args(n);
        static const std::set<std::string> builtin = {
            "sin", "cos", "tan", "sec", "csc", "cot", "arcsin", "arccos", "arctan",
            "sinh", "cosh", "tanh", "exp", "ln", "log", "min", "max"};
        if (builtin.count(f)) return "\\" + f + args(n);
        return "\\operatorname{" + f + "}" + args(n);
    }

    /// A chain a < b ≤ c is parsed as (a < b) and (b ≤ c) sharing b.
    std::string conjunction(const Node& n) const {
        const Node& a = nd(n.kids[0]);
        const Node& b = nd(n.kids[1]);
        if (isCompare(b)) {
            const Node* last = &a;
            while (last->kind == N_AND) last = &nd(last->kids[1]);
            if (isCompare(*last) && last->kids[1] == b.kids[0])
                return tex(n.kids[0]) + " " + relation(b.name) + " " + at(b.kids[1], P_SUM);
        }
        return logical(n.kids[0]) + " \\wedge " + logical(n.kids[1]);
    }
    std::string logical(int i) const {
        const Node& n = nd(i);
        return n.kind == N_IF && n.name == "or" ? paren(tex(i)) : tex(i);
    }

    static std::string relation(const std::string& op) {
        if (op == "<=") return "\\le";
        if (op == ">=") return "\\ge";
        if (op == "==") return "=";
        return op;
    }

    /// The 1 a bare condition stands for: no number token at its position.
    bool impliedOne(int i) const {
        const Node& n = nd(i);
        if (n.kind != N_NUM || n.num != 1.0) return false;
        auto it = tokAt.find({n.line, n.col});
        return it == tokAt.end() || it->second->kind != T_NUMBER;
    }

    std::string piecewise(int i) const {
        std::vector<std::pair<int, int>> branches;   // condition, value
        int tail = i;
        while (nd(tail).kind == N_IF && nd(tail).name == "if") {
            branches.push_back({nd(tail).kids[0], nd(tail).kids[1]});
            tail = nd(tail).kids[2];
        }
        const bool undefinedElse = nd(tail).kind == N_UNDEF;
        // {x < 1}: a restriction, written the way Desmos writes it.
        if (branches.size() == 1 && undefinedElse && impliedOne(branches[0].second))
            return "\\left\\{ " + tex(branches[0].first) + " \\right\\}";
        std::string s = "\\begin{cases}\n";
        for (size_t k = 0; k < branches.size(); k++)
            s += "  " + tex(branches[k].second) + " & \\text{if } " + tex(branches[k].first) + " \\\\\n";
        if (!undefinedElse) s += "  " + tex(tail) + " & \\text{otherwise}\n";
        return s + "\\end{cases}";
    }

    std::string product(const Node& n) const {
        const std::string a = at(n.kids[0], P_PRODUCT);
        const int r = n.kids[1];
        std::string b = prec(r) == P_NEG ? paren(tex(r)) : at(r, P_PRODUCT);
        // Two numbers side by side would read as one: 2·3, not 23.
        const char first = b.empty() ? ' ' : b[0];
        const bool dot = std::isdigit((unsigned char) first) || first == '.';
        return a + (dot ? " \\cdot " : " ") + b;
    }

    std::string tex(int i) const {
        const Node& n = nd(i);
        switch (n.kind) {
        case N_NUM: return number(n);
        case N_SWEPT: return letter(n.name);
        case N_KNOB: return letter(n.name);
        case N_UNDEF: return "\\text{undefined}";
        case N_UNARY: return "-" + (prec(n.kids[0]) <= P_NEG ? paren(tex(n.kids[0])) : tex(n.kids[0]));
        case N_BINARY:
            if (n.name == "+" || n.name == "-") {
                const int r = n.kids[1];
                const bool wrap = prec(r) == P_NEG || (n.name == "-" && prec(r) == P_SUM);
                return tex(n.kids[0]) + " " + n.name + " " + (wrap ? paren(tex(r)) : tex(r));
            }
            if (n.name == "*") return product(n);
            if (n.name == "/") return "\\frac{" + tex(n.kids[0]) + "}{" + tex(n.kids[1]) + "}";
            if (n.name == "^") return at(n.kids[0], P_ATOM) + "^{" + tex(n.kids[1]) + "}";
            return at(n.kids[0], P_SUM) + " " + relation(n.name) + " " + at(n.kids[1], P_SUM);
        case N_CALL: return call(n);
        case N_AND: return conjunction(n);
        case N_IF:
            if (n.name == "or") return logical(n.kids[0]) + " \\vee " + logical(n.kids[2]);
            return piecewise(i);
        case N_INDEX: {
            std::string s;
            for (size_t k = 1; k < n.kids.size(); k++) s += (k > 1 ? ", " : "") + tex(n.kids[k]);
            return "\\left[" + s + "\\right]\\left[" + tex(n.kids[0]) + "\\right]";
        }
        }
        return "";
    }
};

/// The trailing domain as the source wrote it, from the tokens the
/// compiler cut off the end.
std::string domain(const std::string& src, size_t keptTokens) {
    Lexer lx(src);
    lx.run();
    std::string s;
    for (size_t k = keptTokens - 1; k + 1 < lx.out.size(); k++) {
        const Token& t = lx.out[k];
        std::string w = t.text;
        if (w == "{") w = "\\left\\{";
        else if (w == "}") w = "\\right\\}";
        else if (w == "<=") w = "\\le";
        else if (w == ">=") w = "\\ge";
        else if (w == "*") w = "\\cdot";
        else if (w == "pi" || w == "theta" || w == "tau") w = "\\" + w;
        s += w + " ";
    }
    return s;
}

}  // namespace

int main(int argc, char** argv) {
    const bool withDomain = argc > 1 && std::string(argv[1]) == "--domain";
    const std::string src((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());

    Compiler c;
    if (!c.compile(src)) {
        std::fprintf(stderr, "%d:%d %s\n", c.err.line, c.err.col, c.err.message.c_str());
        return 1;
    }
    // The equation's roots are the nodes no other node refers to: one for
    // y = and r =, two (x then y) for a parametric curve.
    std::vector<bool> used(c.nodes.size(), false);
    for (const Node& n : c.nodes)
        for (int k : n.kids) if (k >= 0) used[k] = true;
    std::vector<int> roots;
    for (size_t i = 0; i < c.nodes.size(); i++) if (!used[i]) roots.push_back(int(i));

    Printer pr(c);
    std::string out;
    if (c.prog.mode == MODE_PARAM && roots.size() == 2)
        out = "\\left(" + pr.tex(roots[0]) + ", " + pr.tex(roots[1]) + "\\right)";
    else if (roots.size() == 1)
        out = std::string(c.prog.mode == MODE_POLAR ? "r" : "y") + " = " + pr.tex(roots[0]);
    else {
        std::fprintf(stderr, "expected one equation tree, found %zu\n", roots.size());
        return 1;
    }
    if (withDomain && c.prog.hasDomain) out += " \\quad " + domain(src, c.toks.size());
    std::printf("%s\n", out.c_str());
    return 0;
}
