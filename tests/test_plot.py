# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import importlib.util
import sys
import types

import pytest

import planex as px
from helpers import square, stub_webgui


def legend_labels(ax):
    return [t.get_text() for t in ax.get_legend().get_texts()]


class TestMatplotlib:
    @pytest.fixture(autouse=True)
    def backend(self):
        matplotlib = pytest.importorskip("matplotlib")
        matplotlib.use("Agg")
        yield
        import matplotlib.pyplot as plt
        plt.close("all")

    def test_faces_take_the_planex_face_colour_by_default(self):
        from matplotlib.colors import to_rgba
        from matplotlib.patches import PathPatch
        ax = square(0, 0).Plot()
        fills = [p for p in ax.patches if isinstance(p, PathPatch)]
        assert len(fills) == 1 and fills[0].get_facecolor() == pytest.approx(to_rgba("#dde6f0"))
        assert ax.get_legend() is None
        assert not [p for p in square(0, 0).Plot(face_col=None).patches if isinstance(p, PathPatch)]
        sq = square(0, 0)
        assert legend_labels(sq.Plot(face_col="C0")) == [sq.id]

    def test_draws_edges_and_fills_the_face_with_holes(self):
        from matplotlib.collections import LineCollection
        p = square(0, 0, 3) - square(1, 1, 1)
        ax = p.Plot(face_col="C0", line_col="type")
        lines = [c for c in ax.collections if isinstance(c, LineCollection)]
        assert sum(len(c.get_segments()) for c in lines) == 8
        assert lines[0].get_colors().tolist() == [[0.0, 0.0, 0.0, 1.0]]
        assert len(ax.patches) == 1
        assert len(ax.patches[0].get_path().vertices) == 10
        assert ax.get_aspect() == 1.0
        assert legend_labels(ax) == ["lines", p.id]

    def test_every_edge_in_one_colour_by_default(self):
        from matplotlib.collections import LineCollection
        wp = px.Move((0, 0)).Rectangle(1, 1).Circle((3, 3), 1)
        wp.Spline([(5, 5), (6, 4)], start_from_localpos=False)
        shape = wp.Shape()
        ax = shape.Plot(face_col="C0")
        colours = {tuple(c) for coll in ax.collections if isinstance(coll, LineCollection) for c in coll.get_colors()}
        assert colours == {(0.0, 0.0, 0.0, 1.0)}
        assert legend_labels(ax) == [shape.id]
        ax = wp.Shape().Plot(line_col="C3")
        from matplotlib.colors import to_rgba
        assert {tuple(c) for coll in ax.collections for c in coll.get_colors()} == {to_rgba("C3")}
        assert ax.get_legend() is None

    def test_marks_small_free_and_chosen_edges(self):
        p = px.Compound([square(0, 0, 2), px.Line(px.Vertex((5, 5)), px.Vertex((5.001, 5)))])
        ax = p.Plot(mark_edges_smaller=0.01, mark_free_edges=True, highlight=[0, 1], line_col="type")
        markers = [l for l in ax.lines if l.get_marker() == "x"]
        assert [len(l.get_xdata()) for l in markers] == [1, 1, 2]
        assert legend_labels(ax) == ["lines", "edges < 0.01", "free edges", "marked edges"]
        ax = p.Plot(highlight=[False] * 4 + [True], legend=False)
        assert [len(l.get_xdata()) for l in ax.lines if l.get_marker() == "x"] == [1]
        assert ax.get_legend() is None

    def test_wire_workplane_and_repeated_plots_into_one_axis(self):
        import matplotlib.pyplot as plt
        fig, ax = plt.subplots()
        wp = px.Move((0, 0)).Rectangle(1, 1).Circle((3, 3), 1)
        wp.Spline([(5, 5), (6, 4)], start_from_localpos=False, id="s")
        filled = wp.Shape()
        assert px.Plot(filled, ax=ax, face_col="C5", line_col="type") is ax
        assert wp.Wire("s").Plot(ax=ax, line_col="type") is ax
        assert wp.Shape().Plot(ax=ax, line_col="type", face_col=None) is ax
        for w in wp.Wires():
            w.Plot(ax=ax, line_col="type")
        assert len(ax.patches) == 1
        assert sorted(legend_labels(ax)) == sorted(["arcs", filled.id, "lines", "splines"])

    def test_property_based_colors(self):
        wp = px.Move((0, 0)).Line(1, col=(1, 0, 0)).Line(0, 1).Close()
        ax = wp.Wire().Plot(line_col="property")
        colors = {tuple(c) for coll in ax.collections for c in coll.get_colors()}
        assert colors == {(1.0, 0.0, 0.0, 1.0), (0.0, 0.0, 0.0, 1.0)}


class TestPlotly:
    @pytest.fixture(autouse=True)
    def backend(self):
        pytest.importorskip("plotly")

    def test_draws_edges_face_and_marks(self):
        p = px.Compound([square(0, 0, 3) - square(1, 1, 1), px.Line(px.Vertex((5, 5)), px.Vertex((5.001, 5)))])
        fig = p.Plot(backend="plotly", face_col="C0", mark_edges_smaller=0.01, mark_free_edges=True, highlight=[0],
                     line_col="type")
        assert [t.name for t in fig.data] == ["lines", "edges < 0.01", "free edges", "marked edges"]
        assert fig.data[0].line.color == "rgba(0,0,0,1.0)"
        assert sum(x is None for x in fig.data[0].x) == 9
        assert len(fig.layout.shapes) == 1
        assert fig.layout.shapes[0].fillrule == "nonzero"
        assert fig.layout.shapes[0].path.count("Z") == 2
        assert fig.layout.shapes[0].showlegend
        assert fig.layout.yaxis.scaleanchor == "x"

    def test_every_edge_in_one_colour_by_default(self):
        wp = px.Move((0, 0)).Rectangle(1, 1).Circle((3, 3), 1)
        fig = wp.Shape().Plot(backend="plotly", face_col="C0")
        edges = [t for t in fig.data if t.mode == "lines"]
        assert [t.name for t in edges] == ["edges"] and not edges[0].showlegend
        assert edges[0].line.color == "rgba(0,0,0,1.0)"

    def test_figure_takes_the_aspect_ratio_of_the_drawing(self):
        fig = square(0, 0, 3).Plot(backend="plotly")
        assert (fig.layout.width, fig.layout.height) == (600 + 210, 600 + 80)
        fig = px.Move((0, 0)).Rectangle(10, 1).Wire().Plot(backend="plotly")
        assert (fig.layout.width, fig.layout.height) == (1000 + 210, 150 + 80)

    def test_figure_selects_the_backend_and_legend_entries_are_unique(self):
        import plotly.graph_objects as go
        fig = go.Figure()
        wp = px.Move((0, 0)).Rectangle(1, 1).Circle((3, 3), 1, id="c")
        first = wp.Shape()
        assert px.Plot(first, ax=fig, line_col="type") is fig
        wp.Shape().Plot(ax=fig, face_col="C1", line_col="type")
        wp.Wire("c").Plot(ax=fig, line_col="type")
        shown = [t.name for t in fig.data if t.showlegend]
        assert sorted(shown) == ["arcs", "lines"]
        assert len(fig.data) > len(shown)
        assert all(t.legendgroup == t.name for t in fig.data)
        assert fig.layout.shapes[0].legendgroup == first.id
        assert px.Plot(wp.Shape(), backend="plotly", legend=False).data[0].showlegend is False

    def test_property_based_colors(self):
        wp = px.Move((0, 0)).Line(1, col=(1, 0, 0)).Line(0, 1).Close()
        fig = wp.Wire().Plot(backend="plotly", line_col="property")
        colors = {t.line.color for t in fig.data}
        assert colors == {"rgba(255,0,0,1.0)", "rgba(0,0,0,1.0)"}
        assert not any(t.showlegend for t in fig.data)


def test_unknown_backend():
    with pytest.raises(ValueError):
        square(0, 0).Plot(backend="bokeh")


def sketch_with_references():
    s = px.Move((0, 0)).Line((2, 0), construction=True).Line((2, 0)).Line((2, 1)).Close()
    return s.Vertex("p", (1, 1)).Vertex("q", (0, 1))


def test_px_plot_of_a_sketch_is_the_sketch_look_in_both_backends():
    matplotlib = pytest.importorskip("matplotlib")
    matplotlib.use("Agg")
    from matplotlib.collections import LineCollection
    sk = sketch_with_references()
    ax = px.Plot(sk)
    dashed = [c for c in ax.collections if isinstance(c, LineCollection) and c.get_linestyle()[0][1] is not None]
    assert len(dashed) == 1 and len(dashed[0].get_segments()) == 1
    assert ax.get_aspect() == 1.0 and ax.get_legend() is None
    assert px.Plot(sk, show_constraints=False) is not None and sk.Plot(ax=ax) is ax
    with pytest.raises(ValueError, match="face_col.*sk.Shape"):
        px.Plot(sk, face_col="C0")
    with pytest.raises(ValueError, match="show_ends draw a sketch"):
        px.Plot(sk.Shape(), show_ends=True)
    assert px.Plot(sk.Shape(), face_col="C0") is not None
    if importlib.util.find_spec("plotly"):
        import plotly.graph_objects as go
        assert isinstance(px.Plot(sk, backend="plotly"), go.Figure)
    import matplotlib.pyplot as plt
    plt.close("all")


def test_plot3d_draws_everything_as_one_occ_compound():
    from netgen import occ

    from planex._plot3d import _plot3d_compound

    f = px.Move((0, 0)).Rectangle(1, 1).Face()
    lifted = f.Translated((0, 0, 2))
    s = px.Move((0, 0)).Line((2, 0))
    compound = _plot3d_compound([f, lifted, [px.Line((0, 0), (1, 1)), s], px.Compound([f]),
                                 occ.Box((3, 0, 0), (4, 1, 1))])
    assert len(compound.faces) == 3 + 6 and len(compound.solids) == 1
    assert max(face.center.z for face in compound.faces) == pytest.approx(2)
    assert len(_plot3d_compound(f).faces) == 1
    with pytest.raises(TypeError, match="Plot3d"):
        px.Plot3d([f, 1])
    with pytest.raises(ValueError, match="nothing to draw"):
        px.Plot3d([px.QuickSketch()])


@pytest.fixture
def draws(monkeypatch):
    calls = []

    def draw(backend):
        def Draw(obj, **kwargs):
            calls.append((backend, obj, kwargs))
            return backend + " scene"
        return Draw

    jupyter = types.ModuleType("ngsolve_webgpu.jupyter")
    jupyter.Draw = draw("webgpu")
    monkeypatch.setitem(sys.modules, "ngsolve_webgpu.jupyter", jupyter)
    stub_webgui(monkeypatch, draw("webgui"))
    return calls


@pytest.mark.parametrize("backend", ["webgpu", "webgui"])
def test_plot3d_hands_the_compound_and_kwargs_to_the_backend(draws, backend):
    from netgen import occ

    f = px.Move((0, 0)).Rectangle(1, 1).Face()
    scene = px.Plot3d([f, occ.Box((3, 0, 0), (4, 1, 1))], backend=backend, width=800, height=500, order=1)
    assert scene == backend + " scene"
    (drawn, shape, kwargs), = draws
    assert drawn == backend and isinstance(shape, occ.TopoDS_Shape)
    assert len(shape.faces) == 1 + 6 and len(shape.solids) == 1
    assert kwargs == {"width": 800, "height": 500, "order": 1}
    assert f.Plot3d(backend=backend) == backend + " scene" and len(draws[1][1].faces) == 1


def test_plot3d_draws_with_webgpu_by_default(draws):
    assert px.Move((0, 0)).Rectangle(1, 1).Face().Plot3d() == "webgpu scene"
    assert px.Plot3d(px.XY, plane_size=2) == "webgpu scene"
    assert [d[0] for d in draws] == ["webgpu", "webgpu"] and draws[1][2] == {}


@pytest.mark.occ
def test_plot3d_webgpu_without_the_package_says_how_to_install(monkeypatch):
    monkeypatch.setitem(sys.modules, "ngsolve_webgpu.jupyter", None)
    with pytest.raises(ImportError, match="pip install ngsolve_webgpu.*backend='webgui'"):
        px.Plot3d(px.Move((0, 0)).Rectangle(1, 1).Face())


def test_plot3d_webgui_imports_no_webgpu(monkeypatch):
    before = set(sys.modules)
    calls = []
    stub_webgui(monkeypatch, lambda obj, **kwargs: calls.append(obj) or "webgui scene")
    assert px.Plot3d(px.Move((0, 0)).Rectangle(1, 1).Face(), backend="webgui") == "webgui scene"
    assert len(calls) == 1 and len(calls[0].faces) == 1
    assert not [m for m in set(sys.modules) - before if m.split(".")[0] in ("webgpu", "ngsolve_webgpu")]


def test_plot3d_unknown_backend():
    with pytest.raises(ValueError, match="'vtk'.*'webgpu' or 'webgui'"):
        px.Plot3d(px.Move((0, 0)).Rectangle(1, 1).Face(), backend="vtk")


def test_plotly_fills_overlapping_compound_parts():
    pytest.importorskip("plotly")
    discs = px.Compound([px.QuickSketch().Circle((0, 0), 1).Face(), px.QuickSketch().Circle((1, 0), 1).Face()])
    fig = discs.Plot(backend="plotly", face_col="C0")
    assert fig.layout.shapes[0].fillrule == "nonzero"


def test_highlight_takes_ids_selected_edges_and_selections():
    matplotlib = pytest.importorskip("matplotlib")
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    s = px.Move((0, 0)).Rectangle(2, 1).Shape()
    e = s.edges[2]
    local = e.id[len(s.id) + 1:]

    def marked(highlight):
        line, = [l for l in s.Plot(highlight=highlight).lines if l.get_label() == "marked edges"]
        return sorted(zip(line.get_xdata(), line.get_ydata()))

    expected = marked([2])
    assert expected == [(e.midpoint.x, e.midpoint.y)]
    for highlight in (local, e.id, e, [e], s(local), [local]):
        assert marked(highlight) == expected
    assert len(marked(s.faces[0])) == 4
    with pytest.raises(ValueError, match="no element 'Line.99'"):
        s.Plot(highlight="Line.99")
    plt.close("all")
