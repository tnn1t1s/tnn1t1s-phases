#pragma once

// ExprPreview.hpp -- the y(x) curve of a pure program, for the display.
//
// A pure program (no state, no stateful operator, no clock) has a static
// curve at a snapshot of its parameter values, and the display draws it.
// The curve is evaluated on the UI thread in a throwaway Instance made
// from the same immutable Program -- never the live one, so the preview
// cannot tick anything. Stateful programs have no such curve; the module
// shows history for those instead. The curve is drawn as if PHASE were the
// source, so `u` moves with `x`: u = (x - x0) / (x1 - x0), the inverse of
// the module's x = start + u * (end - start).
//
// Adaptive subdivision with a fixed budget: start with a coarse grid, then
// split the segment at the sharpest bend until the budget is spent or the
// curve is smooth. A point that faults is drawn as a gap rather than joined
// across; a bend that is still sharp at the finest resolution is a jump or
// a pole and is drawn as a break. A pole that lands between samples is
// simply not found -- no finite sampling strategy guarantees it -- and the
// display says `partial` when the budget ran out with bends unresolved.

#include "expr/ExprRuntime.hpp"

#include <cmath>
#include <vector>

namespace Lives {
namespace Expr {

struct PreviewPoint {
    double x;
    double y;
    bool   valid;        // false: this x faulted; draw nothing here
    bool   breakBefore;  // do not join to the previous point
    bool   frozen;       // refinement gave up on this bend
};

struct Preview {
    std::vector<PreviewPoint> points;   // sorted by x
    bool complete = true;               // the budget covered every bend
    int  faults = 0;

    /// Evaluate `prog` over [x0, x1] with the parameter values in `base`.
    /// `coarse` is the initial grid, `budget` the total evaluations allowed.
    void build(const Program& prog, const Inputs& base, double x0, double x1,
               int coarse, int budget) {
        points.clear();
        complete = true;
        faults = 0;
        if (!(x1 > x0) || coarse < 2) return;

        domain0 = x0;
        domainSpan = x1 - x0;
        Instance inst;
        inst.prepare(&prog, 1);
        Inputs in = base;

        for (int i = 0; i < coarse; i++) {
            const double x = x0 + (x1 - x0) * double(i) / double(coarse - 1);
            points.push_back(eval(inst, in, x));
        }
        int used = coarse;
        const double minDx = (x1 - x0) * 1e-4;

        while (used < budget) {
            size_t worst = 0;
            double worstBend = 0.0;
            for (size_t i = 1; i + 1 < points.size(); i++) {
                if (points[i].frozen) continue;
                const PreviewPoint& a = points[i - 1];
                const PreviewPoint& b = points[i];
                const PreviewPoint& c = points[i + 1];
                double bend;
                if (a.valid != b.valid || b.valid != c.valid) {
                    bend = 1e9;   // the edge of an invalid region: locate it
                } else if (!b.valid) {
                    bend = 0.0;
                } else {
                    const double d1 = (b.y - a.y) / (b.x - a.x);
                    const double d2 = (c.y - b.y) / (c.x - b.x);
                    bend = std::fabs(d2 - d1) * (c.x - a.x);
                }
                if (bend > worstBend) { worstBend = bend; worst = i; }
            }
            if (worstBend < 1e-3) break;   // smooth enough everywhere
            const double left = points[worst].x - points[worst - 1].x;
            const double right = points[worst + 1].x - points[worst].x;
            if (left < minDx && right < minDx) {
                // As fine as it goes and still bending: a jump or a pole.
                points[worst].frozen = true;
                points[worst].breakBefore = true;
                continue;
            }
            const size_t lo = left > right ? worst - 1 : worst;
            const double xm = 0.5 * (points[lo].x + points[lo + 1].x);
            points.insert(points.begin() + long(lo) + 1, eval(inst, in, xm));
            used++;
        }
        if (used >= budget) complete = false;
    }

private:
    double domain0 = 0.0, domainSpan = 1.0;

    PreviewPoint eval(Instance& inst, Inputs& in, double x) {
        in.v[IN_X] = x;
        in.v[IN_U] = (x - domain0) / domainSpan;
        const Result r = inst.run(in);
        PreviewPoint p;
        p.x = x;
        p.y = r.fault ? 0.0 : r.value;
        p.valid = !r.fault;
        p.breakBefore = false;
        p.frozen = false;
        if (r.fault) faults++;
        return p;
    }
};

}  // namespace Expr
}  // namespace Lives
