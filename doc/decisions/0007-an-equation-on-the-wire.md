# 0007 — An equation on the wire

Accepted 2026-10-06. Extends [0003](0003-position-is-a-wire.md) and
[0005](0005-the-wire-resolves-by-adjacency.md); nothing in them is
reversed.

## Decision

LIVES EXPR is a family follower: it reads the wire the way TAPES and CLIPS
do — a cable on PHASE, then the family module touching it — and republishes
what it resolved so the chain extends through it. 0–10 V maps across a
declared domain, and the program receives the mapped `x`. It does not
reconstruct time from pulses, and it has no transport of its own:
`clock -> PHASE -> EXPR`, with loop, reverse, window and scrub all coming
from PHASE as they do for every follower.

The module extends the position-wire design in two explicit ways:

1. **A stationary wire can produce a nonzero DC voltage.** A constant
   program is a voltage; `sin(x)` with the wire frozen at a quarter lap is
   1 V, and stays 1 V. There is no motor ramp and no motion-to-silence
   gate in EXPR.
2. **A stateful program runs in sample time whether or not the wire
   moves.** `t` advances, `dt` is a sample, an envelope decays, a filter
   settles, with the wire stopped or running backwards. Reversing the
   traversal reverses `x`; it does not reverse DSP time.

Two further inputs are not on the wire. A cable on X is a direct function
argument (1 V per unit) that takes precedence over PHASE, so a program can
be driven by any CV without pretending it is a position; and with neither
patched, the X knob evaluates the program by hand. TRIG and RST are
explicit edges. A discontinuity on the wire is never promoted to a
trigger.

## Reasoning

The motionless-wire invariant (0003) is a statement about media players: a
tape with no relative motion over the head produces no flux change, a
stopped projector holds its frame and has no sound. It is the physics of
the material, which is why it was never a run flag. An equation has no
such physics. `y = 2` on a stopped wire is 2, and a module that faded it to
0 V after 200 ms would be inventing a behaviour the mathematics does not
contain. So the invariant does not transfer, and saying so explicitly is
better than letting EXPR inherit a gate that would read as a bug.

Sample time for stateful programs follows from the same honesty. The
reference program for the design is a kick drum: trigger-time resets, an
exponential sweep, an envelope in seconds. Its envelope cannot be a
function of position without changing what a kick is, and a reverse lap
should not play the decay backwards. State lives in the module and ticks
with the sample clock; position is what the wire says `x` is.

Keeping EXPR a follower rather than giving it a clock input was decided in
the design discussion and confirmed by the repository: PHASE already does
clock-to-position with PPQN, BEATS, reset, direction and the window, and a
second estimator per instance would be the pulse-era architecture 0003
removed. Loop, reverse, ping-pong and one-shot are all wire shapes, so
they are PHASE's job or patch math — a triangle wire is ping-pong, a ramp
that holds its end is one-shot.

Not promoting wraps to triggers is the event-versus-position argument from
`doc/dev/phase-wire.md`, seen from the other side. The wire is safe to
read one sample stale because it carries position, not edges. Deriving an
edge from it would reintroduce exactly the ambiguity that makes stale
edges unsound: a 0.9 → 0.1 step is a wrap on a running lap, a reset, a
START scrub, or the moment a cable was plugged in, and a module cannot
tell which. The design asked for no silent heuristic here; TRIG and RST
are the contract, and the history marks a large step as an observation.

## Concurrency

The adjacency read is the same plain-float path every follower uses, and
its soundness argument (position, one sample stale, nothing integrates it)
holds for EXPR's `x` exactly as for a tape head. The program handoff is
separate and does not rely on it: one atomic pointer carries a whole
prepared program to the audio thread, a lock-free ring carries retired ones
back, and the history ring publishes with release/acquire over relaxed
atomic fields. Settings the UI changes while the audio thread reads them
are atomics. Nothing in EXPR adds a new shared plain field.

## Consequences

- EXPR can make a signal with no transport at all (X knob, X cable, or
  TRIG alone), which no other follower can. The status strip names the
  active source so a patch that expected the wire can see it is not using
  one.
- The family's "a motionless wire makes no signal" sentence is now
  "...no signal from a media player". The user manual says which modules
  it covers.
- Patches that relied on silence when the clock stops will need to
  silence EXPR's output deliberately (a VCA on a gate), as they would any
  other function generator.
