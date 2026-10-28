# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import os
import shutil
import subprocess
import sys

import pytest

import planex as px

np = pytest.importorskip("numpy")
pytest.importorskip("dolfinx")

import basix.ufl  # noqa: E402
import ufl  # noqa: E402
from dolfinx import fem, geometry, mesh as dmesh  # noqa: E402
from mpi4py import MPI  # noqa: E402

OBLIQUE = dict(origin=(1.0, 0.5, 0.5), normal=(0.3, 0.2, 1.0))


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


@pytest.fixture(scope="module")
def occ_box():
    pytest.importorskip("ngsolve")
    from netgen import occ

    return occ.Box(occ.Pnt(0, 0, 0), occ.Pnt(2, 1, 1))


def rectangle(width=2.0, height=1.0):
    return px.FromPolyline([(0, 0), (width, 0), (width, height), (0, height)], closed=True).Face()


def unit_rectangle_mesh(n=(8, 4), cell="triangle", top=(2.0, 1.0)):
    return dmesh.create_rectangle(MPI.COMM_WORLD, [np.zeros(2), np.array(top)], list(n),
                                  cell_type=getattr(dmesh.CellType, cell))


def box_mesh(n, cell):
    return dmesh.create_box(MPI.COMM_WORLD, [np.zeros(3), np.array([2.0, 1.0, 1.0])], list(n),
                            cell_type=getattr(dmesh.CellType, cell))


def function(msh, element, expr, dtype=None):
    V = fem.functionspace(msh, element)
    u = fem.Function(V) if dtype is None else fem.Function(V, dtype=dtype)
    u.interpolate(expr)
    return u


def quadratic(x):
    return x[0] ** 2 + x[0] * x[1] - x[1] ** 2


def quadratic3d(x):
    return x[0] * x[1] + x[2] ** 2


def frame(plane):
    return (np.asarray(a, dtype=float) for a in (plane.origin, plane.h, plane.v, plane.normal))


def pixels(shape, n):
    """the pixel centres Plot evaluates: xs, ys and all (u, v) points row by row"""
    from planex._plot_mesh import _pixels

    xs, ys, _ = _pixels(shape.bounding_box, n)
    gx, gy = np.meshgrid(xs, ys)
    return xs, ys, np.c_[gx.ravel(), gy.ravel()]


def by_dolfinx(u, points):
    """u at the (n, 3) points located by dolfinx alone (its first colliding cell), NaN where it finds none"""
    msh = u.function_space.mesh
    tree = geometry.bb_tree(msh, msh.topology.dim)
    cells = geometry.compute_colliding_cells(msh, geometry.compute_collisions_points(tree, points), points)
    offsets = np.asarray(cells.offsets)
    found = np.diff(offsets) > 0
    out = np.full((len(points), math.prod(u.ufl_shape)), np.nan, dtype=u.dtype)
    if found.any():
        out[found] = u.eval(points[found], np.asarray(cells.array)[offsets[:-1][found]])
    return out


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
    """the mesh lines drawn as (m, 2, 2)"""
    if backend == "plotly":
        trace, = [t for t in shown.data if t.name == "mesh"]
        x = np.array([np.nan if a is None else a for a in trace.x], dtype=float)
        y = np.array([np.nan if a is None else a for a in trace.y], dtype=float)
        assert len(x) % 3 == 0 and np.isnan(x[2::3]).all()
        return np.c_[x, y].reshape(-1, 3, 2)[:, :2]
    mesh, = [c for c in shown.collections if c.get_label() == "mesh"]
    return np.array(mesh.get_segments(), dtype=float).reshape(-1, 2, 2)


def drawn_names(shown, backend):
    if backend == "plotly":
        return {t.name for t in shown.data}
    return {c.get_gid() for c in shown.collections}


def segments(lines, digits=9):
    """the (m, 2, k) segments as a set, each once whichever way round"""
    ends = np.round(lines, digits) + 0.0
    return {tuple(sorted(map(tuple, s))) for s in ends}


def polygon_disc(n=12):
    """a coarse mesh of the regular n-gon inscribed in the unit circle: a fan of triangles around the centre"""
    t = 2 * np.pi * np.arange(n) / n
    x = np.r_[[[0.0, 0.0]], np.c_[np.cos(t), np.sin(t)]]
    cells = np.array([[0, 1 + k, 1 + (k + 1) % n] for k in range(n)], dtype=np.int64)
    element = basix.ufl.element("Lagrange", "triangle", 1, shape=(2,))
    return dmesh.create_mesh(MPI.COMM_SELF, cells, ufl.Mesh(element), x)


def curved_triangle():
    """one P2 triangle on (0, 0), (1, 0), (0, 1): its lower edge bent inwards to (0.5, 0.1), its long edge outwards
    to (0.6, 0.6)"""
    x = np.array([[0.0, 0.0], [1.0, 0.0], [0.0, 1.0], [0.6, 0.6], [0.0, 0.5], [0.5, 0.1]])
    element = basix.ufl.element("Lagrange", "triangle", 2, shape=(2,))
    return dmesh.create_mesh(MPI.COMM_SELF, np.arange(6, dtype=np.int64)[None, :], ufl.Mesh(element), x)


def test_2d_pixels_are_the_function_at_their_centres_and_nan_off_the_mesh(backend):
    msh = unit_rectangle_mesh((6, 4), top=(1.5, 1.0))
    u = function(msh, ("Lagrange", 2), quadratic)
    shape = rectangle()
    z = raster(px.PlotField(u, shape, samples=120, backend=backend), backend).ravel()
    xs, ys, uv = pixels(shape, 120)
    assert z.size == 120 * 60
    covered = uv[:, 0] < 1.5
    assert np.isnan(z[~covered]).all() and np.isfinite(z[covered]).all() and (~covered).sum() > 1000
    p = np.c_[uv[covered], np.zeros(covered.sum())]
    np.testing.assert_allclose(z[covered], by_dolfinx(u, p)[:, 0], rtol=0, atol=1e-12)
    np.testing.assert_allclose(z[covered], quadratic(p.T), rtol=0, atol=1e-12)


def test_2d_quads_vector_function_with_arrows_field_lines_contours_and_a_colormap(backend):
    msh = unit_rectangle_mesh((8, 4), "quadrilateral")
    u = function(msh, ("Lagrange", 1, (2,)), lambda x: np.vstack([x[1], -x[0]]))
    shape = rectangle()
    shown = px.PlotField(u, shape, vectors=True, field_lines=True, contours=4, colormap="viridis", samples=100, backend=backend)
    xs, ys, uv = pixels(shape, 100)
    np.testing.assert_allclose(raster(shown, backend).ravel(), np.hypot(uv[:, 0], uv[:, 1]), rtol=0, atol=1e-12)
    start, drawn = arrows(shown, backend)
    assert len(start) > 50 and shape.Contains(start).all()
    field = np.c_[start[:, 1], -start[:, 0]]
    np.testing.assert_allclose(drawn, field * (2 / 20 / np.linalg.norm(field, axis=1).max()), rtol=0, atol=1e-9)
    assert {"contours", "field lines"} <= drawn_names(shown, backend)
    from matplotlib import colormaps
    from matplotlib.colors import to_hex

    if backend == "plotly":
        heat, = [t for t in shown.data if t.type == "heatmap"]
        assert heat.colorscale[0][1] == to_hex(colormaps["viridis"](0.0))
        contours, = [t for t in shown.data if t.name == "contours"]
        assert len({v for v in contours.customdata if v is not None}) == 4
    else:
        assert shown.images[0].get_cmap().name == "viridis"
        contours, = [c for c in shown.collections if c.get_gid() == "contours"]
        assert len(contours.levels) == 4


def test_a_complex_function_shows_its_modulus(backend):
    msh = unit_rectangle_mesh((4, 2))
    u = function(msh, ("Lagrange", 1), lambda x: x[0] + 1j * x[1], dtype=np.complex128)
    shape = rectangle()
    _, _, uv = pixels(shape, 60)
    np.testing.assert_allclose(raster(px.PlotField(u, shape, samples=60, backend=backend), backend).ravel(),
                               np.hypot(uv[:, 0], uv[:, 1]), rtol=0, atol=1e-12)


@pytest.mark.parametrize("cell, count", [("triangle", 8 * 5 + 4 * 9 + 8 * 4), ("quadrilateral", 8 * 5 + 4 * 9)])
def test_2d_mesh_lines_are_the_element_edges_each_once(cell, count, backend):
    msh = unit_rectangle_mesh((8, 4), cell)
    lines = drawn_mesh(px.PlotMesh(msh, rectangle(), backend=backend), backend)
    assert len(lines) == count == len(segments(lines))
    np.testing.assert_allclose(lines * 4, np.round(lines * 4), rtol=0, atol=1e-12)
    lengths = np.linalg.norm(lines[:, 1] - lines[:, 0], axis=1)
    assert np.isin(np.round(lengths, 12), np.round([0.25, np.sqrt(2) * 0.25], 12)).all()


@pytest.mark.parametrize("cell", ["tetrahedron", "hexahedron"])
def test_3d_cut_pixels_are_the_function_at_their_3d_points(cell, occ_box, backend):
    msh = box_mesh((6, 3, 3), cell)
    u = function(msh, ("Lagrange", 2), quadratic3d)
    shape = px.Intersect3d(px.Plane(**OBLIQUE), occ_box)
    z = raster(px.PlotField(u, shape, samples=150, backend=backend), backend).ravel()
    _, _, uv = pixels(shape, 150)
    inside = shape.Contains(uv)
    o, u_, v_, _ = frame(px.Plane(shape))
    p = o + uv[inside, :1] * u_ + uv[inside, 1:] * v_
    assert np.isnan(z[~inside]).all() and inside.sum() > 5000
    np.testing.assert_allclose(z[inside], by_dolfinx(u, p)[:, 0], rtol=0, atol=1e-12)
    np.testing.assert_allclose(z[inside], quadratic3d(p.T), rtol=0, atol=1e-12)


def test_3d_vector_function_colour_is_its_length_arrows_its_part_in_the_plane(occ_box, backend):
    msh = box_mesh((4, 2, 2), "tetrahedron")
    u = function(msh, ("Lagrange", 1, (3,)), lambda x: np.vstack([x[1], -x[0], x[2]]))
    shape = px.Intersect3d(px.Plane(**OBLIQUE), occ_box)
    shown = px.PlotField(u, shape, vectors=True, samples=100, backend=backend)
    _, _, uv = pixels(shape, 100)
    inside = shape.Contains(uv)
    o, u_, v_, _ = frame(px.Plane(shape))
    p = o + uv[inside, :1] * u_ + uv[inside, 1:] * v_
    np.testing.assert_allclose(raster(shown, backend).ravel()[inside], np.linalg.norm(p, axis=1), rtol=0, atol=1e-12)
    start, drawn = arrows(shown, backend)
    assert len(start) > 50 and shape.Contains(start).all()
    q = o + start[:, :1] * u_ + start[:, 1:] * v_
    in_plane = np.c_[q[:, 1], -q[:, 0], q[:, 2]] @ np.c_[u_, v_]
    (u0, v0), (u1, v1) = shape.bounding_box
    step = max(u1 - u0, v1 - v0) / 20
    np.testing.assert_allclose(drawn, in_plane * (step / np.linalg.norm(in_plane, axis=1).max()), rtol=0, atol=1e-9)


def test_3d_cut_lines_of_hexes_are_the_grid_at_that_height(occ_box, backend):
    msh = box_mesh((4, 2, 2), "hexahedron")
    shape = px.Intersect3d(px.Plane(origin=(0.0, 0.0, 0.43), normal=(0.0, 0.0, 1.0)), occ_box)
    lines = drawn_mesh(px.PlotMesh(msh, shape, backend=backend), backend)
    assert len(lines) == 4 * 4 * 2
    o, u, v, _ = frame(px.Plane(shape))
    xyz = o + lines[..., :1] * u + lines[..., 1:] * v
    np.testing.assert_allclose(xyz[..., 2], 0.43, rtol=0, atol=1e-12)
    grid = [((x, y), (x + 0.5, y)) for x in (0.0, 0.5, 1.0, 1.5) for y in (0.0, 0.5, 1.0)]
    grid += [((x, y), (x, y + 0.5)) for x in (0.0, 0.5, 1.0, 1.5, 2.0) for y in (0.0, 0.5)]
    assert segments(xyz[..., :2]) == segments(np.array(grid))


def test_3d_cut_lines_of_tets_join_where_the_plane_crosses_their_edges(occ_box, backend):
    msh = box_mesh((3, 2, 2), "tetrahedron")
    plane = px.Plane(origin=(1.0, 0.5, 0.47), normal=(0.3, 0.2, 1.0))
    shape = px.Intersect3d(plane, occ_box)
    lines = drawn_mesh(px.PlotMesh(msh, shape, backend=backend), backend)
    o, u, v, n = frame(px.Plane(shape))
    corners = msh.geometry.x[np.asarray(msh.geometry.dofmaps[0])]
    d = (corners - o) @ n
    assert np.abs(d).min() > 1e-3
    a, b = np.array([(i, j) for i in range(4) for j in range(i + 1, 4)]).T
    crossing = d[:, a] * d[:, b] < 0
    assert len(lines) == crossing.sum()
    t = d[:, a] / np.where(crossing, d[:, a] - d[:, b], 1.0)
    points = (corners[:, a] + t[..., None] * (corners[:, b] - corners[:, a]))[crossing] - o
    uv = np.c_[points @ u, points @ v]
    ends = lines.reshape(-1, 2)
    nearest = np.linalg.norm(ends[:, None] - uv[None], axis=-1).min(axis=1)
    assert nearest.max() < 1e-12


def test_pixels_between_a_coarse_disc_mesh_and_the_true_circle_are_nan(backend):
    msh = polygon_disc(12)
    u = function(msh, ("Lagrange", 1), lambda x: 1.0 + x[0])
    shape = px.QuickSketch().Circle((0, 0), 1).Face()
    z = raster(px.PlotField(u, shape, samples=200, backend=backend), backend).ravel()
    _, _, uv = pixels(shape, 200)
    mid = 2 * np.pi * (np.arange(12) + 0.5) / 12
    beyond = uv @ np.c_[np.cos(mid), np.sin(mid)].T - np.cos(np.pi / 12)
    in_polygon, off_polygon = (beyond < -1e-9).all(axis=1), (beyond > 1e-9).any(axis=1)
    gap = shape.Contains(uv) & off_polygon
    assert gap.sum() > 100 and np.isnan(z[gap]).all()
    np.testing.assert_allclose(z[in_polygon], 1.0 + uv[in_polygon, 0], rtol=0, atol=1e-12)


def test_a_point_just_outside_a_dolfinx_mesh_has_no_value():
    from planex._plot_mesh import _at

    msh = polygon_disc(12)
    u = function(msh, ("Lagrange", 1), lambda x: 1.0 + x[0])
    normal = np.array([np.cos(np.pi / 12), np.sin(np.pi / 12)])
    edge = np.cos(np.pi / 12) * normal
    outside, inside = edge + 1e-7 * normal, edge - 1e-7 * normal
    assert np.isfinite(by_dolfinx(u, np.array([[*outside, 0.0]]))).all()
    got = _at(u, msh, np.array([outside, inside]), None)
    assert np.isnan(got[0, 0]) and abs(got[1, 0] - (1.0 + inside[0])) < 1e-12


def test_a_curved_cell_holds_the_points_inside_its_curved_edges_only():
    from planex._plot_mesh import _at

    msh = curved_triangle()
    u = function(msh, ("Lagrange", 1), lambda x: 2.0 + 0.0 * x[0])
    pts = np.array([[0.3, 0.3], [0.5, 0.05], [0.5, 0.12], [0.56, 0.56], [0.62, 0.62]])
    assert np.isfinite(by_dolfinx(u, np.c_[pts, np.zeros(len(pts))])[1]).all()
    got = _at(u, msh, pts, None)[:, 0]
    np.testing.assert_array_equal(np.isnan(got), [False, True, False, False, True])
    np.testing.assert_allclose(got[[0, 2, 3]], 2.0, rtol=0, atol=1e-12)


def test_ngsolve_and_dolfinx_draw_the_same_2d_picture(backend):
    ngs = pytest.importorskip("ngsolve")
    shape = rectangle()
    gf = ngs.GridFunction(ngs.H1(ngs.Mesh(px.OCCGeometry(shape).GenerateMesh(maxh=0.3)), order=2))
    gf.Set(ngs.x * ngs.x + ngs.x * ngs.y - ngs.y * ngs.y)
    u = function(unit_rectangle_mesh((7, 4)), ("Lagrange", 2), quadratic)
    a = raster(px.PlotField(gf, shape, samples=120, backend=backend), backend)
    b = raster(px.PlotField(u, shape, samples=120, backend=backend), backend)
    np.testing.assert_array_equal(np.isnan(a), np.isnan(b))
    np.testing.assert_allclose(a, b, rtol=0, atol=1e-10)


def test_ngsolve_and_dolfinx_draw_the_same_cut(occ_box, backend):
    ngs = pytest.importorskip("ngsolve")
    from netgen import occ

    gf = ngs.GridFunction(ngs.H1(ngs.Mesh(occ.OCCGeometry(occ_box).GenerateMesh(maxh=0.3)), order=2))
    gf.Set(ngs.x * ngs.y + ngs.z * ngs.z)
    u = function(box_mesh((5, 3, 3), "tetrahedron"), ("Lagrange", 2), quadratic3d)
    shape = px.Intersect3d(px.Plane(**OBLIQUE), occ_box)
    a = raster(px.PlotField(gf, shape, samples=120, backend=backend), backend)
    b = raster(px.PlotField(u, shape, samples=120, backend=backend), backend)
    np.testing.assert_array_equal(np.isnan(a), np.isnan(b))
    np.testing.assert_allclose(a, b, rtol=0, atol=1e-10)


def test_dolfinx_errors(mpl):
    msh = unit_rectangle_mesh((4, 2))
    u = function(msh, ("Lagrange", 2), quadratic)
    shape = rectangle()
    for expr in (ufl.grad(u), u * u, fem.Constant(msh, 1.0)):
        with pytest.raises(ValueError, match="interpolate it into one"):
            px.PlotField(expr, shape)
    with pytest.raises(ValueError, match="own mesh"):
        px.PlotField(u, unit_rectangle_mesh((4, 2)), shape)
    with pytest.raises(ValueError, match="on='edges' takes planex measurements"):
        px.PlotField(u, shape, on="edges")
    with pytest.raises(ValueError, match="2d mesh lies on the XY plane"):
        px.PlotField(u, px.Plane(**OBLIQUE))
    surface = dmesh.create_rectangle(MPI.COMM_WORLD, [np.zeros(2), np.array([2.0, 1.0])], [4, 2], gdim=3)
    with pytest.raises(ValueError, match="surface meshes"):
        px.PlotMesh(surface, shape)
    with pytest.raises(ValueError, match="dim 1"):
        px.PlotMesh(dmesh.create_interval(MPI.COMM_WORLD, 4, [0.0, 2.0]), shape)
    wave = function(msh, ("Lagrange", 1, (2,)), lambda x: np.vstack([x[1] + 1j, -x[0] + 0j]), dtype=np.complex128)
    with pytest.raises(ValueError, match="webgpu"):
        px.PlotField(wave, shape, vectors=True)
    three = function(msh, ("Lagrange", 1, (3,)), lambda x: np.vstack([x[1], -x[0], x[0]]))
    with pytest.raises(ValueError, match="two components"):
        px.PlotField(three, shape, vectors=True)
    ngs = pytest.importorskip("ngsolve")
    with pytest.raises(ValueError, match="ngsolve.Mesh"):
        px.PlotField(ngs.x + 0, msh, shape)


def test_a_mesh_without_geometry_is_its_own_region(backend):
    msh = unit_rectangle_mesh((8, 4))
    u = function(msh, ("Lagrange", 2), quadratic)
    z = raster(px.PlotField(u, backend=backend), backend).ravel()
    assert np.isfinite(z).all() and z.size == 400 * 200
    np.testing.assert_array_equal(z, raster(px.PlotField(u, px.XY, backend=backend), backend).ravel())
    ax = px.PlotMesh(msh)
    outline, = [c for c in ax.collections if c.get_label() == "_nolegend_"]
    sides = np.array(outline.get_segments(), dtype=float)
    assert len(sides) == 2 * (8 + 4) and len(drawn_mesh(ax, "matplotlib")) > len(sides)
    assert np.isclose(np.linalg.norm(sides[:, 1] - sides[:, 0], axis=1), 0.25).all()


def test_a_mesh_distributed_over_mpi_processes_raises():
    mpiexec = shutil.which("mpiexec", path=os.path.dirname(sys.executable))
    if mpiexec is None:
        pytest.skip("no mpiexec next to this python")
    code = "\n".join([
        "from mpi4py import MPI",
        "import numpy as np",
        "import planex as px",
        "from dolfinx import fem, mesh",
        "m = mesh.create_rectangle(MPI.COMM_WORLD, [np.zeros(2), np.array([2.0, 1.0])], [8, 4])",
        "u = fem.Function(fem.functionspace(m, ('Lagrange', 1)))",
        "shape = px.FromPolyline([(0, 0), (2, 0), (2, 1), (0, 1)], closed=True).Face()",
        "for call in (lambda: px.PlotField(u, shape), lambda: px.PlotMesh(m, shape)):",
        "    try:",
        "        call()",
        "    except ValueError as e:",
        "        print('raised', 'distributed over 2 MPI processes' in str(e), flush=True)",
    ])
    run = subprocess.run([mpiexec, "-n", "2", sys.executable, "-c", code], capture_output=True, text=True, timeout=300)
    assert run.returncode == 0, run.stderr
    assert run.stdout.split().count("raised") == 4 and "False" not in run.stdout


def mesh_segments(face):
    from planex._geom import xy

    return {frozenset((xy(e.start), xy(e.end))) for e in face.edges}


def closed(shape):
    from planex._plot import _free_edges

    return all(w.IsClosed() for w in shape.wires) and not _free_edges(list(shape.edges))


@pytest.mark.parametrize("cell", ["tetrahedron", "hexahedron"])
@pytest.mark.parametrize("plane", [OBLIQUE, dict(origin=(0.0, 0.0, 0.5), normal=(0.0, 0.0, 1.0)),
                                   dict(origin=(0.0, 0.0, 1.0), normal=(0.0, 0.0, 1.0)),
                                   dict(origin=(0.0, 0.0, 0.0), normal=(0.0, 0.0, 1.0))])
def test_the_cut_of_a_dolfinx_box_is_the_cut_of_the_box(cell, plane, occ_box):
    msh = box_mesh((4, 2, 2), cell)
    by_mesh, by_solid = px.Intersect3d(px.Plane(**plane), msh), px.Intersect3d(px.Plane(**plane), occ_box)
    assert len(by_mesh.faces) == len(by_solid.faces) == 1 and closed(by_mesh)
    assert by_mesh.mass == pytest.approx(by_solid.mass, abs=1e-9) and by_mesh.mass > 1.9
    assert {e.name for e in by_mesh.edges} == {None}


def test_meshdata_names_the_regions_after_the_cell_tags_and_the_edges_after_the_facet_tags(occ_box):
    from dolfinx.io import gmsh as gmshio

    msh = box_mesh((4, 2, 2), "tetrahedron")
    msh.topology.create_connectivity(2, 3)
    left = dmesh.locate_entities_boundary(msh, 2, lambda x: np.isclose(x[0], 0.0))
    lower = dmesh.locate_entities(msh, 3, lambda x: x[2] <= 0.5 + 1e-12)
    upper = np.setdiff1d(np.arange(msh.topology.index_map(3).size_local, dtype=np.int32), lower)
    cells = np.concatenate([lower, upper]).astype(np.int32)
    values = np.concatenate([np.full(len(lower), 1), np.full(len(upper), 2)]).astype(np.int32)
    order = np.argsort(cells)
    groups = {"left": gmshio.PhysicalGroup(dim=2, tag=3), "steel": gmshio.PhysicalGroup(dim=3, tag=1),
              "air": gmshio.PhysicalGroup(dim=3, tag=2)}
    data = gmshio.MeshData(mesh=msh, cell_tags=dmesh.meshtags(msh, 3, cells[order], values[order]),
                           facet_tags=dmesh.meshtags(msh, 2, left, np.full(len(left), 3, dtype=np.int32)),
                           ridge_tags=None, peak_tags=None, physical_groups=groups)
    plane = px.Plane(**OBLIQUE)
    cut, exact = px.Intersect3d(plane, data), px.Intersect3d(plane, occ_box)
    assert sorted(f.name for f in cut.faces) == ["air", "steel"] and closed(cut)
    assert cut.mass == pytest.approx(exact.mass, abs=1e-9)
    steel, = [f for f in cut.faces if f.name == "steel"]
    air, = [f for f in cut.faces if f.name == "air"]
    assert mesh_segments(steel) & mesh_segments(air)
    assert {e.name for e in cut.edges} == {"left", None}
    o, u, v, _ = frame(px.Plane(cut))
    from planex._geom import xy

    for e in cut.edges:
        x = [(o + p[0] * u + p[1] * v)[0] for p in (xy(e.start), xy(e.end))]
        assert (e.name == "left") == bool(np.allclose(x, 0.0, rtol=0, atol=1e-9))
    bare = px.Intersect3d(plane, msh)
    assert len(bare.faces) == 1 and bare.faces[0].name is None


def test_a_field_on_the_cut_of_its_dolfinx_mesh(backend):
    msh = box_mesh((6, 3, 3), "hexahedron")
    u = function(msh, ("Lagrange", 2), quadratic3d)
    cut = px.Intersect3d(px.Plane(**OBLIQUE), msh)
    z = raster(px.PlotField(u, msh, cut, samples=120, backend=backend), backend).ravel()
    _, _, uv = pixels(cut, 120)
    inside = cut.Contains(uv)
    o, u_, v_, _ = frame(px.Plane(cut))
    p = o + uv[inside, :1] * u_ + uv[inside, 1:] * v_
    assert np.isnan(z[~inside]).all() and inside.sum() > 3000
    np.testing.assert_allclose(z[inside], quadratic3d(p.T), rtol=0, atol=1e-12)


def test_intersect3d_of_a_dolfinx_mesh_errors_and_a_plane_that_misses_it():
    with pytest.raises(ValueError, match="dim 2"):
        px.Intersect3d(px.XY, unit_rectangle_mesh((4, 2)))
    with pytest.warns(px.PlanexWarning, match="misses the shape"):
        missed = px.Intersect3d(px.Plane(origin=(0.0, 0.0, 5.0), normal=(0.0, 0.0, 1.0)), box_mesh((2, 1, 1), "tetrahedron"))
    assert len(missed.faces) == 0


def test_a_float32_mesh_and_function_leave_no_empty_pixel_inside_the_mesh(backend):
    msh = dmesh.create_rectangle(MPI.COMM_WORLD, [np.zeros(2, dtype=np.float32), np.array([2.0, 1.0], dtype=np.float32)],
                                 [16, 8], dtype=np.float32)
    assert msh.geometry.x.dtype == np.float32
    u = function(msh, ("Lagrange", 2), quadratic, dtype=np.float32)
    shape = rectangle()
    z = raster(px.PlotField(u, shape, samples=200, backend=backend), backend).ravel()
    _, _, uv = pixels(shape, 200)
    assert z.size == 200 * 100 and np.isfinite(z).all()
    np.testing.assert_allclose(z, quadratic(uv.T), rtol=0, atol=1e-5)


@pytest.mark.parametrize("index, expr", [(0, lambda x: np.vstack([x[1], -x[0]])), (1, lambda x: x[0] + 2 * x[1])])
def test_a_sub_function_of_a_mixed_space_is_drawn_as_the_field_it_is(index, expr, backend):
    msh = unit_rectangle_mesh((8, 4))
    element = basix.ufl.mixed_element([basix.ufl.element("Lagrange", "triangle", 2, shape=(2,)),
                                       basix.ufl.element("Lagrange", "triangle", 1)])
    w = fem.Function(fem.functionspace(msh, element))
    w.sub(index).interpolate(expr)
    shape = rectangle()
    z = raster(px.PlotField(w.sub(index), shape, samples=100, backend=backend), backend).ravel()
    _, _, uv = pixels(shape, 100)
    exact = expr(uv.T)
    exact = exact if index else np.hypot(exact[0], exact[1])
    np.testing.assert_allclose(z, exact, rtol=0, atol=1e-12)
