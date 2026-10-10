# Changelog

## 2.0.0 (unreleased)

First release.

- **PHASES PHASOR**: clock-to-phase converter. One 0–10 V ramp lap every LAP
  beats of the clock (one pulse per beat by default), RATE speeding it up as far as audio rate, slope from the previous interval, frozen when the
  clock stops. RST snaps to the top; DIRECTION reverses; START (wrapping)
  and LEN window the lap. A module standing next to PHASOR reads the wire
  without a cable; a cable always overrides.
- **PHASES EXPR**: a live-compiled equation on the wire, written as Desmos
  writes it: explicit `y =`, polar `r =` and parametric `(x(t), y(t))`
  curves, implied multiplication, piecewise braces and lists. Each
  undefined letter is a knob/CV slot. Compiled off the audio thread and
  swapped in at a sample boundary with a short crossfade; an invalid edit
  changes nothing. The domain is a restriction at the end of the
  equation, `{-8 ≤ x ≤ 8}`, or 0 to 2π without one; LOOP plays a half,
  third, quarter and so on down to a sixteenth of it, POS picks which, and
  PHASE maps 0–10 V across that slice. Volts only: gain, offset, hard/soft limiter, lag or slew, a
  finite guard. The display draws the curve with the playhead on it.
