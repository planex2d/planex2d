# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import warnings

import pytest

import planex as px
from helpers import square

EXPORT_DEFAULTS = ["fit_splines", "fit_arcs", "remove_edges_smaller", "close_gaps_smaller", "remove_free_edges",
                   "merge_collinear_closer"]


@pytest.fixture(autouse=True)
def restore_config():
    saved = {name: getattr(px.config, name) for name in EXPORT_DEFAULTS}
    yield
    for name, value in saved.items():
        setattr(px.config, name, value)


def test_config_holds_the_export_defaults():
    assert px.config.remove_edges_smaller == 1e-7
    assert px.config.fit_splines["enabled"] is False
    assert px.config.fit_arcs["enabled"] is False
    assert px.config.fit_arcs["max_edge_angle"] == 30.0


def test_config_fits_are_one_dict_per_fit():
    keys = {"enabled", "tol", "max_edge_length", "max_edge_angle", "ignore_edges_smaller"}
    assert set(px.config.fit_splines) == keys and set(px.config.fit_arcs) == keys
    angle = px.config.fit_splines["max_edge_angle"]
    px.config.fit_splines = dict(tol=1e-3)
    assert px.config.fit_splines["enabled"] is True and px.config.fit_splines["tol"] == 1e-3
    assert px.config.fit_splines["max_edge_angle"] == angle
    px.config.fit_splines = False
    assert px.config.fit_splines["enabled"] is False and px.config.fit_splines["tol"] == 1e-3
    px.config.fit_splines = dict(enabled=False, max_edge_length=0.5)
    assert px.config.fit_splines["enabled"] is False and px.config.fit_splines["max_edge_length"] == 0.5
    saved = px.config.fit_arcs
    px.config.fit_arcs = saved
    assert px.config.fit_arcs == saved
    with pytest.raises(TypeError, match="no option 'size'"):
        px.config.fit_arcs = dict(size=1)
    with pytest.raises(TypeError, match="None"):
        px.config.fit_arcs = None
    assert not hasattr(px.config, "max_spline_edge_length") and not hasattr(px.config, "arc_fit_tol")


def test_export_arguments_fall_back_to_config():
    p = px.Move((0, 0)).Line((0.5, 0)).Line((1, 0)).Line((1, 1)).Line((0, 1)).Close().Wire(unify=False)
    assert len(p.ToOCCWires().edges) == 4
    px.config.merge_collinear_closer = 0
    assert len(p.ToOCCWires().edges) == 5
    assert len(p.ToOCCWires(merge_collinear_closer=1e-7).edges) == 4


def test_arc_fit_defaults_from_config():
    wp = px.Move((1, 0))
    for k in range(1, 60):
        wp.Line((math.cos(2 * math.pi * k / 60), math.sin(2 * math.pi * k / 60)))
    wp.Close()
    assert len(wp.ToOCCFace().edges) == 60
    px.config.fit_arcs = dict(max_edge_length=1.0, tol=1e-2)
    assert len(wp.ToOCCFace().edges) < 60
    assert len(wp.ToOCCFace(fit_arcs=False).edges) == 60
    p = wp.Face()
    p.FitArcs()
    assert len(p.arcs) >= 1 and len(p.lines) + len(p.arcs) < 60


def test_simplify_applies_the_export_defeaturing_to_planex_objects():
    p = px.Line(px.Vertex((3, 3)), px.Vertex((4, 4)))
    q = p.Simplified(remove_free_edges=True)
    assert (len(p.edges), len(q.edges)) == (1, 0)
    assert len(p.Simplify().edges) == 1
    assert len(p.Simplify(remove_free_edges=True).edges) == 0
    assert len(square(0, 0).Wire().Simplify(remove_free_edges=True).edges) == 4
    wp = px.Move((1, 0))
    for k in range(1, 60):
        wp.Line((math.cos(2 * math.pi * k / 60), math.sin(2 * math.pi * k / 60)))
    wp.Close()
    w = wp.Wire().Simplified(fit_arcs=dict(max_edge_length=1.0, tol=1e-2))
    assert len(w.edges) < 60
    assert any(isinstance(e.edge, px.ArcEdge) for e in w.edges)
    assert len(wp.Wire().edges) == 60


def test_remove_edges_smaller_reaches_occgeometry():
    # an edge OCC still accepts (above its 1e-7 confusion), nearly collinear with its neighbour: the collinear merge is
    # off to see the size rule alone
    p = px.Move((0, 0)).Line((2, 0)).Line((2, 2)).Line((0, 2)).Line((1e-7, 1e-5)).Close().Face()
    assert len(p.edges) == 5
    assert len(px.OCCGeometry(p, merge_collinear_closer=0).shape.edges) == 5
    assert len(px.OCCGeometry(p, remove_edges_smaller=1e-4, merge_collinear_closer=0).shape.edges) == 4
    px.config.remove_edges_smaller = 1e-4
    px.config.merge_collinear_closer = 0
    assert len(px.OCCGeometry(p).shape.edges) == 4
    assert len(px.OCCGeometry(p, remove_edges_smaller=0).shape.edges) == 5


def test_warnings_are_controlled_by_python_filters_only():
    assert not hasattr(px.config, "print_warnings")
    assert not hasattr(px.config, "throw_on_warnings")
    with warnings.catch_warnings(record=True) as record:
        warnings.simplefilter("ignore", px.PlanexWarning)
        px.Move((0, 0)).Line((1, 0)).Wire().Face()
    assert record == []


def test_warnings_go_to_python_with_the_caller_as_location():
    assert issubclass(px.PlanexWarning, UserWarning)
    with pytest.warns(px.PlanexWarning, match="open wire") as record:
        px.Move((0, 0)).Line((1, 0)).Wire().Face()
    assert record[0].filename == __file__


def test_error_filter_raises_the_warning():
    with warnings.catch_warnings():
        warnings.simplefilter("error", px.PlanexWarning)
        with pytest.raises(px.PlanexWarning, match="open wire"):
            px.Move((0, 0)).Line((1, 0)).Wire().Face()


def with_rounding_edges(x):
    # a face shrunk below the rounding tolerance after it was made, in the box of a triangle but outside it: the
    # boolean of the two keeps its edges and removes them as rounding, with warnings
    triangle = px.Move((x, 0)).Line((x + 1, 0)).Line((x, 1)).Close().Face()
    # shrunk to a few ulps, not to nothing: an edge of no length at all goes without a warning
    return [triangle, square(x + 0.9, 0.9).Scaled((x + 0.9, 0.9), 5e-15)]


def test_warnings_from_released_gil_and_worker_threads():
    faces = [f for k in range(8) for f in with_rounding_edges(2 * k)]
    with pytest.warns(px.PlanexWarning, match="rounding") as record:
        u = px.Unify(faces, multithreaded=True)
    assert len(record) >= 8
    assert u.mass == pytest.approx(4) and len(u.faces) == 8
    with warnings.catch_warnings():
        warnings.simplefilter("error", px.PlanexWarning)
        with pytest.raises(px.PlanexWarning, match="rounding"):
            px.Unify(faces, multithreaded=True)


def test_occshape_and_bounding_box():
    from netgen import occ
    p = px.Move((1, 2)).Rectangle(3, 3).Face()
    assert isinstance(p.ToOCCShape(), occ.Face)
    assert isinstance(p.Wire().ToOCCShape(), occ.Compound)
    assert isinstance(px.Move((0, 0)).Rectangle(1, 1).ToOCCShape(), occ.Face)
    assert p.bounding_box == ((1, 2), (4, 5))
    assert p.Wire().bounding_box == ((1, 2), (4, 5))
    assert px.Move((1, 2)).Rectangle(3, 3).bounding_box == ((1, 2), (4, 5))
    assert sorted(e.bounding_box for e in p.edges)[0] == ((1, 2), (1, 5))
    assert px.Compound([p, px.Line((-1, 3), (6, 3))]).bounding_box == ((-1, 2), (6, 5))
    assert px.QuickSketch().Circle(0, 0, 2).Face().bounding_box == ((-2, -2), (2, 2))
    assert px.Face().bounding_box is None and px.Shape().bounding_box is None
    assert px.Compound().bounding_box is None


@pytest.mark.constraints
def test_default_sketch_type_decides_what_move_to_makes():
    assert px.config.default_sketch_type is px.QuickSketch
    sk = px.Move((1, 2))
    assert type(sk) is px.QuickSketch and sk.pen.position == (1, 2)
    assert px.Move((1, 2), dir=(0, 1)).pen.direction == (0, 1)
    with pytest.raises(ValueError, match="a QuickSketch has no constraints"):
        px.Move((0, 0), px.Fix)
    with pytest.raises(TypeError, match="starts at a point"):
        px.Move(px.XY)
    try:
        px.config.default_sketch_type = px.Sketch
        assert px.config.default_sketch_type is px.Sketch
        sk = px.Move((1, 2), px.Fix, dir=(0, 1), id="fixed")
        assert type(sk) is px.Sketch and sk.id == "fixed" and sk.pen.position == (1, 2) and sk.pen.direction == (0, 1)
        with pytest.raises(ValueError, match="px.QuickSketch or px.Sketch"):
            px.config.default_sketch_type = px.Face
        assert px.config.default_sketch_type is px.Sketch
    finally:
        px.config.default_sketch_type = px.QuickSketch

