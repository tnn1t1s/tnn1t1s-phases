#pragma once

// LapCounter.hpp -- where a lap begins, and when its length is allowed to
// change.
//
// A lap spans BEATS beats of PPQN pulses each. Both of those are user
// controls, so the length can change while a patch runs -- and changing it
// mid-lap moves the downbeat. The counter used to free-run and derive position
// as (pulseIdx % length), so a new length rescaled AND re-origined the
// position in one instruction: measured jumps of up to half a lap, which every
// follower reads as a wrap.
//
// The rule this type exists to enforce:
//
//     defer what moves the lap boundary; apply immediately what moves the
//     playhead within a fixed boundary.
//
// So a pending length is adopted only on the pulse that OPENS a lap. That one
// rule covers the boundary case, RST, the first pulse after patching, and a
// re-patch, with no branches of its own -- `lapStart` already meant "the next
// pulse opens a lap", and a lap boundary means the same thing.
//
// DIRECTION is the other side of the rule and stays immediate: reflecting the
// ramp maps the boundary onto itself (phase 0 -> 1, which is 0), so it moves
// the playhead without moving the downbeat. START and LEN likewise -- they are
// applied after the transport and preserve one wrap per lap.
//
// INVARIANT: `active` is constant over the interior of a lap. Every writer of
// `active` also zeroes `pulseIdx`. Keep those together: latching the length
// without re-origining the counter does not fix the jump, it just delays it --
// at pulseIdx 96, adopting a length of 192 gives 96 % 192 = 96, or phase 0.5.
//
// Header-only and free of <rack.hpp> so it can be tested without a Rack.

#include <cmath>

namespace Lives {

struct LapCounter {
    int  pulseIdx = 0;      // pulses into the current lap, always < active
    int  active   = 1;      // the length this lap is being measured against
    bool lapStart = true;   // armed: the next pulse opens a lap

    void reset() {
        pulseIdx = 0;
        active   = 1;
        lapStart = true;
    }

    /// RST, or a re-patch: abandon this lap. The length is not adopted here --
    /// the user may keep turning the knob before the pulse lands, and the
    /// value that matters is the one at pulse time.
    void arm() {
        pulseIdx = 0;
        lapStart = true;
    }

    /// Adopt a length immediately and open a fresh lap. Only for the case
    /// where no lap is in flight to protect -- a stopped transport -- where
    /// the caller must also be holding its output, or this moves the wire.
    void adoptNow(int want) {
        pulseIdx = 0;
        active   = want > 1 ? want : 1;
        lapStart = true;
    }

    /// One clock pulse arrived. `want` is the length the controls currently
    /// ask for. Returns true if this pulse opened a lap.
    ///
    /// `>=` rather than `==` on the wrap test: if any future path ever leaves
    /// active <= pulseIdx without zeroing, `==` misses the boundary and the
    /// lap free-runs; `>=` recovers on the next pulse.
    bool pulse(int want) {
        if (want < 1) want = 1;
        if (lapStart) {
            pulseIdx = 0;
            active   = want;
            lapStart = false;
            return true;
        }
        if (++pulseIdx >= active) {
            pulseIdx = 0;
            active   = want;
            return true;
        }
        return false;
    }

    /// Position in the lap, 0..0.9995. `frac` interpolates across the pulse so
    /// a coarse clock still yields a smooth wire.
    ///
    /// At an adopting pulse pulseIdx and frac are both 0, so this returns
    /// exactly 0 for ANY length -- which is why adoption changes the ramp's
    /// slope without moving its position, and so cannot click.
    double phase(double frac) const {
        const double p = (double(pulseIdx) + frac) / double(active);
        return p < 0.9995 ? p : 0.9995;
    }
};

}  // namespace Lives
