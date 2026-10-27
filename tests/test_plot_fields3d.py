# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px

np = pytest.importorskip("numpy")

OBLIQUE = dict(origin=(1.0, 0.5, 0.5), normal=(0.3, 0.2, 1.0))
CUTS = {
    "box oblique": ("box", OBLIQUE),
    "box parallel to xy": ("box", dict(origin=(0.0, 0.0, 0.43), normal=(0.0, 0.0, 1.0))),
    "cylinder oblique": ("cylinder", dict(origin=(0.0, 0.0, 0.5), normal=(0.1, 0.0, 1.0))),
}


@pytest.fixture(scope="module")
def ngs():
    return pytest.importorskip("ngsolve")


@pytest.fixture(scope="module")
def occ(ngs):
    from netgen import occ

    return occ


@pytest.fixture(scope="module")
def box(ngs, occ):
    solid = occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(2, 1, 1))
    return solid, ngs.Mesh(occ.OCCGeometry(solid).GenerateMesh(maxh=0.3))


@pytest.fixture(scope="module")
def cylinder(ngs, occ):
    solid = occ.Cylinder(occ.Pnt(0, 0, 0), occ.Z, r=0.5, h=1)
    return solid, ngs.Mesh(occ.OCCGeometry(solid).GenerateMesh(maxh=0.2))


@pytest.fixture(params=list(CUTS))
def cut(request, box, cylinder):
    solid, plane = CUTS[request.param]
    solid, mesh = {"box": box, "cylinder": cylinder}[solid]
    return px.Intersect3d(px.Plane(**plane), solid), mesh, request.param.startswith("box")


@pytest.fixture
def mpl():
    matplotlib = pytest.importorskip("matplotlib")
    matplotlib.use("Agg")
    yield
    import matplotlib.pyplot as plt
    plt.close("all")


@pytest.fixture(params=["matplotlib", "plotly"])
def backend(request):
    if request.param == "plotly":
        pytest.importorskip("plotly")
        yield "plotly"
        return
    matplotlib = pytest.importorskip("matplotlib")
    matplotlib.use("Agg")
    yield "matplotlib"
    import matplotlib.pyplot as plt
    plt.close("all")


def frame(plane):
    return (np.asarray(a, dtype=float) for a in (plane.origin, plane.h, plane.v, plane.normal))


def located(shape, mesh, n):
    """as Plot locates the pixels of a cut: the pixel centres xs, ys; per pixel whether it lies inside the cut and
    whether the mesh has it; the 3d points and the mesh points of those found"""
    from planex._plot_fields import MESH_TOL
    from planex._plot_mesh import _pixels

    xs, ys, _ = _pixels(shape.bounding_box, n)
    gx, gy = np.meshgrid(xs, ys)
    uv = np.c_[gx.ravel(), gy.ravel()]
    inside = shape.Contains(uv)
    o, u, v, _ = frame(px.Plane(shape))
    p = o + uv[inside, :1] * u + uv[inside, 1:] * v
    mips = mesh(*(np.ascontiguousarray(c) for c in p.T), tol=MESH_TOL)
    hit = mips["nr"] >= 0
    found = np.zeros(len(uv), dtype=bool)
    found[np.flatnonzero(inside)[hit]] = True
    return xs, ys, inside, found, p[hit], mips[hit]


def raster(shown, backend):
    if backend == "plotly":
        heat, = [t for t in shown.data if t.type == "heatmap"]
        return np.asarray(heat.z, dtype=float)
    im, = shown.images
    return np.ma.filled(im.get_array().astype(float), np.nan)


def arrows(shown, backend):
    """the arrows drawn: their starts and their vectors"""
    if backend == "plotly":
        trace, = [t for t in shown.data if t.name == "vectors"]
        x, y = list(trace.x), list(trace.y)
        n = len(x) // 7
        assert len(x) == 7 * n
        start = np.array([x[0:3 * n:3], y[0:3 * n:3]], dtype=float).T
        end = np.array([x[1:3 * n:3], y[1:3 * n:3]], dtype=float).T
        return start, end - start
    from matplotlib.quiver import Quiver

    quiver, = [c for c in shown.collections if isinstance(c, Quiver)]
    return np.c_[quiver.X, quiver.Y], np.c_[np.asarray(quiver.U), np.asarray(quiver.V)]


def drawn_mesh(shown, backend):
    """the mesh lines drawn as (m, 2, 2), and whether they lie above the outline"""
    if backend == "plotly":
        names = [t.name for t in shown.data]
        trace = shown.data[names.index("mesh")]
        x = np.array([np.nan if a is None else a for a in trace.x], dtype=float)
        y = np.array([np.nan if a is None else a for a in trace.y], dtype=float)
        assert len(x) % 3 == 0 and np.isnan(x[2::3]).all()
        outline = [i for i, name in enumerate(names) if name in ("edges", "lines", "arcs", "splines")]
        return np.c_[x, y].reshape(-1, 3, 2)[:, :2], names.index("mesh") > max(outline)
    from matplotlib.collections import LineCollection

    mesh, = [c for c in shown.collections if c.get_label() == "mesh"]
    outline = [c for c in shown.collections if isinstance(c, LineCollection) and c is not mesh]
    lines = np.array(mesh.get_segments(), dtype=float).reshape(-1, 2, 2)
    return lines, mesh.get_zorder() > max(c.get_zorder() for c in outline)


def tets_of(ngs, mesh):
    points = np.array([v.point for v in mesh.vertices], dtype=float)
    tets = np.array([[v.nr for v in el.vertices] for el in mesh.Elements(ngs.VOL)])
    assert tets.shape[1] == 4
    return points, tets


def on_triangles(p, tris, tol):
    """(m, f): the points p (m, 3) lie on the triangles (f, 3, 3)"""
    a = tris[None, :, 0]
    e0, e1, w = tris[None, :, 1] - a, tris[None, :, 2] - a, p[:, None] - a
    d00, d01, d11 = (e0 * e0).sum(-1), (e0 * e1).sum(-1), (e1 * e1).sum(-1)
    d20, d21 = (w * e0).sum(-1), (w * e1).sum(-1)
    den = d00 * d11 - d01 * d01
    beta, gamma = (d11 * d20 - d01 * d21) / den, (d00 * d21 - d01 * d20) / den
    off = w - beta[..., None] * e0 - gamma[..., None] * e1
    return ((beta >= -tol) & (gamma >= -tol) & (beta + gamma <= 1 + tol)
            & (np.linalg.norm(off, axis=-1) <= tol * np.sqrt(d00)))


def check_cut_lines(lines, points, tets, plane):
    """every line lies on a tet face touching the plane, and every face crossing it (no corner on it) has its line"""
    assert len(lines) > 0 and np.isfinite(lines).all()
    size = np.ptp(points, axis=0).max()
    o, u, v, n = frame(plane)
    tris = points[tets[:, [[0, 1, 2], [0, 1, 3], [0, 2, 3], [1, 2, 3]]]].reshape(-1, 3, 3)
    d = (tris - o) @ n
    tol = 1e-9 * size
    touching = (d.min(axis=1) <= tol) & (d.max(axis=1) >= -tol)
    ends = o + lines[..., :1] * u + lines[..., 1:] * v
    on = on_triangles(ends[:, 0], tris[touching], 1e-7) & on_triangles(ends[:, 1], tris[touching], 1e-7)
    assert on.any(axis=1).all(), "a line on no element face"
    strict = (d.min(axis=1) < -tol) & (d.max(axis=1) > tol) & (np.abs(d) > tol).all(axis=1)
    tris, d = tris[strict], d[strict]
    cuts = []
    for j in range(3):
        a, b, da, db = tris[:, j], tris[:, (j + 1) % 3], d[:, j], d[:, (j + 1) % 3]
        crossing = da * db < 0
        t = np.where(crossing, da / np.where(crossing, da - db, 1.0), np.nan)
        cuts.append(a + t[:, None] * (b - a))
    cuts = np.stack(cuts, axis=1)
    known = ~np.isnan(cuts[..., 0])
    assert (known.sum(axis=1) == 2).all()
    expected = cuts[known].reshape(-1, 2, 3) - o
    expected = np.stack([expected @ u, expected @ v], axis=-1)
    assert drawn(expected, lines, 1e-9 * size).all(), "a face crossing the plane without its line"


def drawn(expected, lines, gap):
    """which of the (k, 2, 2) segments are among the lines, either way round"""
    same = ((np.abs(expected[:, None, 0] - lines[None, :, 0]).max(-1) < gap)
            & (np.abs(expected[:, None, 1] - lines[None, :, 1]).max(-1) < gap))
    flipped = ((np.abs(expected[:, None, 0] - lines[None, :, 1]).max(-1) < gap)
               & (np.abs(expected[:, None, 1] - lines[None, :, 0]).max(-1) < gap))
    return (same | flipped).any(axis=1)


def test_cut_pixels_are_the_field_at_their_3d_points(ngs, cut, backend):
    shape, mesh, exact_mesh = cut
    f = ngs.x * ngs.y * ngs.z
    z = raster(px.PlotField(f, mesh, shape, samples=200, backend=backend), backend).ravel()
    xs, ys, inside, found, p, mips = located(shape, mesh, 200)
    assert z.size == len(xs) * len(ys) and max(len(xs), len(ys)) == 200
    expected = np.full(z.size, np.nan)
    expected[found] = np.asarray(f(mips))[:, 0]
    np.testing.assert_array_equal(z, expected)
    assert np.abs(z[found] - p[:, 0] * p[:, 1] * p[:, 2]).max() < 1e-12
    assert found.sum() > 0.9 * inside.sum()
    if exact_mesh:
        assert (found == inside).all()
    gf = ngs.GridFunction(ngs.H1(mesh, order=3))
    gf.Set(f)
    z = raster(px.PlotField(gf, shape, samples=200, backend=backend), backend).ravel()
    xs, ys, inside, found, p, mips = located(shape, mesh, 200)
    expected = np.full(z.size, np.nan)
    expected[found] = np.asarray(gf(mips))[:, 0]
    np.testing.assert_array_equal(z, expected)
    assert np.abs(z[found] - p[:, 0] * p[:, 1] * p[:, 2]).max() < 1e-6


def test_cut_pixels_are_400_along_the_longer_side_by_default(ngs, box, mpl):
    solid, mesh = box
    shape = px.Intersect3d(px.Plane(**OBLIQUE), solid)
    z = raster(px.PlotField(ngs.z + 0, mesh, shape), "matplotlib")
    xs, ys, inside, found, p, _ = located(shape, mesh, 400)
    assert z.shape == (len(ys), len(xs)) and max(z.shape) == 400
    assert np.abs(z.ravel()[found] - p[:, 2]).max() < 1e-12 and np.isnan(z.ravel()[~found]).all()


def test_cut_vector_field_colour_is_its_3d_length_arrows_its_part_in_the_plane(ngs, box, backend):
    solid, mesh = box
    shape = px.Intersect3d(px.Plane(**OBLIQUE), solid)
    field = ngs.CF((ngs.y, -ngs.x, ngs.z))
    shown = px.PlotField(field, mesh, shape, samples=200, backend=backend, vectors=True)
    z = raster(shown, backend).ravel()
    xs, ys, inside, found, p, mips = located(shape, mesh, 200)
    expected = np.full(z.size, np.nan)
    expected[found] = np.linalg.norm(np.asarray(field(mips)), axis=1)
    np.testing.assert_array_equal(z, expected)
    assert np.abs(z[found] - np.linalg.norm(p, axis=1)).max() < 1e-12
    start, drawn_arrows = arrows(shown, backend)
    assert 50 < len(start) <= 400 and shape.Contains(start).all()
    o, u, v, _ = frame(px.Plane(shape))
    q = o + start[:, :1] * u + start[:, 1:] * v
    in_plane = np.c_[q[:, 1], -q[:, 0], q[:, 2]] @ np.c_[u, v]
    (u0, v0), (u1, v1) = shape.bounding_box
    step = max(u1 - u0, v1 - v0) / 20
    longest = np.linalg.norm(in_plane, axis=1).max()
    np.testing.assert_allclose(drawn_arrows, in_plane * (step / longest), rtol=0, atol=1e-9)


def test_cut_vectors_need_a_real_field_of_three_components(ngs, box, mpl):
    solid, mesh = box
    shape = px.Intersect3d(px.Plane(**OBLIQUE), solid)
    flat = ngs.CF((ngs.y, -ngs.x))
    with pytest.raises(ValueError, match="three components"):
        px.PlotField(flat, mesh, shape, vectors=True)
    with pytest.raises(ValueError, match="webgpu"):
        px.PlotField(ngs.GridFunction(ngs.VectorH1(mesh, order=1, complex=True)), shape, vectors=True)
    z = raster(px.PlotField(flat, mesh, shape, samples=100), "matplotlib").ravel()
    _, _, _, found, p, _ = located(shape, mesh, 100)
    assert np.abs(z[found] - np.linalg.norm(p[:, :2], axis=1)).max() < 1e-12
    rect = px.QuickSketch().Rectangle(2, 1).Face()
    mesh2d = ngs.Mesh(px.OCCGeometry(rect).GenerateMesh(maxh=0.5))
    with pytest.raises(ValueError, match="two components"):
        px.PlotField(ngs.CF((ngs.y, -ngs.x, ngs.x)), mesh2d, rect, vectors=True)


def test_straight_cylinder_mesh_leaves_the_gap_in_a_cut_empty(ngs, cylinder, monkeypatch, backend):
    import planex._plot_mesh

    seen = []
    at = planex._plot_mesh._at

    def recording(f, mesh, pts, plane):
        seen.append(np.array(pts))
        return at(f, mesh, pts, plane)

    monkeypatch.setattr(planex._plot_mesh, "_at", recording)
    solid, mesh = cylinder
    shape = px.Intersect3d(px.Plane(**CUTS["cylinder oblique"][1]), solid)
    shown = px.PlotField(ngs.CF((ngs.y, -ngs.x, ngs.z)), mesh, shape, backend=backend, vectors=True)
    xs, ys, inside, found, _, _ = located(shape, mesh, 400)
    assert (inside & ~found).sum() > 100
    np.testing.assert_array_equal(np.isnan(raster(shown, backend)).ravel(), ~found)
    assert len(seen) == 2 and min(len(p) for p in seen) > 100
    for pts in seen:
        assert shape.Contains(pts).all()


def test_a_point_just_outside_the_mesh_has_no_value(ngs, cylinder):
    from planex._plot_fields import MESH_TOL
    from planex._plot_mesh import _at

    _, mesh = cylinder
    lateral = []
    for el in mesh.Elements(ngs.BND):
        corners = np.array([mesh.vertices[v.nr].point for v in el.vertices], dtype=float)
        if np.abs(np.linalg.norm(corners[:, :2], axis=1) - 0.5).max() < 1e-6 and np.ptp(corners[:, 2]) > 0.01:
            lateral.append(corners)
    a, b, c = min(lateral, key=lambda t: np.linalg.norm(t.mean(axis=0)[:2]))
    centre = (a + b + c) / 3
    normal = np.cross(b - a, c - a)
    normal *= np.sign(normal[:2] @ centre[:2]) / np.linalg.norm(normal)
    outside, inside = centre + 1e-7 * normal, centre - 1e-7 * normal
    assert np.linalg.norm(outside[:2]) < 0.5 - 1e-3
    assert mesh(*(np.array([x]) for x in outside))["nr"][0] >= 0
    assert mesh(*(np.array([x]) for x in outside), tol=MESH_TOL)["nr"][0] < 0
    f = ngs.x + 0
    for point, value in ((outside, None), (inside, inside[0])):
        plane = px.Plane(origin=tuple(point.tolist()), normal=tuple(normal.tolist()))
        got = _at(f, mesh, np.zeros((1, 2)), plane)[0, 0]
        assert np.isnan(got) if value is None else abs(got - value) < 1e-12


def test_a_3d_mesh_takes_any_plane_a_2d_mesh_only_the_xy_plane(ngs, box, mpl):
    solid, mesh = box
    shape = px.Intersect3d(px.Plane(**OBLIQUE), solid)
    rect = px.QuickSketch().Rectangle(2, 1).Face()
    flat = ngs.Mesh(px.OCCGeometry(rect).GenerateMesh(maxh=0.5))
    with pytest.raises(ValueError, match="XY plane only for now"):
        px.PlotMesh(flat, shape)
    with pytest.raises(ValueError, match="XY plane only for now"):
        px.PlotField(ngs.x + 0, flat, shape)
    z = raster(px.PlotField(ngs.x + 0, mesh, shape, samples=100), "matplotlib")
    assert np.isfinite(z).sum() > 0.5 * z.size


def test_a_face_on_xy_with_a_3d_mesh(ngs, occ, backend):
    disc = px.QuickSketch().Circle((0, 0), 1).Face() - px.QuickSketch().Circle((0.3, 0), 0.3).Face()
    solid = occ.Box((-1, -1, -1), (1, 1, 1))
    mesh = ngs.Mesh(occ.OCCGeometry(solid).GenerateMesh(maxh=0.5))
    z = raster(px.PlotField(ngs.x * ngs.y + ngs.z, mesh, disc, samples=100, backend=backend),
               backend)
    xs, ys, inside, found, _, _ = located(disc, mesh, 100)
    assert (found == inside).all() and found.sum() > 0.5 * z.size
    gx, gy = np.meshgrid(xs, ys)
    assert np.abs(z.ravel()[found] - (gx * gy).ravel()[found]).max() < 1e-12
    lines, on_top = drawn_mesh(px.PlotMesh(mesh, disc, backend=backend), backend)
    assert on_top
    check_cut_lines(lines, *tets_of(ngs, mesh), px.XY)


def test_cut_lines_lie_on_the_element_faces_crossing_the_plane(ngs, box, backend):
    solid, mesh = box
    plane = px.Plane(**OBLIQUE)
    shape = px.Intersect3d(plane, solid)
    lines, on_top = drawn_mesh(px.PlotMesh(mesh, shape, backend=backend), backend)
    assert on_top
    check_cut_lines(lines, *tets_of(ngs, mesh), plane)
    (u0, v0), (u1, v1) = shape.bounding_box
    assert (lines[..., 0] >= u0 - 1e-9).all() and (lines[..., 0] <= u1 + 1e-9).all()
    assert (lines[..., 1] >= v0 - 1e-9).all() and (lines[..., 1] <= v1 + 1e-9).all()
    o, u, v, _ = frame(plane)
    ends = o + lines[..., :1] * u + lines[..., 1:] * v
    assert (ends >= -1e-9).all() and (ends <= np.array([2, 1, 1]) + 1e-9).all()


def test_cut_lines_through_mesh_vertices_and_along_an_element_face(ngs, box, backend):
    solid, mesh = box
    points, tets = tets_of(ngs, mesh)
    middle = np.array([1.0, 0.5, 0.5])
    vertex = points[np.argmin(np.linalg.norm(points - middle, axis=1))]
    a, b, c = points[tets[np.argmin(np.linalg.norm(points[tets].mean(axis=1) - middle, axis=1))][:3]]
    through_vertex = px.Plane(origin=(0.0, 0.0, float(vertex[2])), normal=(0.0, 0.0, 1.0))
    along_face = px.Plane(origin=tuple(a.tolist()), normal=tuple(np.cross(b - a, c - a).tolist()))
    for plane in (through_vertex, along_face):
        lines, _ = drawn_mesh(px.PlotMesh(mesh, px.Intersect3d(plane, solid), backend=backend), backend)
        check_cut_lines(lines, points, tets, plane)
    o, u, v, _ = frame(along_face)
    corners = np.stack([(a - o) @ np.c_[u, v], (b - o) @ np.c_[u, v], (c - o) @ np.c_[u, v]])
    sides = np.stack([corners, np.roll(corners, -1, axis=0)], axis=1)
    assert drawn(sides, lines, 1e-9 * np.ptp(points, axis=0).max()).all()


def test_cut_lines_of_a_curved_mesh_join_the_element_corners(ngs, occ, backend):
    solid = occ.Cylinder(occ.Pnt(0, 0, 0), occ.Z, r=0.5, h=1)
    mesh = ngs.Mesh(occ.OCCGeometry(solid).GenerateMesh(maxh=0.3))
    mesh.Curve(2)
    plane = px.Plane(**CUTS["cylinder oblique"][1])
    lines, on_top = drawn_mesh(px.PlotMesh(mesh, px.Intersect3d(plane, solid), backend=backend), backend)
    assert on_top
    check_cut_lines(lines, *tets_of(ngs, mesh), plane)


@pytest.mark.parametrize("kind, origin, normal, sides, area", [
    ("TET", (0.0, 0.0, 0.5), (0.0, 0.0, 1.0), 3, 0.125),
    ("TET", (0.3, 0.3, 0.3), (1.0, 1.0, -1.0), 4, 0.3325 * 3 ** 0.5 / 2),
    ("TET", (0.0, 0.0, 0.0), (0.0, 0.0, 1.0), 3, 0.5),
    ("TET", (0.0, 0.0, 0.0), (1.0, -1.0, 0.0), 3, 0.5 ** 0.5 / 2),
    ("PRISM", (0.0, 0.0, 0.5), (0.0, 0.0, 1.0), 3, 0.5),
    ("PRISM", (0.5, 0.0, 0.0), (1.0, 0.0, 0.0), 4, 0.5),
    ("PYRAMID", (0.0, 0.0, 0.5), (0.0, 0.0, 1.0), 4, 0.25),
    ("PYRAMID", (0.0, 0.0, 0.0), (0.0, 0.0, 1.0), 4, 1.0),
    ("HEX", (0.5, 0.5, 0.5), (1.0, 1.0, 1.0), 6, 3 * 3 ** 0.5 / 4),
    ("HEX", (0.5, 0.5, 0.5), (0.0, 0.0, 1.0), 4, 1.0),
    ("HEX", (0.0, 0.0, 0.0), (0.0, 0.0, 1.0), 4, 1.0),
    ("HEX", (0.0, 0.0, 0.0), (0.0, 1.0, -1.0), 4, 2 ** 0.5),
    ("HEX", (0.0, 0.0, 0.0), (1.0, 1.0, 0.0), 1, None),
    ("HEX", (0.0, 0.0, 0.0), (1.0, 1.0, 1.0), 0, None),
    ("HEX", (0.0, 0.0, 2.0), (0.0, 0.0, 1.0), 0, None),
])
def test_section_of_each_element_kind(kind, origin, normal, sides, area):
    from planex._plot_fields import ELEMENTS3D
    from planex._plot_mesh import _section

    corners, edges = ELEMENTS3D[kind]
    lines = _section(np.array([corners], dtype=float), edges, px.Plane(origin=origin, normal=normal), 1e-12)
    assert lines.shape == (sides, 2, 2) and np.isfinite(lines).all()
    if area is not None:
        np.testing.assert_array_equal(lines[:, 1], np.roll(lines[:, 0], -1, axis=0))
        shoelace = 0.5 * (lines[:, 0, 0] * lines[:, 1, 1] - lines[:, 1, 0] * lines[:, 0, 1]).sum()
        assert abs(shoelace) == pytest.approx(area, abs=1e-9)


def overlay(shown, backend, name):
    """the lines drawn as "contours" ({level: lines}) or as "field lines" ({None: lines})"""
    if backend == "plotly":
        trace, = [t for t in shown.data if t.name == name]
        x, y = (np.array([np.nan if a is None else a for a in seq], dtype=float) for seq in (trace.x, trace.y))
        level = trace.customdata if name == "contours" else [None] * len(x)
        out = {}
        for at in np.split(np.arange(len(x)), np.flatnonzero(np.isnan(x)) + 1):
            at = at[~np.isnan(x[at])]
            if len(at):
                out.setdefault(level[at[0]], []).append(np.c_[x[at], y[at]])
        return out
    found, = [c for c in shown.collections if c.get_gid() == name]
    if name == "field lines":
        return {None: list(found.get_segments())}
    return {float(level): [s for s in segs if len(s)] for level, segs in zip(found.levels, found.allsegs)
            if any(len(s) for s in segs)}


def test_field_lines_in_a_cut_follow_the_part_in_the_plane(ngs, box, backend):
    solid, mesh = box
    shape = px.Intersect3d(px.Plane(**OBLIQUE), solid)
    field = ngs.CF((1.0, 2.0, 3.0))
    lines = overlay(px.PlotField(field, mesh, shape, samples=200, field_lines=True, backend=backend), backend, "field lines")[None]
    assert len(lines) > 5
    _, u, v, _ = frame(px.Plane(shape))
    along = np.array([u @ (1.0, 2.0, 3.0), v @ (1.0, 2.0, 3.0)])
    for line in lines:
        step = np.diff(line, axis=0)
        length = np.linalg.norm(step, axis=1)
        step, length = step[length > 0], length[length > 0]
        assert (np.abs(step[:, 0] * along[1] - step[:, 1] * along[0]) <= 1e-9 * length * np.linalg.norm(along)).all()
        assert (step @ along > 0).all()
    with pytest.raises(ValueError, match="three components"):
        px.PlotField(ngs.CF((ngs.y, -ngs.x)), mesh, shape, field_lines=True, backend=backend)


def test_contours_in_a_cut_lie_on_their_level(ngs, box, backend):
    solid, mesh = box
    plane = px.Plane(**OBLIQUE)
    shape = px.Intersect3d(plane, solid)
    lines = overlay(px.PlotField(ngs.z + 0, mesh, shape, samples=200, contours=[0.3, 0.5, 0.7], backend=backend), backend, "contours")
    assert sorted(lines) == [0.3, 0.5, 0.7]
    o, u, v, _ = frame(plane)
    for level, pieces in lines.items():
        for line in pieces:
            assert np.abs((o + line[:, :1] * u + line[:, 1:] * v)[:, 2] - level).max() < 1e-9


def test_a_plane_draws_what_it_cuts_from_the_mesh_geometry(box, ngs, occ, backend):
    solid, mesh = box
    gf = ngs.GridFunction(ngs.H1(mesh, order=2))
    gf.Set(ngs.x * ngs.y * ngs.z)
    plane = px.Plane(**OBLIQUE)
    expected = raster(px.PlotField(gf, px.Intersect3d(plane, solid), backend=backend), backend)
    assert np.isfinite(expected).sum() > 1000
    normal = np.cross(plane.h, plane.v)
    axes = occ.Axes(p=plane.origin, n=tuple(normal), h=plane.h)
    for shown in (px.PlotField(gf, plane, backend=backend), px.PlotField(gf, axes, backend=backend)):
        np.testing.assert_allclose(raster(shown, backend), expected, rtol=0, atol=1e-12)
    assert drawn_mesh(px.PlotMesh(mesh, plane, backend=backend), backend)[0].size > 0


def test_a_plane_alone_has_no_bounds(ngs, mpl):
    with pytest.raises(ValueError, match="has no bounds.*PlotMesh"):
        px.Plot(px.Plane(**OBLIQUE))
    with pytest.raises(ValueError, match="the plane cuts an OCC shape"):
        px.Plot(px.FromPolyline([(0, 0), (1, 0), (1, 1)], closed=True).Face(), px.XY)


def test_a_3d_mesh_without_geometry_is_its_own_region_cut_at_the_plane(ngs, mpl, backend):
    from netgen.csg import CSGeometry, OrthoBrick, Pnt

    geo = CSGeometry()
    geo.Add(OrthoBrick(Pnt(0, 0, -1), Pnt(2, 1, 1)))
    mesh = ngs.Mesh(geo.GenerateMesh(maxh=0.4))
    gf = ngs.GridFunction(ngs.H1(mesh, order=1))
    gf.Set(ngs.x)
    z = raster(px.PlotField(gf, backend=backend), backend)
    assert np.isfinite(z).sum() > 1000 and np.nanmin(z) == pytest.approx(0, abs=0.01) and np.nanmax(z) == pytest.approx(2, abs=0.01)
    lines, above = drawn_mesh(px.PlotMesh(mesh, backend=backend), backend)
    assert above and lines.size > 0 and lines[..., 0].min() == pytest.approx(0, abs=1e-9) and lines[..., 1].max() == pytest.approx(1, abs=1e-9)
    with pytest.raises(ValueError, match="does not cut the mesh"):
        px.PlotMesh(mesh, px.Plane(origin=(0, 0, 5)))


def test_the_xy_plane_draws_the_geometry_of_a_2d_mesh(ngs, mpl):
    face = px.FromPolyline([(0, 0), (2, 0), (2, 1), (0, 1)], closed=True).Face()
    mesh = ngs.Mesh(px.OCCGeometry(face).GenerateMesh(maxh=0.3))
    gf = ngs.GridFunction(ngs.H1(mesh, order=2))
    gf.Set(ngs.x * ngs.y)
    np.testing.assert_array_equal(raster(px.PlotField(gf, px.XY), "matplotlib"),
                                  raster(px.PlotField(gf, face), "matplotlib"))


def test_a_flat_occ_shape_plots_and_a_solid_points_to_a_section(box, ngs, occ, mpl):
    from matplotlib.collections import LineCollection

    solid, mesh = box
    top = solid.faces.Max(occ.Z)
    ax = px.Plot(top)
    assert sum(len(c.get_segments()) for c in ax.collections if isinstance(c, LineCollection)) == 4
    gf = ngs.GridFunction(ngs.H1(mesh, order=2))
    gf.Set(ngs.x * ngs.y * ngs.z)
    np.testing.assert_array_equal(raster(px.PlotField(gf, top), "matplotlib"),
                                  raster(px.PlotField(gf, px.FromOCC(top)), "matplotlib"))
    with pytest.raises(ValueError, match=r"Plot: an OCC shape is drawn when it is flat.*Intersect3d"):
        px.Plot(solid)
