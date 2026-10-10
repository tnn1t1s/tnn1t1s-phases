// expr_test.cpp -- the EXPR language, compiler and runtime, headless.
//
// The language is one Desmos-style equation
// (doc/decisions/0008-desmos-is-the-language.md). These cover the notation
// people paste from Desmos (implied multiplication, |...|, π and θ,
// piecewise braces, 1-indexed lists), letters as knobs, the three modes and
// their X/Y outputs, faults with source positions, the conditioning
// pipeline, history that keeps its spikes, and the preview curve. A
// compiler that fails without saying where is a compiler nobody edits
// against, so the errors are tested by position as well.

#include "expr/ExprCompiler.hpp"
#include "expr/ExprCondition.hpp"
#include "expr/ExprDomain.hpp"
#include "expr/ExprExamples.hpp"
#include "expr/ExprHistory.hpp"
#include "expr/ExprPreview.hpp"
#include "expr/ExprRuntime.hpp"

#include <cmath>
#include <cstdio>
#include <string>

using namespace Lives::Expr;

namespace {

int failures = 0;

void ok(const char* what, bool pass) {
    if (!pass) failures++;
    std::printf("  %-64s %s\n", what, pass ? "ok" : "*** FAIL ***");
}

void nearly(const char* what, double got, double want, double tol) {
    const bool pass = std::fabs(got - want) <= tol;
    ok(what, pass);
    if (!pass) std::printf("      got %.9g, want %.9g\n", got, want);
}

const double kPi = 3.14159265358979323846;
const double SR = 48000.0;
const double DT = 1.0 / SR;

/// Compile, and print the error if it fails.
bool build(const char* src, Program& p) {
    CompileError e;
    if (!compile(src, p, e)) {
        std::printf("      compile error %d:%d: %s\n", e.line, e.col, e.message.c_str());
        return false;
    }
    return true;
}

/// Compile and expect failure; return the error for inspection.
CompileError reject(const char* src) {
    Program p;
    CompileError e;
    compile(src, p, e);
    return e;
}

/// Evaluate a program once at swept value v with default knob values.
Result runAt(const Program& p, double v) {
    Instance i;
    i.prepare(&p, 1);
    Inputs in;
    in.v[IN_X] = v;
    for (size_t k = 0; k < p.params.size(); k++)
        in.param[p.params[k].slot] = p.params[k].def;
    return i.run(in);
}

/// Y at swept value v.
double at(const Program& p, double v) { return runAt(p, v).value; }

/// Y at x with knob values given in slot order.
double withKnobs(const Program& p, double v, double k0, double k1 = 0) {
    Instance i;
    i.prepare(&p, 1);
    Inputs in;
    in.v[IN_X] = v;
    in.param[0] = k0; in.param[1] = k1;
    return i.run(in).value;
}

/// Compile two spellings and check they agree at a few points.
void same(const char* what, const char* a, const char* b) {
    Program pa, pb;
    if (!build(a, pa) || !build(b, pb)) { ok(what, false); return; }
    bool eq = pa.params.size() == pb.params.size();
    const double xs[] = {-2.5, -0.3, 0.7, 1.9, 4.2};
    for (int i = 0; i < 5 && eq; i++) {
        const Result ra = runAt(pa, xs[i]), rb = runAt(pb, xs[i]);
        if (ra.fault != rb.fault || (!ra.fault && std::fabs(ra.value - rb.value) > 1e-12)) eq = false;
    }
    ok(what, eq);
}

// ---------------------------------------------------------------------------

void corpus_tests() {
    std::printf("equations\n");
    Program p;

    ok("sin(x) compiles as a bare expression", build("sin(x)", p));
    ok("a bare expression is y = f(x)", p.mode == MODE_X);
    nearly("sin(pi/2) = 1", at(p, kPi / 2), 1.0, 1e-12);
    nearly("X out is x itself", runAt(p, 0.75).x, 0.75, 0.0);
    ok("the program is named by its equation", p.name == "sin(x)");

    ok("y = sin(x)", build("y = sin(x)", p));
    nearly("evaluates", at(p, 1.0), std::sin(1.0), 1e-12);
    ok("f(x) = sin(x)", build("f(x) = sin(x)", p));
    nearly("evaluates", at(p, 1.0), std::sin(1.0), 1e-12);

    // The TikTok formula shape: one line, nested mod/abs/max.
    ok("a long one-line formula compiles",
       build("y = max(2.4sin(1.32|2mod(x/16, 1) - 1|), 0.3cos(5x) - |sin(x/3)|)", p));
    {
        const double x = 7.3;
        const double m = std::fmod(x / 16, 1.0);
        const double want = std::max(2.4 * std::sin(1.32 * std::fabs(2 * m - 1)),
                                     0.3 * std::cos(5 * x) - std::fabs(std::sin(x / 3)));
        nearly("and agrees with the C++", at(p, x), want, 1e-12);
    }

    // Lazy piecewise with a pole in the other branch.
    ok("{x < pi: sin(x), tan(2x)}", build("y = {x < pi: sin(x), tan(2x)}", p));
    nearly("selects sin below pi", at(p, 1.0), std::sin(1.0), 1e-12);
    nearly("boundary: x = pi takes the else", at(p, kPi), std::tan(2 * kPi), 1e-12);
    ok("lazy sqrt compiles", build("y = {x >= 0: sqrt(x), -sqrt(-x)}", p));
    nearly("negative x takes the safe branch", at(p, -4.0), -2.0, 1e-12);
    nearly("positive x takes the sqrt branch", at(p, 9.0), 3.0, 1e-12);

    // Notation the creators' videos use beyond plain Desmos.
    ok("floor and ceiling brackets", build("y = \u230Ax\u230B + \u2308x\u2309", p));
    nearly("evaluate as floor and ceil", at(p, 2.5), 5.0, 0.0);
    ok("infix mod", build("y = (x + 2) mod 3", p));
    nearly("is mod((x + 2), 3)", at(p, 2.0), 1.0, 1e-12);
    ok("log2", build("y = log2(x)", p));
    nearly("log2(8) = 3, not log(2*8)", at(p, 8.0), 3.0, 1e-12);
    ok("log_2", build("y = log_2(x)", p));
    nearly("log_2(8) = 3", at(p, 8.0), 3.0, 1e-12);
    ok("log_{10}", build("y = log_{10}(x)", p));
    nearly("log_{10}(100) = 2", at(p, 100.0), 2.0, 1e-12);
    ok("an unsupported log base is an error", !build("y = log_3(x)", p));
    ok("and joins conditions", build("y = {x > 1 and x < 3: 1, 0}", p));
    nearly("inside", at(p, 2), 1.0, 0.0);
    nearly("outside", at(p, 4), 0.0, 0.0);
    ok("and is not three knobs", p.params.empty());
    ok("\u2227 is and", build("y = {x > 1 \u2227 x < 3: 1, 0}", p));
    nearly("inside", at(p, 2), 1.0, 0.0);
    ok("or of bracketed chains", build("y = {(-2 <= x < 0) or (14 <= x < 16): 1, 0}", p));
    nearly("first range", at(p, -1), 1.0, 0.0);
    nearly("second range", at(p, 15), 1.0, 0.0);
    nearly("between", at(p, 5), 0.0, 0.0);
    ok("\u2228 is or", build("y = {x < 0 \u2228 x > 1: 1, 0}", p));
    nearly("between", at(p, 0.5), 0.0, 0.0);
    ok("a bracketed expression still starts a comparison", build("y = {(x + 1) < 2: 1, 0}", p));
    nearly("holds", at(p, 0.5), 1.0, 0.0);
    nearly("fails", at(p, 1.5), 0.0, 0.0);
    ok("a bracketed else value", build("y = {x < 0: 1, (x + 1)}", p));
    nearly("evaluates", at(p, 2), 3.0, 0.0);

    // Several branches, a chained condition, no else.
    ok("{x < 0: -1, 0 <= x < 1: x, x >= 1: 1}",
       build("y = {x < 0: -1, 0 <= x < 1: x, x >= 1: 1}", p));
    nearly("first branch", at(p, -3), -1.0, 0.0);
    nearly("chained middle branch", at(p, 0.25), 0.25, 0.0);
    nearly("last branch", at(p, 3), 1.0, 0.0);
    ok("{0 < x < 1} is 1 inside", build("y = {0 < x < 1}", p));
    nearly("inside", at(p, 0.5), 1.0, 0.0);
    {
        const Result r = runAt(p, 2.0);
        ok("and undefined outside, as a gap in Desmos",
           r.fault && std::string(r.faultWhat) == "no branch applies");
    }
    ok("= in a condition is equality", build("y = {x = 1: 5, 0}", p));
    nearly("at 1", at(p, 1.0), 5.0, 0.0);
    nearly("elsewhere", at(p, 1.5), 0.0, 0.0);

    // Lists index from 1; anything but a whole number in range is a gap.
    ok("[0,3,5,7,10][floor(x)+1]", build("y = [0,3,5,7,10][floor(x)+1]", p));
    nearly("index 1 is the first element", at(p, 0.5), 0.0, 0.0);
    nearly("index 2 is 3", at(p, 1.2), 3.0, 0.0);
    nearly("index 5 is 10", at(p, 4.9), 10.0, 0.0);
    {
        const Result r = runAt(p, 5.0);
        ok("index 6 is out of range", r.fault && std::string(r.faultWhat) == "list index out of range");
        ok("index 0 is out of range", runAt(p, -0.5).fault);
    }
    ok("a fractional index is undefined", build("y = [1,2,3][x]", p) && runAt(p, 1.5).fault);
    ok("list elements are expressions, evaluated lazily",
       build("y = [sqrt(-1), x][2]", p) && !runAt(p, 3.0).fault && at(p, 3.0) == 3.0);

    // Mathematical modulo, Desmos's mod().
    ok("mod(x, 32)", build("y = mod(x, 32)", p));
    nearly("mod(-1, 32) = 31", at(p, -1), 31.0, 1e-12);
    nearly("mod(47.5, 32) = 15.5", at(p, 47.5), 15.5, 1e-12);

    // Desmos's log is base 10; ln is natural.
    ok("log and ln", build("y = log(x) + ln(x)", p));
    nearly("log(100) + ln(100)", at(p, 100.0), 2.0 + std::log(100.0), 1e-12);
    ok("sec, csc, cot", build("y = sec(x) + csc(x) + cot(x)", p));
    nearly("at 0.4", at(p, 0.4), 1 / std::cos(0.4) + 1 / std::sin(0.4) + 1 / std::tan(0.4), 1e-12);
    ok("arctan(y, x) is atan2", build("y = arctan(-1, x)", p));
    nearly("arctan(-1, -1)", at(p, -1.0), std::atan2(-1.0, -1.0), 1e-12);
    ok("min and max take any number of arguments", build("y = max(1, x, 3) - min(x, 2, 9, 4)", p));
    nearly("max(1,5,3) - min(5,2,9,4)", at(p, 5.0), 3.0, 0.0);

    // Constants, precedence, right-associative power.
    ok("constant equation", build("y = 2.5", p));
    nearly("evaluates", at(p, 123.0), 2.5, 0.0);
    ok("a constant does not use x", !p.usesX);
    ok("tau - 2pi", build("y = tau - 2pi", p));
    nearly("is 0", at(p, 0), 0.0, 1e-15);
    ok("precedence", build("y = 2 + 3 * 4 ^ 2 ^ 0.5 - -1", p));
    nearly("2 + 3*4^(2^0.5) + 1", at(p, 0), 2 + 3 * std::pow(4.0, std::pow(2.0, 0.5)) + 1, 1e-12);
    ok("-2^2", build("y = -2^2", p));
    nearly("is -4", at(p, 0), -4.0, 0.0);
    ok("2^-x", build("y = 2^-x", p));
    nearly("is 0.25 at 2", at(p, 2.0), 0.25, 0.0);
}

void notation_tests() {
    std::printf("Desmos notation\n");
    same("2.4sin(x) is 2.4*sin(x)", "y = 2.4sin(x)", "y = 2.4*sin(x)");
    same("2(x + 1) is 2*(x + 1)", "y = 2(x + 1)", "y = 2*(x + 1)");
    same("(x + 1)(x - 1)", "y = (x + 1)(x - 1)", "y = (x + 1)*(x - 1)");
    same("πx is pi*x", "y = sin(\xCF\x80x)", "y = sin(pi*x)");
    same("pix splits as pi x", "y = sin(pix)", "y = sin(pi*x)");
    same("|2x - 1| is abs", "y = |2x - 1|", "y = abs(2*x - 1)");
    same("2|x| - |x - 1|", "y = 2|x| - |x - 1|", "y = 2*abs(x) - abs(x - 1)");
    same("|x|x is abs(x)*x", "y = |x|x", "y = abs(x)*x");
    same("nested |(|x| - 1)|", "y = |(|x| - 1)|", "y = abs(abs(x) - 1)");
    same("sin x without parentheses", "y = sin x", "y = sin(x)");
    same("sinx without a space", "y = sinx", "y = sin(x)");
    same("sin 2x takes the product", "y = sin 2x", "y = sin(2*x)");
    same("sin 2x + 1 stops at +", "y = sin 2x + 1", "y = sin(2*x) + 1");
    same("x sin x cos x", "y = x sin x cos x", "y = x*sin(x)*cos(x)");
    same("x^2 sin x", "y = x^2 sin x", "y = x^2*sin(x)");
    same("2x^2 is 2(x^2)", "y = 2x^2", "y = 2*(x^2)");
    same("\xE2\x88\x92 and \xC2\xB7 are minus and times",
         "y = 2\xC2\xB7x \xE2\x88\x92 1", "y = 2*x - 1");
    same("\xE2\x89\xA4 is <=", "y = {x \xE2\x89\xA4 1: 1, 0}", "y = {x <= 1: 1, 0}");
    same("whitespace and newlines are free", "y =\n  sin( x )", "y = sin(x)");
    same("e is Euler's number", "y = e^x", "y = exp(x)");
}

void knob_tests() {
    std::printf("letters are knobs\n");
    Program p;
    ok("y = a sin(bx)", build("y = a sin(bx)", p));
    ok("two knobs, in order of first appearance",
       p.params.size() == 2 && p.params[0].name == "a" && p.params[0].slot == 0
       && p.params[1].name == "b" && p.params[1].slot == 1);
    ok("knobs default to 1 on 0..10, one unit per volt",
       p.params[0].def == 1.0 && p.params[0].min == 0.0 && p.params[0].max == 10.0
       && p.params[0].cvScale == 1.0 && !p.params[0].generic);
    nearly("at the defaults it is sin(x)", at(p, 0.3), std::sin(0.3), 1e-12);
    nearly("a = 2, b = 3", withKnobs(p, 0.3, 2.0, 3.0), 2 * std::sin(0.9), 1e-12);

    ok("ab is a times b", build("y = abx", p) && p.params.size() == 2);
    nearly("2 * 3 * 0.5", withKnobs(p, 0.5, 2.0, 3.0), 3.0, 1e-12);
    ok("a repeated letter is one knob", build("y = a x + a", p) && p.params.size() == 1);
    ok("a subscripted letter is its own knob",
       build("y = a_1 x + a_2", p) && p.params.size() == 2 && p.params[1].name == "a_2");
    ok("two letters fit", build("y = ab x", p) && p.params.size() == 2);
    CompileError e = reject("y = abc x");
    ok("a third is an error naming it",
       !e.ok && e.message.find("'c'") != std::string::npos && e.col == 7);
    ok("e, pi and x are not knobs", build("y = e pi x", p) && p.params.empty());
}

void mode_tests() {
    std::printf("polar and parametric\n");
    Program p;
    ok("r = 1 + cos(θ)", build("r = 1 + cos(\xCE\xB8)", p));
    ok("is polar", p.mode == MODE_POLAR);
    {
        const double th = 0.8, r = 1 + std::cos(th);
        const Result res = runAt(p, th);
        nearly("X = r cos θ", res.x, r * std::cos(th), 1e-12);
        nearly("Y = r sin θ", res.value, r * std::sin(th), 1e-12);
    }
    same("theta spelled out", "r = 1 + cos(theta)", "r = 1 + cos(\xCE\xB8)");
    ok("f(θ) = is polar", build("f(\xCE\xB8) = 2", p) && p.mode == MODE_POLAR);
    nearly("a circle of radius 2", runAt(p, kPi / 2).value, 2.0, 1e-12);
    ok("a rose with a knob", build("r = cos(k\xCE\xB8)", p) && p.params.size() == 1);

    ok("(cos(3t), sin(2t))", build("(cos(3t), sin(2t))", p));
    ok("is parametric", p.mode == MODE_PARAM);
    {
        const Result res = runAt(p, 0.4);
        nearly("X = x(t)", res.x, std::cos(1.2), 1e-12);
        nearly("Y = y(t)", res.value, std::sin(0.8), 1e-12);
    }
    ok("a parenthesised y = is not parametric",
       build("y = (x + 1)", p) && p.mode == MODE_X);
    ok("a bracketed comma is not parametric",
       build("(max(x, 1))", p) && p.mode == MODE_X);

    CompileError e = reject("y = sin(\xCE\xB8)");
    ok("θ in y = ... says to write r =", !e.ok && e.message.find("r =") != std::string::npos);
    e = reject("r = x");
    ok("x in a polar curve is an error", !e.ok);
    e = reject("y = t");
    ok("t in y = ... points to the parametric form", !e.ok && e.message.find("parametric") != std::string::npos);
    e = reject("(x, t)");
    ok("x in a parametric curve is an error", !e.ok);
}

void fault_tests() {
    std::printf("faults\n");
    Program p;

    ok("1/x compiles", build("y = 1/x", p));
    {
        Instance i; i.prepare(&p, 1);
        Inputs in; in.v[IN_X] = 0.0;
        Result r = i.run(in);
        ok("divide by zero faults", r.fault);
        ok("fault names the operation", std::string(r.faultWhat) == "divide by zero");
        ok("fault has a source position", r.faultLine == 1 && r.faultCol == 6);
        ok("fault count advanced", i.faultCount == 1);
        in.v[IN_X] = 2.0;
        ok("the next sample is fine", !i.run(in).fault);
    }
    ok("ln(0) faults", build("y = ln(x)", p) && runAt(p, 0.0).fault);
    ok("sqrt(-1) faults", build("y = sqrt(x)", p) && runAt(p, -1.0).fault);
    ok("(-1)^0.5 faults", build("y = x^0.5", p) && runAt(p, -1.0).fault);
    ok("zero modulus faults", build("y = mod(1, x)", p) && runAt(p, 0.0).fault);
    ok("a negative modulus does not fault", !runAt(p, -3.0).fault);
    nearly("mod(1, -3) is -2, floored as in Desmos", at(p, -3.0), -2.0, 1e-12);
    ok("exp overflow faults", build("y = exp(x)", p) && runAt(p, 1000.0).fault);
    ok("exp underflow to zero is fine", !runAt(p, -1000.0).fault && at(p, -1000.0) == 0.0);
    ok("tan near its pole is large but finite",
       build("y = tan(x)", p) && !runAt(p, kPi / 2).fault && std::fabs(at(p, kPi / 2)) > 1e10);
    ok("a polar fault in r faults the point",
       build("r = 1/\xCE\xB8", p) && runAt(p, 0.0).fault);
}

void compile_error_tests() {
    std::printf("compile errors\n");
    CompileError e;

    e = reject("sin(x");
    ok("unbalanced paren is rejected", !e.ok);
    e = reject("y = 1 +");
    ok("dangling operator is rejected", !e.ok && e.message.find("end") != std::string::npos);
    e = reject("y = foo(x)");
    ok("an unknown word is letters: foo(x) is f*o*o*x, as in Desmos",
       e.ok);
    e = reject("y = sin(x, 2)");
    ok("wrong arity is rejected", !e.ok && e.message.find("takes 1 argument") != std::string::npos);
    e = reject("y = sin");
    ok("a function needs an argument", !e.ok && e.message.find("argument") != std::string::npos);
    e = reject("");
    ok("empty equation is rejected", !e.ok);
    e = reject("y = x\ny = 2x");
    ok("two equations are rejected", !e.ok && e.line == 2);
    e = reject("sin(x) = y");
    ok("y on the right is rejected", !e.ok);
    e = reject("x = sin(y)");
    ok("x = ... says to write y = f(x)", !e.ok && e.message.find("y = f(x)") != std::string::npos);
    e = reject("y = x < 1");
    ok("a bare comparison points to piecewise", !e.ok && e.message.find("piecewise") != std::string::npos);
    e = reject("y = [1,2,3]");
    ok("a list needs an index", !e.ok && e.message.find("index") != std::string::npos);
    e = reject("y = []");
    ok("an empty list is rejected", !e.ok);
    e = reject("y = {1, 2}");
    ok("a piecewise needs a condition", !e.ok);
    e = reject("y = {2, x < 1: 3}");
    ok("the else must come last", !e.ok);
    e = reject("y = \\sin\\left(x\\right)");
    ok("LaTeX is named as LaTeX", !e.ok && e.message.find("LaTeX") != std::string::npos);
    e = reject("y = 1 $ 2");
    ok("stray character reports its column", !e.ok && e.col == 7);
    e = reject("y = sin(x)\n  + $");
    ok("and its line", !e.ok && e.line == 2 && e.col == 5);
    e = reject("y = x % 2");
    ok("% is not Desmos: use mod()", !e.ok);

    std::string deep = "y = ";
    for (int i = 0; i < 70; i++) deep += "(";
    deep += "x";
    for (int i = 0; i < 70; i++) deep += ")";
    e = reject(deep.c_str());
    ok("nesting past the limit is rejected", !e.ok && e.message.find("nested") != std::string::npos);

    std::string big(kMaxSourceBytes + 1, ' ');
    e = reject(big.c_str());
    ok("oversize source is rejected", !e.ok);
}

void condition_tests() {
    std::printf("output conditioning\n");
    Conditioner c;
    float target;

    nearly("unity: 1 in is 1 V out", c.process(1.0, false, DT, &target), 1.0, 0.0);
    c.gain = 2.f; c.offset = 0.5f;
    nearly("gain and offset", c.process(1.0, false, DT, &target), 2.5, 1e-6);
    c.gain = 1.f; c.offset = 0.f;

    // Hard limit at 5 V.
    nearly("hard limit clamps at +L", c.process(8.0, false, DT, &target), 5.0, 0.0);
    nearly("hard limit clamps at -L", c.process(-8.0, false, DT, &target), -5.0, 0.0);
    ok("clipping is flagged", c.lastClipped);
    nearly("below L passes", c.process(3.0, false, DT, &target), 3.0, 0.0);
    ok("not clipped", !c.lastClipped);

    // Soft knee: unity below L - W/2, exactly L above L + W/2, monotonic.
    c.knee = 2.f;   // a = 4, b = 6
    nearly("soft knee: unity below the knee", c.limitOnly(3.9), 3.9, 1e-7);
    nearly("soft knee: L above the knee", c.limitOnly(6.5), 5.0, 1e-7);
    nearly("soft knee: continuous at a", c.limitOnly(4.0), 4.0, 1e-7);
    nearly("soft knee: continuous at b", c.limitOnly(6.0), 5.0, 1e-7);
    nearly("soft knee: midpoint 4 + 1 - 1/4", c.limitOnly(5.0), 4.75, 1e-7);
    bool mono = true, bounded = true;
    double last = -10;
    for (double v = -10; v <= 10; v += 0.01) {
        double y = c.limitOnly(v);
        if (y < last - 1e-9) mono = false;
        if (std::fabs(y) > 5.0 + 1e-9) bounded = false;
        last = y;
    }
    ok("soft knee is monotonic", mono);
    ok("soft knee never exceeds L", bounded);
    nearly("soft knee is odd", c.limitOnly(-5.0), -4.75, 1e-7);
    c.knee = 0.f;

    // Fault policy.
    c.reset();
    c.process(2.0, false, DT, &target);
    nearly("fault -> 0 by default", c.process(99.0, true, DT, &target), 0.0, 0.0);
    c.faultPolicy = Conditioner::FAULT_HOLD;
    c.process(2.0, false, DT, &target);
    nearly("fault -> hold last valid target", c.process(99.0, true, DT, &target), 2.0, 0.0);
    c.faultPolicy = Conditioner::FAULT_ZERO;

    // Lag: one time constant reaches 1 - 1/e.
    c.reset();
    c.smooth = Conditioner::SMOOTH_LAG;
    c.tauSec = 0.010f;
    double y = 0;
    for (int n = 0; n < 480; n++) y = c.process(1.0, false, DT, &target);
    nearly("lag after one tau", y, 1.0 - std::exp(-1.0), 2e-3);
    ok("target reports the unsmoothed value", target == 1.f);

    // Slew: constant speed.
    c.reset();
    c.smooth = Conditioner::SMOOTH_SLEW;
    c.riseVps = 480.f; c.fallVps = 48000.f;    // 0.01 V/sample up, 1 V/sample down
    nearly("slew rise step", c.process(5.0, false, DT, &target), 0.01, 1e-9);
    nearly("slew rise step 2", c.process(5.0, false, DT, &target), 0.02, 1e-9);
    nearly("slew fall is independent", c.process(-5.0, false, DT, &target), -0.98, 1e-6);

    // The final guard: lowering the limit while lag still holds a higher
    // value never lets the old value through above the ceiling, and NaN
    // never reaches the output.
    c.reset();
    c.smooth = Conditioner::SMOOTH_LAG;
    c.tauSec = 1.f;
    c.limit = 10.f;
    for (int n = 0; n < 48000; n++) c.process(10.0, false, DT, &target);
    c.limit = 1.f;
    double out = c.process(10.0, false, DT, &target);
    ok("a reduced limit is approached, not jumped (lag holds the old value)",
       out > 1.0 && out <= 10.0);
    c.prev = std::nan("");
    out = c.process(1.0, false, DT, &target);
    ok("NaN in the smoothing memory is guarded to a finite output", std::isfinite(out));
    c.smooth = Conditioner::SMOOTH_NONE;
    c.limit = 5.f;
    c.gain = 1e30f;
    out = c.process(1e30, false, DT, &target);
    ok("gain overflow is guarded", std::isfinite(out) && std::fabs(out) <= 10.f);
}

void history_tests() {
    std::printf("history\n");
    HistoryRing<64> h;
    h.setSampleRate(1280.f);   // 10 samples per bin
    ok("bin size follows the sample rate", h.binSamples == 10);

    // A one-sample spike inside a bin survives aggregation.
    for (int n = 0; n < 10; n++)
        h.record(n == 4 ? 9.f : 0.f, n == 4 ? 5.f : 0.f, float(n), n == 4 ? uint32_t(HF_CLIP) : 0u, 3);
    HistoryBin out[8];
    int got = h.snapshot(out, 8);
    ok("one completed bin", got == 1);
    ok("spike preserved in max", out[0].tMax == 9.f && out[0].aMax == 5.f);
    ok("first and last kept", out[0].tFirst == 0.f && out[0].tLast == 0.f);
    ok("x endpoints kept", out[0].xFirst == 0.f && out[0].xLast == 9.f);
    ok("flags accumulate over the bin", out[0].flags == HF_CLIP);
    ok("revision stamped", out[0].revision == 3);

    // A partial bin is not published.
    h.record(1.f, 1.f, 0.f, 0, 3);
    ok("partial bin is not visible", h.snapshot(out, 8) == 1);

    // Many bins: the newest n, oldest first.
    for (int b = 0; b < 100; b++)
        for (int n = 0; n < 10; n++) h.record(float(b), float(b), 0.f, 0, 3);
    got = h.snapshot(out, 8);
    ok("snapshot returns the last 8", got == 8);
    ok("oldest first, newest last", out[0].tLast == 92.f && out[7].tLast == 99.f);
    ok("the ring wrapped without harm", h.written.load() > 64);
}

void preview_tests() {
    std::printf("preview\n");
    Program p;
    build("sin(x)", p);
    Inputs in;
    Preview pv;
    pv.build(p, in, 0.0, 2 * kPi, 32, 256);
    ok("preview covers the domain", !pv.points.empty() && pv.points.front().x == 0.0
                                     && std::fabs(pv.points.back().x - 2 * kPi) < 1e-12);
    ok("preview refined past the coarse grid", pv.points.size() > 32);
    bool sorted = true;
    for (size_t i = 1; i < pv.points.size(); i++) if (pv.points[i].x < pv.points[i - 1].x) sorted = false;
    ok("preview points are sorted", sorted);
    ok("sine has no faults", pv.faults == 0);

    build("y = {x >= 0: sqrt(x), ln(x)}", p);
    pv.build(p, in, -1.0, 1.0, 16, 128);
    int invalid = 0;
    for (size_t i = 0; i < pv.points.size(); i++) if (!pv.points[i].valid) invalid++;
    ok("invalid region is drawn as gaps, not joined", invalid > 0 && pv.faults == invalid);

    build("y = 1/x", p);
    pv.build(p, in, -1.0, 1.0, 17, 400);
    bool anyBreak = false;
    for (size_t i = 0; i < pv.points.size(); i++) if (pv.points[i].breakBefore || !pv.points[i].valid) anyBreak = true;
    ok("a pole produces a break or a gap", anyBreak);
}


void domain_tests() {
    std::printf("domain\n");
    Program p;
    ok("no restriction: no domain", build("y = sin(x)", p) && !p.hasDomain);
    Domain d = domainOf(&p);
    ok("default domain is 0..2pi", d.lo == 0.0 && std::fabs(d.hi - 2 * kPi) < 1e-12);

    ok("restriction is the domain", build("y = sin(x) {-8 \xE2\x89\xA4 x \xE2\x89\xA4 8}", p)
                                    && p.hasDomain && p.domainLo == -8 && p.domainHi == 8);
    ok("restriction leaves the equation", p.mode == MODE_X && p.usesX);
    ok("ascii <= works", build("y = x {0 <= x <= 1}", p) && p.domainLo == 0 && p.domainHi == 1);
    ok(">= both ways swaps the ends", build("y = x {4 >= x >= -2}", p)
                                      && p.domainLo == -2 && p.domainHi == 4);
    ok("pi expressions as ends", build("y = sin x {-\xCF\x80/2 \xE2\x89\xA4 x \xE2\x89\xA4 3\xCF\x80/2}", p)
                                 && std::fabs(p.domainLo + kPi / 2) < 1e-12
                                 && std::fabs(p.domainHi - 1.5 * kPi) < 1e-12);
    ok("polar takes theta", build("r = cos(2\xCE\xB8) {0 \xE2\x89\xA4 \xCE\xB8 \xE2\x89\xA4 \xCF\x80}", p)
                            && p.mode == MODE_POLAR && std::fabs(p.domainHi - kPi) < 1e-12);
    ok("parametric takes t", build("(cos t, sin(2t)) {0 \xE2\x89\xA4 t \xE2\x89\xA4 4\xCF\x80}", p)
                             && p.mode == MODE_PARAM && std::fabs(p.domainHi - 4 * kPi) < 1e-12);
    ok("a piecewise value is not a domain", build("y = {0 \xE2\x89\xA4 x \xE2\x89\xA4 1}", p) && !p.hasDomain);
    ok("a one-sided restriction is not a domain", build("y = {x < 1: x, 0}", p) && !p.hasDomain);

    CompileError e = reject("y = sin(ax) {0 \xE2\x89\xA4 x \xE2\x89\xA4 a}");
    ok("a knob in an end is refused", !e.ok && e.message.find("numbers") != std::string::npos);
    e = reject("y = sin(x) {0 \xE2\x89\xA4 t \xE2\x89\xA4 1}");
    ok("the wrong variable is refused", !e.ok && e.message.find("sweeps") != std::string::npos);
    e = reject("y = x {1 \xE2\x89\xA4 x \xE2\x89\xA4 1}");
    ok("equal ends are refused", !e.ok && e.message.find("two different ends") != std::string::npos);

    // LOOP and POS: exact slices that meet themselves at the wrap.
    ok("LOOP positions, whole domain at the right end",
       loopDivisions(0.f) == 16 && loopDivisions(4.f) == 4
       && loopDivisions(7.f) == 1 && loopDivisions(99.f) == 1);
    ok("POS picks equal shares", sliceOf(0.f, 4) == 0 && sliceOf(0.26f, 4) == 1
                                 && sliceOf(0.99f, 4) == 3 && sliceOf(1.f, 4) == 3);
    const Domain full = {0.0, 2 * kPi};
    nearly("slice start", sliceX(full, 1, 4, 0.0), kPi / 2, 1e-12);
    nearly("slice end is the next slice's start", sliceX(full, 1, 4, 1.0), sliceX(full, 2, 4, 0.0), 0.0);
    nearly("whole domain at LOOP 1", sliceX(full, 0, 1, 1.0), 2 * kPi, 0.0);
    // Repeating: slice 1 of 4 plays four times a lap, each pass at the
    // speed the whole domain plays at.
    nearly("a repeat starts at the slice start", sliceX(full, 1, 4, repeatU(0.25, 4)), kPi / 2, 1e-12);
    nearly("x moves at whole-domain speed",
           sliceX(full, 1, 4, repeatU(0.30, 4)) - sliceX(full, 1, 4, repeatU(0.25, 4)),
           sliceX(full, 0, 1, 0.30) - sliceX(full, 0, 1, 0.25), 1e-12);
    nearly("LOOP whole is unchanged", repeatU(0.7, 1), 0.7, 1e-12);

    // Text: what the display writes, and what migration appends.
    ok("pi fractions", piFraction(2 * kPi) == "2\xCF\x80" && piFraction(-kPi) == "-\xCF\x80"
                       && piFraction(1.5 * kPi) == "3\xCF\x80/2" && piFraction(kPi / 12) == "\xCF\x80/12"
                       && piFraction(1.0).empty());
    ok("0..2pi is in pi, 0..1 is not", domainInPi(full) && !domainInPi(Domain{0.0, 1.0}));
    ok("display text", domainText(kPi / 2, true) == "\xCF\x80/2" && domainText(2.5, false) == "2.5");
    ok("bound text round-trips decimals", boundText(0.1) == "0.1" && boundText(-32) == "-32"
                                               && boundText(10) == "10" && boundText(1e-3) == "0.001");
    const std::string r = "y = sin(x)" + restrictionText(MODE_X, -kPi, kPi);
    ok("appended restriction compiles back to the same domain",
       build(r.c_str(), p) && p.domainLo == -kPi && p.domainHi == kPi);
    const std::string rp = "r = cos(3\xCE\xB8)" + restrictionText(MODE_POLAR, 0, 10);
    ok("polar restriction uses theta", build(rp.c_str(), p) && p.domainHi == 10);
}

/// Every equation in the Examples menu compiles, and over its own domain
/// with default knobs most of the lap is defined and finite.
void example_one(const Example& ex) {
    Program p;
    const bool built = build(ex.source, p);
    int defined = 0;
    double peak = 0;
    const int N = 4096;
    // No restriction means the module's default lap, 0 to 2π.
    const double lo = p.hasDomain ? p.domainLo : 0.0;
    const double hi = p.hasDomain ? p.domainHi : 2 * kPi;
    for (int i = 0; built && i <= N; i++) {
        const double v = lo + (hi - lo) * i / N;
        const Result r = runAt(p, v);
        if (r.fault || !std::isfinite(r.value)) continue;
        defined++;
        if (std::fabs(r.value) > peak) peak = std::fabs(r.value);
    }
    std::printf("      %-28s defined %5.1f%%  peak %.3g\n", ex.name, 100.0 * defined / (N + 1), peak);
    ok(ex.name, built && defined > 0);
}

void example_tests() {
    std::printf("examples\n");
    for (int i = 0; i < kPatternExampleCount; i++) example_one(kPatternExamples[i]);
    for (int i = 0; i < kBasicExampleCount; i++) example_one(kBasicExamples[i]);
}

}  // namespace

int main() {
    corpus_tests();
    notation_tests();
    knob_tests();
    mode_tests();
    fault_tests();
    compile_error_tests();
    condition_tests();
    history_tests();
    preview_tests();
    domain_tests();
    example_tests();
    std::printf("\n%s\n", failures ? "FAILURES" : "all expr checks passed");
    return failures ? 1 : 0;
}
