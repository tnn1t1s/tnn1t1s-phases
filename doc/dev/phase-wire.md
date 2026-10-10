# The phase wire: how a follower finds its transport

`src/lives/PhaseWire.hpp`. A follower resolves its transport two ways, in
order: a cable on its PHASE input, then the family module touching it. If
neither answers, the wire is dead and the follower is silent. This document
records why the adjacency read is sound, because the timing argument is not
obvious and the technique is easy to copy into a case where it is wrong.

## Implementation

`AgentModule` carries the resolved wire:

    struct AgentModule : rack::Module {
        float wireVolts;
        bool  wireLive;
        WireSource wireSource;   // display only
    };

- LIVES PHASE writes `wireVolts` / `wireLive` at the end of `process()`.
- A follower calls `Lives::resolveWire`, which checks its cable, then
  `leftExpander.module`, then `rightExpander.module`, `dynamic_cast` to
  `AgentModule` and takes the first with `wireLive`.
- The follower republishes what it resolved, so the chain extends through
  it. PHASE and two EXPRs standing in a row share one transport with no
  cables: the second EXPR reads the first, which reads PHASE.
- A follower that resolves nothing sets `wireLive = false`, which breaks
  the chain for everything past it. That is correct — the chain carries a
  live wire, not a promise of one.

## Timing: the neighbour read is exactly as fast as a cable

Rack runs every module's `process()` once per sample, in an order the API
does not specify, possibly across several engine threads. Two cases:

- The follower processes after its neighbour in the same step: it reads
  this sample's value. Latency 0.
- The follower processes before it, or concurrently on another thread: it
  reads last sample's value. Latency 1 sample (~21 us at 48 kHz).

A physical cable in Rack has the same two cases — port voltages are plain
writes read by whoever processes next — so adjacency adds nothing over
copper. The bound holds at any engine thread count, and it is the same
bound Rack's own expander message API gives, without the double buffer.

## Why staleness is harmless here, and when it would not be

This is the part that generalizes. The wire carries an **absolute
position** (a phase ramp). A one-sample-stale read is a constant
sub-sample position offset: on a 1.74 s bar lap, 0.001 % of the loop,
identical every lap. It cannot accumulate, because nothing downstream
integrates it — followers are pure functions of the wire.

The same channel carrying **events** (clock pulses, triggers, gates) would
be unsound: a stale read across an edge means a missed or doubled pulse,
and anything counting edges then drifts by a whole step. The technique is
safe because of the signal design, not despite it. If you copy it, carry
state, never edges.

A chain compounds the offset by one sample per hop, which is why the
argument is about position and not about latency budgets: three modules
deep is three samples, still constant, still not accumulating within a lap.

## Alternatives considered

- **A plugin-wide bus** (a static float any follower reads when its input
  is unpatched): shipped, then removed. It let followers sit anywhere,
  which sounded like a feature and was the reason it was built. But two
  LIVES PHASE modules are two transports, and a single global slot cannot
  say which one a follower belongs to — the last writer in a sample won,
  across engine threads, unstable frame to frame. Making the bus refuse to
  answer when contested fixed the nondeterminism and left a worse problem:
  whether position mattered depended on how many PHASE modules existed,
  which is invisible at the point of use. Adjacency answers the question
  the same way every time. See DR-0005.
- **Rack expander messages** (`Module::leftExpander.producerMessage`):
  the sanctioned channel, double-buffered. Unnecessary here — the buffer
  exists to make a struct write atomic across the frame boundary, and a
  single float that tolerates a one-sample stale read needs neither.
- **Cables**: still the primary interface. Anything adjacency does, a
  cable does explicitly, and a cable always wins.

## Constraints

- Adjacency is strict. A gap, or any non-family module between PHASE and
  its follower, breaks the chain. This makes rack layout load-bearing:
  a patch can be changed by dragging a module without touching a cable.
  The escape hatch is a cable, and the failure is legible — the gap is
  visible on the panel and EXPR's status strip reads `manual` instead of `adj`.
- Plugin-scope only: the `dynamic_cast` is to this plugin's own base
  class, so foreign modules can neither feed the chain nor disturb it.
- Resolution does not appear in patch files. A patch that relies on
  adjacency relies on module positions, which patch files do store.
- A stopped clock does not guarantee a still wire. PHASE applies START
  and LEN after the transport freeze, so the output is
  `START + phase * LEN` and a moving START moves the wire with no pulses
  arriving. Followers move with it, which is deliberate: the wire carries
  position, not the clock. Modulating START with the transport stopped is
  the family's scrub gesture.

## Verification

Null test: the same equation in two EXPR instances, one fed by cable, one by
adjacency, cable to the left channel and adjacency to the right. A timing
error large enough to matter is audible as interchannel width; sample-level
agreement collapses to dead-center mono. Invert one side and sum for the
numeric version: residual silence is the proof.

## Reference

The phase-as-position architecture the wire carries follows Graham
Wakefield and Gregory Taylor, *Generating Sound & Organizing Time:
Thinking with gen~* (Cycling '74, 2022) — time as ramps, followers as
functions of a ramp. Written for gen~ in Max; maps to Rack directly.

The adjacency mechanism is the one the Ghost drums use for their
controller (`Lives::resolveBus`, `tnn1t1s-ghost/src/GhostBus.hpp`), so the
two plugins answer "what am I connected to" the same way.
