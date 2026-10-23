# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pathlib
import warnings

import numpy as np
import pytest

import planex as px

ngsolve = pytest.importorskip("ngsolve")
from netgen import occ  # noqa: E402
from ngsolve import H1, GridFunction, Mesh, x, y  # noqa: E402


def _mesh(periodic, maxh=0.1):
    face = occ.WorkPlane().Rectangle(1, 1).Face()
    face.edges.Min(occ.X).name = "left"
    face.edges.Max(occ.X).name = "right"
    face.edges.Min(occ.Y).name = "bottom"
    face.edges.Max(occ.Y).name = "top"
    if periodic:
        face.edges.Min(occ.X).Identify(face.edges.Max(occ.X), "px", occ.IdentificationType.PERIODIC)
        face.edges.Min(occ.Y).Identify(face.edges.Max(occ.Y), "py", occ.IdentificationType.PERIODIC)
    return Mesh(occ.OCCGeometry(face, dim=2).GenerateMesh(maxh=maxh))


def _field(mesh):
    gf = GridFunction(H1(mesh, order=2))
    gf.Set(0.3 + 0.05 * x - y)
    return gf


def _points(shape):
    return np.array([(p.x, p.y) for e in shape.edges for p in (e.start, e.end)])


def _area(shape):
    total = 0.0
    for e in shape.edges:
        total += e.start.x * e.end.y - e.end.x * e.start.y
    return abs(0.5 * total)


def _side_ys(shape, xs):
    pts = _points(shape)
    return sorted(set(np.round(pts[np.abs(pts[:, 0] - xs) < 1e-12, 1], 15)))


def _interface_ends(shape, xs):
    ys = []
    for e in shape.edges:
        if e.name != "interface":
            continue
        for p in (e.start, e.end):
            if abs(p.x - xs) < 1e-12:
                ys.append(p.y)
    return ys


def test_crossings_match_on_identified_sides():
    gf = _field(_mesh(True))
    on = px.FromLevelSet(gf, 0.05, periodic=True)
    off = px.FromLevelSet(gf, 0.05, periodic=False)
    left, right = _interface_ends(on, 0.0), _interface_ends(on, 1.0)
    assert len(left) == len(right) == 1
    (a, b, trafo, _), = on.identifications.values()
    shift = tuple(trafo.vector) if (a, b) == ("left", "right") else tuple(-v for v in trafo.vector)
    assert np.allclose(shift, (1.0, 0.0), atol=1e-14, rtol=0)
    assert right[0] == left[0] + shift[1]
    assert abs(_area(on) - _area(off)) < 1e-3
    left, right = _interface_ends(off, 0.0), _interface_ends(off, 1.0)
    assert abs(left[0] - right[0]) > 1e-3


def test_translated_copies_share_their_vertices():
    shape = px.FromLevelSet(_field(_mesh(True)), 0.05, periodic=True)
    glued = px.Glue([shape, shape.Translated((1.0, 0.0))])
    assert min(np.hypot(e.end.x - e.start.x, e.end.y - e.start.y) for e in glued.edges) > 1e-9


def test_translated_by_the_identification_matches_exactly():
    shape = px.FromLevelSet(_field(_mesh(True)), 0.05, periodic=True)
    (a, b, trafo, _), = shape.identifications.values()
    vec = tuple(trafo.vector) if (a, b) == ("left", "right") else tuple(-v for v in trafo.vector)
    moved = shape.Translated(vec)
    assert len(_interface_ends(moved, 1.0)) == 1
    assert _interface_ends(shape, 1.0) == _interface_ends(moved, 1.0)


def test_shape_carries_identifications():
    shape = px.FromLevelSet(_field(_mesh(True)), 0.05, periodic=True)
    ids = shape.identifications
    assert len(ids) == 1
    (name, (a, b, trafo, _)), = ids.items()
    assert name == "periodic.1"
    assert {a, b} == {"left", "right"}
    expected = (1.0, 0.0) if (a, b) == ("left", "right") else (-1.0, 0.0)
    assert np.allclose(trafo.vector, expected)
    assert len(px.FromLevelSet(_field(_mesh(True)), 0.05, periodic=False).identifications) == 0


def test_mesh_without_identifications_is_unchanged():
    gf = _field(_mesh(False))
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        on = px.FromLevelSet(gf, 0.05, periodic=True)
    off = px.FromLevelSet(gf, 0.05, periodic=False)
    assert np.array_equal(_points(on), _points(off))
    assert len(on.identifications) == 0


def _one_name_mesh(maxh=0.1):
    face = occ.WorkPlane().Rectangle(1, 1).Face()
    face.edges.Min(occ.X).Identify(face.edges.Max(occ.X), "px", occ.IdentificationType.PERIODIC)
    face.edges.Min(occ.Y).Identify(face.edges.Max(occ.Y), "py", occ.IdentificationType.PERIODIC)
    return Mesh(occ.OCCGeometry(face, dim=2).GenerateMesh(maxh=maxh))


def test_sides_that_share_one_name_are_paired_by_the_identifications():
    gf = GridFunction(H1(_one_name_mesh(), order=2))
    gf.Set(0.3 + 0.05 * x - y)
    with pytest.warns(px.PlanexWarning, match=r"default\.periodic\.\da"):
        shape = px.FromLevelSet(gf, 0.05)
    # the region is below y = 0.3: only the left and right sides are identified in it
    (name, (a, b, trafo, _)), = shape.identifications.items()
    k = a[-2]
    assert {a, b} == {f"default.periodic.{k}a", f"default.periodic.{k}b"}
    assert {a, b} <= {e.name for e in shape.edges}
    assert np.allclose(np.abs(trafo.vector), (1.0, 0.0))
    assert _side_ys(shape, 0.0) == _side_ys(shape, 1.0)


def test_the_level_set_page_cell_is_periodic_by_default():
    data = pathlib.Path(__file__).parent / "data"
    mesh = Mesh(str(data / "auxetic_levelset.vol.gz"))
    phi = GridFunction(H1(mesh, order=1))
    phi.vec.FV().NumPy()[:] = np.load(data / "auxetic_levelset.npy")
    with pytest.warns(px.PlanexWarning, match="periodic identification"):
        cell = px.FromLevelSet(phi, edge_length=1e-3)
    names = {e.name for e in cell.edges}
    assert cell.identifications and all({a, b} <= names for a, b, _, _ in cell.identifications.values())
    assert cell.mass == pytest.approx(px.FromLevelSet(phi, edge_length=1e-3, periodic=False).mass, rel=1e-6)
