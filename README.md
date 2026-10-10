# TNN1T1S Phases

**An equation on a clock: see the curve, hear the curve.**

Phases is two Rack modules. PHASES PHASOR turns a clock into position, a 0–10 V
ramp on a wire with 0 V at the top of the lap and 10 V at the end. PHASES EXPR
reads that wire and plays an equation across it, so the curve drawn on the
panel is the voltage leaving the module.

## Modules

| Module | Role |
|---|---|
| **PHASES PHASOR** | Clock-to-phase converter. One ramp lap every LAP beats of the clock; RATE speeds the lap up, as far as audio rate; freezes when the clock stops; RST snaps to the top; DIRECTION reverses; START and LEN window the lap. |
| **PHASES EXPR** | A live-compiled equation whose argument is the wire. Type `y = a sin(bx)`, press Enter, and it is a voltage while the patch runs. Each undefined letter becomes one of six knob/CV slots, X gives it an argument without a transport, and the display draws the curve. Volts only, limited and optionally smoothed. |

The default patch is two cables: clock → PHASOR, then EXPR's Y to whatever it
should move. EXPR with an unpatched PHASE input picks up the wire from a
PHASOR standing next to it; a cable always overrides.

## Expressions

EXPR plays one equation written the way Desmos writes it
(`doc/dev/expr-language.md`):

```text
y = a sin(bx) / cosh(x/4)
r = cos(4θ)
(cos(3t), sin(2t))
```

Implied multiplication, `|...|`, `π` and `θ`, piecewise braces and
1-indexed lists all work as typed in Desmos, and each undefined letter
becomes one of six knobs. The domain is written in the equation as a
restriction, `y = floor(x)/4 {0 ≤ x ≤ 16}`, and is 0 to 2π without one, so
`sin(x)` is one cycle per lap. LOOP and POS play an exact slice of it, so a
loop always meets itself at the wrap. There is no
state: a clock reaches EXPR through PHASOR. An equation that fails to
compile changes nothing. The saved description is plain JSON in the
patch, so an agent can author it directly.

## Building

```sh
make                    # no external dependencies
make deploy             # build + install into the local Rack plugin folder
```

`RACK_DIR` defaults to `~/Developer/vcv-rack/vendor/rack-sdk`; override it
from the environment.

## Tests

```sh
uv run pytest           # panel specs
make -C tests/wire run  # the adjacency rule and the lap counter
make -C tests/expr run  # the EXPR compiler and runtime
```

## Panels

`res/*.svg` are generated from `panelkit/specs/panels/*.panel.yaml` by the
external [panelkit](../panelkit) toolkit (a dev dependency wired to a
local checkout):

```sh
uv sync
uv run panelkit render <Module> --no-preview
```

## Documentation

- [User manual](doc/PhasesUserManual.md)
- [The EXPR language](doc/dev/expr-language.md)
- [The phase wire](doc/dev/phase-wire.md)
- [Decision records](doc/decisions/)

The phase architecture follows Wakefield & Taylor, *Generating Sound &
Organizing Time: Thinking with gen~* (Cycling '74, 2022).

## License

MIT. Bundled fonts are under the SIL Open Font License.
