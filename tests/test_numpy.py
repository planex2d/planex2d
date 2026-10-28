# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import numpy as np
import pytest

import planex as px
from helpers import square


def flange():
    sk = px.QuickSketch().Circle((0, 0), 3).Circle((0, 0), 1)
    for k in range(6):
        a = math.pi * k / 3
        sk.Circle((2 * math.cos(a), 2 * math.sin(a)), 0.25)
    return sk.Face()


def rows(n=400, seed=1, lo=-3.5, hi=3.5, columns=2):
    return np.random.default_rng(seed).uniform(lo, hi, (n, columns))


def on_boundary():
    t = np.linspace(0, 2 * math.pi, 9)[:-1] + 0.1
    pts = [(3 * math.cos(a), 3 * math.sin(a)) for a in t] + [(math.cos(a), math.sin(a)) for a in t]
    pts += [(2 + 0.25 * math.cos(a), 0.25 * math.sin(a)) for a in t]
    return np.array(pts + [(3.0, 0.0), (0.0, 1.0), (2.25, 0.0)])


def holes():
    return np.array([(0.0, 0.0), (2.0, 0.0), (1.0, math.sqrt(3)), (-2.0, 0.0), (0.5, 0.5)])


def singles(f, pts, *args, **kwargs):
    return [f(tuple(p), *args, **kwargs) for p in pts]


@pytest.mark.parametrize("pts", [rows(), on_boundary(), holes()], ids=["random", "boundary", "holes"])
@pytest.mark.parametrize("boundary", [True, False])
def test_contains_of_many_points_is_the_single_call_per_row(pts, boundary):
    f = flange()
    got = f.Contains(pts, boundary=boundary)
    assert isinstance(got, np.ndarray) and got.dtype == bool and got.shape == (len(pts),)
    assert got.tolist() == singles(f.Contains, pts, boundary=boundary)


def test_contains_on_the_boundary_and_in_holes():
    f = flange()
    assert f.Contains(on_boundary()).all() and not f.Contains(on_boundary(), boundary=False).any()
    assert not f.Contains(holes()).any()
    assert f.Contains([(0, 2), (5, 5), (2.5, 0)]).tolist() == [True, False, True]


@pytest.mark.parametrize("pts", [rows(), on_boundary(), holes(), rows(50, 2, -9, 9)], ids=["random", "boundary", "holes", "far"])
def test_distance_and_closest_point_of_many_points(pts):
    f = flange()
    d = f.Distance(pts)
    assert d.dtype == np.float64 and d.shape == (len(pts),)
    assert d.tolist() == singles(f.Distance, pts)
    c = f.ClosestPoint(pts)
    assert c.dtype == np.float64 and c.shape == (len(pts), 2)
    assert c.tolist() == [list(p) for p in singles(f.ClosestPoint, pts)]


def test_distance_values():
    f = flange()
    out = np.array([(5 * math.cos(a), 5 * math.sin(a)) for a in np.linspace(0, 6, 7)])
    assert f.Distance(out).tolist() == pytest.approx([2.0] * 7)
    assert f.Distance(np.array([[2.5, 0.0], [0.0, 0.0], [2.0, 0.0]])).tolist() == pytest.approx([0, 1, 0.25])


def test_components_are_columns():
    f = flange()
    pts = rows(200, 3, -6, 6)
    uv = f.Distance(pts, components="hv")
    assert uv.shape == (200, 2) and uv.tolist() == [list(t) for t in singles(f.Distance, pts, components="hv")]
    z = f.Distance(pts, components="z")
    assert z.shape == (200,) and z.tolist() == singles(f.Distance, pts, components="z")
    with pytest.raises(ValueError, match="hvxyz"):
        f.Distance(pts, components="hw")


def test_points_in_3d():
    f = flange().Rotated((0, 0, 0), (1, 0, 0), 30).Translated((0, 0, 1))
    on = [f.ToXYZ((u, v)) for u, v in rows(100, 4)]
    pts = np.array(on + [tuple(p) for p in rows(200, 5, -4, 4, columns=3)])
    assert f.Contains(pts).tolist() == singles(f.Contains, pts)
    assert f.Contains(pts)[:100].tolist() == singles(f.Contains, pts[:100]) and f.Contains(pts)[:100].any()
    for project in (False, True):
        assert f.Distance(pts, project=project).tolist() == singles(f.Distance, pts, project=project)
    xyz = f.Distance(pts, components="xyz")
    assert xyz.shape == (300, 3) and xyz.tolist() == [list(t) for t in singles(f.Distance, pts, components="xyz")]
    c = f.ClosestPoint(pts)
    assert c.shape == (300, 3) and c.tolist() == [list(p) for p in singles(f.ClosestPoint, pts)]


def test_shapes_wires_edges_and_vertices():
    pts = rows(300, 6, -2, 6)
    two = square(0, 0, 2) + square(3, 0, 2)
    glued = px.Glue([square(0, 0, 2), square(2, 0, 2)])
    w = px.Move((0, 0)).Line((2, 0)).Arc((3, 1), tangent=(1, 0)).Line((3, 3)).Wire()
    for s in (two, glued):
        assert s.Contains(pts).tolist() == singles(s.Contains, pts)
        assert s.faces[1].Contains(pts).tolist() == singles(s.faces[1].Contains, pts)
    for x in (two, glued, w, w.edges[1], two.faces[0]):
        assert x.Distance(pts).tolist() == singles(x.Distance, pts)
        assert x.ClosestPoint(pts).tolist() == [list(p) for p in singles(x.ClosestPoint, pts)]
    v = w.vertices[1]
    assert v.Distance(pts).tolist() == singles(v.Distance, pts)
    assert v.Distance(pts, components="hv").tolist() == [list(t) for t in singles(v.Distance, pts, components="hv")]
    p3 = rows(50, 7, -2, 2, columns=3)
    assert v.Distance(p3).tolist() == singles(v.Distance, p3)


def test_on_the_carrier():
    pts = rows(100, 8, -5, 5)
    for e in (px.Line((0, 0), (1, 0)).edges[0], px.Move((1, 0)).Arc(1, 90).Wire().edges[0]):
        got = e.Distance(pts, on_carrier=True)
        assert got.tolist() == pytest.approx(singles(e.Distance, pts, on_carrier=True), abs=1e-13)
        uv = e.Distance(pts, on_carrier=True, components="hv")
        expected = [c for t in singles(e.Distance, pts, on_carrier=True, components="hv") for c in t]
        assert uv.shape == (100, 2) and uv.ravel().tolist() == pytest.approx(expected, abs=1e-13)


def test_one_point_or_many():
    f = flange()
    assert f.Contains(np.array([0.0, 2.0])) is True and f.Contains((0.0, 2.0)) is True
    assert isinstance(f.Distance(np.array([5.0, 0.0])), float) and f.Distance(np.array([5.0, 0.0])) == pytest.approx(2)
    assert f.ClosestPoint(np.array([5.0, 0.0])) == pytest.approx((3, 0))
    one = np.array([[5.0, 0.0]])
    assert f.Contains(one).shape == (1,) and f.Distance(one).shape == (1,) and f.ClosestPoint(one).shape == (1, 2)
    assert f.Contains(np.empty((0, 2))).shape == (0,) and f.Contains(np.empty((0, 2))).dtype == bool
    assert f.Distance(np.empty((0, 2))).shape == (0,) and f.Distance(np.empty((0, 3)), components="hv").shape == (0, 2)
    assert f.ClosestPoint(np.empty((0, 3))).shape == (0, 3)
    assert f.Contains([[0, 2], [5, 5]]).tolist() == [True, False]
    assert f.Distance(((5, 0), (0, 5))).tolist() == pytest.approx([2, 2])


def test_dtypes_and_layouts():
    f = flange()
    pts = rows(100, 9)
    expected = f.Distance(pts)
    assert f.Distance(pts.astype(np.float32)).tolist() == f.Distance(pts.astype(np.float32).astype(np.float64)).tolist()
    wide = np.zeros((100, 4))
    wide[:, ::2] = pts
    assert f.Distance(wide[:, ::2]).tolist() == expected.tolist()
    assert f.Distance(np.asfortranarray(pts)).tolist() == expected.tolist()
    assert f.Contains(np.array([[0, 2], [5, 5]])).tolist() == [True, False]


def test_bad_shapes_and_rows_raise():
    f = flange()
    with pytest.raises(ValueError, match=r"shape \(5, 4\)"):
        f.Contains(np.zeros((5, 4)))
    with pytest.raises(ValueError, match=r"shape \(2, 2, 2\)"):
        f.Distance(np.zeros((2, 2, 2)))
    with pytest.raises(ValueError, match=r"shape \(4,\)"):
        f.ClosestPoint(np.zeros(4))
    with pytest.raises(ValueError, match="array"):
        f.Distance([[0, 1], [2]])
    pts = rows(20, 10)
    pts[7, 1] = np.nan
    for call in (f.Contains, f.Distance, f.ClosestPoint):
        with pytest.raises(ValueError, match="row 7 is not finite"):
            call(pts)
    p3 = rows(20, 11, columns=3)
    p3[12, 2] = np.inf
    with pytest.raises(ValueError, match="row 12 is not finite"):
        f.Distance(p3)


def test_empty_things():
    assert not px.Shape().Contains(np.zeros((3, 2))).any()
    with pytest.raises(ValueError, match="nothing to measure from"):
        px.Shape().Distance(np.zeros((3, 2)))
    with pytest.raises(ValueError, match="nothing to measure from"):
        px.Shape().ClosestPoint(np.zeros((3, 2)))


def test_many_rows_on_threads_equal_small_batches():
    f = flange()
    pts = rows(30000, 12, -4, 4)
    parts = range(0, len(pts), 1000)
    assert f.Contains(pts).tolist() == np.concatenate([f.Contains(pts[k:k + 1000]) for k in parts]).tolist()
    assert f.Distance(pts).tolist() == np.concatenate([f.Distance(pts[k:k + 1000]) for k in parts]).tolist()
    assert f.ClosestPoint(pts).tolist() == np.concatenate([f.ClosestPoint(pts[k:k + 1000]) for k in parts]).tolist()


def test_contains_of_glued_faces():
    g = px.Glue([square(0, 0), square(1, 0)])
    assert g.Contains((0.5, 0.5)) and g.Contains((1.5, 0.5))
    assert g.Contains((1, 0.5)) and not g.Contains((1, 0.5), boundary=False)
    assert g.Contains(np.array([[0.5, 0.5], [1, 0.5]]), boundary=False).tolist() == [True, False]
    pts = rows(200, 14, -0.5, 2.5)
    assert g.Contains(pts).tolist() == (g.Distance(pts) == 0).tolist()
    crack = px.Glue([square(0, 0, 2), px.Line((1, 0.5), (1, 1.5))])
    assert len(crack.faces) == 1 and crack.Contains((0.5, 1)) and crack.Contains((1.5, 1))


def test_a_point_on_a_slit_inside_a_face():
    crack = px.Glue([square(0, 0, 2), px.Line((1, 0.5), (1, 1.5))])
    assert crack.Contains((1, 1)) and crack.Contains((1, 1), boundary=False) is True


def test_compound():
    a, b = flange(), square(4, -1, 2)
    w = px.Move((-5, 0)).Line((-5, 3)).Wire()
    comp = px.Compound([a, px.Compound([b]), w])
    pts = rows(300, 13, -6, 7)
    got = comp.Contains(pts)
    assert got.dtype == bool and got.tolist() == singles(comp.Contains, pts)
    assert got.tolist() == (a.Contains(pts) | b.Contains(pts)).tolist()
    d = comp.Distance(pts)
    assert d.tolist() == singles(comp.Distance, pts)
    assert d.tolist() == np.minimum(a.Distance(pts), np.minimum(b.Distance(pts), w.Distance(pts))).tolist()
    uv = comp.Distance(pts, components="hv")
    assert uv.shape == (300, 2) and uv.tolist() == [list(t) for t in singles(comp.Distance, pts, components="hv")]
    v = comp.Distance(pts, components="v")
    assert v.shape == (300,) and v.tolist() == singles(comp.Distance, pts, components="v")
    c = comp.ClosestPoint(pts)
    assert c.shape == (300, 2) and c.tolist() == [list(p) for p in singles(comp.ClosestPoint, pts)]
    p3 = rows(100, 15, -6, 7, columns=3)
    assert comp.Distance(p3).tolist() == singles(comp.Distance, p3)
    assert comp.ClosestPoint(p3).tolist() == [list(p) for p in singles(comp.ClosestPoint, p3)]
    assert comp.Contains((5, 0)) is True and comp.Contains((-5, 1)) is False
    assert comp.Distance((-6, 1)) == pytest.approx(1) and comp.ClosestPoint((-6, 1)) == pytest.approx((-5, 1))
    assert comp.Distance(a) == 0 and comp.Distance(square(10, 0)) == pytest.approx(4)
    assert comp.Distance(np.empty((0, 2))).shape == (0,) and comp.ClosestPoint(np.empty((0, 2))).shape == (0, 2)


def test_compound_on_several_planes():
    lifted = px.Compound([square(0, 0), square(0, 0).Translated((0, 0, 1))])
    for call in (lifted.Contains, lifted.Distance, lifted.ClosestPoint):
        with pytest.raises(ValueError, match="different planes"):
            call((0.5, 0.5))
        with pytest.raises(ValueError, match="different planes"):
            call(np.zeros((3, 2)))
    p3 = np.array([[0.5, 0.5, 1.0], [0.5, 0.5, 0.5], [3.0, 3.0, 3.0]])
    assert lifted.Contains(p3).tolist() == [True, False, False]
    assert lifted.Distance(p3).tolist() == pytest.approx([0, 0.5, math.sqrt(12)])
    assert lifted.ClosestPoint(p3)[2].tolist() == pytest.approx([1, 1, 1])


def test_empty_compound():
    empty = px.Compound()
    assert empty.Contains((0, 0)) is False and not empty.Contains(np.zeros((4, 2))).any()
    with pytest.raises(ValueError, match="nothing to measure from"):
        empty.Distance((0, 0))
    with pytest.raises(ValueError, match="nothing to measure from"):
        empty.ClosestPoint(np.zeros((2, 2)))
    with pytest.raises(ValueError, match="row 1 is not finite"):
        empty.Contains(np.array([[0, 0], [np.nan, 0]]))


def test_signed_distance_of_a_disc():
    disc = px.QuickSketch().Circle((1, 2), 2).Face()
    assert disc.Distance((1, 2), signed=True) == pytest.approx(-2)
    c = disc.center
    assert disc.Distance((c[0], c[1]), signed=True) < 0 and disc.Distance((6, 2), signed=True) == pytest.approx(3)
    assert disc.Distance((3, 2), signed=True) == 0.0 and disc.Distance((1, 4), signed=True) == 0.0
    pts = rows(300, 16, -2, 4)
    got = disc.Distance(pts, signed=True)
    assert got.dtype == np.float64 and got.shape == (300,) and got.tolist() == singles(disc.Distance, pts, signed=True)
    assert got.tolist() == pytest.approx((np.hypot(pts[:, 0] - 1, pts[:, 1] - 2) - 2).tolist(), abs=1e-12)


def test_signed_distance_of_a_flange():
    f = flange()
    pts = rows(500, 17, -4, 4)
    got = f.Distance(pts, signed=True)
    assert got.tolist() == singles(f.Distance, pts, signed=True)
    assert (got < 0).tolist() == f.Contains(pts, boundary=False).tolist()
    outside = got > 0
    assert got[outside].tolist() == f.Distance(pts[outside]).tolist()
    assert (f.Distance(on_boundary(), signed=True) == 0.0).all()
    assert f.Distance(np.array([[2.5, 0.0], [0.0, 0.0]]), signed=True).tolist() == pytest.approx([-0.25, 1])
    assert f.faces[0].Distance(pts, signed=True).tolist() == got.tolist()


def test_signed_distance_ignores_the_edges_between_faces():
    g = px.Glue([square(0, 0, 2), square(2, 0, 2)])
    assert g.Distance((2.01, 1), signed=True) == pytest.approx(-1) and g.Distance((2, 1), signed=True) == pytest.approx(-1)
    assert g.Distance((1, 0.5), signed=True) == pytest.approx(-0.5) and g.Distance((5, 1), signed=True) == pytest.approx(1)
    assert g.Distance(np.array([[2.01, 1], [2, 1]]), signed=True).tolist() == pytest.approx([-1, -1])
    with_wire = px.Glue([square(0, 0, 2), px.Line((1, 0.5), (1, 1.5)), px.Line((3, 0), (3, 2))])
    assert with_wire.Distance((1.1, 1), signed=True) == pytest.approx(-0.9) and with_wire.Distance((3, 1), signed=True) == pytest.approx(1)
    two = square(0, 0) + square(3, 0)
    pts = rows(200, 18, -1, 5)
    assert two.Distance(pts, signed=True).tolist() == singles(two.Distance, pts, signed=True)


def test_signed_distance_errors():
    f = flange()
    w = px.Move((0, 0)).Line((2, 0)).Line((2, 2)).Close().Wire()
    for x in (w, w.edges[0], w.vertices[0], px.Glue([w])):
        with pytest.raises(ValueError, match="no inside"):
            x.Distance((1, 1), signed=True)
    with pytest.raises(ValueError, match="components"):
        f.Distance((1, 1), signed=True, components="h")
    with pytest.raises(ValueError, match="point"):
        f.Distance(square(5, 5), signed=True)
    with pytest.raises(ValueError, match=r"\(h, v\)"):
        f.Distance((1, 1, 0), signed=True)
    with pytest.raises(ValueError, match=r"\(h, v\)"):
        f.Distance(np.zeros((4, 3)), signed=True)
    pts = rows(10, 19)
    pts[3, 0] = np.inf
    with pytest.raises(ValueError, match="row 3 is not finite"):
        f.Distance(pts, signed=True)
    with pytest.raises(ValueError, match="nothing to measure from"):
        px.Shape().Distance((0, 0), signed=True)


def test_signed_distance_of_a_compound():
    overlapping = px.Compound([square(0, 0, 2), px.Compound([square(1, 0, 2)]), px.Line((5, 0), (5, 2))])
    assert overlapping.Distance((1.5, 1), signed=True) == pytest.approx(-1)
    pts = rows(200, 20, -1, 4)
    got = overlapping.Distance(pts, signed=True)
    assert got.tolist() == singles(overlapping.Distance, pts, signed=True)
    assert got.tolist() == px.Unify([square(0, 0, 2), square(1, 0, 2)]).Distance(pts, signed=True).tolist()
    assert px.Compound([flange()]).Distance(pts, signed=True).tolist() == flange().Distance(pts, signed=True).tolist()
    with pytest.raises(ValueError, match="no inside"):
        px.Compound([px.Line((0, 0), (1, 0))]).Distance((0, 0), signed=True)
    with pytest.raises(ValueError, match="different planes"):
        px.Compound([square(0, 0), square(0, 0).Translated((0, 0, 1))]).Distance((0, 0), signed=True)
    with pytest.raises(ValueError, match="nothing to measure from"):
        px.Compound().Distance((0, 0), signed=True)


def shoelace(a):
    return 0.5 * float(np.sum(a[:-1, 0] * a[1:, 1] - a[1:, 0] * a[:-1, 1]))


def no_repeats(a):
    return bool(np.all(np.any(np.diff(a, axis=0) != 0, axis=1)))


def test_polylines_of_a_face_converge_to_its_area():
    f = flange()
    perimeter = sum(e.mass for e in f.edges)
    errors = []
    for tol in (1e-2, 1e-4, 1e-6):
        loops = f.Polylines(tol)
        assert len(loops) == 8 and all(a.dtype == np.float64 and a.ndim == 2 and a.shape[1] == 2 for a in loops)
        assert all((a[0] == a[-1]).all() and no_repeats(a) for a in loops)
        assert shoelace(loops[0]) > 0 and all(shoelace(a) < 0 for a in loops[1:])
        errors.append(abs(sum(shoelace(a) for a in loops) - f.mass))
        assert errors[-1] <= perimeter * tol
    assert errors[0] > errors[1] > errors[2]


def test_arc_samples_stay_within_tol():
    for e in (px.Arc((3, 0), (0, 3), (0, 0)).edges[0], px.Arc((3, 0), (0, -3), (0, 0)).edges[0],
              px.Arc((0, 3), (3, 0), (0, 0), reverse=True).edges[0]):
        for tol in (5, 0.5, 1e-3, 1e-7):
            a = e.Sample(tol)
            assert a.dtype == np.float64 and a.shape[1] == 2 and len(a) >= 2
            assert tuple(a[0]) == (e.start.x, e.start.y) and tuple(a[-1]) == (e.end.x, e.end.y)
            assert np.hypot(a[:, 0], a[:, 1]) == pytest.approx(3, abs=1e-12)
            mid = 0.5 * (a[1:] + a[:-1])
            assert (3 - np.hypot(mid[:, 0], mid[:, 1])).max() <= tol * (1 + 1e-9) + 1e-14
    assert len(px.Arc((3, 0), (0, 3), (0, 0)).edges[0].Sample(1e-3)) > len(px.Arc((3, 0), (0, 3), (0, 0)).edges[0].Sample(1e-2))


def test_sample_of_every_edge_kind():
    assert px.Line((0, 0), (2, 1)).edges[0].Sample(0.1).tolist() == [[0, 0], [2, 1]]
    cam = px.QuickSketch().Spline([(3, 0), (1, 2), (-1, 2), (-3, 0), (-1, -2), (1, -2)], periodic=True, start_from_localpos=False).Wire()
    s = cam.edges[0].edge.Sample(1e-3)
    assert isinstance(s, np.ndarray) and s.dtype == np.float64 and s.shape[1] == 2 and len(s) > 2
    assert cam.edges[0].Sample(1e-3).tolist() == s.tolist()
    for call in (cam.edges[0].Sample, cam.edges[0].edge.Sample, cam.Polylines, flange().Polylines):
        for tol in (0, -1, float("nan"), float("inf")):
            with pytest.raises(ValueError, match="tol"):
                call(tol)


def test_polylines_follow_the_walk_whatever_the_edge_directions():
    w = px.Wire()
    for a, b in [((0, 0), (1, 0)), ((1, 1), (1, 0)), ((1, 1), (0, 1)), ((0, 0), (0, 1))]:
        w.Add(px.LineEdge(a, b))
    [loop] = px.Face([w]).Polylines(0.1)
    assert len(loop) == 5 and (loop[0] == loop[-1]).all() and shoelace(loop) == 1.0
    assert all(abs(p - q).sum() == 1 for p, q in zip(loop[:-1], loop[1:]))
    [walk] = w.Polylines(0.1)
    assert len(walk) == 5 and (walk[0] == walk[-1]).all() and abs(shoelace(walk)) == 1.0
    holed = (square(0, 0, 4) - square(1, 1, 1)).faces[0]
    outer, hole = holed.Polylines(0.1)
    assert shoelace(outer) == 16 and shoelace(hole) == -1


def test_polylines_of_chains_shapes_and_splines():
    chain = px.Move((0, 0)).Line((2, 0)).Arc((3, 1), tangent=(1, 0)).Line((3, 3)).Wire()
    [a] = chain.Polylines(1e-3)
    assert {tuple(a[0]), tuple(a[-1])} == {(0.0, 0.0), (3.0, 3.0)} and no_repeats(a)
    g = px.Glue([square(0, 0), square(3, 0), px.Line((5, 0), (6, 1))])
    loops = g.Polylines(0.1)
    assert len(loops) == 3 and all(len(a) == 5 and shoelace(a) == 1.0 for a in loops[:2])
    assert sorted(map(tuple, loops[2].tolist())) == [(5.0, 0.0), (6.0, 1.0)]
    assert [x.tolist() for x in g.faces[1].Polylines(0.1)] in ([loops[0].tolist()], [loops[1].tolist()])
    cam = px.QuickSketch().Spline([(3, 0), (1, 2), (-1, 2), (-3, 0), (-1, -2), (1, -2)], periodic=True, start_from_localpos=False).Wire()
    face = px.Face([cam])
    [loop] = face.Polylines(1e-4)
    assert (loop[0] == loop[-1]).all() and no_repeats(loop) and shoelace(loop) > 0
    assert len(loop) == sum(len(e.Sample(1e-4)) - 1 for e in face.edges) + 1
    assert shoelace(loop) == pytest.approx(face.mass, abs=sum(e.mass for e in face.edges) * 1e-4 + 1e-4)


@pytest.mark.constraints
def test_polylines_of_sketches():
    sk = px.QuickSketch().Circle((0, 0), 3).Circle((0, 0), 1)
    assert [a.tolist() for a in sk.Polylines(1e-3)] == [a.tolist() for a in sk.Shape().Polylines(1e-3)]
    outer, bore = sk.Polylines(1e-3)
    assert shoelace(outer) > 0 > shoelace(bore)
    cs = px.Sketch()
    cs.Move((0, 0)).Line(2, 0).Line(0, 2).Line(-2, 0).Close()
    cs.Circle((1, 1), 0.5)
    loops = cs.Polylines(1e-4)
    assert [a.tolist() for a in loops] == [a.tolist() for a in cs.Shape().Polylines(1e-4)]
    assert len(loops) == 2 and shoelace(loops[0]) == pytest.approx(4) and shoelace(loops[1]) == pytest.approx(-math.pi / 4, abs=1e-3)


def test_polylines_of_compounds():
    a, b, w = flange(), square(5, 0), px.Line((8, 0), (9, 1))
    comp = px.Compound([a, px.Compound([b, px.Compound(), px.Shape()]), w])
    expected = a.Polylines(1e-3) + b.Polylines(1e-3) + w.Polylines(1e-3)
    assert [x.tolist() for x in comp.Polylines(1e-3)] == [x.tolist() for x in expected]
    assert px.Compound().Polylines(1e-3) == []
    with pytest.raises(ValueError, match="different planes"):
        px.Compound([square(0, 0), square(0, 0).Translated((0, 0, 1))]).Polylines(1e-3)


def limit_of(sample, kind):
    with pytest.raises(ValueError, match="below the rounding of this " + kind) as e:
        sample(1e-300)
    return float(str(e.value).rsplit(", ", 1)[1])


def test_sampling_down_to_the_rounding():
    t = 1e-3
    arc = px.Arc((1, 0), (math.cos(t), math.sin(t)), (0, 0)).edges[0]
    limit = limit_of(arc.Sample, "arc")
    assert 1e-15 < limit < 1e-13
    assert 1000 < len(arc.Sample(limit * (1 + 1e-5))) < 10000
    with pytest.raises(ValueError, match=r"Sample\(tol\): tol .* below the rounding of this arc"):
        arc.Sample(limit * (1 - 1e-5))
    with pytest.raises(ValueError, match=r"Polylines\(tol\): tol .* below the rounding of this arc"):
        arc.Wire().Polylines(limit * (1 - 1e-5))
    ring = [(1000 + 1e-3 * math.cos(a), 1e-3 * math.sin(a)) for a in np.linspace(0, 2 * math.pi, 7)[:-1]]
    piece = px.Spline(ring, periodic=True).edges[0]
    for sample in (piece.Sample, piece.edge.Sample):
        limit = limit_of(sample, "spline")
        assert 1e-12 < limit < 1e-10
        assert 1000 < len(sample(limit * (1 + 1e-5))) < 100000
        with pytest.raises(ValueError, match="below the rounding of this spline"):
            sample(limit * (1 - 1e-5))
    assert len(px.Line((0, 0), (1e9, 0)).edges[0].Sample(1e-300)) == 2


def test_polyline_through_the_rows():
    t = np.linspace(0, 2 * math.pi, 2001)[:-1]
    circle = np.column_stack([2 * np.cos(t), 2 * np.sin(t)])
    w = px.FromPolyline(circle, closed=True)
    assert isinstance(w, px.Wire) and len(w.edges) == 2000 and w.IsClosed()
    closed = np.vstack([circle, circle[:1]])
    assert w.Face().mass == pytest.approx(shoelace(closed), rel=1e-12)
    assert np.array_equal(np.unique(w.Polylines(1)[0], axis=0), np.unique(circle, axis=0))
    again = px.FromPolyline(closed, closed=True)
    assert len(again.edges) == 2000 and again.Face().mass == w.Face().mass


def test_polyline_skips_repeated_points_and_closes():
    w = px.FromPolyline([(0, 0), (0, 0), (1, 0), (1, 0), (1, 1)])
    assert [(e.start.x, e.start.y, e.end.x, e.end.y) for e in w.edges] == [(0, 0, 1, 0), (1, 0, 1, 1)]
    square = [(0, 0), (1, 0), (1, 1), (0, 1)]
    for pts in (square, square + [(0, 0)]):
        w = px.FromPolyline(pts, closed=True)
        assert len(w.edges) == 4 and w.Face().mass == pytest.approx(1)
    assert len(px.FromPolyline(square + [(0, 0)]).edges) == 4 and len(px.FromPolyline(square).edges) == 3
    with pytest.raises(ValueError, match="overlap"):
        px.FromPolyline([(0, 0), (1, 0)], closed=True)


def test_polyline_properties_and_axes():
    w = px.FromPolyline(np.array([[0, 0], [1, 0], [1, 1]]), closed=True, name="contour", maxh=0.1, col=(1, 0, 0))
    assert len(w.edges) == 3 and all(e.name == "contour" and e.maxh == 0.1 for e in w.edges)
    assert w.edges.name == "contour" and list(w.edges[2].col) == [1, 0, 0, 1]
    lifted = px.FromPolyline([(0, 0), (1, 2)], plane=px.YZ)
    assert lifted.Normal() == pytest.approx((1, 0, 0)) and lifted.ToXYZ((1, 2)) == pytest.approx((0, 1, 2))
    assert lifted.Normal() == pytest.approx(px.Line((0, 0), (1, 2), plane=px.YZ).Normal())
    assert px.FromPolyline([(0, 0), (1, 0)], id="cut").id == "cut"


def test_polyline_errors():
    with pytest.raises(ValueError, match=r"shape \(3, 4\)"):
        px.FromPolyline(np.zeros((3, 4)))
    with pytest.raises(ValueError, match=r"shape \(2,\)"):
        px.FromPolyline((0, 1))
    with pytest.raises(ValueError, match="row 2 is not finite"):
        px.FromPolyline(np.array([[0, 0], [1, 0], [np.nan, 1], [2, 2]]))
    for pts in ([(1, 1), (1, 1)], np.empty((0, 2))):
        with pytest.raises(ValueError, match="needs two"):
            px.FromPolyline(pts)
    with pytest.raises(ValueError, match="needs two"):
        px.FromPolyline([(1, 1)] * 3, closed=True)


def test_polyline_of_many_points():
    t = np.linspace(0, 1, 100000)
    w = px.FromPolyline(np.column_stack([t, np.sin(20 * t)]))
    assert len(w.edges) == 99999 and w.edges[-1].end.x == 1.0
