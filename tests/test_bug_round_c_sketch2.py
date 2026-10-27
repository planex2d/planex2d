# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import re

import pytest

import planex as px
from planex import _svg


def _profile_points(chord=100.0, n=24, t=0.12):
    xs = [0.5 * (1 - math.cos(math.pi * i / n)) for i in range(n + 1)]

    def y(x):
        return 5 * t * (0.2969 * math.sqrt(x) - 0.126 * x - 0.3516 * x * x + 0.2843 * x ** 3 - 0.1036 * x ** 4)

    upper = [(chord * x, chord * y(x)) for x in xs]
    return upper[::-1] + [(chord * x, -chord * y(x)) for x in xs[1:-1]]


def _wing():
    wing = px.QuickSketch()
    wing.Spline(_profile_points() + [_profile_points()[0]], periodic=True, id="profile")
    return wing


def test_0100_hinge_line_between_profile_intersections():
    wing = _wing()
    wing.Line(("profile", (75, -10)), ("profile", (75, 10)), parallel=px.V, id="hinge", construction=True)
    a, b = wing.Location("hinge.start"), wing.Location("hinge.end")
    assert a[0] == pytest.approx(b[0], abs=1e-9)
    assert a[1] < 0 < b[1]
    for p in (a, b):
        assert wing.Geometry("profile").Distance(p) < 1e-9 if hasattr(wing.Geometry("profile"), "Distance") else True


def test_0100_no_intersection_raises_and_draws_nothing():
    wing = _wing()
    before = list(wing.elements)
    with pytest.raises(ValueError, match="Line"):
        wing.Line(("profile", (250, -10)), ("profile", (250, 10)), parallel=px.V, id="hinge", construction=True)
    assert list(wing.elements) == before


def _fillet_setup(c):
    c.Move((-2, 0)).Line((8, 0), id="x", construction=True)
    c.Move((3, -3)).Line((3, 3), id="y", construction=True)
    return c


def _arc_data(c, id):
    return (c.Location(id + ".center"), c.Location(id + ".start"), c.Location(id + ".end"))


@pytest.mark.parametrize("cls", [px.QuickSketch, px.Sketch])
def test_0101_arc_radius_tangent_pair(cls):
    short = _fillet_setup(cls())
    short.Arc(1, tangent=("x", "y"), id="f")
    long = _fillet_setup(cls())
    long.Arc("x", "y", 1, tangent=("x", "y"), id="f")
    for p, q in zip(_arc_data(short, "f"), _arc_data(long, "f")):
        assert p[0] == pytest.approx(q[0], abs=1e-12) and p[1] == pytest.approx(q[1], abs=1e-12)


def _link():
    sk = px.Sketch(auto_constraint=True)
    sk.Circle((0, 0), 1.5, id="boss1", construction=True)
    sk.Circle((6, 0), 1.0, id="boss2", construction=True)
    sk.Line(tangent=("boss1", "boss2"), id="upper")
    sk.Line(tangent=("boss1", "boss2"), reverse=True, id="lower")
    sk.Arc("lower.end", "upper.end", coincident="boss2", id="end2")
    sk.Arc("upper.start", "lower.start", coincident="boss1", id="end1")
    return sk


def test_0102_no_redundant_auto_constraints():
    sk = _link()
    assert sk.redundant == []
    assert sk.dof == _link().dof


def _boxes(svg):
    out = []
    for m in re.finditer(r'<text x="([-\d.]+)" y="([-\d.]+)" font-family="sans-serif" font-size="10" font-weight="bold"[^>]*>([^<]*)</text>', svg):
        x, y, t = float(m[1]), float(m[2]), m[3]
        out.append((t, x - 3 * len(t), x + 3 * len(t), y - 5, y + 5))
    return out


def _overlaps(boxes):
    return [(a[0], b[0]) for i, a in enumerate(boxes) for b in boxes[i + 1:]
            if a[1] < b[2] and b[1] < a[2] and a[3] < b[4] and b[3] < a[4]]


def _dimensioned_link():
    sk = px.Sketch(auto_constraint=True)
    sk.Circle((0, 0), 1.5, id="boss1")
    sk.Circle((0, 0), 0.6, id="hole")
    sk.Circle((6, 0), 1.0, id="boss2")
    sk.Constrain("boss1", px.Radius, 1.5, id="r1")
    sk.Constrain("hole", px.Radius, 0.6, id="d1")
    sk.Constrain("boss2", px.Radius, 1.0, id="r2")
    sk.Constrain("boss1.center", "boss2.center", px.Distance, 6, id="c")
    return sk


def _signs(sk, k=40.0):
    return _svg._signs(sk, {g["id"]: g for g in sk._Drawing()}, k)


def test_0103_redundant_rings_on_their_points():
    sk = _link()
    sk.Constrain("upper.start", "boss1", px.Coincident)
    sk.Constrain("upper.start", "boss1", px.Coincident)
    over = [g for g in _signs(sk) if g["colour"] == _svg.STATE["over"]]
    rings = [item for g in over for item in g["items"] if item[0] == "circle"]
    assert rings
    assert all(item[2] == (0.0, 0.0) for item in rings)


def test_0103_concentric_radius_labels_do_not_overlap():
    sk = _dimensioned_link()
    sk.Update({"r2": 1.3, "c": 8.0})
    assert _overlaps(_boxes(sk.ToSVG(260, show_constraints=True))) == []


def _bracket():
    sk = px.Sketch(auto_constraint=True)
    sk.Move((0, 0)).Line((30, 0), px.Horizontal, id="a").Line((30, 10), px.Vertical, id="b").Line((10, 10), px.Horizontal, id="c")
    sk.Line((10, 50), px.Vertical, id="d").Line((0, 50), px.Horizontal, id="e").Line((0, 0), px.Vertical, id="f")
    sk.Constrain("d", px.Distance, 40, id="g")
    sk.Constrain("b", px.Distance, 10, id="s")
    sk.Constrain("a", px.Distance, 30, id="w")
    sk.Constrain("c", px.Distance, 20, id="u")
    sk.Circle((5, 30), 2, id="hole")
    sk.Constrain("hole.center", "f", px.Distance, 5, id="dx")
    sk.Constrain("a", "hole.center", px.Distance, 30, id="dy")
    return sk


def _pattern():
    sk = px.Sketch(auto_constraint=True)
    sk.Circle((0, 0), 40, id="plate")
    sk.Circle((25, 0), 4, id="hole")
    sk.Constrain("hole", px.Diameter, 8, id="d")
    sk.Circle((0, 0), 3, id="hub")
    sk.CircularPattern("hole", "hub.center", 6, id="k")
    return sk


def _text_boxes(sk, dpi):
    matplotlib = pytest.importorskip("matplotlib")
    from matplotlib.transforms import Bbox

    matplotlib.use("Agg")
    with matplotlib.rc_context({"figure.dpi": dpi}):
        ax = sk.Plot(show_constraints=True)
        ax.figure.canvas.draw()
        return [(t.get_text(), Bbox.from_extents(*(t.get_window_extent().extents + [-2, -2, 2, 2]))) for t in ax.texts]


@pytest.mark.parametrize("dpi", [72, 100])
@pytest.mark.parametrize("make", [_bracket, _pattern])
def test_0107_labels_apart(make, dpi):
    boxes = _text_boxes(make(), dpi)
    assert len(boxes) >= 1
    assert [(a, b) for i, (a, p) in enumerate(boxes) for b, q in boxes[i + 1:] if p.overlaps(q)] == []


def test_0107_pattern_label_once():
    labels = [text for text, _ in _text_boxes(_pattern(), 100) if "⌀" in text]
    assert len(labels) == 1
