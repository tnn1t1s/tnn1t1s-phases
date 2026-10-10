# 0005 — The wire resolves by adjacency

Accepted 2026-08-30. Replaces the plugin-wide bus in
[0003](0003-position-is-a-wire.md); the rest of that record stands.

## Decision

A follower resolves its transport two ways, first match wins:

1. a cable on the PHASE input
2. the family module touching it, left or right

If neither answers, the wire is dead, which every follower already renders as
silence. Each follower republishes what it resolved, so the chain extends
through it: PHASE, CLIPS and TAPES standing in a row share one transport with
no cables. The implementation is `Lives::resolveWire` in
`src/lives/PhaseWire.hpp`; the timing argument is `doc/dev/phase-wire.md`.

There is no rack-wide fallback. Two LIVES PHASE modules are two transports,
and which one a follower belongs to is answered by where it sits.

## What this replaces

0003 gave followers a plugin-wide bus: one static float, written by every
PHASE, read by any follower with an unpatched input. The premise was that
followers should be able to sit anywhere in the rack, and the bus delivered
that.

It also meant a single global slot had to represent an unbounded number of
transports. With two PHASE modules the last writer in a sample won, across
engine threads, unstable frame to frame — recorded at the time as an accepted
constraint ("multi-clock patches use cables"), which in practice presents as a
follower that sounds broken with nothing on screen to explain it.

An intermediate version kept the bus but made it refuse to answer when more
than one PHASE was publishing. That removed the nondeterminism and left a
worse problem: whether module position mattered depended on how many PHASE
modules the rack contained, a global fact invisible at the point of use. A
user with one PHASE would learn that position is irrelevant, and be wrong the
moment they added a second.

Adjacency answers the same question the same way every time, and it is the
answer the Ghost drums already give — `Lives::resolveBus` resolves the drum
controller by `leftExpander` / `rightExpander` and has since it shipped. One
rule across both plugins rather than two.

## Cost

Rack layout is load-bearing. A gap, or any non-family module between PHASE and
its follower, breaks the chain, so a patch can be changed by dragging a module
without touching a cable. This is how hardware expanders behave and the escape
hatch is a cable, but it is a real change: position was previously decorative.

Of the 38 patches in `patches/`, 37 need no change — every uncabled follower
is already adjacent. The exception is `demos/clips-tapes-filters-mix.vcv`,
where a 3 HP Fundamental Push sits between CLIPS and TAPES and TAPES was
crossing that gap on the bus.

The failure is legible, which is the argument for accepting the cost: a
follower with no transport is silent, the gap that caused it is visible on the
panel, and CLIPS' status strip reads `no wire`. The bus failed invisibly and
this fails in a way you can point at.

## Consequences

The tier that answered is recorded in `AgentModule::wireSource` for display
only. Nothing downstream branches on it, and nothing should — followers are
pure functions of the voltage regardless of where it came from, which is the
0003 invariant and the reason this change is confined to resolution.
