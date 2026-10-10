# 0004 — Lap length is a knob

Accepted 2026-08-30. Supersedes one consequence of
[0003](0003-position-is-a-wire.md).

## Decision

LIVES PHASE takes a tempo clock at PPQN pulses per beat and gets its lap
length from a BEATS knob. PPQN is a menu setting defaulting to 24, the rate
a drum machine takes off a sync cable; BEATS is a snapped 1–16 parameter
that reads back in bars on hover. Setting PPQN to 1 restores the old
contract exactly, where each pulse is a whole lap.

Everything else in 0003 stands. Position is still a wire, followers are
still pure functions of its voltage, a motionless wire still makes no
sound, and the ramp still takes its slope from the previous pulse-to-pulse
interval. What changes is only where a lap's length comes from.

## What this reverses

0003 accepted, as a deliberate consequence, that "musical lap lengths are
patch math (divide the clock upstream — every serious clock ships that
knob)." That was true and it was still wrong, for a reason that only showed
up in a rack.

The clock divider is the answer to a question nobody asks in those terms. To
sync a one-bar clip to a drum machine on Impromptu Clocked, you needed one
ratio at ×24 for the drums and a second at ÷8 for PHASE — two settings, on
two knobs, derived from an indexed ratio table, and neither number appears
anywhere in the musical intent, which was "this clip is one bar long." The
divider ratio is a function of the drum sync rate and the desired lap
length, and the user has to compute it. In testing, the only way to arrive
at the pair was to already know the answer.

The deeper problem is that a clock divider is per-cable while lap length is
per-module. Two PHASEs at different lap lengths off one clock needs two
dividers; with BEATS it needs two knob positions. And when the divider
approach fails, it fails as drift and slip, at which point the patch is
debugged by suspecting the module.

## Why the knob is not a special case

A lap is a count of pulses, so an integer number of beats is the only lap
length the wire can produce. BEATS doesn't add a mode or a second timebase;
it names a multiplier that was already implicit in the divider ratio, and
puts it where the musical decision is made. Nothing is inferred and nothing
is measured, so the 0003 objection to clock following does not apply.

Taking 24 PPQN as the default is what makes it disappear: it's the rate the
drums already want, so the same cable feeds both and the ratio question
never comes up.

## Consequences

One tutorial ceased to exist. The old lesson 2 taught the divider
arithmetic, and there is no rewrite of it because the arithmetic is gone.
The tutorial series now builds one patch across three lessons.

PPQN is patch state, saved in `dataToJson`. A patch written before this
change has no `data` object at all — the module did not serialize anything —
and taking the new defaults of PPQN 24 and BEATS 4 would give a 96-pulse lap
against an old contract of one pulse per lap, so absence is read as the old
contract: PPQN 1 and BEATS 1, which is one pulse per lap exactly. New
instances still default to 24.

That restoration happens in `fromJson`, not in `dataFromJson`. Rack does not
call `dataFromJson` when the `data` property is absent — `Module.hpp`: *"Not
called if 'data' property is not present."* — so a legacy branch there can
never run, which is exactly the bug this paragraph originally described as
fixed. `fromJson` is documented as virtual for unserializing legacy data, and
it runs after params and after `dataFromJson`, so the override wins.

The version test is the key's presence rather than a stored version number,
which works because nothing was stored before the feature existed.
