# 0008 — Desmos is the language

Proposed 2026-10-07. Replaces the EXPR language of
[doc/dev/expr-language.md](../dev/expr-language.md) and revises
[0007](0007-an-equation-on-the-wire.md) where noted below.

## Decision

An EXPR program is an equation you could type into Desmos. That is the whole
scope of the language: if Desmos cannot express it as one equation, EXPR does
not do it. There is no `state`, no `let`, no `param`, no counter, and no
operator with memory. The user never manages state.

The current language (version 1) is removed, not kept beside the new one. No
patch using it has shipped, so a clean break costs nothing today.

## What a program can be

The variable the user writes picks the mode, so there is no mode menu.

| Program | Swept variable | X out | Y out |
|---|---|---|---|
| `y = f(x)` or `f(x) = …` | `x` | x | f(x) |
| `r = f(θ)` | `θ` | r cos θ | r sin θ |
| `(x(t), y(t))` | `t` | x(t) | y(t) |

The module gains a second output. X and Y are always the point being drawn,
so the two outputs into a scope in XY mode show the Desmos graph itself. A
pitch patch uses Y and leaves X unpatched. Polar is a special case of the
parametric form and costs nothing extra once both outputs exist.

## Notation

The parser accepts what people paste from Desmos: `y =`, implied
multiplication (`2.4sin(x)`, `2(x + 1)`, `πx`), absolute-value bars `|…|`,
`π` and `θ` as characters and as `pi` and `theta`, piecewise braces
`{x < 0: a, b}`, and lists, which Desmos indexes from 1 (`[0,3,5,7,10][2]` is
3). Angles are radians; Desmos's degree mode is not supported. Function names
are Desmos's, including `mod(a, b)`.

Which further Desmos features belong in scope (sums, products, user-defined
helper functions, list comprehensions) is a research question that the
example corpus answers. Each example either compiles or names the feature it
needs.

## Letters are knobs

An undefined letter is a Desmos slider, and in EXPR it is a knob slot. The
slots are assigned in order of first appearance, up to six; a seventh is a
compile error naming the letter. `y = a sin(bx)` gets knobs `a` and `b` with
nothing declared. The reserved letters `x`, `y`, `r`, `θ`, `t` and `e` never
become knobs.

The knob range is a default, owned by the module. Knobs run from 0 to 10
in whole numbers, starting at 1; a knob can be set to fine steps from the
context menu, stored per slot in the patch. Whole numbers keep a curve
closed on the 0..2π lap whatever the knob says, so `sin(bx)` never jumps
at the wrap; a negative frequency only mirrors the curve, so the range
starts at 0. This replaces Desmos's −10 to 10 slider default, which on a
knob read as confusing (2026-10-07), and is chosen to be tried by ear.
The examples are all written for the default domain and never change
it.

## Where the swept variable comes from

There are three sources, in this order:

1. A cable on PHASE: the 0–10 V lap maps across the domain, as in 0007.
2. A cable on V/OCT: EXPR is an oscillator. It sweeps the domain once per
   cycle at `f = 261.63 Hz · 2^V`, the same reference as the Fundamental VCO,
   so 0 V is middle C. SYNC restarts the cycle.
3. Neither: the family module touching EXPR, then the X knob by hand.

A cable on PHASE takes precedence over V/OCT, keeping the family rule that
a cable comes first. This order is a default, not an invariant; patching
both is unusual enough that no evidence yet says which should win.

PHASE is the prerequisite for clock time. A clock patched straight into EXPR
does nothing, because nothing in a stateless program can count pulses: the
patch is `clock → PHASE → EXPR`. EXPR does not build a ramp from a clock.
The oscillator ramp under V/OCT is module state the user never sees, and it
does not count events, so it doesn't break the no-state rule for the program.

## Ranges

Each mode has a range for its swept variable, stored in the patch:

| Mode | Default range | Reason |
|---|---|---|
| `x` | 0 to 2π | One period of `sin(x)` per lap |
| `θ` | 0 to 2π | One turn; a rose with a fractional frequency needs more, `cos(5θ/3)` closes at 6π |
| `t` | 0 to 2π | Matches the other two so switching modes changes nothing else |

Desmos sets these per curve as well, so the setting maps directly onto the
source material.

## What changes from 0007

0007's first extension stands: a stationary wire can produce a DC voltage.

Its second extension, that a stateful program runs in sample time, is
withdrawn along with state. The TRIG input and the `t` (seconds), `dt`,
`sr` and `trig` names go with it. RST becomes SYNC, which matters only to
the oscillator, and `t` now means the parametric variable, as in Desmos.

The X input stays as a direct argument (1 V per unit) under the PHASE
cable, for driving an equation from any CV.

Output conditioning (gain, offset, limiter, smoothing, the finite guard) is
unchanged. It is a module setting, not something a program writes, and it
applies to both outputs.

## Unknowns

- Audio-rate cost, observed 2026-10-07 on an Apple M3 (clang -O3, one
  core, 48 kHz, ten seconds of samples per case): `sin(x)` costs 12 ns a
  sample, `2.4*sin(1.32*abs(2*mod(x/16, 1) - 1))` 41 ns, and a
  TikTok-length one-liner with `max`, two `mod`s and three `abs` 106 ns,
  0.5% of the core. The interpreter is not what limits the oscillator.
  Rack's own per-module overhead and the Windows and Linux builds are not
  measured.
- The panel needs V/OCT, SYNC and a second output, and loses TRIG. The jack
  names (an X input beside an X output) and whether 22 HP still holds are
  settled in the panelkit spec, not here.
- How a bipolar program maps to pitch (units of volts or semitones) waits
  for the corpus. Today it is gain: 1/12 turns semitones into V/oct.
