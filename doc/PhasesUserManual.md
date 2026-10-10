# Phases user manual

## Getting started

1. Add a clock (VCV's Clock, or Impromptu's Clocked) and run it.
2. Add PHASES PHASOR and patch the clock's beat output into CLK. The CLK
   light glows while pulses arrive.
3. Add PHASES EXPR directly to the right of PHASOR, touching it. It picks
   up PHASOR's ramp with no cable, and its display draws `sin(x)` with a
   playhead moving across it, one cycle every 4 beats.
4. Patch Y to something you can hear move: a filter cutoff, or a VCO's
   V/OCT through a quantizer.
5. Right-click EXPR → *Examples* and pick one of the Patterns. Turn GAIN down
   first if Y feeds an audio path, because most patterns swing several
   volts.

If the playhead creeps, your clock sends more than one pulse per beat:
turn RATE on PHASOR counter-clockwise until one lap takes LAP beats
(24 PPQN for a drum machine's sync cable or the Ghost drums). The demo
patch `patches/demos/phases-ghost-groove.vcv` is a complete groove built
this way.

Click EXPR's display to type your own equation. Cmd+Enter (Ctrl+Enter off
the Mac) applies it. The notation, the function list and the error
messages are in [The EXPR language](dev/expr-language.md).

A few words used below: the *wire* is PHASOR's 0–10 V ramp; one *lap* is
one rise from 0 V to 10 V; a *family module* is PHASOR or EXPR, which find
each other's wire when they stand side by side.

## The wire

Position travels between Phases modules as a 0–10 V ramp: 0 V is the top
of the lap, 10 V the end. PHASES PHASOR makes the wire from clock pulses;
PHASES EXPR follows it. An EXPR whose PHASE input is unpatched finds its
wire on its own; a cable always overrides. Any 0–10 V ramp is a legal
wire, so PHASOR is the family's source, not a requirement.

With nothing patched, a follower takes the wire from the family module
touching it, on either side. Standing EXPR next to PHASOR is enough, and
the chain carries on through: PHASOR and two EXPRs in a row share one
transport with no cables at all.

Two PHASES PHASOR modules are simply two transports: each row of modules
follows the PHASOR it is standing next to, and there is nothing to
configure. Slide EXPR away from its neighbours and it loses the wire; its
status line reads `no wire` and the equation holds at the start of its
slice. Put it back, or run a cable, which always wins and reaches anywhere.

Stopping the clock freezes the wire, and EXPR holds the voltage the curve
had there. START and LEN are applied after the transport, so modulating
either one moves the wire with the clock stopped. That is scrubbing by
hand on a stopped machine, and it is the intended behaviour.

## PHASES PHASOR

A lap spans LAP beats of a tempo clock. The previous pulse-to-pulse
interval sets the slope, so the ramp needs two pulses before it moves.

| Control / jack | Function |
|---|---|
| CLK (in) | A tempo clock; RATE says how many of its pulses make a beat. The jack's light glows while pulses arrive and flashes once per lap, so a dark light means no clock rather than one of the other reasons for silence. |
| RST (in) | Opens a lap on the next pulse, anchoring lap boundaries to the transport rather than to whenever pulses began. The interrupted span is not measured as tempo. Patch it from your clock's RESET; without it the lap still runs in time, but where its boundary sits against the bar is arbitrary. |
| LAP | Lap length in beats. Reads back in bars on hover: 4 is *1 bar*, 16 is *4 bars*. |
| RATE | How many clock pulses make a beat: 48, 24, 16, 12, 8, 4, 2 or 1, from fully counter-clockwise to fully clockwise. The default is 1, fully clockwise, which is what most Rack clocks send. A drum machine's sync cable and the Ghost drums run at 24 PPQN, one step clockwise from the bottom. Hover reads the pulses per beat and the speed against a 24 PPQN clock. On a fast clock, counting fewer pulses per beat pushes the lap to audio rate and EXPR's curve becomes a waveform. At RATE 1 with LAP 1, each pulse is a whole lap, so any trigger source drives the loop. |
| DIR (+CV) | Forward or reverse ramp. The gate inverts the switch, so a momentary gate is a reverse stab; the flip lands immediately, mid-lap. |
| START, LEN | The window: which part of the lap the wire plays. START offsets and wraps around the lap, LEN scales (5-100 %); a window can cross the end of the lap. Every follower windows together. Both have CV inputs (additive, 10 V = full range). |
| PHASE (out) | The wire. Also read directly by a family module standing next to PHASOR, with no cable. |

## PHASES EXPR

An equation played by the wire, written as you would type it into
Desmos. The display is the equation's curve, knobs 1 and 2 are its
undefined letters in order of appearance, and Y is the result in volts.
An equation with a third letter does not compile. Click the display to
open the editor; Enter makes a new line, Cmd+Enter (Ctrl+Enter off the Mac)
applies. The box holds five lines, to keep equations short. A program that does
not compile changes nothing — the previous one keeps playing and the
editor says the line, column and what is wrong. Right-click → *Examples*
loads a working program to edit from. Patterns swing several volts, so
turn GAIN down before one reaches an audio path.

```text
y = a sin(bx) / cosh(x/4)
```

`a` and `b` are knobs 1 and 2, each a whole number from 0 to 10 starting
at 1, so on the default 0 to 2π domain the curve always meets itself
at the end of the lap. *Knob steps* in the menu makes a knob fine for an
amplitude or an offset. The menu's Basics are written for 0 to 2π; its
Patterns bring their own domains ([EXPR examples](examples.md)).
`r = cos(4θ)` is a polar curve and `(cos(3t), sin(2t))` a parametric
one: Y carries the curve's y coordinate and X its x, so the two jacks
into a scope in X-Y mode draw the curve. On a `y =` equation X is the
swept x itself, in the domain's own units.

**The domain.** The range one lap sweeps is part of the equation, written
the way Desmos restricts a curve: `y = a sin(bx) / cosh(x/4) {-10 ≤ x ≤ 10}`
traverses the whole curve above in one lap. Without a restriction the
domain is 0 to 2π, so `sin(x)` is one cycle per lap. The ends are numbers
(`-8`, `π/2`, `4π`); polar equations restrict `θ` and parametric ones `t`.
The display writes the two ends in its top corners.

**Loops.** LOOP cuts the domain into equal slices (1/16, 1/12, 1/8, 1/6,
1/4, 1/3, 1/2, or the whole domain) and POS picks the one each lap plays,
so PHASE maps 0–10 V across that slice alone. Both are knobs with CV
jacks, beside knobs 1 and 2. The rest of the curve sits under a shade,
and the display reads the slice above each knob: `1/4` over LOOP, `2/4`
over POS. A slice is an exact fraction of the domain, so the loop
lands back on its own start at every wrap: on 0 to 2π, LOOP 1/4 is a
quarter cycle of `sin(x)`, and POS walks it through the four quarters. The slice keeps the whole
domain's tempo and plays several times a lap: LOOP 1/4 plays its quarter
four times. Uncheck *LOOP repeats the slice at full speed* (menu) and the
slice plays once a lap instead, slower, since one lap spans it.

**The argument.** x comes from the wire alone. The status line names
where the wire comes from: `phase` for a cable, `adj` for a neighbour,
`no wire` for neither. Reverse the lap and the curve plays backwards;
window it and the curve is windowed.

**No state.** An equation has no memory: the same x and knobs always
give the same Y, so a reversed lap plays the curve backwards and a still
wire holds a DC value. A clock reaches EXPR through PHASOR. A wrap of the
wire is never a trigger.

| Control / jack | Function |
|---|---|
| 1, 2 (+CV) | The equation's letters, in order of first appearance. Hover for the letter, range, CV scale and live value. The bottom of the display shows each letter and its live value straight above its knob. An unused slot does nothing; a slot the equation uses has an orange ring around its knob. |
| LOOP (+CV) | The share of the domain one lap plays: 1/16 to the whole. The CV adds to the knob, 10 V sweeping all eight steps. |
| POS (+CV) | Which slice plays. The CV adds to the knob, 10 V sweeping every slice, so a stepped CV sequences the slices. |
| GAIN (+CV) | Scales the result, 0 to 2, unity at noon. On the bottom row with LIMIT, next to Y. |
| LIMIT (+CV) | The limiter ceiling, 0.1 to 10 V, 5 V by default; the display's vertical scale follows it. |
| Display | Click to edit the equation. Right-click → *Show equation* opens it typeset in a window over the rack: drag to move, drag the corner to resize, × to close. The window is saved with the patch. Right-click → *Layout* → *Wide display* makes the module 76 HP: the controls in a column on the left and the display across the rest, pushing the modules to its right along. |
| PHASE (in) | The wire, mapped across the looped slice. Unpatched, follows the family module touching it. When the wire comes from a PHASOR, two dark grey bars on the display mark its START and START + LEN. |
| RST (in) | Reset output smoothing. Does not move the wire. |
| GAIN CV (in) | Added to the GAIN knob at 0.2× per volt, so 0 to 10 V sweeps it from 0 to 2 with the knob at zero. |
| LIMIT CV (in) | Added to the LIMIT knob, 1 V per volt, held within 0.1 to 10 V. The display's scale follows the modulated ceiling. |
| Y, X (out) | The curve's two coordinates as conditioned voltages: Y is the result, X is r cos θ for a polar equation, x(t) for a parametric one, and the swept x for a `y =` equation. Both go through the same gain, offset, limiter and smoothing. The light beside Y flashes on a fault (division by zero, `ln` of a nonpositive, overflow, or a piecewise with no branch for this x), so it also marks the gaps between pieces. |

**Volts.** A result of 1 is 1 V. Then GAIN, offset (menu), a limiter
at ±LIMIT (hard, or a soft knee from the menu), optional lag or slew, and a
final guard at ±10 V: nothing nonfinite ever reaches Y. On a fault the
output is 0 V, or holds the last valid value (menu). No pitch or scale
interpretation is built in; patch a quantizer after it if you want notes.

**Editing live.** Apply swaps the new program in at a sample boundary
with a 10 ms crossfade (menu: immediate, or 50 ms); the wire stays where
it is. Edits are in Rack's undo
history. The saved patch carries the program text and every setting as
plain JSON, so a program can be written into a patch or preset without the
editor. The language is documented in
[The EXPR language](dev/expr-language.md).

## Recipes

**One cycle per bar.** Clock → PHASOR, LAP at 4, RATE at the clock's
pulses per beat, EXPR beside it. The
default `sin(x)` on 0 to 2π is one sine per bar, ±1 V, locked to the
transport: patch Y to a filter cutoff. Write `y = floor(x) / 4 {0 ≤ x ≤ 16}`
for a 16-step staircase per bar; set LOOP to 1/4 and turn POS to play one
beat's four steps on repeat.

**A melody per lap.** With the same patch,
`y = [0,2,4,7,9][floor(mod(ax/(2π), 5)) + 1] / 12` walks C minor
pentatonic across the lap; patch Y to a VCO's V/OCT. Knob `a` sets how
many notes one lap covers.

**Moving the playhead.** A window is START and LEN on PHASOR, or an attenuverter
and offset on the wire; reverse is DIR, or 10 V minus the wire; stutter is
a sample-and-hold on it; scrub is any CV source into EXPR's PHASE input.
