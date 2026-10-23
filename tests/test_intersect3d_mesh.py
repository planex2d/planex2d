# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px
from planex._geom import xy

np = pytest.importorskip("numpy")

MAXH = 0.2
OBLIQUE = dict(origin=(1.0, 0.5, 0.5), normal=(0.3, 0.2, 1.0))
# solid, plane, faces, holes, radius of its curved faces, the sign of (area of the mesh cut - area of the exact cut),
# the length of the cut's straight edges on a curved face
CASES = {
    "bored box oblique": ("bored", OBLIQUE, 1, 1, 0.25, 1, 0.0),
    "bored box along the bore": ("bored", dict(origin=(1.0, 0.5, 0.5), normal=(0.0, 1.0, 0.0)), 2, 0, 0.25, 1, 2.0),
    "bored box on its top face": ("bored", dict(origin=(0.0, 0.0, 1.0), normal=(0.0, 0.0, 1.0)), 1, 1, 0.25, 1, 0.0),
    "bored box on its bottom face": ("bored", dict(origin=(0.0, 0.0, 0.0), normal=(0.0, 0.0, 1.0)), 1, 1, 0.25, 1, 0.0),
    "cylinder oblique": ("cylinder", dict(origin=(0.0, 0.0, 0.5), normal=(0.1, 0.0, 1.0)), 1, 0, 0.5, -1, 0.0),
    "cylinder parallel to xy": ("cylinder", dict(origin=(0.0, 0.0, 0.43), normal=(0.0, 0.0, 1.0)), 1, 0, 0.5, -1, 0.0),
}


@pytest.fixture(scope="module")
def ngs():
    return pytest.importorskip("ngsolve")


@pytest.fixture(scope="module")
def occ(ngs):
    from netgen import occ

    return occ


@pytest.fixture(scope="module")
def solids(ngs, occ):
    """a 2 x 1 x 1 box with a bore of radius 0.25 along z (its left face and the bore named), and a cylinder of radius
    0.5; each with its straight mesh"""
    box = occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(2, 1, 1))
    box.faces.Min(occ.X).name = "left"
    bore = occ.Cylinder(occ.Pnt(1, 0.5, 0), occ.Z, r=0.25, h=1)
    bore.faces.name = "bore"
    bored = box - bore
    cylinder = occ.Cylinder(occ.Pnt(0, 0, 0), occ.Z, r=0.5, h=1)
    return {name: (solid, ngs.Mesh(occ.OCCGeometry(solid).GenerateMesh(maxh=MAXH)))
            for name, solid in (("bored", bored), ("cylinder", cylinder))}


@pytest.fixture(scope="module")
def two_materials(ngs, occ):
    """air in a 2 x 1 x 1 box around a steel core, a cylinder of radius 0.25 from z = 0.2 to 0.8, glued"""
    core = occ.Cylinder(occ.Pnt(1, 0.5, 0.2), occ.Z, r=0.25, h=0.6)
    air = occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(2, 1, 1)) - core
    air.mat("air")
    core.mat("steel")
    solid = occ.Glue([air, core])
    return solid, ngs.Mesh(occ.OCCGeometry(solid).GenerateMesh(maxh=MAXH))


@pytest.fixture
def mpl():
    matplotlib = pytest.importorskip("matplotlib")
    matplotlib.use("Agg")
    yield
    import matplotlib.pyplot as plt
    plt.close("all")


def frame(plane):
    return (np.asarray(a, dtype=float) for a in (plane.origin, plane.h, plane.v, plane.normal))


def holes(shape):
    return len(shape.wires) - len(shape.faces)


def curved_length(shape):
    from planex._plot import _length

    return sum(_length(e.edge) for e in shape.edges if not isinstance(e.edge, px.LineEdge))


def sagitta(radius):
    """how far a facet of the straight mesh lies from a curved face of this radius at most: its edges up to 1.5 maxh"""
    return (1.5 * MAXH) ** 2 / (8 * radius)


def segments(face):
    return {frozenset((xy(e.start), xy(e.end))) for e in face.edges}


def closed(shape):
    from planex._plot import _free_edges

    return all(w.IsClosed() for w in shape.wires) and not _free_edges(list(shape.edges))


@pytest.mark.parametrize("case", list(CASES))
def test_the_cut_of_a_mesh_has_the_faces_holes_and_area_of_the_cut_of_its_solid(case, solids):
    name, plane, faces, hole_count, radius, sign, straight = CASES[case]
    solid, mesh = solids[name]
    by_mesh, by_solid = px.Intersect3d(px.Plane(**plane), mesh), px.Intersect3d(px.Plane(**plane), solid)
    assert len(by_mesh.faces) == len(by_solid.faces) == faces
    assert holes(by_mesh) == holes(by_solid) == hole_count
    assert closed(by_mesh)
    error = sign * (by_mesh.mass - by_solid.mass)
    assert -1e-9 <= error <= (curved_length(by_solid) + straight) * sagitta(radius) + 1e-9
    assert by_mesh.mass > 0.5


def test_the_edges_are_named_after_the_boundaries_of_their_facets(solids):
    solid, mesh = solids["bored"]
    plane = px.Plane(**OBLIQUE)
    by_mesh = px.Intersect3d(plane, mesh)
    assert {e.name for e in by_mesh.edges} == {e.name for e in px.Intersect3d(plane, solid).edges} == {"left", "bore",
                                                                                                      None}
    o, u, v, _ = frame(px.Plane(by_mesh))
    for e in by_mesh.edges:
        ends = np.array([o + p[0] * u + p[1] * v for p in (xy(e.start), xy(e.end))])
        if e.name == "left":
            np.testing.assert_allclose(ends[:, 0], 0.0, rtol=0, atol=1e-9)
        if e.name == "bore":
            r = np.hypot(ends[:, 0] - 1.0, ends[:, 1] - 0.5)
            assert (r <= 0.25 + 1e-9).all() and (r >= 0.25 - sagitta(0.25)).all()


def test_two_materials_give_two_faces_named_after_them_glued(ngs, two_materials):
    solid, mesh = two_materials
    volume = {m: ngs.Integrate(ngs.CF(1), mesh, definedon=mesh.Materials(m)) for m in ("air", "steel")}
    core = math.pi * 0.25 ** 2 * 0.6
    assert 0.85 * core < volume["steel"] <= core + 1e-12
    assert volume["air"] + volume["steel"] == pytest.approx(2.0, abs=1e-9)
    plane = px.Plane(**OBLIQUE)
    by_mesh, by_solid = px.Intersect3d(plane, mesh), px.Intersect3d(plane, solid)
    assert sorted(f.name for f in by_mesh.faces) == sorted(f.name for f in by_solid.faces) == ["air", "steel"]
    assert holes(by_mesh) == holes(by_solid) and closed(by_mesh)
    area = {f.name: px.Shape(f.Copy()).mass for f in by_mesh.faces}
    exact = {f.name: px.Shape(f.Copy()).mass for f in by_solid.faces}
    bound = curved_length(by_solid) * sagitta(0.25) + 1e-9
    assert -1e-9 <= exact["steel"] - area["steel"] <= bound and -1e-9 <= area["air"] - exact["air"] <= bound
    assert by_mesh.mass == pytest.approx(by_solid.mass, abs=1e-9)
    steel, = [f for f in by_mesh.faces if f.name == "steel"]
    air, = [f for f in by_mesh.faces if f.name == "air"]
    assert segments(steel) <= segments(air)


def test_a_field_on_the_cut_of_its_mesh_has_no_gap(ngs, solids, mpl):
    from planex._plot_mesh import _pixels

    solid, mesh = solids["cylinder"]
    cut = px.Intersect3d(px.Plane(**CASES["cylinder oblique"][1]), mesh)
    f = ngs.x * ngs.y * ngs.z
    ax = px.PlotField(f, mesh, cut, samples=150)
    z = np.ma.filled(ax.images[0].get_array().astype(float), np.nan).ravel()
    xs, ys, _ = _pixels(cut.bounding_box, 150)
    gx, gy = np.meshgrid(xs, ys)
    uv = np.c_[gx.ravel(), gy.ravel()]
    inside = cut.Contains(uv)
    o, u, v, _ = frame(px.Plane(cut))
    p = o + uv[inside, :1] * u + uv[inside, 1:] * v
    assert inside.sum() > 5000 and np.isnan(z[~inside]).all()
    np.testing.assert_allclose(z[inside], p[:, 0] * p[:, 1] * p[:, 2], rtol=0, atol=1e-12)


def test_errors_and_a_plane_that_misses_the_mesh(ngs, solids):
    _, mesh = solids["bored"]
    with pytest.warns(px.PlanexWarning, match="misses the shape"):
        missed = px.Intersect3d(px.Plane(origin=(0.0, 0.0, 5.0), normal=(0.0, 0.0, 1.0)), mesh)
    assert len(missed.faces) == 0 and len(missed.edges) == 0
    flat = ngs.Mesh(px.OCCGeometry(px.QuickSketch().Rectangle(2, 1).Face()).GenerateMesh(maxh=0.5))
    with pytest.raises(ValueError, match="dim 2"):
        px.Intersect3d(px.XY, flat)
    lid = px.QuickSketch(px.Plane(origin=(0, 0, 1), normal=(0, 0, 1))).Rectangle(2, 1).Face()
    surface = ngs.Mesh(px.OCCGeometry(lid).GenerateMesh(maxh=0.5))
    assert surface.dim == 3 and surface.ne == 0
    with pytest.raises(ValueError, match="surface mesh"):
        px.Intersect3d(px.Plane(origin=(0, 0, 1), normal=(0, 1, 0)), surface)


def test_a_plane_on_a_mesh_without_geometry_cuts_the_mesh_itself(ngs, mpl):
    from netgen.csg import CSGeometry, OrthoBrick, Pnt

    geometry = CSGeometry()
    geometry.Add(OrthoBrick(Pnt(0, 0, 0), Pnt(2, 1, 1)))
    mesh = ngs.Mesh(geometry.GenerateMesh(maxh=0.5))
    assert px.PlotMesh(mesh, px.Plane(**OBLIQUE)) is not None
    cut = px.Intersect3d(px.Plane(**OBLIQUE), mesh)
    assert len(cut.faces) == 1 and closed(cut)
    assert px.PlotMesh(mesh, cut) is not None
