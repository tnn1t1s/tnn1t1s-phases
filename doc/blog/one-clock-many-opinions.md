# One clock, many opinions

Before MIDI, Roland machines synchronised over DIN sync: a five-pin connector
carrying two things, a run/stop line and a clock running at twenty-four pulses
per quarter note. That is the whole protocol. It says when, twenty-four times a
beat, and it says whether the transport is moving. It says nothing at all about
what any machine should do with that information.

That silence is the interesting part. A drum machine receiving Sync24 decides
for itself how a pulse becomes a step, whether a step is early or late, how long
a trigger lasts. A sequencer on the same cable divides the same pulses into a
different grid. Chain three boxes and you have three interpretations of one
timebase, agreeing on the beat and disagreeing about everything inside it.

They also disagree by accident. The clock arrives as a voltage into an input
circuit, and input circuits are made of components with tolerances. Trigger
thresholds differ, opto-isolators respond at slightly different speeds, and the
propagation delay through one unit is not the propagation delay through another.
Two identical drum machines, same model, same clock, will not fire at the same
instant. The difference is small and it is consistent, which is the worst
combination if you want a grid and the best one if you want a groove.

## What that produced

The records people describe as machine-made rarely sound mechanical in the way
the phrase implies. Blue Monday was assembled from a rack of separate machines
locked to a common clock — an Oberheim drum machine, a sequencer, a monosynth —
each converting that clock into events on its own terms. The rigidity is real,
and so is the lurch underneath it. Both come from the same place: absolute
agreement about the beat, no agreement whatever about the subdivision.

The same is audible across early Detroit records and through Hi-NRG, where the
sequencing method was more or less the same and the equipment varied from studio
to studio. When a producer changed one machine in the chain, the feel changed
with it, and not because anyone had programmed a different pattern.

None of this survives being quantised. Put all those parts in one sequencer, on
one grid, and the stiffness stays while the lurch disappears.

## Where this lands in LIVES

PHASE exists because of that distinction. It takes a clock and publishes
position: a ramp that says where you are in the lap, not when the next pulse
arrives. EXPR reads the ramp. It never sees a clock and cannot infer one, so
it cannot form an opinion about subdivision; its opinion is the ramp's shape,
which somebody else decides.

That makes the disagreement deliberate rather than accidental. Clocked publishes
several clock outputs and swings them independently. Send Clk1 to the drum
sequencer with swing applied and Clk2 to PHASE with none, and the drums shuffle
against an EXPR melody whose timebase stays linear. Both are locked to the same
transport and reset together. Neither is drifting. What changes is what each one
does between the pulses, which is exactly the freedom DIN sync left open by
saying so little.

Reverse it and the point is clearer. Swing the clock feeding PHASE and the melody
shuffles too, because position itself is now bending. The two cases sound
different for a structural reason rather than a cosmetic one, and having both
available in the same patch is the argument for keeping position and clock as
separate signals.
