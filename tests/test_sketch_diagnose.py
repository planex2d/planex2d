# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""sk.Diagnose and Plot(show_diagnostics=): the five kinds of geometry faults of a sketch's drawing, on both sketch
classes, and their markers in both plot backends."""
import math

import pytest

import planex as px

KINDS = ("open_ends", "missing_coincident", "crossings", "overlaps", "degenerate")


@pytest.fixture(params=[px.Sketch, px.QuickSketch], ids=["Sketch", "QuickSketch"])
def cls(request):
    return request.param


def _ids(found, key):
    return sorted(e["ids"] for e in found[key])


def _close(a, b, tol=1e-9):
    return abs(a[0] - b[0]) <= tol and abs(a[1] - b[1]) <= tol


def _only(found, *keys):
    """the kinds not named are empty"""
    return all(not found[k] for k in KINDS if k not in keys)


def test_open_chain_has_two_open_ends(cls):
    found = cls().Move((0, 0)).Line((2, 0), id="a").Line((2, 1), id="b").Diagnose()
    assert set(found) == set(KINDS)
    assert _ids(found, "open_ends") == [("a",), ("b",)]
    at = {e["ids"]: e["at"] for e in found["open_ends"]}
    assert _close(at[("a",)], (0, 0)) and _close(at[("b",)], (2, 1))
    assert _only(found, "open_ends")


def test_closed_profile_is_clean(cls):
    sk = cls().Move((0, 0)).Line((4, 0)).Line((4, 3)).Line((0, 3)).Line((0, 0))
    sk.Circle((2, 1.5), 0.5)
    assert sk.Diagnose() == {k: [] for k in KINDS}


def test_missing_coincident_below_the_default_gap(cls):
    sk = cls().Move((0, 0)).Line((1, 0), id="a").Move((1 + 1e-4, 0)).Line((1 + 1e-4, 1), id="b")
    found = sk.Diagnose()
    assert _ids(found, "missing_coincident") == [("a", "b")]
    assert _close(found["missing_coincident"][0]["at"], (1 + 5e-5, 0), 1e-12)
    assert len(found["open_ends"]) == 4
    assert _only(found, "open_ends", "missing_coincident")
    assert sk.Diagnose(gap=1e-5)["missing_coincident"] == []
    assert _ids(sk.Diagnose(gap=1e-3), "missing_coincident") == [("a", "b")]


def test_crossing_lines(cls):
    found = cls().Move((0, 0)).Line((2, 2), id="a").Move((0, 2)).Line((2, 0), id="b").Diagnose()
    assert _ids(found, "crossings") == [("a", "b")]
    assert _close(found["crossings"][0]["at"], (1, 1))
    assert _only(found, "open_ends", "crossings")


def test_end_on_a_line_is_a_crossing(cls):
    found = cls().Move((0, 0)).Line((2, 0), id="a").Move((1, 0)).Line((1, 1), id="b").Diagnose()
    assert _ids(found, "crossings") == [("a", "b")]
    assert _close(found["crossings"][0]["at"], (1, 0))
    assert _ids(found, "open_ends") == [("a",), ("a",), ("b",), ("b",)]


def test_collinear_lines_overlap(cls):
    found = cls().Move((0, 0)).Line((2, 0), id="a").Move((1, 0)).Line((3, 0), id="b").Diagnose()
    assert _ids(found, "overlaps") == [("a", "b")]
    assert _close(found["overlaps"][0]["at"], (1.5, 0))
    assert found["crossings"] == []


def test_arcs_on_one_circle_overlap(cls):
    c, s = math.cos(math.radians(45)), math.sin(math.radians(45))
    sk = cls().Arc((1, 0), (0, 1), 1, id="a").Arc((c, s), (-1, 0), 1, id="b")
    found = sk.Diagnose()
    assert _ids(found, "overlaps") == [("a", "b")]
    m = math.radians(67.5)
    assert _close(found["overlaps"][0]["at"], (math.cos(m), math.sin(m)), 1e-6)
    assert found["crossings"] == []


def test_circle_of_no_radius_is_degenerate(cls):
    # the drawing calls and the solver refuse lines, arcs and splines that shrink to a point; a circle of a radius
    # below the rounding gets through
    sk = cls().Move((0, 0)).Line((2, 0), id="a").Circle((1, 0), 1e-17, id="c")
    found = sk.Diagnose()
    assert _ids(found, "degenerate") == [("c",)]
    assert _close(found["degenerate"][0]["at"], (1, 0))
    assert found["crossings"] == [] and found["overlaps"] == []


def test_construction_elements_are_ignored(cls):
    sk = cls().Move((0, 0)).Line((4, 0)).Line((4, 3)).Line((0, 3)).Line((0, 0))
    sk.Move((-1, 1)).Line((5, 1), construction=True)
    assert sk.Diagnose() == {k: [] for k in KINDS}


def test_gap_none_raises(cls):
    with pytest.raises(TypeError, match="px.Auto"):
        cls().Move((0, 0)).Line((1, 0)).Diagnose(gap=None)


def _faulty(cls):
    """two open ends and a crossing"""
    return cls().Move((0, 0)).Line((2, 2), id="a").Move((0, 2)).Line((2, 0), id="b")


@pytest.fixture(autouse=True)
def _agg():
    try:
        import matplotlib
    except ImportError:
        return
    matplotlib.use("Agg")


def _matplotlib(ax):
    """the diagnosis markers and the legend texts of a sketch plot"""
    markers = [c for c in ax.collections if not c.get_label().startswith("_")]
    legend = ax.get_legend()
    return markers, [t.get_text() for t in legend.get_texts()] if legend else []


def test_matplotlib_markers_and_legend(cls):
    pytest.importorskip("matplotlib")
    import matplotlib.pyplot as plt

    sk = _faulty(cls)
    for show in (True, sk.Diagnose()):
        ax = sk.Plot(show_diagnostics=show)
        markers, texts = _matplotlib(ax)
        assert texts == ["open end (4)", "crossing (1)"]
        assert [len(m.get_offsets()) for m in markers] == [4, 1]
        plt.close(ax.figure)
    ax = px.Plot(sk, show_diagnostics=True)
    assert _matplotlib(ax)[1] == ["open end (4)", "crossing (1)"]
    plt.close(ax.figure)


def test_matplotlib_clean_sketch_plots_as_before(cls):
    pytest.importorskip("matplotlib")
    import matplotlib.pyplot as plt
    import numpy as np

    sk = cls().Move((0, 0)).Line((4, 0)).Line((4, 3)).Line((0, 3)).Line((0, 0))
    images = []
    for kwargs in ({}, {"show_diagnostics": True}):
        ax = sk.Plot(**kwargs)
        assert ax.get_legend() is None
        ax.figure.canvas.draw()
        images.append(np.asarray(ax.figure.canvas.buffer_rgba()).copy())
        plt.close(ax.figure)
    assert images[0].shape == images[1].shape and (images[0] == images[1]).all()


def test_plotly_markers_legend_and_hover(cls):
    pytest.importorskip("plotly")
    sk = _faulty(cls).Move((3, 0)).Line((4, 0), id="c").Move((4 + 2e-3, 0)).Line((5, 0), id="d")
    for show in (True, sk.Diagnose()):
        fig = sk.Plot(show_diagnostics=show, backend="plotly")
        traces = [t for t in fig.data if t.showlegend]
        assert [t.name for t in traces] == ["open end (8)", "missing coincident (1)", "crossing (1)"]
        assert fig.layout.showlegend
        hover = {t.name: list(t.hovertext) for t in traces}
        assert hover["crossing (1)"] == ["crossing: a, b"]
        assert hover["missing coincident (1)"] == ["missing coincident: c, d (gap 0.002)"]
        assert "open end: a" in hover["open end (8)"]
        assert all(t.marker.symbol == "x" and t.marker.size == 12 for t in traces)
    fig = px.Plot(sk, show_diagnostics=True, backend="plotly")
    assert [t.name for t in fig.data if t.showlegend][0] == "open end (8)"


def test_plotly_clean_sketch_plots_as_before(cls):
    pytest.importorskip("plotly")
    sk = cls().Move((0, 0)).Line((4, 0)).Line((4, 3)).Line((0, 3)).Line((0, 0))
    a = sk.Plot(backend="plotly").to_plotly_json()
    b = sk.Plot(show_diagnostics=True, backend="plotly").to_plotly_json()
    assert a == b
    assert not b["layout"]["showlegend"]


def test_show_diagnostics_checks_its_value():
    pytest.importorskip("matplotlib")
    sk = px.QuickSketch().Move((0, 0)).Line((4, 0)).Line((4, 3)).Line((0, 3)).Line((0, 0))
    with pytest.raises(TypeError, match="show_diagnostics"):
        sk.Plot(show_diagnostics="yes")
    with pytest.raises(ValueError, match="show_diagnostics draw a sketch"):
        px.Plot(sk.Shape(), show_diagnostics=True)
