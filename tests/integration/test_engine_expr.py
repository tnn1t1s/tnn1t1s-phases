"""End-to-end: the declarative Expr spec compiles to a 20 HP panel with the
display band, one row of four knob/CV pairs (slots 1 and 2, LOOP, POS),
and a bottom row of PHASE, RST, GAIN and LIMIT (each a knob with its CV
jack), Y1 and Y2 that clears its neighbours and the panel edges.

The module draws the slot names and the LOOP and POS readings inside the
display at runtime; the spec carries only the engraved labels."""
from panelkit.dsl import load_panel_spec
from panelkit.layout.engine import build_panel
from panelkit.paths import panel_spec_path

SPEC = panel_spec_path("Expr")
HP = 20
WIDTH_MM = HP * 5.08

SLOTS = [(f"param.main.p{i}", f"cv.main.p{i}") for i in range(1, 3)]
SLICE = [("param.main.loop", "cv.main.loop"), ("param.main.pos", "cv.main.pos")]
KNOB_ROW = SLOTS + SLICE
OUTPUT_STAGE = [("param.main.gain", "cv.main.gain"), ("param.main.limit", "cv.main.limit")]
# left to right along the bottom row
BOTTOM = ("in.main.phase", "trig.main.reset", "param.main.gain", "cv.main.gain",
          "param.main.limit", "cv.main.limit", "out.main.y", "out.main.y2")
ANCHORS = ({a for pair in KNOB_ROW for a in pair} | set(BOTTOM)
           | {"display.main.tl", "display.main.br", "light.main.fault"})


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


def test_anchors_present(theme, layout, footprints, fontbook):
    ids = _all_ids(_build(theme, layout, footprints, fontbook))
    assert ids == ANCHORS


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


def _hitbox(footprints, eid):
    name = "RoundBlackKnob" if eid.startswith("param.") else "PJ301MPort"
    return footprints[name].hitbox_d_mm


def test_bottom_row_order_and_clearance(theme, layout, footprints, fontbook):
    els = _build(theme, layout, footprints, fontbook)
    xs = [_centre(els, i)[0] for i in BOTTOM]
    assert xs == sorted(xs)
    ys = {round(_centre(els, i)[1], 4) for i in BOTTOM}
    assert ys == {round(layout.io.row_y_mm, 4)}
    for (a, xa), (b, xb) in zip(zip(BOTTOM, xs), zip(BOTTOM[1:], xs[1:])):
        assert xb - xa > (_hitbox(footprints, a) + _hitbox(footprints, b)) / 2
    assert xs[0] - _hitbox(footprints, BOTTOM[0]) / 2 > 0
    assert xs[-1] + _hitbox(footprints, BOTTOM[-1]) / 2 < WIDTH_MM


def test_output_stage_uses_the_slot_dial(theme, layout, footprints, fontbook):
    spec = load_panel_spec(SPEC)
    io = [s for s in spec.sections if s.kind == "io_row"][0]
    dials = [c for g in io.cell_groups for c in g if c.param]
    assert [c.param for c in dials] == ["param.main.gain", "param.main.limit"]
    assert {c.knob for c in dials} == {"RoundBlackKnob"}


def test_bottom_labels_share_one_line(theme, layout, footprints, fontbook,
                                      monkeypatch):
    import panelkit.components as comp
    from panelkit.primitives.dial import outer_radius
    seen = {}
    real = comp.label

    def record(x, y, text, *a, **k):
        seen[text] = y
        return real(x, y, text, *a, **k)
    monkeypatch.setattr(comp, "label", record)
    _build(theme, layout, footprints, fontbook)
    names = ("PHASE", "RST", "GAIN", "LIMIT", "Y1", "Y2")
    ys = {round(seen[n], 4) for n in names}
    assert len(ys) == 1
    # and below every dial's tick ring
    ring = outer_radius(footprints["RoundBlackKnob"], theme)
    assert ys.pop() > layout.io.row_y_mm + ring


def test_knobs_are_one_row(theme, layout, footprints, fontbook):
    els = _build(theme, layout, footprints, fontbook)
    knob = footprints["RoundBlackKnob"]
    port = footprints["PJ301MPort"]
    row = [_centre(els, k) for k, _ in KNOB_ROW]
    assert len({round(c[1], 3) for c in row}) == 1
    xs = [c[0] for c in row]
    assert xs == sorted(xs)
    # each CV jack clears the next slot's knob
    for (_, jack), (knob_id, _) in zip(KNOB_ROW, KNOB_ROW[1:]):
        cx, _ = _centre(els, jack)
        kx, _ = _centre(els, knob_id)
        assert cx + port.hitbox_d_mm / 2 < kx - knob.visual_d_mm / 2
    # the row sits below the display and above the bottom chevron
    _, disp_bottom = _centre(els, "display.main.br")
    y = row[0][1]
    assert disp_bottom + knob.visual_d_mm / 2 < y < layout.io.chevron_y_mm - knob.visual_d_mm / 2


def test_cv_jack_right_of_its_knob(theme, layout, footprints, fontbook):
    els = _build(theme, layout, footprints, fontbook)
    for k, j in KNOB_ROW + OUTPUT_STAGE:
        kx, ky = _centre(els, k)
        cx, cy = _centre(els, j)
        assert cx > kx
        assert round(cy, 3) == round(ky, 3)


# Mirrors kRingMm in ExprWidget (src/Expr.cpp).
RING_MM = 12.0


def test_active_ring_clears_its_jack(theme, layout, footprints, fontbook):
    els = _build(theme, layout, footprints, fontbook)
    port = footprints["PJ301MPort"]
    for k, j in SLOTS:
        kx, _ = _centre(els, k)
        cx, _ = _centre(els, j)
        assert cx - kx - port.hitbox_d_mm / 2 >= RING_MM / 2
