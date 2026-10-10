# res

Runtime assets shipped with the plugin.

- `*.svg` — module panels. **Generated** from `panelkit/specs/` — edit the
  specs and re-render, not the SVGs.
- `fonts/` — fonts used by the panel toolkit and by NanoVG at runtime.

Render a panel with `uv run panelkit render <Module> --no-preview`.
