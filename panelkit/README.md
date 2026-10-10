# panelkit/

This plugin's panel intent. The panelkit toolkit itself is an external package
(a dev dependency); what lives here is only the per-module specs it compiles.

- `specs/panels/<Module>.panel.yaml` — one file per module

Render with `uv run panelkit render <Module>` from the repo root, which writes
`res/<Module>.svg`.
