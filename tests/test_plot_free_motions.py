# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later


import pytest

import planex as px
from planex import _plot_sketch

pytestmark = pytest.mark.constraints

RECTANGLE_LEGEND = ["motion 1: drag bottom.start.h (4 elements)", "motion 2: drag bottom.start.v (4 elements)",
                    "motion 3: drag bottom.end.h (3 elements)", "motion 4: drag right.end.v (3 elements)"]


def hv_rectangle():
    sk = px.Sketch((1, 2))
    sk.Line(4, 0, px.Horizontal, id="bottom").Line(0, 2, px.Vertical, id="right") \
        .Line(-4, 0, px.Horizontal, id="top").Close(px.Vertical, id="left")
    return sk


def arrows(ax, colour):
    from matplotlib.colors import to_rgba
    from matplotlib.patches import Polygon

    return [p for p in ax.patches if isinstance(p, Polygon) and p.get_facecolor() == to_rgba(colour)]


def shapes(fig, colour):
    return [s for s in fig.layout.shapes if s.fillcolor == colour]


def legend(ax):
    return [t.get_text() for t in ax.get_legend().get_texts()]


def test_every_motion_has_its_arrows_colour_and_legend_line():
    pytest.importorskip("matplotlib")
    ax = hv_rectangle().Plot(show_free_motions=True)
    assert legend(ax) == RECTANGLE_LEGEND
    assert [len(arrows(ax, c)) for c in _plot_sketch.MOTIONS[:4]] == [4, 4, 2, 2]


def test_a_number_draws_that_motion_only():
    pytest.importorskip("matplotlib")
    ax = hv_rectangle().Plot(show_free_motions=3)
    assert legend(ax) == [RECTANGLE_LEGEND[2]]
    assert len(arrows(ax, _plot_sketch.MOTIONS[2])) == 2 and arrows(ax, _plot_sketch.MOTIONS[0]) == []


def test_plotly_hovers_the_dimension_that_removes_a_motion():
    pytest.importorskip("plotly")
    fig = px.Plot(hv_rectangle(), show_free_motions=True, backend="plotly")
    assert [t.name for t in fig.data if t.showlegend] == RECTANGLE_LEGEND and fig.layout.showlegend
    assert [len(shapes(fig, c)) for c in _plot_sketch.MOTIONS[:4]] == [4, 4, 2, 2]
    (width,) = [t for t in fig.data if not t.showlegend and t.legendgroup == RECTANGLE_LEGEND[2]]
    assert width.hovertext[0] == RECTANGLE_LEGEND[2] + ', constrain with sk.Constrain("bottom", px.Distance, 4)'
    assert {(s.xanchor, s.yanchor) for s in shapes(fig, _plot_sketch.MOTIONS[2])} == {(5, 2), (5, 4)}
    assert all(s.xsizemode == "pixel" and s.path.startswith("M 16.00 0.00") for s in shapes(fig, _plot_sketch.MOTIONS[2]))


def test_a_radius_motion_points_out_of_the_circle():
    pytest.importorskip("plotly")
    sk = px.Sketch()
    sk.Circle((1, 2), 0.5)
    fig = sk.Plot(show_free_motions=3, backend="plotly")
    assert [t.name for t in fig.data if t.showlegend] == ["motion 3: drag Circle.1.r (1 element)"]
    (arrow,) = shapes(fig, _plot_sketch.MOTIONS[2])
    assert (arrow.xanchor, arrow.yanchor) == pytest.approx((1 + 0.5 ** 1.5, 2 + 0.5 ** 1.5))
    assert arrow.path.startswith("M 11.31 11.31")


def test_the_motions_after_the_eighth_share_one_grey_entry():
    pytest.importorskip("matplotlib")
    sk = px.Sketch()
    for k in range(3):
        sk.Move((3 * k, 0)).Line(2, 1)
    ax = sk.Plot(show_free_motions=True)
    names = legend(ax)
    assert len(names) == 9 and names[-1] == "motions 9-12 (show_free_motions=k: one)"
    assert len(arrows(ax, _plot_sketch.MORE_MOTIONS)) > 0


def test_a_fully_defined_sketch_plots_as_without_the_flag():
    pytest.importorskip("plotly")
    sk = hv_rectangle()
    sk.AutoConstrain()
    assert sk.Plot(show_free_motions=True, backend="plotly").to_json() == sk.Plot(backend="plotly").to_json()


def test_wrong_values_and_the_quick_sketch():
    pytest.importorskip("matplotlib")
    sk = hv_rectangle()
    with pytest.raises(ValueError, match="has 4 free motions"):
        sk.Plot(show_free_motions=5)
    with pytest.raises(TypeError, match="show_free_motions"):
        sk.Plot(show_free_motions="all")
    with pytest.raises(ValueError, match="show_free_motions"):
        px.Plot(px.Spline([(0, 0), (1, 1), (2, 0)]), show_free_motions=True)
    quick = px.QuickSketch()
    quick.Line(1, 0)
    with pytest.warns(px.PlanexWarning, match="no free motions"):
        quick.Plot(show_free_motions=True)


def test_the_quicksketch_warning_names_the_caller_s_line():
    import warnings
    pytest.importorskip("matplotlib")
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        px.QuickSketch().Line(1, 0).Plot(show_free_motions=True)
    mine = [w for w in caught if "free motions" in str(w.message)]
    assert mine and all(w.filename == __file__ for w in mine)
