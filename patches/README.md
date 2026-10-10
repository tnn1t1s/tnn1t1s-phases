# patches

`demos/` holds the patches to open first. Each one gates its audio on the
clock, so stopping the clock silences it.

## demos/expr-pentatonic.vcv

A clock, PHASE, one EXPR and one VCO. EXPR walks C minor pentatonic across
each lap of the PHASE wire:

```text
y = [0,2,4,7,9][floor(mod(ax/(2π), 5)) + 1] / 12
```

Its output is the VCO's pitch, and knob `a` sets how many notes one lap
covers.
