# EXPR examples

Right-click EXPR → *Examples* loads one of these equations. The source is
in `src/expr/ExprExamples.hpp`, and `tests/expr` compiles each one and
sweeps it across its domain.

## Patterns

Each one is an equation transcribed from one frame of the TikTok video
[@indent126/7693583477320584478](https://www.tiktok.com/@indent126/video/7693583477320584478).
Only the equations are used, not the audio or the video. Each pattern
carries its own domain, and with both knobs at 1 (their default) it is the
equation in the frame exactly. Most swing several volts, so set GAIN low
before you send one to an audio path.

| Menu name | Frame | Knobs |
|---|---|---|
| Kick and bursts | 04 | d: kick decay, f: ring rate |
| Sixteen-step rings | 10 | d: ring decay, f: ring rate |
| Ringing decays | 02 | d: low decay, f: ring rate |
| Bit dashes | 03 | g: note length, k: decay |
| Tanh and arches | 01 | p: piece length, w: arch narrowness |
| Exp ramps and arches | 12 | p: piece length, w: arch narrowness |
| Shrinking arches | 11 | g: gate, d: drive |
| Parabola gates | 06 | g: parabola gate, d: drive |
| Domes | 08 | w: dome narrowness, b: bump height |
| Log saw wobble | 07 | a: saw depth, f: wobble rate |
| Tan chirp | 05 | a: tan amount, b: chirp rate |
| Nested mods | 09 | a: sin rate, b: cos rate |
| Tan of nested mod | 13 | a: outer rate, b: inner rate |

Some patterns leave gaps that no branch covers: a quarter of the lap in
Kick and bursts and Sixteen-step rings, and nearly half in Bit dashes. The
fault policy decides what plays in those gaps.

## Basics

Short equations on the default 0 to 2π domain. Each one closes at the end
of the lap when its knobs are on whole numbers: Sine, Harmonics, Damped,
Half and half, Pentatonic, Folded, Rose (polar) and Lissajous (parametric).
