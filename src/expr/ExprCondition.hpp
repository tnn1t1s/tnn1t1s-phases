#pragma once

// ExprCondition.hpp -- from a program's number to a Rack voltage.
//
//     program result
//       -> gain + offset
//       -> fault policy (zero, or hold the last valid target)
//       -> hard or soft-knee limiter
//       -> lag, slew, or bypass
//       -> final finite / range guard
//       -> Y (volts)
//
// A result of 1 is 1 V at unity gain and zero offset: there is no implicit
// audio-normalized scaling anywhere in this module. The limiter is a
// memoryless transfer characteristic with no attack or release; smoothing
// is the only thing here with memory, and a program RESET clears it.
//
// Header-only and free of <rack.hpp> (tests/expr).

#include "lives/Denormal.hpp"

#include <cmath>

namespace Lives {
namespace Expr {

struct Conditioner {
    enum Smooth { SMOOTH_NONE = 0, SMOOTH_LAG = 1, SMOOTH_SLEW = 2 };
    enum FaultPolicy { FAULT_ZERO = 0, FAULT_HOLD = 1 };

    // Absolute output ceiling, whatever the limit is set to.
    static constexpr float kCeiling = 10.f;

    // Settings (UI thread writes between samples; one-sample staleness is
    // harmless, these are plain floats read once per sample).
    float gain    = 1.f;
    float offset  = 0.f;     // volts
    float limit   = 5.f;     // L, volts, 0 < L <= 10
    float knee    = 0.f;     // W, full knee width in volts, 0 = hard, <= 2L
    int   smooth  = SMOOTH_NONE;
    float tauSec  = 0.010f;  // lag
    float riseVps = 100.f;   // slew, volts per second
    float fallVps = 100.f;
    int   faultPolicy = FAULT_ZERO;

    // State.
    double prev       = 0.0;   // smoothing memory, volts
    double lastTarget = 0.0;   // last valid mapped target, for FAULT_HOLD
    bool   haveTarget = false;
    bool   lastClipped = false;

    void reset() {
        prev = 0.0;
        lastTarget = 0.0;
        haveTarget = false;
        lastClipped = false;
    }

    /// The limiter alone: v in, bounded value out. Soft knee of full width
    /// W around L: unity slope below L - W/2, slope falling smoothly to
    /// zero at L + W/2, never above L. W = 0 is the hard form, with no
    /// division.
    float limitOnly(double v) const {
        const double L = limit > 0.f ? (limit < kCeiling ? limit : kCeiling) : 1e-3;
        double W = knee;
        if (W > 2.0 * L) W = 2.0 * L;
        const double m = std::fabs(v);
        double shaped;
        if (W <= 0.0) {
            shaped = m > L ? L : m;
        } else {
            const double a = L - W / 2.0, b = L + W / 2.0;
            if (m <= a)      shaped = m;
            else if (m >= b) shaped = L;
            else {
                const double d = m - a;
                shaped = a + d - d * d / (2.0 * W);
            }
        }
        return float(v < 0.0 ? -shaped : shaped);
    }

    /// One sample. `raw` is the program's result (ignored when `fault`),
    /// dt the sample time. `targetOut` receives the limited, unsmoothed
    /// target, for the history. Returns the finite, bounded output volts.
    float process(double raw, bool fault, double dt, float* targetOut) {
        double v;
        if (fault) {
            v = (faultPolicy == FAULT_HOLD && haveTarget) ? lastTarget : 0.0;
        } else {
            v = raw * double(gain) + double(offset);
            if (!std::isfinite(v)) v = 0.0;   // gain/offset overflow guard
            lastTarget = v;
            haveTarget = true;
        }
        const float target = limitOnly(v);
        lastClipped = std::fabs(v) > double(limit);

        double y;
        switch (smooth) {
        case SMOOTH_LAG: {
            const double tau = tauSec;
            if (tau > 0.0) {
                const double alpha = 1.0 - std::exp(-dt / tau);
                y = prev + alpha * (double(target) - prev);
            } else {
                y = target;
            }
            break;
        }
        case SMOOTH_SLEW: {
            double d = double(target) - prev;
            const double up = double(riseVps < 0.f ? 0.f : riseVps) * dt;
            const double down = double(fallVps < 0.f ? 0.f : fallVps) * dt;
            if (d > up) d = up;
            if (d < -down) d = -down;
            y = prev + d;
            break;
        }
        default:
            y = target;
            break;
        }

        // Final guard: a reduced limit while smoothing still holds an
        // older, higher value; a bypass transition; anything nonfinite.
        if (!std::isfinite(y)) { y = 0.0; prev = 0.0; }
        if (y >  kCeiling) y =  kCeiling;
        if (y < -kCeiling) y = -kCeiling;
        prev = y;
        float out = float(y);
        flush(out);
        if (targetOut) *targetOut = target;
        return out;
    }
};

}  // namespace Expr
}  // namespace Lives
