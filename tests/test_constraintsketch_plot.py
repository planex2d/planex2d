# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import re
import warnings

import numpy as np
import pytest

import planex as px

pytestmark = pytest.mark.constraints


def plate():
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Line(3.7, 0.25, px.Horizontal, id="bottom").Arc(0.9, 95, px.Tangent, id="fillet") \
        .Line(-0.1, 1.7, px.Vertical, px.Tangent, id="right").Line(-4.6, 0.3, px.Perpendicular, id="top") \
        .Close(px.Perpendicular, id="left")
    sk.Constrain("fillet", px.Radius, 1, id="r")
    sk.Constrain("bottom.start", "right", px.Distance, 5, id="width")
    sk.Constrain("bottom", "top", px.Distance, 3, id="height")
    sk.Circle(2, 1.5, 0.5, px.Fix, id="hole")
    return sk


def titles(svg):
    return re.findall(r"<title>(.*?)</title>", svg)


def test_every_element_shows_its_id_when_hovered():
    t = titles(plate().ToSVG())
    for i in ("bottom", "fillet", "right", "top", "left", "hole"):
        assert i in t


def test_every_constraint_gets_a_sign_with_its_text():
    sk = plate()
    t = titles(px.ToSVG(sk, show_constraints=True))
    signs = [x for x in t if ": " in x]
    assert len(signs) == sum(len(c) for c in sk.constraints.values())
    assert 'width: "bottom.start", "right", px.Distance, 5' in signs
    svg = px.ToSVG(sk, show_constraints=True)
    assert "width = 5" in svg and "r = R1" in svg and ">H<" in svg and ">V<" in svg
    assert "<title>" in px.ToSVG(sk) and "width = 5" not in px.ToSVG(sk)


def test_a_redundant_constraint_sign_is_pink():
    sk = plate()
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        sk.Constrain("right", "left", px.Parallel)
    svg = px.ToSVG(sk, show_constraints=True)
    sign = re.search(r"<g class=\"sg\"><title>Parallel.1: .*?</g>", svg).group(0)
    assert 'fill="#d6338c"' in sign


def test_ends_and_directions():
    sk = plate()
    svg = px.ToSVG(sk, show_ends=True, show_direction=True)
    assert svg.count('stroke="#1c7ed6"') == 5 and svg.count('stroke="#f08c00"') == 5
    assert len(re.findall(r'<path d="M [^"]* Z" fill="#2b8a3e" stroke="none">', svg)) == 5


def test_plotly_signs_keep_their_size_on_screen():
    sk = plate()
    fig = sk.Plot(show_constraints=True, show_ends=True, show_direction=True, backend="plotly")
    texts = [a.text for a in fig.layout.annotations]
    assert "<b>width = 5</b>" in texts and "<b>H</b>" in texts
    assert all(s.xsizemode == "pixel" and s.ysizemode == "pixel" for s in fig.layout.shapes)
    ends = [s for s in fig.layout.shapes if s.line.width == 3]
    assert len(ends) == 10
    corners = [s for s in fig.layout.shapes if s.type == "path" and s.path.count("L") == 2 and s.line.width == 1]
    assert len(corners) == 2 and all(c.path.startswith("M 8.400") or c.path.startswith("M -8.400") or c.path.startswith("M 0.000")
                                     or c.path.startswith("M -0.000") for c in corners)
    assert any(t.marker.symbol == "triangle-right" for t in fig.data if t.mode == "markers")
    hover = [h for t in fig.data if t.hovertext is not None for h in t.hovertext]
    assert {"bottom", "fillet", "right", "top", "left", "hole"} <= set(hover)
    assert "Line.1.end · bottom.start (Point.1)" in hover or "bottom.start · left.end (Point.1)" in hover


def test_matplotlib_draws_the_same_signs_at_a_fixed_size_on_screen():
    matplotlib = pytest.importorskip("matplotlib")
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D

    sk = plate()
    ax = sk.Plot(show_constraints=True, show_ends=True, show_direction=True)
    fig = sk.Plot(show_constraints=True, show_ends=True, show_direction=True, backend="plotly")
    texts = sorted(t.get_text() for t in ax.texts)
    assert texts == sorted(a.text[3:-4] for a in fig.layout.annotations if a.text != " ")
    ends = [l for l in ax.lines if l.get_linewidth() == pytest.approx(3 * 72 / 96)]
    assert len(ends) == 10 and {l.get_color() for l in ends} == {"#1c7ed6", "#f08c00"}
    assert len(ax.patches) >= 5
    ax.figure.canvas.draw()
    size = [np.ptp(l.get_transform().transform(l.get_xydata()), axis=0) for l in ends]
    (x0, x1), (y0, y1) = ax.get_xlim(), ax.get_ylim()
    ax.set_xlim(x0, x0 + (x1 - x0) / 3)
    ax.set_ylim(y0, y0 + (y1 - y0) / 3)
    ax.figure.canvas.draw()
    np.testing.assert_allclose([np.ptp(l.get_transform().transform(l.get_xydata()), axis=0) for l in ends], size, atol=1e-9)
    assert all(isinstance(l, Line2D) for l in ends)
    plt.close("all")


def test_signs_of_redundant_constraints_are_pink_in_both_views():
    sk = plate()
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        sk.Constrain("right", "left", px.Parallel)
    fig = sk.Plot(show_constraints=True, backend="plotly")
    pink = [a for a in fig.layout.annotations if a.font.color == "#d6338c"]
    assert any("Parallel.1" in a.hovertext for a in pink)
    assert any(s.line.color == "#d6338c" for s in fig.layout.shapes)
    svg = px.ToSVG(sk, show_constraints=True, show_ends=True)
    assert re.search(r"<g class=\"sg\"><title>Perpendicular.1: [^<]*</title>(<path class=\"ld\"[^>]*>)?<path [^>]*stroke=\"#d6338c\"", svg)
    assert "<title>bottom.start</title>" in svg and "<title>bottom.end</title>" in svg


def test_the_coincidence_circle_sits_next_to_its_point():
    sk = plate()
    item = next(i for g in px._svg._signs(sk, {g["id"]: g for g in sk._Drawing()}) for i in g["items"] if i[0] == "circle")
    assert item[1] == (0, 0) and item[2] != (0.0, 0.0)


def test_show_parent_draws_the_solid_under_the_sketch_with_its_edge_names():
    occ = pytest.importorskip("netgen.occ")
    box = occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(40, 20, 10)) - occ.Cylinder(occ.Pnt(30, 10, -1), occ.Z, r=4, h=12)
    top = box.faces.Max(occ.Z)
    for k, e in enumerate(top.edges):
        e.name = "top%d" % k
    sk = px.Sketch(top)
    sk.Move((5, 5)).Line((15, 5)).Line((15, 15)).Line((5, 15)).Close()
    plain = sk.Plot(backend="plotly")
    fig = sk.Plot(show_parent=box, backend="plotly")
    parent = fig.data[:len(fig.data) - len(plain.data)]
    assert [t.fill for t in parent].count("toself") >= 1
    names = {t.hovertext for t in parent if t.mode == "lines"}
    assert {"top0", "top1", "top2", "top3", "top4"} <= names
    assert all(t.line.color == "black" for t in parent if t.mode == "lines")
    assert not any(n.startswith(("Line.", "Arc.", "Circle.")) for n in names if n)
