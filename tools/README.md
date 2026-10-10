# tools

Development scripts. Nothing here ships with the plugin.

`expr-tex/`: EXPR equations typeset as images, the way the videos show
them. `uv run tools/expr-tex/render.py --patch PATCH.vcv -o DIR` writes one
PNG per EXPR; pass an equation instead of `--patch` for a single image.
