# patches

`demos/` holds the patch to open first.

## demos/phases-ghost-groove.vcv

A Clocked clock at 116 BPM drives PHASOR and a Drumsequencer. The
sequencer plays the tnn1t1s-ghost kick, snare and hats, and EXPR plays the
Tanh and arches pattern from the Examples menu:

```text
y = {mod(x, 1.6p) < 0.8p: 2.4 tanh(2 sin((π x)/(2))), -2.4 + 4.8 sqrt(max(0, 1 - 0.28w (mod(x, 4) - 2)^2))} {-16 ≤ x ≤ 16}
```

LOOP holds a quarter of the domain and POS picks which quarter. One Y
goes through a quantizer to the VCO's pitch, and the other drives the
Substation filter's cutoff on the VCO's saw. A Substation envelope,
triggered from the sequencer, shapes that voice through a VCA. A mixer
sums the drums and the voice and sends the voice to a reverb and a
SurgeXT delay. A compressor sits on the main out.

Plugins it uses besides Phases: tnn1t1s-ghost, ImpromptuModular,
Hora-Sequencers, SlimeChild-Substation, Coffee, Autinn, SurgeXTRack,
Fundamental, VCV-Pro.
