# LIVES C++ style & best practices

The rubric for reviewing `src/` (the C++ that ships in the plugin). Tooling
(clang-format / clang-tidy / cppcheck) enforces the mechanical rules; this doc is
for the judgment rules a linter can't check. Review validates code against this.

> Accepted 2026-06-02. Three choices locked: `kCamelCase` constants, explicit
> `onReset()` per voice, denormal floor on every decaying state.

## 1. Philosophy

- **Real-time safety is non-negotiable.** `process()` runs on the audio thread;
  a single allocation or lock there is a bug, not a style nit.
- **Clarity over cleverness.** This is DSP people will read to learn from.
- **No magic numbers.** Named constants with units; derive from code/registry
  where possible (per repo CLAUDE.md).
- **Two is one.** A value that matters (a calibration, a dB level) gets a name
  and a comment, not a bare literal.

## 2. Real-time safety — the `process()` contract

Inside `process()` (and anything it calls) **never**:
- allocate or free heap (`new`/`delete`/`malloc`/`std::vector` growth/`std::string`)
- lock a mutex or do any blocking call
- do I/O (`printf`, `std::cout`, file/network)
- throw, or call anything that can throw
- run unbounded or data-dependent-length loops

Everything `process()` needs is a pre-sized member, set in the constructor or
`onReset()`. UI-thread code (`appendContextMenu`, widget ctors) may allocate.

## 3. Numerical safety

- Every audio output is **clamped** before `setVoltage` (no runaway/NaN to the DAC).
- Guard divides, `log`, `sqrt(negative)`, `pow` of bad inputs; no `÷0`.
- Prefer `rack::math::clamp` / `std::clamp`.
- **Denormals:** every decaying state (envelopes, filter/feedback tails) must be
  **floored to zero** once it drops below an inaudible threshold —
  `if (x < kDenormalFloor) x = 0.f;` (e.g. `kDenormalFloor = 1e-20f`). Do NOT
  rely on the CPU's flush-to-zero mode alone; Rack does not guarantee it for
  plugins. The review checks every decay path is protected.
- No `Date`/random in DSP (determinism; matches repo rule).

## 4. State & initialization

- **Every member is initialized** (default member initializers preferred) so the
  first sample is deterministic — no garbage trigger/env state.
- **Each voice implements `onReset()`** that returns it to a clean state — zero
  the envelopes, sample positions, latched accent/choke flags, triggers — so
  Rack's "Initialize" and first-load behavior are deterministic and silent.

## 5. Module structure (the canonical pattern)

Each voice follows the same shape — keep it:
- `enum ParamId / InputId / OutputId` (order is the saved-patch contract; never
  reorder, only append before `NUM_*`).
- Constructor: `config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS)` then
  `configParam/configSwitch/configInput/configOutput` for **every** id, with
  ranges, sane defaults, units, and human labels.
- DSP lives in a separate engine struct/namespace (`Lives::…`); the
  `Module` wires params/CV to it. Keep DSP and Rack glue separable.
- `Widget` binds live controls **by anchor name** via SvgHelper
  (`bindParam`/`bindInput`/`bindOutput`) — **no hardcoded coordinates in C++**;
  the panel is panelkit-generated SVG.

## 6. Params, CV, units

- `configParam` carries min/max/default + unit + label; defaults are musically
  sensible, not 0.
- CV scaling convention is consistent (e.g. `+ input.getVoltage() * 0.1f` for a
  0..1 knob), wrapped in a helper (`normWithCV`) rather than repeated inline.
- Levels in **dB** where they're relationships (AccentMix), normalized 0..1 where
  they're knob positions; never mix the two without a conversion at the seam.

## 7. The accent / bus contract

- Read the bus **once** per `process()` (`resolveBus(this)`), then use the result.
- Per-voice `AccentMix` defines the four-case level ladder; shared policy lives in
  the `Accent::` namespace (`kickMix`, `gentleMix`, …) — don't scatter dB literals.
- Accent is latched **at the trigger** (`sampleAccentAtTrig`) and held for the hit.

## 8. Naming

- Types/structs `PascalCase`; functions & locals `camelCase`; compile-time
  constants **`kCamelCase`** (UPPER_SNAKE reads like a macro/#define, so it's
  reserved for enum ids only). Enum ids `UPPER_SNAKE` ending `_PARAM/_INPUT/_OUTPUT`.
  Existing UPPER_SNAKE constants (e.g. `CHOKE_DECAY_SEC`) get renamed to
  `kChokeDecaySec` during the review fixes.
- Namespaces lowercase (`Ghost`). One module per file, file named for it.

## 9. Comments & docstrings

- **Every struct/class** carries a doc comment immediately above it: what it is
  and its contract (one line minimum; more for the DSP engines).
- **Every non-trivial free function / method** carries a brief doc comment —
  purpose, key params and their **units**, and what the return means. Focus on
  the contract, not a restatement of the signature.
- **Exempt:** trivial one-liners (getters, `makeKick() { return Config{}; }`),
  obvious overrides. A docstring must add information, not boilerplate.
- Within a body: explain **why**, not what. Document non-obvious DSP (a filter
  topology, a calibration value, a magic coefficient and where it came from).
- **Provenance comments** (a value `matched`/`ear-tuned`/`calibrated against` a
  reference, dated) are first-class — they're what makes a "magic number" not
  magic. Keep them. BUT when a calibration is **cross-cutting** (one tuning that
  spans several params/knobs, e.g. the kick's reference match), the full story lives
  in `doc/calibration.md` and the inline comment is a one-line pointer to it, so
  the story survives a refactor and is discoverable in one place.
- Match the surrounding density. No commented-out dead code in shipped files.

## 9b. Trademark framing (see ADR 0002)

- GHOST is **"inspired by / 909-style," never a clone.** User-facing text
  (manifest names + descriptions, README, manual, panel labels) must NOT contain
  "TR-909 / TR-09 / Roland".
- Module/file-header comments say "909-style", not "TR-909".
- Calibration provenance references "the reference machine" / "a 909-style
  hardware reference", not "TR-909".
- Internal identifiers were renamed too (ADR 0002 escalation): the namespace is
  `Lives::` (no sub-namespace), files/classes are `Ghost*` — there is no `TR909`
  / `Tr909*` / "909-spelled-out" identifier anywhere in `src/`.

## 10. What must not ship

- Lab/bench variants stay **unregistered** (source ok, not in `plugin.cpp`).
- No debug prints, no `TODO` that's actually a known bug, no dead code paths.
- The browser shows only the intended kit (verified against `plugin.json`).
