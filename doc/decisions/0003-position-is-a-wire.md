# 0003 — Position is a wire

Accepted 2026-08-02.

## Decision

Two clauses below have since been replaced and are kept as written: lap
length by [0004](0004-lap-length-is-a-knob.md), and the plugin-wide bus by
[0005](0005-the-wire-resolves-by-adjacency.md). The thesis stands.

Transport in the Lives family is a 0–10 V phase ramp on a wire: 0 V the
top of the loop, 10 V the end. LIVES PHASE converts clock pulses to the
wire (one lap per pulse; slope from the previous interval; frozen when
pulses stop; RST snaps to the top; DIRECTION, START and LEN shape the
lap). Followers — LIVES TAPES, LIVES CLIPS — are pure functions of the
wire's voltage, with no clock inference of any kind. A follower whose
PHASE input is unpatched reads the wire from a plugin-wide bus published
by LIVES PHASE; a cable always overrides. Family invariant: a motionless
wire makes no signal.

The prior design gave every module its own pulse-clock follower: a beat
counter anchored by RST, a median period estimator, a silence timeout,
and a RUN input whose pulse-versus-gate dialect was classified per cable
session, with an authority hierarchy arbitrating among them
(src/lives/Transport.hpp, deleted; the design docs of that era are
doc/dev/transport-*.md).

## Reasoning

A pulse clock transmits events; position must be reconstructed by
counting, and every reconstructor can disagree. The pulse-era machinery
grew defenses against its own failure modes — the run-dialect classifier
manufactured an anti-phase trap with Impromptu's pulse RUN, idle-high
clock lines poisoned the first period estimate, and two modules following
one clock could still drift, because each carried divergent state. A
phasor transmits position itself: followers of one wire agree by
construction, since there is nothing in them that could disagree. The
change was validated by throwaway modules before the rewrite
(src/experiments/, kept as tutorial material): two wavetable followers on
one wire cannot drift; the wrap quantizes recording with no beat
arithmetic; tempo and pitch are the wire's slope.

Consequences accepted with the decision: musical lap lengths are patch
math (divide the clock upstream — every serious clock ships that knob);
the wire needs two pulses before it moves; a windowed lap's boundary jump
is LEN tall, so followers detect wraps at 10% of the lap rather than 50.

Reference: Wakefield & Taylor, *Generating Sound & Organizing Time:
Thinking with gen~* (Cycling '74, 2022) — time as ramps, everything
downstream a function of a ramp. ZZC's module line is the precedent for
the same thesis inside Rack. The timing argument for the cable-less path
is doc/dev/phase-wire.md.
