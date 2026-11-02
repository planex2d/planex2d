# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px


def plate():
    return px.Face([px.FromPolyline([(0, 0), (4, 0), (4, 2), (0, 2)], closed=True)])


def rounded():
    face = plate()
    for corner in ((0, 0), (4, 0), (4, 2), (0, 2)):
        face.Fillet(corner, 0.5)
    return face


def midpoints(selection):
    return {(round(e.midpoint.x, 9), round(e.midpoint.y, 9)) for e in selection}


def test_minimal_takes_the_chosen_element_alone():
    assert px.Minimal is px.Minimal and repr(px.Minimal) == "px.Minimal"
    face = rounded()
    assert len(face.edges(near=(2, 0))) == 1
    assert face.edges(near=(2, 0), propagate=px.Minimal).ids == face.edges(near=(2, 0)).ids


def test_tangent_follows_the_fillets_around_and_stops_at_corners():
    for propagate, count in ((px.Coincident, 8), (px.Tangent, 8), (px.Curvature, 1)):
        assert len(rounded().edges(near=(2, 0), propagate=propagate)) == count
    for propagate, count in ((px.Coincident, 4), (px.Tangent, 1), (px.Curvature, 1)):
        assert len(plate().edges(near=(2, 0), propagate=propagate)) == count


def test_propagation_stops_where_the_continuity_branches():
    # two plates side by side: three edges meet at (2, 0) and (2, 1)
    two = px.Glue([px.Face([px.FromPolyline([(0, 0), (2, 0), (2, 1), (0, 1)], closed=True)]),
                   px.Face([px.FromPolyline([(2, 0), (4, 0), (4, 1), (2, 1)], closed=True)])])
    assert midpoints(two.edges(near=(1, 0), propagate=px.Coincident)) == {(1, 0), (0, 0.5), (1, 1)}
    # one of the two others goes on tangentially: the bottom continues, the middle edge does not count
    assert midpoints(two.edges(near=(1, 0), propagate=px.Tangent)) == {(1, 0), (3, 0)}
    assert len(two(near=(1, 0), propagate=px.Coincident)) == 3


def test_a_sketch_selection_and_its_wire_follow_a_slot():
    sk = px.QuickSketch().Line(10).Arc(2, 180).Line(10).Arc(2, 180)
    assert set(sk("Line.1", propagate=px.Tangent).ids) == {sk.id + "." + i for i in ("Line.1", "Arc.1", "Line.2", "Arc.2")}
    assert sk("Line.1", propagate=px.Curvature).ids == [sk.id + ".Line.1"]
    wire = sk.Wire("Line.1", propagate=px.Tangent)
    assert len(wire.edges) == 4 and wire.IsClosed()
    assert len(sk.Wire("Line.1").edges) == 1


def test_curvature_follows_a_curvature_continuous_blend_not_an_arc():
    sk = px.Sketch()
    sk.Move((0, 0)).Line((5, 0), id="a")
    sk.Move((8, 3)).Line((13, 3), id="b")
    sk.Connect("a.end", "b.start", px.Curvature)
    assert sk("a", propagate=px.Curvature).ids == [sk.id + "." + i for i in ("a", "Connect.1", "b")]
    turn = px.QuickSketch().Line(5).Arc(2, 90).Line(5)
    assert turn("Line.1", propagate=px.Curvature).ids == [turn.id + ".Line.1"]
    assert len(turn("Line.1", propagate=px.Tangent)) == 3


def test_construction_continues_construction_only():
    sk = px.QuickSketch().Move((0, 0)).Line((5, 0)).Line((10, 0), construction=True)
    assert sk("Line.1", propagate=px.Tangent).ids == [sk.id + ".Line.1"]
    assert sk("Line.2", propagate=px.Tangent).ids == [sk.id + ".Line.2"]


def test_propagate_takes_a_continuity_and_wire_an_element():
    for bad in (px.Horizontal, "tangent"):
        with pytest.raises(ValueError, match=r"propagate=.*px\.Minimal, px\.Coincident, px\.Tangent or px\.Curvature"):
            rounded().edges(near=(2, 0), propagate=bad)
    sk = px.QuickSketch().Line(10).Arc(2, 180).Line(10).Arc(2, 180)
    with pytest.raises(ValueError, match=r"Wire\(propagate=\.\.\.\): propagates from an element"):
        sk.Wire(propagate=px.Tangent)
    with pytest.raises(ValueError, match=r"Wire\(propagate=.*px\.Minimal, px\.Coincident"):
        sk.Wire("Line.1", propagate=px.Horizontal)
