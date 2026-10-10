// lap_test.cpp -- when a lap is allowed to change length.
//
// Changing BEATS or RATE mid-lap used to move the downbeat: position was
// (pulseIdx % length), so a new length rescaled and re-origined it at once,
// jumping up to half a lap. Followers read that as a wrap.
//
// The load-bearing claim these tests pin is that adopting a length AT a lap
// boundary is free: pulseIdx and frac are both 0 there, so phase is 0 for any
// length, and the ramp's slope changes without its position moving. Case 1
// checks that exhaustively rather than by argument.

#include "lives/LapCounter.hpp"

#include <cmath>
#include <initializer_list>
#include <cstdio>

namespace {

int failures = 0;

void ok(const char* what, bool pass) {
    if (!pass) failures++;
    std::printf("  %-58s %s\n", what, pass ? "ok" : "*** FAIL ***");
}

// every length the panel can ask for: BEATS 1..16 x RATE {1,2,4,8,12,16,24,48}
const int kRates[] = {1, 2, 4, 8, 12, 16, 24, 48};

void case1_position_continuity() {
    std::printf("1. position is 0 at adoption, for every length pair\n");
    int checked = 0;
    bool allZero = true, allSame = true;
    for (int b1 = 1; b1 <= 16 && allZero; b1++)
    for (int r1 = 0; r1 < 8; r1++)
    for (int b2 = 1; b2 <= 16; b2++)
    for (int r2 = 0; r2 < 8; r2++) {
        const int L1 = b1 * kRates[r1], L2 = b2 * kRates[r2];
        Lives::LapCounter c;
        c.pulse(L1);                       // opens a lap at L1
        for (int i = 1; i < L1; i++) c.pulse(L1);   // run to the boundary
        const bool opened = c.pulse(L2);   // the adopting pulse
        if (!opened) allSame = false;
        if (c.phase(0.0) != 0.0) allZero = false;
        if (c.active != L2) allSame = false;
        checked++;
    }
    std::printf("     %d length pairs\n", checked);
    ok("phase is exactly 0.0 on every adopting pulse", allZero);
    ok("and the new length is in force", allSame);
}

void case2_boundary_only() {
    std::printf("2. a pending length waits for the boundary\n");
    Lives::LapCounter c;
    c.pulse(96);
    bool heldMidLap = true;
    for (int i = 1; i < 96; i++) {
        if (c.pulse(48)) heldMidLap = false;      // asking for 48 all the way
        if (c.active != 96) heldMidLap = false;
    }
    ok("length unchanged for the whole lap", heldMidLap);
    const bool opened = c.pulse(48);
    ok("and adopted exactly at the boundary", opened && c.active == 48
                                              && c.pulseIdx == 0);
}

void case3_shrink_below_index() {
    std::printf("3. a shrink cannot strand the counter\n");
    Lives::LapCounter c;
    c.pulse(96);
    for (int i = 1; i <= 89; i++) c.pulse(96);
    ok("mid-lap at pulse 89", c.pulseIdx == 89 && c.active == 96);
    bool early = false;
    for (int i = 90; i < 96; i++) if (c.pulse(1)) early = true;
    ok("asking for a length below the current index wraps nothing early",
       !early);
    ok("and the boundary still arrives on time",
       c.pulse(1) && c.active == 1 && c.pulseIdx == 0);
}

void case4_ge_not_eq_self_heals() {
    std::printf("4. the wrap test self-heals\n");
    Lives::LapCounter c;
    c.pulse(96);
    for (int i = 1; i < 50; i++) c.pulse(96);
    c.active = 10;                     // white-box: active < pulseIdx
    ok("recovers on the next pulse instead of free-running",
       c.pulse(8) && c.pulseIdx == 0 && c.active == 8);
}

void case5_arm_adopts() {
    std::printf("5. RST opens a lap at the length on the panel\n");
    Lives::LapCounter c;
    c.pulse(96);
    for (int i = 1; i < 40; i++) c.pulse(96);
    c.arm();
    ok("arm() does not adopt on its own", c.active == 96);
    ok("the pulse after it does", c.pulse(24) && c.active == 24
                                  && c.phase(0.0) == 0.0);
}

void case6_first_pulse() {
    std::printf("6. the first pulse after patching\n");
    Lives::LapCounter c;
    ok("opens a lap at the requested length",
       c.pulse(48) && c.active == 48 && c.pulseIdx == 0
       && c.phase(0.0) == 0.0);
}

void case7_steady_state() {
    std::printf("7. steady state walks the lap once\n");
    Lives::LapCounter c;
    c.pulse(8);
    bool walk = (c.phase(0.0) == 0.0);
    for (int i = 1; i < 8; i++) {
        const bool opened = c.pulse(8);
        if (opened) walk = false;                       // no early boundary
        if (std::fabs(c.phase(0.0) - double(i) / 8.0) > 1e-12) walk = false;
    }
    ok("phase steps 0, 1/8 .. 7/8 with no early wrap", walk);
    ok("then returns to 0", c.pulse(8) && c.phase(0.0) == 0.0);
}

void case8_dithering_want() {
    std::printf("8. a moving request (the BEATS-CV case)\n");
    Lives::LapCounter c;
    c.pulse(96);
    int changes = 0;
    bool inRange = true, onlyAtBoundary = true;
    int last = c.active;
    for (int i = 0; i < 4000; i++) {
        const int want = 1 + (i * 7) % 16;              // thrashes every pulse
        const bool opened = c.pulse(want);
        if (c.active != last) { changes++; if (!opened) onlyAtBoundary = false; }
        last = c.active;
        const double p = c.phase(0.5);
        if (!(p >= 0.0 && p <= 0.9995)) inRange = false;
    }
    ok("length changes only on lap boundaries", onlyAtBoundary);
    ok("phase never leaves [0, 0.9995]", inRange);
    std::printf("     (%d adoptions over 4000 pulses)\n", changes);
}

void case9_invariants() {
    std::printf("9. invariants across a sweep\n");
    bool monotonic = true, bounded = true, indexInRange = true;
    for (int r = 0; r < 8; r++) {
        const int L = 4 * kRates[r];
        Lives::LapCounter c;
        c.pulse(L);
        double prev = -1.0;
        for (int i = 0; i < L; i++) {
            for (double f : {0.0, 0.25, 0.5, 0.75}) {
                const double p = c.phase(f);
                if (p < prev - 1e-12) monotonic = false;
                if (p < 0.0 || p > 0.9995) bounded = false;
                prev = p;
            }
            if (c.pulseIdx >= c.active) indexInRange = false;
            c.pulse(L);
            if (c.pulseIdx == 0) prev = -1.0;           // new lap
        }
    }
    ok("phase is non-decreasing within a lap", monotonic);
    ok("phase stays in [0, 0.9995]", bounded);
    ok("pulseIdx stays below active (no free-run, no overflow)", indexInRange);
}

}  // namespace

int main() {
    case1_position_continuity();
    case2_boundary_only();
    case3_shrink_below_index();
    case4_ge_not_eq_self_heals();
    case5_arm_adopts();
    case6_first_pulse();
    case7_steady_state();
    case8_dithering_want();
    case9_invariants();
    std::printf("\n%s\n", failures ? "FAILURES" : "all lap checks passed");
    return failures ? 1 : 0;
}
