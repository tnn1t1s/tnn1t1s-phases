# src

| File | Role |
|---|---|
| `plugin.cpp` | Rack entry point, model registration. |
| `Phase.cpp` | PHASES PHASOR: clock-to-phase converter, the family time source. |
| `Expr.cpp` | PHASES EXPR: live-compiled equation on the wire; program handoff, sources, editor, display, widget. |
| `expr/` | The EXPR language, Rack-free: `ExprCompiler.hpp` (a Desmos-style equation → bytecode), `ExprRuntime.hpp` (an Instance running it), `ExprCondition.hpp` (volts: gain, offset, limiter, smoothing), `ExprHistory.hpp` (bounded output history), `ExprPreview.hpp` (the y(x) curve). See doc/dev/expr-language.md. |
| `AgentModule.hpp` | Marker base class; carries the resolved wire. |
| `lives/PhaseWire.hpp` | Transport resolution: cable, then adjacency (doc/dev/phase-wire.md). |
| `lives/WireFresh.hpp` | The adjacency freshness rule, Rack-free so tests/wire covers it. |
| `lives/LapCounter.hpp` | PHASOR's pulse-to-lap counter, Rack-free so tests/wire covers it. |
| `lives/Denormal.hpp` | Denormal floor. |
