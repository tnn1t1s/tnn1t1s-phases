"""Render EXPR equations as typeset images, the way the videos show them.

    uv run tools/expr-tex/render.py 'y = a sin(bx)' -o eq.png
    uv run tools/expr-tex/render.py --patch track.vcv -o out/

With an equation, writes one image. With --patch, writes one image per
EXPR in the patch, named <patch>-<module id>.png, into the -o directory.
--domain adds the trailing {lo ≤ x ≤ hi}; --svg writes SVG instead of PNG.

The LaTeX comes from build/expr2tex (tools/expr-tex/expr2tex.cpp), which
parses with EXPR's own compiler; this script rebuilds it when its sources
are newer. Typesetting needs tectonic and pdftocairo (Homebrew).
"""
import argparse
import io
import json
import subprocess
import sys
import tarfile
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
TOOL = REPO / "build" / "expr2tex"
SOURCES = [REPO / "tools" / "expr-tex" / "expr2tex.cpp", *sorted((REPO / "src" / "expr").glob("*.hpp"))]

# 300 dpi: the equation stays sharp when the image is scaled up beside a
# full-screen video frame. A display choice, not a measurement.
DPI = 300

DOC = r"""\documentclass[border=10pt]{standalone}
\usepackage{amsmath}
\begin{document}
$\displaystyle %s$
\end{document}
"""


def build():
    if TOOL.exists() and all(TOOL.stat().st_mtime >= s.stat().st_mtime for s in SOURCES):
        return
    TOOL.parent.mkdir(exist_ok=True)
    subprocess.run(["c++", "-std=c++11", "-O1", f"-I{REPO / 'src'}", "-o", str(TOOL), str(SOURCES[0])], check=True)


def latex(source, domain):
    r = subprocess.run([str(TOOL)] + (["--domain"] if domain else []), input=source,
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"does not compile: {r.stderr.strip()}\n{source}")
    return r.stdout.strip()


def render(tex, out, svg):
    with tempfile.TemporaryDirectory() as d:
        doc = Path(d) / "eq.tex"
        doc.write_text(DOC % tex)
        r = subprocess.run(["tectonic", "--chatter", "minimal", "--outdir", d, str(doc)],
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit(f"tectonic failed:\n{r.stderr}\n{tex}")
        pdf = Path(d) / "eq.pdf"
        if svg:
            subprocess.run(["pdftocairo", "-svg", str(pdf), str(out)], check=True)
        else:
            stem = str(out.with_suffix(""))
            subprocess.run(["pdftocairo", "-png", "-r", str(DPI), "-singlefile", str(pdf), stem], check=True)


def patch_sources(path):
    raw = subprocess.run(["zstd", "-dc", str(path)], capture_output=True, check=True).stdout
    tf = tarfile.open(fileobj=io.BytesIO(raw))
    member = next(m for m in tf.getmembers() if Path(m.name).name == "patch.json")
    patch = json.load(tf.extractfile(member))
    return [(m["id"], m["data"]["source"]) for m in patch["modules"]
            if m.get("plugin") == "tnn1t1s-lives" and m["model"] == "Expr"]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("equation", nargs="?")
    ap.add_argument("--patch", type=Path)
    ap.add_argument("-o", "--out", type=Path, required=True)
    ap.add_argument("--domain", action="store_true")
    ap.add_argument("--svg", action="store_true")
    a = ap.parse_args()
    if (a.equation is None) == (a.patch is None):
        ap.error("give an equation or --patch, not both")
    build()
    ext = ".svg" if a.svg else ".png"
    if a.equation is not None:
        out = a.out.with_suffix(ext)
        render(latex(a.equation, a.domain), out, a.svg)
        print(out)
        return
    a.out.mkdir(parents=True, exist_ok=True)
    found = patch_sources(a.patch)
    if not found:
        sys.exit(f"no EXPR in {a.patch}")
    for mid, source in found:
        out = a.out / f"{a.patch.stem}-{mid}{ext}"
        render(latex(source, a.domain), out, a.svg)
        print(out)


if __name__ == "__main__":
    main()
