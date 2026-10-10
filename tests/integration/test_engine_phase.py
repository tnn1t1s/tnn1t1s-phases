"""End-to-end: the declarative Phase spec compiles to a 6 HP panel with the
bound anchors, vector-only labels, and a three-jack I/O row that clears its
neighbours and the panel edges."""
import pytest
from panelkit.dsl import load_panel_spec
from panelkit.layout.engine import build_panel
from panelkit.paths import panel_spec_path

SPEC = panel_spec_path("Phase")
HP = 8
WIDTH_MM = HP * 5.08

IO = ("clock.main.in", "trig.main.reset", "out.main.phase")
ANCHORS = ({"param.main.dir", "param.main.start", "param.main.len",
            "param.main.beats", "param.main.rate",
            "cv.main.start", "cv.main.len", "cv.main.dir",
            "light.main.clock"} | set(IO))


def _all_ids(els):
    ids = set()
    for e in els:
        for n in e.xml().iter():
            if n.get("id"):
                ids.add(n.get("id"))
    return ids


def _centre(els, eid):
    for e in els:
        for n in e.xml().iter():
            if n.get("id") == eid:
                return (float(n.get("x")) + float(n.get("width")) / 2,
                        float(n.get("y")) + float(n.get("height")) / 2)
    return None


def _build(theme, layout, footprints, fontbook):
    return build_panel(load_panel_spec(SPEC), theme, layout, footprints,
                       fontbook)


def test_anchors_exact(theme, layout, footprints, fontbook):
    assert _all_ids(_build(theme, layout, footprints, fontbook)) == ANCHORS


def test_panel_dimensions(theme, layout, footprints, fontbook):
    from panelkit import units
    from panelkit.render import panel_svg
    spec = load_panel_spec(SPEC)
    els = build_panel(spec, theme, layout, footprints, fontbook)
    svg = panel_svg(spec.hp, els, theme)
    assert spec.hp == HP
    assert f'width="{units.hp_to_mm(spec.hp):.2f}mm"' in svg


def test_no_text_elements(theme, layout, footprints, fontbook):
    els = _build(theme, layout, footprints, fontbook)
    assert all(not n.tag.endswith("text") for e in els for n in e.xml().iter())


def test_io_row_order_and_clearance(theme, layout, footprints, fontbook):
    els = _build(theme, layout, footprints, fontbook)
    port = footprints["PJ301MPort"]
    xs = [_centre(els, i)[0] for i in IO]
    assert xs == sorted(xs)
    ys = {round(_centre(els, i)[1], 4) for i in IO}
    assert ys == {round(layout.io.row_y_mm, 4)}
    for a, b in zip(xs, xs[1:]):
        assert b - a > port.hitbox_d_mm
    assert xs[0] - port.hitbox_d_mm / 2 > 0
    assert xs[-1] + port.hitbox_d_mm / 2 < WIDTH_MM


def test_dir_switch_between_header_and_io(theme, layout, footprints,
                                          fontbook):
    els = _build(theme, layout, footprints, fontbook)
    _, y = _centre(els, "param.main.dir")
    assert layout.header.ghost_y_mm < y < layout.io.chevron_y_mm
