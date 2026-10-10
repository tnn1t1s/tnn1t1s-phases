#pragma once

// ExprDomain.hpp -- the domain one lap sweeps, and the slice of it LOOP plays.
//
// The domain is part of the equation, a Desmos restriction at its end,
// `y = sin(x) {-8 ≤ x ≤ 8}` (ExprCompiler.hpp); without one it is 0..2π.
// LOOP divides the domain into equal slices and POS picks one, so a lap
// plays exactly one slice and the wrap lands where the slice began. A
// slice is an exact fraction of the domain, which is what makes a loop
// close: the knobs never produce an end the equation did not imply.
//
// Header-only and free of <rack.hpp>, like the rest of src/expr, so the
// arithmetic is covered in tests/expr.

#include "expr/ExprProgram.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace Lives {
namespace Expr {

static const double kDomainPi = 3.14159265358979323846;

/// The domain without a restriction: 0..2π, the range one cycle of sin(x)
/// spans and the one every example is written for (default).
static const double kDefaultDomainLo = 0.0;
static const double kDefaultDomainHi = 2.0 * kDomainPi;

/// LOOP's positions, as slices per domain (parameter). Divisors of 48, the
/// counting PHASE uses, so a slice lines up with the beat subdivisions a
/// lap is usually cut into: halves, thirds, quarters, sixths, eighths,
/// twelfths, sixteenths. Smallest slice first, so turning the knob
/// clockwise plays more of the domain and the right end plays all of it.
static const int kLoopDivisions[] = {16, 12, 8, 6, 4, 3, 2, 1};
static const int kLoopCount = int(sizeof(kLoopDivisions) / sizeof(kLoopDivisions[0]));

struct Domain { double lo, hi; };

inline Domain domainOf(const Program* p) {
    if (p && p->hasDomain) return {p->domainLo, p->domainHi};
    return {kDefaultDomainLo, kDefaultDomainHi};
}

/// Slices per domain for a LOOP knob value, rounded and clamped to a position.
inline int loopDivisions(float loopKnob) {
    int i = int(std::lround(loopKnob));
    if (i < 0) i = 0;
    if (i >= kLoopCount) i = kLoopCount - 1;
    return kLoopDivisions[i];
}

/// The slice POS (0..1) selects out of n: equal shares of the knob's travel,
/// the last one including the end stop.
inline int sliceOf(float pos, int n) {
    int k = int(std::floor(double(pos) * n));
    if (k < 0) k = 0;
    if (k > n - 1) k = n - 1;
    return k;
}

/// The lap position inside the slice when the slice repeats: n times per
/// lap, each pass at the speed the whole domain plays at, so a smaller
/// slice keeps its tempo and loops instead of stretching across the lap.
inline double repeatU(double u, int n) {
    const double t = u * double(n);
    return t - std::floor(t);
}

/// x for a lap position u (0..1) inside slice k of n.
inline double sliceX(const Domain& d, int k, int n, double u) {
    return d.lo + (d.hi - d.lo) * ((double(k) + u) / double(n));
}

/// v as a whole multiple of π/den for a den up to 12, written the way the
/// equation would be: "0", "π", "-2π", "3π/2", "π/12". Empty if v is not one.
inline std::string piFraction(double v) {
    static const int dens[] = {1, 2, 3, 4, 6, 12};
    for (int den : dens) {
        const double k = v * den / kDomainPi;
        const double r = std::round(k);
        if (std::fabs(k - r) > 1e-9 || std::fabs(r) > 1e6) continue;
        const long n = long(r);
        if (n == 0) return "0";
        const char* sign = n < 0 ? "-" : "";
        const long a = n < 0 ? -n : n;
        char buf[48];
        if (a == 1 && den == 1) std::snprintf(buf, sizeof buf, "%sπ", sign);
        else if (den == 1) std::snprintf(buf, sizeof buf, "%s%ldπ", sign, a);
        else if (a == 1) std::snprintf(buf, sizeof buf, "%sπ/%d", sign, den);
        else std::snprintf(buf, sizeof buf, "%s%ldπ/%d", sign, a, den);
        return buf;
    }
    return "";
}

/// True when both ends are multiples of π/12, so the display writes x in π.
/// 0..1 is not: an end of 0 alone says nothing about the units.
inline bool domainInPi(const Domain& d) {
    const std::string a = piFraction(d.lo), b = piFraction(d.hi);
    return !a.empty() && !b.empty() && (a != "0" || b != "0");
}

/// A value for the display: exact π fractions where they exist when the
/// domain is in π, otherwise three significant figures ("1.57π" or "2.5").
inline std::string domainText(double v, bool inPi) {
    char buf[48];
    if (inPi) {
        const std::string f = piFraction(v);
        if (!f.empty()) return f;
        std::snprintf(buf, sizeof buf, "%.3gπ", v / kDomainPi);
    } else {
        std::snprintf(buf, sizeof buf, "%.3g", v);
    }
    return buf;
}

/// An end as equation text the compiler reads back to the same number:
/// a π fraction when it is one, otherwise the shortest plain decimal that
/// round-trips. Never exponent notation, which the equation reads as e.
inline std::string boundText(double v) {
    const std::string f = piFraction(v);
    if (!f.empty()) return f;
    char buf[400];
    for (int places = 0; places <= 17; places++) {
        std::snprintf(buf, sizeof buf, "%.*f", places, v);
        if (std::strtod(buf, nullptr) == v) break;
    }
    return buf;
}

/// The restriction that gives a program of this mode the domain lo..hi,
/// with a leading space, ready to append: " {-8 ≤ x ≤ 8}".
inline std::string restrictionText(Mode mode, double lo, double hi) {
    const char* var = mode == MODE_POLAR ? "θ" : mode == MODE_PARAM ? "t" : "x";
    return " {" + boundText(lo) + " ≤ " + var + " ≤ " + boundText(hi) + "}";
}

}  // namespace Expr
}  // namespace Lives
