# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import subprocess
import sys

import pytest

import planex as px
import planex.measurements as pm

np = pytest.importorskip("numpy")


@pytest.fixture(scope="module")
def ngs():
    return pytest.importorskip("ngsolve")


def holed_disc():
    return px.QuickSketch().Circle((0, 0), 1).Face() - px.QuickSketch().Circle((0.3, 0), 0.3).Face()


def ring():
    return px.QuickSketch().Circle((0, 0), 2).Face() - px.QuickSketch().Circle((0, 0), 1).Face()


@pytest.fixture(scope="module")
def disc(ngs):
    shape = holed_disc()
    mesh = ngs.Mesh(px.OCCGeometry(shape).GenerateMesh(maxh=0.25))
    mesh.Curve(3)
    return shape, mesh


@pytest.fixture(scope="module")
def scalar(ngs, disc):
    gf = ngs.GridFunction(ngs.H1(disc[1], order=2))
    gf.Set(ngs.x * ngs.x - ngs.y)
    return gf


@pytest.fixture
def mpl():
    matplotlib = pytest.importorskip("matplotlib")
    matplotlib.use("Agg")
    yield
    import matplotlib.pyplot as plt
    plt.close("all")


@pytest.fixture
def plotly():
    pytest.importorskip("plotly")


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


def pixels(shape, n=400):
    from planex._plot_mesh import _pixels

    return _pixels(shape.bounding_box, n)


def expected(shape, mesh, f, xs, ys, size=lambda v: v[:, 0]):
    """f at the pixel centres inside the shape and the mesh, NaN elsewhere"""
    gx, gy = np.meshgrid(xs, ys)
    x, y = gx.ravel(), gy.ravel()
    mips = mesh(x, y)
    keep = shape.Contains(np.c_[x, y]) & (mips["nr"] >= 0)
    z = np.full(len(x), np.nan)
    z[keep] = size(np.asarray(f(mips[keep])))
    return z.reshape(gx.shape)


def heatmap(fig):
    heat, = [t for t in fig.data if t.type == "heatmap"]
    return heat


def raster(ax, backend):
    """the pixel values and the colour range"""
    if backend == "plotly":
        heat = heatmap(ax)
        return np.asarray(heat.z, dtype=float), (heat.zmin, heat.zmax)
    im, = ax.images
    return np.ma.filled(im.get_array().astype(float), np.nan), im.get_clim()


def outside_and_hole(xs, ys):
    gx, gy = np.meshgrid(xs, ys)
    return (gx * gx + gy * gy > 1.0001) | ((gx - 0.3) ** 2 + gy * gy < 0.2999 ** 2)


def test_scalar_field_plotly_pixels_are_the_field_at_their_centres(disc, scalar, plotly):
    shape, mesh = disc
    xs, ys, _ = pixels(shape)
    fig = px.PlotField(scalar, shape, backend="plotly")
    heat = heatmap(fig)
    assert (len(heat.x), len(heat.y)) == (400, 400)
    np.testing.assert_array_equal(np.asarray(heat.x), xs)
    np.testing.assert_array_equal(np.asarray(heat.y), ys)
    z = np.asarray(heat.z, dtype=float)
    np.testing.assert_array_equal(z, expected(shape, mesh, scalar, xs, ys))
    assert np.isnan(z[outside_and_hole(xs, ys)]).all()
    assert np.isfinite(z).sum() > 0.6 * 400 * 400 and np.nanmin(z) < 0
    assert heat.hoverinfo == "z" and heat.zmin is None and heat.zmax is None
    kinds = [t.type if t.type == "heatmap" else t.name for t in fig.data]
    assert kinds[0] == "heatmap" and "edges" in kinds[1:]


def test_scalar_field_matplotlib_image_with_the_outline_on_top(disc, scalar, mpl):
    shape, mesh = disc
    xs, ys, _ = pixels(shape)
    ax = px.PlotField(scalar, shape)
    im, = ax.images
    assert im.origin == "lower"
    step = xs[1] - xs[0]
    assert im.get_extent() == pytest.approx((xs[0] - step / 2, xs[-1] + step / 2, ys[0] - step / 2, ys[-1] + step / 2))
    assert im.get_extent()[:2] == pytest.approx((-1, 1))
    z = np.ma.filled(im.get_array().astype(float), np.nan)
    assert z.shape == (400, 400)
    np.testing.assert_array_equal(z, expected(shape, mesh, scalar, xs, ys))
    assert im.get_zorder() < min(c.get_zorder() for c in ax.collections)
    assert len(ax.figure.axes) == 2


def test_samples_sets_the_pixels_along_the_longer_side(ngs, backend):
    rect = px.QuickSketch().Rectangle(2, 1).Face()
    mesh = ngs.Mesh(px.OCCGeometry(rect).GenerateMesh(maxh=0.5))
    for samples, shape in ((None, (200, 400)), (50, (25, 50))):
        shown = px.PlotField(ngs.x + 0, mesh, rect, samples=samples, backend=backend)
        z, _ = raster(shown, backend)
        assert z.shape == shape
        xs, ys, _ = pixels(rect, shape[1])
        assert np.nanmax(np.abs(z - xs[None, :])) < 1e-12


def test_coefficient_function_needs_a_mesh_and_a_gridfunction_its_own(ngs, disc, scalar, mpl):
    shape, mesh = disc
    cf = ngs.Norm(ngs.grad(scalar))
    with pytest.raises(ValueError, match="give the mesh"):
        px.PlotField(cf, shape)
    ax = px.PlotField(cf, mesh, shape)
    xs, ys, _ = pixels(shape)
    z, _ = raster(ax, "matplotlib")
    np.testing.assert_array_equal(z, expected(shape, mesh, cf, xs, ys))
    other = ngs.Mesh(px.OCCGeometry(shape).GenerateMesh(maxh=0.5))
    with pytest.raises(ValueError, match="own mesh"):
        px.PlotField(scalar, other, shape)
    z, _ = raster(px.PlotField(scalar, mesh, shape), "matplotlib")
    np.testing.assert_array_equal(z, expected(shape, mesh, scalar, xs, ys))


def test_vector_field_shows_its_norm_and_arrows_only_when_asked(ngs, disc, backend):
    shape, mesh = disc
    field = ngs.CF((ngs.y, -ngs.x))
    shown = px.PlotField(field, mesh, shape, backend=backend)
    z, _ = raster(shown, backend)
    xs, ys, _ = pixels(shape)
    np.testing.assert_array_equal(z, expected(shape, mesh, field, xs, ys, lambda v: np.linalg.norm(v, axis=1)))
    gx, gy = np.meshgrid(xs, ys)
    assert np.nanmax(np.abs(z - np.sqrt(gx * gx + gy * gy))) < 1e-10
    if backend == "plotly":
        assert not [t for t in shown.data if t.name == "vectors"]
        fig = px.PlotField(field, mesh, shape, backend="plotly", vectors=True)
        quiver, = [t for t in fig.data if t.name == "vectors"]
        x, y = list(quiver.x), list(quiver.y)
        n = len(x) // 7
        assert len(x) == 7 * n and 100 < n <= 400
        start = np.array([x[0:3 * n:3], y[0:3 * n:3]], dtype=float).T
        end = np.array([x[1:3 * n:3], y[1:3 * n:3]], dtype=float).T
        arrows = end - start
    else:
        from matplotlib.quiver import Quiver

        assert not [c for c in shown.collections if isinstance(c, Quiver)]
        ax = px.PlotField(field, mesh, shape, vectors=True)
        quiver, = [c for c in ax.collections if isinstance(c, Quiver)]
        assert (quiver.scale, quiver.scale_units, quiver.angles) == (1.0, "xy", "xy")
        start = np.c_[quiver.X, quiver.Y]
        arrows = np.c_[np.asarray(quiver.U), np.asarray(quiver.V)]
        assert 100 < len(start) <= 400
    assert shape.Contains(start).all()
    lengths = np.linalg.norm(arrows, axis=1)
    assert lengths.max() == pytest.approx(2 / 20)
    assert np.abs(arrows[:, 0] * -start[:, 0] - arrows[:, 1] * start[:, 1]).max() < 1e-9
    assert (arrows[:, 0] * start[:, 1] - arrows[:, 1] * start[:, 0] >= 0).all()


def test_vectors_needs_a_real_vector_field(ngs, disc, scalar, mpl):
    shape, mesh = disc
    with pytest.raises(ValueError, match="two components"):
        px.PlotField(scalar, shape, vectors=True)
    with pytest.raises(ValueError, match="NGSolve vector field"):
        px.PlotField(pm.MinThickness(), ring(), vectors=True)
    with pytest.raises(TypeError):
        px.Plot(shape, vectors=True)
    gf = ngs.GridFunction(ngs.VectorH1(mesh, order=1, complex=True))
    gf.Set(ngs.CF((1j * ngs.y, ngs.x)))
    with pytest.raises(ValueError, match="webgpu"):
        px.PlotField(gf, shape, vectors=True)


def test_complex_field_shows_its_modulus(ngs, disc, backend):
    shape, mesh = disc
    gf = ngs.GridFunction(ngs.H1(mesh, order=3, complex=True))
    gf.Set(ngs.exp(3j * ngs.x) * ngs.y)
    z, _ = raster(px.PlotField(gf, shape, backend=backend), backend)
    xs, ys, _ = pixels(shape)
    np.testing.assert_array_equal(z, expected(shape, mesh, gf, xs, ys, lambda v: np.abs(v[:, 0])))
    assert np.nanmin(z) >= 0


def given(end):
    """an end of the colour range, px.Auto for None"""
    return px.Auto if end is None else end


@pytest.mark.parametrize("lo, hi", [(-0.5, 0.5), (-0.5, None), (None, 0.5), (None, None)])
def test_min_max_set_the_colour_range_of_a_field(disc, scalar, backend, lo, hi):
    shape, _ = disc
    z, clim = raster(px.PlotField(scalar, shape, backend=backend, min=given(lo), max=given(hi)), backend)
    if backend == "plotly" and lo is None and hi is None:
        assert clim == (None, None)
    else:
        assert clim == (lo if lo is not None else np.nanmin(z), hi if hi is not None else np.nanmax(z))


@pytest.mark.parametrize("lo, hi", [(5.0, None), (None, -5.0)])
def test_one_end_alone_beyond_the_values_never_passes_it(disc, scalar, backend, lo, hi):
    shape, _ = disc
    from planex._plot_style import _limits

    end = lo if lo is not None else hi
    z, clim = raster(px.PlotField(scalar, shape, backend=backend, min=given(lo), max=given(hi)), backend)
    assert np.nanmax(np.abs(z)) < 2 and _limits(z, (lo, hi)) == (end, end)
    if backend == "plotly":
        assert clim == (end, end)
    else:
        import matplotlib.pyplot as plt

        plt.gcf().canvas.draw()
        assert clim[0] <= end <= clim[1]


@pytest.mark.parametrize("method", ["ball", "ray"])
def test_min_max_set_the_colour_range_of_a_measurement(backend, method):
    for faces in (True, False):
        if faces:
            shown = px.PlotField(pm.MinThickness(method=method), ring(), samples=80, backend=backend, min=0.5, max=1.5)
        else:
            shown = px.PlotField(pm.MinThickness(method=method), ring(), samples=80, backend=backend, min=0.5, max=1.5, on="edges")
        if backend == "plotly":
            coloured = [t for t in shown.data if t.type == "heatmap" or (t.mode == "markers" and t.marker.colorscale)]
            trace, = coloured
            clim = (trace.zmin, trace.zmax) if trace.type == "heatmap" else (trace.marker.cmin, trace.marker.cmax)
        else:
            mappable, = [m for m in list(shown.images) + list(shown.collections) if m.get_array() is not None]
            clim = mappable.get_clim()
        assert clim == (0.5, 1.5)
        alone = px.PlotField(pm.MinThickness(method=method), ring(), samples=80, backend=backend, min=0.5)
        if backend == "plotly":
            heat = heatmap(alone)
            assert (heat.zmin, heat.zmax) == (0.5, np.nanmax(np.asarray(heat.z, dtype=float)))
        else:
            mappable, = [m for m in list(alone.images) + list(alone.collections) if m.get_array() is not None]
            assert mappable.get_clim()[0] == 0.5 and mappable.get_clim()[1] > 0.5


def test_min_above_max_raises():
    with pytest.raises(ValueError, match="min"):
        px.PlotField(pm.MinThickness(), ring(), min=2, max=1)


def mesh_lines(shown, backend):
    """the drawn element edges as (m, k, 2), and whether they lie above the outline"""
    if backend == "plotly":
        names = [t.name for t in shown.data]
        trace = shown.data[names.index("mesh")]
        x = np.array([np.nan if v is None else v for v in trace.x], dtype=float)
        y = np.array([np.nan if v is None else v for v in trace.y], dtype=float)
        breaks = np.flatnonzero(np.isnan(x))
        k = breaks[0]
        assert (np.diff(breaks) == k + 1).all() and len(x) == len(breaks) * (k + 1)
        lines = np.c_[x, y].reshape(-1, k + 1, 2)[:, :k]
        return lines, names.index("mesh") > names.index("edges")
    from matplotlib.collections import LineCollection

    mesh, = [c for c in shown.collections if c.get_label() == "mesh"]
    outline = [c for c in shown.collections if isinstance(c, LineCollection) and c is not mesh]
    assert len({len(s) for s in mesh.get_segments()}) == 1
    return np.array(mesh.get_segments()), mesh.get_zorder() > max(c.get_zorder() for c in outline)


def test_mesh_alone_draws_its_curved_element_edges_on_top(ngs, disc, backend):
    shape, mesh = disc
    shown = px.PlotMesh(mesh, shape, backend=backend)
    lines, on_top = mesh_lines(shown, backend)
    assert on_top
    assert lines.shape == (3 * mesh.ne, 8, 2)
    r = np.linalg.norm(lines, axis=2)
    ends_on_circle = (np.abs(r[:, 0] - 1) < 1e-9) & (np.abs(r[:, -1] - 1) < 1e-9)
    assert ends_on_circle.sum() >= 20
    assert np.median(np.abs(r[ends_on_circle, 4] - 1)) < 1e-4
    assert r.max() < 1 + 1e-4
    if backend == "plotly":
        assert not [t for t in shown.data if t.type == "heatmap"]
    else:
        assert not shown.images
        assert "mesh" in [t.get_text() for t in shown.get_legend().get_texts()]


@pytest.mark.parametrize("quads", [False, True])
def test_curved_boundary_edges_lie_on_the_circle(ngs, backend, quads):
    disc = px.QuickSketch().Circle((0, 0), 1).Face()
    mesh = ngs.Mesh(px.OCCGeometry(disc).GenerateMesh(maxh=0.3, quad_dominated=quads))
    mesh.Curve(3)
    kinds = {str(el.type) for el in mesh.Elements(ngs.VOL)}
    assert ("ET.QUAD" in kinds) == quads
    lines, _ = mesh_lines(px.PlotMesh(mesh, disc, backend=backend), backend)
    starts, ends = lines[:, 0], lines[:, -1]
    segments = 0
    for el in mesh.Elements(ngs.BND):
        a, b = (np.array(mesh.vertices[v.nr].point) for v in el.vertices)
        same = (np.abs(starts - a).max(axis=1) < 1e-9) & (np.abs(ends - b).max(axis=1) < 1e-9)
        flipped = (np.abs(starts - b).max(axis=1) < 1e-9) & (np.abs(ends - a).max(axis=1) < 1e-9)
        edge, = lines[same | flipped]
        assert np.abs(np.linalg.norm(edge, axis=1) - 1).max() < 1e-4
        segments += 1
    assert segments >= 15


def test_straight_mesh_draws_two_points_per_edge(ngs, backend):
    shape = holed_disc()
    mesh = ngs.Mesh(px.OCCGeometry(shape).GenerateMesh(maxh=0.25))
    lines, _ = mesh_lines(px.PlotMesh(mesh, shape, backend=backend), backend)
    assert lines.shape == (3 * mesh.ne, 2, 2)


def test_mesh_is_drawn_into_the_axes_of_a_field(disc, scalar, mpl):
    shape, mesh = disc
    ax = px.PlotField(scalar, mesh, shape)
    assert not [c for c in ax.collections if c.get_label() == "mesh"]
    assert px.PlotMesh(mesh, shape, ax=ax) is ax
    assert len(ax.images) == 1 and len([c for c in ax.collections if c.get_label() == "mesh"]) == 1


def test_on_edges_takes_no_field(ngs, disc, scalar, mpl):
    shape, mesh = disc
    with pytest.raises(ValueError, match="on='edges' takes planex measurements"):
        px.PlotField(scalar, shape, on="edges")
    with pytest.raises(ValueError, match="on='edges' takes planex measurements"):
        px.PlotField(ngs.x + 0, mesh, shape, on="edges")
    with pytest.raises(ValueError, match="on='edges' takes planex measurements"):
        px.PlotField(ngs.CF((ngs.y, -ngs.x)), mesh, shape, vectors=True, on="edges")
    with pytest.raises(ValueError, match="on is 'faces' or 'edges'"):
        px.PlotField(scalar, shape, on="lines")


def test_the_main_inputs_are_positional_and_the_mesh_may_be_left_out(ngs, disc, scalar, mpl):
    shape, mesh = disc
    a, _ = raster(px.PlotField(scalar, shape), "matplotlib")
    b, _ = raster(px.PlotField(scalar, mesh, shape), "matplotlib")
    np.testing.assert_array_equal(a, b)
    with pytest.raises(TypeError, match="mesh is an ngsolve.Mesh"):
        px.PlotField(scalar, shape, shape)
    with pytest.raises(TypeError):
        px.PlotField(field=scalar, obj=shape)
    import planex.measurements as pm

    with pytest.raises(ValueError, match="without a mesh"):
        px.PlotField(pm.MinThickness(), mesh, shape)
    with pytest.raises(ValueError, match="needs the object it measures"):
        px.PlotField(pm.MinThickness())


def test_a_2d_mesh_without_geometry_is_its_own_region(ngs, mpl, backend):
    from netgen.geom2d import unit_square

    mesh = ngs.Mesh(unit_square.GenerateMesh(maxh=0.25))
    z, _ = raster(px.PlotField(ngs.x + 0, mesh, backend=backend), backend)
    assert np.isfinite(z).sum() > 0.95 * z.size and np.nanmax(z) == pytest.approx(1, abs=0.01)
    for ax in (px.PlotMesh(mesh), px.PlotMesh(mesh, px.XY)):
        outline, = [c for c in ax.collections if c.get_label() == "_nolegend_"]
        assert [c for c in ax.collections if c.get_label() == "mesh"]
        ends = np.array([s[[0, -1]] for s in outline.get_segments()], dtype=float)
        assert len(ends) == len(mesh.ngmesh.Elements1D())
        on_side = np.isclose(ends, 0, atol=1e-12) | np.isclose(ends, 1, atol=1e-12)
        assert (on_side[..., 0] | on_side[..., 1]).all()
    with pytest.raises(ValueError, match="2d mesh lies on the XY plane"):
        px.PlotMesh(mesh, px.Plane(origin=(0, 0, 1), normal=(1, 0, 0)))


def test_fields_and_meshes_only_on_the_xy_plane(ngs, disc, scalar, mpl):
    shape, mesh = disc
    for plane in (px.Plane(origin=(0, 0, 1), normal=(0, 0, 1)), px.Plane(origin=(2, 3, 0), normal=(0, 0, 1)),
                  px.Plane(origin=(0, 0, 0), normal=(0, 0, 1), axis=(1, 1, 0)), px.YZ):
        placed = px.QuickSketch(plane).Circle((0, 0), 1).Face()
        with pytest.raises(ValueError, match="XY plane only for now"):
            px.PlotField(scalar, placed)
        with pytest.raises(ValueError, match="XY plane only for now"):
            px.PlotMesh(mesh, placed)
        assert px.Plot(placed) is not None
    on_xy = px.QuickSketch(px.XY).Circle((0, 0), 1).Face() - px.QuickSketch().Circle((0.3, 0), 0.3).Face()
    np.testing.assert_array_equal(raster(px.PlotField(scalar, on_xy), "matplotlib")[0],
                                  expected(shape, mesh, scalar, *pixels(shape)[:2]))


def test_other_meshes_raise(ngs, disc, mpl):
    shape, _ = disc
    lifted = px.QuickSketch(px.Plane(origin=(0.0, 0.0, 1.0), normal=(0.0, 0.0, 1.0))).Rectangle(1, 1).Face()
    surface = ngs.Mesh(px.OCCGeometry(lifted).GenerateMesh(maxh=0.5))
    assert surface.dim == 3 and surface.ne == 0
    with pytest.raises(ValueError, match="surface meshes"):
        px.PlotMesh(surface, lifted)
    with pytest.raises(ValueError, match="surface meshes"):
        px.PlotField(ngs.x + 0, surface, lifted)
    with pytest.raises(ValueError, match="surface meshes"):
        px.PlotField(ngs.GridFunction(ngs.H1(surface, order=1)), lifted)
    netgen_mesh = px.OCCGeometry(shape).GenerateMesh(maxh=0.5)
    with pytest.raises(TypeError, match="ngsolve.Mesh"):
        px.PlotMesh(netgen_mesh, shape)
    with pytest.raises(TypeError, match="ngsolve.Mesh"):
        px.PlotField(ngs.x + 0, netgen_mesh, shape)


@pytest.fixture(scope="module")
def straight(ngs):
    shape = holed_disc()
    return shape, ngs.Mesh(px.OCCGeometry(shape).GenerateMesh(maxh=0.1))


def test_straight_mesh_leaves_the_curved_boundary_gap_empty(ngs, straight, backend):
    shape, mesh = straight
    xs, ys, _ = pixels(shape)
    gx, gy = np.meshgrid(xs, ys)
    x, y = gx.ravel(), gy.ravel()
    inside = shape.Contains(np.c_[x, y])
    found = mesh(x, y)["nr"] >= 0
    assert (inside & ~found).sum() > 50 and (found & ~inside).sum() > 50
    z, _ = raster(px.PlotField(ngs.x - ngs.y * ngs.y, mesh, shape, backend=backend), backend)
    np.testing.assert_array_equal(np.isnan(z).ravel(), ~(inside & found))


def test_only_points_inside_the_shape_reach_the_mesh(ngs, straight, monkeypatch, mpl):
    import planex._plot_mesh

    seen = []
    at = planex._plot_mesh._at

    def recording(f, mesh, pts, plane):
        seen.append(np.array(pts))
        return at(f, mesh, pts, plane)

    monkeypatch.setattr(planex._plot_mesh, "_at", recording)
    shape, mesh = straight
    px.PlotField(ngs.CF((ngs.y, -ngs.x)), mesh, shape, vectors=True)
    assert len(seen) == 2 and min(len(p) for p in seen) > 100
    for pts in seen:
        assert shape.Contains(pts).all()


def eccentric_ring():
    return px.QuickSketch().Circle((0, 0), 2).Face() - px.QuickSketch().Circle((0.5, 0), 1).Face()


@pytest.fixture(scope="module")
def round_disc(ngs):
    shape = px.QuickSketch().Circle((0, 0), 1).Face()
    mesh = ngs.Mesh(px.OCCGeometry(shape).GenerateMesh(maxh=0.25))
    mesh.Curve(3)
    return shape, mesh


def pieces(trace, *names):
    """the polylines of a plotly trace with None between them, with the first value of every other array named"""
    x, y, *more = (np.array([np.nan if a is None else a for a in getattr(trace, n)], dtype=float)
                   for n in ("x", "y") + names)
    out = []
    for at in np.split(np.arange(len(x)), np.flatnonzero(np.isnan(x)) + 1):
        at = at[~np.isnan(x[at])]
        if len(at):
            out.append((np.c_[x[at], y[at]], *(m[at[0]] for m in more)))
    return out


def drawn_contours(shown, backend):
    """None without contours, else their levels (matplotlib: all, plotly: those with lines) and {level: lines}"""
    if backend == "plotly":
        traces = [t for t in shown.data if t.name == "contours"]
        if not traces:
            return None
        lines = {}
        for line, level in pieces(traces[0], "customdata"):
            lines.setdefault(float(level), []).append(line)
        return np.array(sorted(lines)), lines
    from matplotlib.contour import ContourSet

    sets = [c for c in shown.collections if isinstance(c, ContourSet)]
    if not sets:
        return None
    cs, = sets
    lines = {float(level): [s for s in segs if len(s)] for level, segs in zip(cs.levels, cs.allsegs)}
    return np.asarray(cs.levels), {level: segs for level, segs in lines.items() if segs}


def drawn_field_lines(shown, backend):
    """None without field lines, else the lines and the number of arrowheads"""
    if backend == "plotly":
        traces = [t for t in shown.data if t.name == "field lines"]
        if not traces:
            return None
        arrows, = [t for t in shown.data if t.name == "field line arrows"]
        lines = [line for line, in pieces(traces[0])]
        heads = np.c_[np.asarray(arrows.x, dtype=float), np.asarray(arrows.y, dtype=float)]
        for line, head in zip(lines, heads):
            s = np.cumsum(np.hypot(*np.diff(line, axis=0).T))
            n = np.searchsorted(s, s[-1] / 2.0)
            np.testing.assert_array_equal(head, line[n:n + 2].mean(axis=0))
        return lines, len(heads)
    from matplotlib.patches import FancyArrowPatch

    found = [c for c in shown.collections if c.get_gid() == "field lines"]
    if not found:
        return None
    lc, = found
    return list(lc.get_segments()), len([p for p in shown.patches if isinstance(p, FancyArrowPatch)])


def same_lines(a, b):
    return len(a) == len(b) and all(np.array_equal(p, q) for p, q in zip(a, b))


def test_contours_n_levels_inside_the_colour_range_the_same_in_both_backends(ngs, disc, mpl, plotly):
    shape, mesh = disc
    f = ngs.x + 2 * ngs.y
    for kwargs, ends in ((dict(), (None, None)), (dict(min=-1.0, max=1.5), (-1.0, 1.5)), (dict(min=0.0), (0.0, None))):
        drawn = {}
        for backend in ("matplotlib", "plotly"):
            shown = px.PlotField(f, mesh, shape, contours=4, samples=200, backend=backend, **kwargs)
            z, _ = raster(shown, backend)
            lo = np.nanmin(z) if ends[0] is None else ends[0]
            hi = np.nanmax(z) if ends[1] is None else ends[1]
            expected = np.linspace(lo, hi, 6)[1:-1]
            levels, drawn[backend] = drawn_contours(shown, backend)
            np.testing.assert_array_equal(levels, expected)
            for level, lines in drawn[backend].items():
                for line in lines:
                    assert np.abs(line[:, 0] + 2 * line[:, 1] - level).max() < 1e-9
        assert drawn["matplotlib"].keys() == drawn["plotly"].keys()
        for level, lines in drawn["matplotlib"].items():
            assert same_lines(lines, drawn["plotly"][level])


def test_contours_list_gives_exactly_those_levels(ngs, disc, backend):
    shape, mesh = disc
    shown = px.PlotField(ngs.x + 2 * ngs.y, mesh, shape, contours=[1.0, -0.5, 1.0, 0.25, 9.0], samples=200, backend=backend, min=-0.2, max=0.2)
    levels, lines = drawn_contours(shown, backend)
    np.testing.assert_array_equal(levels, [-0.5, 0.25, 1.0, 9.0] if backend == "matplotlib" else [-0.5, 0.25, 1.0])
    assert sorted(lines) == [-0.5, 0.25, 1.0]


@pytest.mark.parametrize("method", ["ball", "ray"])
def test_contours_on_measurements(method, mpl, plotly):
    from planex._plot import _disc_raster, _field

    shape = eccentric_ring()
    measure = pm.MinThickness(method=method)
    data = _field(shape, measure, 80)
    values = _disc_raster(data)[2] if method == "ball" else np.asarray(data["values"], dtype=float)
    for lo, hi in ((None, None), (0.7, 1.3)):
        expected = np.linspace(np.nanmin(values) if lo is None else lo, np.nanmax(values) if hi is None else hi, 5)[1:-1]
        drawn = {}
        for backend in ("matplotlib", "plotly"):
            shown = px.PlotField(measure, shape, samples=80, contours=3, min=given(lo), max=given(hi), backend=backend)
            levels, drawn[backend] = drawn_contours(shown, backend)
            if backend == "matplotlib":
                np.testing.assert_array_equal(levels, expected)
            assert set(drawn[backend]) <= set(expected.tolist()) and drawn[backend]
        assert drawn["matplotlib"].keys() == drawn["plotly"].keys()
        for level, lines in drawn["matplotlib"].items():
            assert same_lines(lines, drawn["plotly"][level])


def test_contours_on_edge_colours_warn_and_draw_nothing(backend):
    with pytest.warns(px.PlanexWarning, match="contours"):
        shown = px.PlotField(pm.MinThickness(method="ray"), ring(), samples=80, contours=3, backend=backend, on="edges")
    assert drawn_contours(shown, backend) is None
    with pytest.raises(TypeError):
        px.Plot(ring(), contours=[1.0], backend=backend)


def test_contours_and_field_lines_take_only_their_values(mpl):
    for bad in (0, -1, 2.5, True, [], "levels", [[0.0, 1.0]], [0.0, "a"], [float("nan")]):
        with pytest.raises(ValueError, match="contours="):
            px.PlotField(pm.MinThickness(), ring(), samples=60, contours=bad)
    for bad in (0, -1.0, "dense", None, float("inf")):
        with pytest.raises(ValueError, match="field_lines="):
            px.PlotField(pm.MinThickness(), ring(), field_lines=bad)


def test_field_lines_of_a_rotation_circle_its_centre(ngs, round_disc, backend):
    shape, mesh = round_disc
    shown = px.PlotField(ngs.CF((-ngs.y, ngs.x)), mesh, shape, field_lines=True, backend=backend)
    lines, heads = drawn_field_lines(shown, backend)
    assert heads == len(lines)
    long = [line for line in lines if len(line) > 20 and np.linalg.norm(line, axis=1).mean() > 0.2]
    assert len(long) >= 5
    for line in long:
        assert np.ptp(np.linalg.norm(line, axis=1)) < 0.01
        step = np.diff(line, axis=0)
        moving = np.linalg.norm(step, axis=1) > 0
        assert (line[:-1, 0] * step[:, 1] - line[:-1, 1] * step[:, 0])[moving].min() > 0
    swept = [np.abs(np.diff(np.unwrap(np.arctan2(line[:, 1], line[:, 0])))).sum() for line in long]
    assert sum(s > 1.8 * np.pi for s in swept) >= 3


def test_field_lines_density_changes_the_line_count(ngs, round_disc, mpl):
    shape, mesh = round_disc
    field = ngs.CF((-ngs.y, ngs.x))
    counts = [len(drawn_field_lines(px.PlotField(field, mesh, shape, field_lines=density),
                                    "matplotlib")[0]) for density in (0.5, True, 2)]
    assert counts[0] < counts[1] < counts[2]


def test_field_lines_match_in_both_backends_with_vectors_and_contours(ngs, round_disc, mpl, plotly):
    from matplotlib.quiver import Quiver

    shape, mesh = round_disc
    spiral = ngs.CF((ngs.x - ngs.y, ngs.x + ngs.y))
    kwargs = dict(field_lines=1.5, vectors=True, contours=3)
    ax = px.PlotField(spiral, mesh, shape, **kwargs)
    fig = px.PlotField(spiral, mesh, shape, backend="plotly", **kwargs)
    lines, _ = drawn_field_lines(ax, "matplotlib")
    assert len(lines) > 5 and same_lines(lines, drawn_field_lines(fig, "plotly")[0])
    assert drawn_contours(ax, "matplotlib")[1] and drawn_contours(fig, "plotly")[1]
    assert [c for c in ax.collections if isinstance(c, Quiver)] and [t for t in fig.data if t.name == "vectors"]


def test_field_lines_need_a_real_vector_field(ngs, disc, scalar, mpl):
    shape, mesh = disc
    with pytest.warns(px.PlanexWarning, match="field lines need a vector field"):
        ax = px.PlotField(scalar, shape, field_lines=True)
    assert drawn_field_lines(ax, "matplotlib") is None
    with pytest.warns(px.PlanexWarning, match="field lines need a vector field"):
        ax = px.PlotField(pm.MinThickness(), ring(), samples=60, field_lines=True)
    assert drawn_field_lines(ax, "matplotlib") is None
    with pytest.warns(px.PlanexWarning, match="field lines need a vector field"):
        px.PlotField(pm.MinThickness(method="ray"), ring(), samples=60, field_lines=2, on="edges")
    with pytest.warns(px.PlanexWarning, match="field lines need a vector field"):
        px.PlotField(ngs.GridFunction(ngs.H1(mesh, order=1, complex=True)), shape, field_lines=True)
    with pytest.raises(ValueError, match="webgpu"):
        px.PlotField(ngs.GridFunction(ngs.VectorH1(mesh, order=1, complex=True)), shape, field_lines=True)
    with pytest.raises(ValueError, match="two components"):
        px.PlotField(ngs.CF((ngs.y, -ngs.x, ngs.x)), mesh, shape, field_lines=True)


def test_triangulation_masks_the_triangles_outside_the_faces():
    pytest.importorskip("matplotlib")
    from planex._plot import _field, _triangulation

    shape = ring()
    data = _field(shape, pm.MinThickness(method="ray"), 80)
    tri, values = _triangulation(shape, data)
    pts = np.asarray(data["points"], dtype=float)
    centres = pts[tri.triangles].mean(axis=1)
    one_by_one = [not shape.Contains((float(c[0]), float(c[1]))) for c in centres]
    assert tri.mask.tolist() == one_by_one
    assert any(one_by_one) and not all(one_by_one)
    assert values.tolist() == [float(v) for v in data["values"]]


def test_plot_without_a_field_does_not_import_ngsolve():
    pytest.importorskip("matplotlib")
    code = "\n".join([
        "import sys",
        "import matplotlib",
        "matplotlib.use('Agg')",
        "import planex as px",
        "import planex.measurements as pm",
        "assert 'ngsolve' not in sys.modules, 'import planex imports ngsolve'",
        "f = px.QuickSketch().Circle((0, 0), 2).Face() - px.QuickSketch().Circle((0, 0), 1).Face()",
        "px.Plot(f, face_col='C0')",
        "px.PlotField(pm.MinThickness(), f, samples=60, min=0.5)",
        "px.PlotField(pm.MinThickness(method='ray'), f, on='edges', samples=60, max=1.5)",
        "try:",
        "    import plotly",
        "except ImportError:",
        "    plotly = None",
        "if plotly is not None:",
        "    px.PlotField(pm.MinThickness(), f, backend='plotly', samples=60)",
        "assert 'ngsolve' not in sys.modules, 'Plot imports ngsolve'",
    ])
    result = subprocess.run([sys.executable, "-c", code], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr


def colormap_shown(backend, **kwargs):
    """the colours a thickness map is drawn with: matplotlib's Colormap, or plotly's colorscale"""
    plate = px.FromPolyline([(0, 0), (2, 0), (2, 1), (0, 1)], closed=True).Face()
    shown = px.PlotField(pm.MinThickness(), plate, backend=backend, **kwargs)
    if backend == "matplotlib":
        return shown.get_images()[0].get_cmap()
    return [t for t in shown.data if t.type == "heatmap"][0].colorscale


@pytest.mark.parametrize("backend", ["matplotlib", "plotly"])
def test_the_default_colormap_is_the_docs_map(backend):
    matplotlib = pytest.importorskip("matplotlib")
    matplotlib.use("Agg")
    from matplotlib.colors import to_hex, to_rgba

    if backend == "plotly":
        pytest.importorskip("plotly")
        assert [c for _, c in colormap_shown(backend)] == px._plot_style.COLORMAP
        assert [c for _, c in colormap_shown(backend, colormap="default_r")] == px._plot_style.COLORMAP[::-1]
        viridis = colormap_shown(backend, colormap="viridis")
        assert len(viridis) == 32 and viridis[0][1] == "#440154" and viridis[-1][1] == "#fde725"
        assert [c for _, c in colormap_shown(backend, colormap=["white", "black"])][::31] == ["#ffffff", "#000000"]
    else:
        cmap = colormap_shown(backend)
        assert to_hex(cmap(0.0)) == "#d1495b" and to_hex(cmap(1.0)) == "#0f7ea8"
        assert to_hex(colormap_shown(backend, colormap="default_r")(0.0)) == "#0f7ea8"
        assert colormap_shown(backend, colormap="coolwarm_r").name == "coolwarm_r"
        assert colormap_shown(backend, colormap=["white", "black"])(0.0) == to_rgba("white")
    with pytest.raises(ValueError, match="colormap 'nope'"):
        colormap_shown(backend, colormap="nope")
