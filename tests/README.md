# tests

| Directory | Tool | Covers |
|---|---|---|
| `integration/` | pytest | Panel specs end-to-end: spec → rendered SVG anchors. |
| `wire/` | C++ + ASan/UBSan | The adjacency freshness rule (`Lives::wireFresh`): a chain stays live at depth in every process order, and a leaderless pair goes dead. PHASOR's lap counter. No SDK needed. |
| `expr/` | C++ + ASan/UBSan | The EXPR language: compiler errors with positions, the semantic corpus, faults that commit nothing, output conditioning, history aggregation, curve preview. No SDK needed. |

```sh
uv run pytest
make -C tests/wire run
make -C tests/expr run
```

`wire/` needs no Rack SDK: the predicates it covers are pure functions,
split into `src/lives/` headers so they could be tested at all.
