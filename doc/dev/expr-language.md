# The EXPR language

`src/expr/`. What PHASES EXPR compiles, how it runs, and what it promises.
The language is one equation as Desmos writes it (DR-0008); the module's
place in the family, with its two stated exceptions, is DR-0007.

## An equation

```text
y = a sin(bx)                        a curve in x; a and b are knobs
r = 1 + cos(5θ/3)                    a polar curve in θ
(cos(3t), sin(2t))                   a parametric curve in t
y = [0,2,4,7,9][floor(mod(x,5))+1]   a list, indexed from 1
y = {x < π: sin(x), -1}              piecewise
```

The left side picks the mode. `y =`, `f(x) =` or no left side at all is a
curve in x; `r =` or `f(θ) =` is polar; a parenthesised pair is
parametric. Any single letter may name the function. There is one
equation and nothing else: no statements, no comments, no state. The
editor names the program by its text.

| Mode | Swept variable | X | Y |
|---|---|---|---|
| curve | `x` | x | f(x) |
| polar | `θ` | r cos θ | r sin θ |
| parametric | `t` | x(t) | y(t) |

The swept variable is whatever drives the module: the PHASE wire mapped
across the domain (or the slice of it LOOP and POS select). With no wire
it holds at the start of the slice. Using another mode's variable is an
error that names the right form, so `y = sin(θ)` asks for `r =`. The
compiled program returns both X and Y in every mode, and the module puts
each on its own jack, conditioned the same way.

## Notation

What Desmos accepts when typed:

- Multiplication may be implied: `2x`, `2.4sin(x)`, `2(x + 1)`,
  `(x + 1)(x - 1)`, `πx`, `x sin x`. A run of letters splits the way
  Desmos splits it, longest function or constant name first and single
  letters otherwise, so `ab` is a·b and `sinx` is sin x.
- A function written without parentheses takes the product that follows
  it, up to an operator or another function: `sin 2x + 1` is
  `sin(2x) + 1`.
- `|...|` is the absolute value; `2|x| - |x - 1|` and `|(|x| - 1)|` nest
  as written.
- `π`, `θ` and `τ` may be typed as symbols or as `pi`, `theta` and `tau`;
  `e` is Euler's number. `·`, `×`, `−`, `≤` and `≥` are read as `*`, `*`,
  `-`, `<=` and `>=`.
- `^` is real power, right-associative, tighter than unary minus: `-2^2`
  is -4, `2^-x` is allowed. Numbers have no exponent notation, so `2e3`
  is 2·e·3.
- Piecewise: `{c1: a, c2: b, else}`. Conditions use `< <= > >= =` and
  chain (`0 <= x < 1`). Only the chosen branch runs, so a pole or a
  square root of a negative in another branch cannot fault the result. A
  condition without a value is 1 where it holds, and with no else the
  value outside every condition is undefined. Conditions combine with
  `and` (`∧`) and `or` (`∨`), `and` binding tighter, and can be bracketed:
  `{(-2 <= x < 0) or (14 <= x < 16): 1, 0}`. Desmos itself has no `or`;
  it is accepted because equations written for video use it.
- Lists: `[a, b, c][i]` is element i counting from 1. An index that is
  not a whole number from 1 to the length is undefined. A list must be
  indexed; one equation draws one curve. Only the selected element runs.
- Angles are radians.
- Functions, with Desmos's names: `sin cos tan sec csc cot arcsin arccos
  arctan sinh cosh tanh abs sign sgn floor ceil round mod exp ln log sqrt
  min max`. `log` is base 10, `ln` is natural, and `log2`, `log_2` or
  `log_{2}` is base 2; `log_10` is plain `log`, and any other base is an
  error that suggests `ln(x)/ln(b)`. `arctan(y, x)` is the
  two-argument form. `min` and `max` take any number of arguments.
  `mod(a, m)` is `a - m*floor(a/m)`: in `[0, m)`, or `(m, 0]` for a
  negative `m`, as in Desmos. Infix `a mod m` means
  the same, binding like `*`, and `⌊a⌋` and `⌈a⌉` are `floor(a)` and
  `ceil(a)`.

Not accepted yet, each with an error at its position: LaTeX copied out of
Desmos (`\sin\left(x\right)`), `sin^2(x)`, factorial
`!`, and `%`.

## The domain

A restriction at the very end of the equation, with numbers for ends, is
the domain: the range of the swept variable across one lap.

```text
y = floor(x) / 4 {0 ≤ x ≤ 16}
r = cos(3θ) {0 ≤ θ ≤ π}
(cos t, sin(2t)) {0 ≤ t ≤ 4π}
```

`≤` and `<` are the same here, `≥` both ways works with the ends swapped,
and the ends may be arithmetic on numbers and `π` (`-π/2`, `3π/2`). A knob
in an end is an error, as are equal ends and a variable from another mode.
Braces anywhere else, or braces after `=` (`y = {0 ≤ x ≤ 1}`), are a
piecewise as before. Without a restriction the domain is 0..2π.

The module divides the domain into LOOP's equal slices and maps the wire
across the one POS selects: `x = lo + (hi − lo)(k + u)/n` for slice k of
n and wire position u in 0..1. Patches saved with the former DOMAIN
setting have it appended to their source as a restriction when they load.

## Letters are knobs

A letter that is not `x`, `y`, `r`, `θ`, `t` or `e` is a knob, as an
undefined letter is a slider in Desmos. Knobs take slots 1 and 2 in order
of first appearance; a third letter is an error that names it. `a_1` and
`a_2` are two knobs. `y` and `r` appear only on the left.

Each knob runs from 0 to 10 in whole numbers and starts at 1. On the
default 0..2π domain a whole-number frequency always closes the curve at
the end of the lap, so `sin(bx)`, `cos(kθ)` and `(cos(at), sin(bt))`
never jump at the wrap. The context menu's *Knob steps* sets a knob to
fine steps for an amplitude or offset, where whole numbers are too
coarse. The module maps the knob's normalized position into the range,
adds CV at one unit per volt, rounds unless the knob is fine, and clamps:
`p = clamp(round(knob * 10 + volts), 0, 10)`. CV is read every sample. On
an edit, a slot that keeps its letter keeps its knob position and its
step setting.

## Faults

Divide by zero, `ln` or `log` of a nonpositive, `sqrt` of a negative, an
invalid real power, a zero modulus, `arcsin`/`arccos` outside
[-1, 1], an undefined piecewise or list value, and any nonfinite result
(overflow) are faults; underflow to zero is not. In polar mode a fault in
r faults the point. `tan`
near a pole is large but finite and continues to the limiter. A fault
aborts the sample with the operation's source line and column; the module
lights the light beside the Y jack and
outputs 0 V or holds the last valid target (menu), through the limiter and
smoothing as usual. Nothing nonfinite reaches a Rack output.

## Output conditioning

```text
result -> gain -> + offset -> fault policy -> limiter -> lag | slew | none -> guard -> Y
```

A result of 1 is 1 V at unity. The limiter is memoryless: hard
`clamp(v, -L, L)`, or a soft knee of full width W in volts (unity slope
below `L - W/2`, slope falling smoothly to zero at `L + W/2`, never above
L; `W = 0` is the hard form). Lag is a one-pole with tau in seconds; slew
is a per-sample movement bound in V/s with independent rise and fall.
Smoothing persists across transport wraps and clears on RESET. The guard
clamps to ±10 V and replaces anything nonfinite with 0 V, which also
covers a limit reduced while smoothing still holds an older, higher value.

Defaults: gain 1, offset 0 V, limit 5 V hard, smoothing off, fault → 0 V.

## Events

RESET (jack, menu, or the editor's button) clears output smoothing. It
does not touch the wire; reset PHASOR for that. Per sample: accept a prepared program, read inputs, RESET, run,
condition, record history.

A wrap of the wire is never a trigger: a position wire alone cannot tell a
wrap from a reset, a scrub, or a source switch. History flags a jump of
more than a tenth of a lap as `phase jump`, which is an observation, not
an event.

## Limits

16 KiB of source, 64 nesting levels, 1024 instructions, 256 registers,
two knobs. These are admission limits the compiler enforces, not a CPU
guarantee. Measured on an Apple M3 (clang -O3, one core, 48 kHz), `sin(x)`
costs 12 ns a sample and a TikTok-length one-liner with `max`, two `mod`s
and three `abs` costs 106 ns, 0.5% of the core.

## Live editing

Apply compiles on the UI thread and publishes a whole prepared program,
bytecode and its instance together, through one atomic pointer. The audio thread
takes it at a sample boundary and, by default, crossfades 10 ms (menu:
immediate, or 50 ms; a second edit mid-fade does not stack). The old
program is handed back for the UI thread to free. An equation that fails to
compile changes nothing: the previous one keeps playing and the editor
shows the line, column and message. Latest edit wins. A new program
does not reset the wire.

## Saved form

`dataToJson` writes `version`, `source` (the last committed valid text),
`draft` (uncommitted editor text, only when it differs), `offset`, `knee`, `smooth` (0 none, 1
lag, 2 slew), `tau`, `rise`, `fall`, `fault` (0 zero, 1 hold), `seed`,
`crossfadeMs`, `window`, `showTarget`, `fine` (per-slot knob steps) and
`loop` (2: the domain is in the source and LOOP counts up to the whole domain). Rack's own metadata holds the
knobs, LOOP and POS among them. An agent writing this object into a patch or preset uses the same
validator and compiler as the editor; a saved source that no longer
compiles is kept as the draft and the default program plays.

## Not in the language

No state, counters, or memory of earlier samples: a clock reaches EXPR as
a PHASE wire. No lists without an index, no list arithmetic, no sums,
products or user-defined helper functions; whether any of these belong
is for the example corpus to decide (DR-0008). No polyphony: channel 1
in, one channel out. No native JIT: the bytecode walk is the backend, and
its cost is per instruction per sample.
