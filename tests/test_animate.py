# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px

matplotlib = pytest.importorskip("matplotlib")
matplotlib.use("Agg")


def slider_crank():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line((2, 0), id="crank").Line((8, 0), id="rod")
    sk.Constrain("rod.end", px.FixV)
    sk.Constrain("crank", px.Distance, 2)
    sk.Constrain("rod", px.Distance, 6, id="rod_length")
    sk.Constrain("H", "crank", px.Angle, 30, id="phi")
    return sk


def title(anim, k):
    anim._draw_frame(k)
    return anim._fig.axes[0].get_title()


def test_a_sweep_has_a_frame_per_value_and_leaves_the_sketch():
    from matplotlib.animation import FuncAnimation

    sk = slider_crank()
    at = sk.Location("rod.end")
    anim = sk.Animate({"phi": [0, 90, 180, 270, 360]}, fps=20)
    assert isinstance(anim, FuncAnimation) and anim._interval == pytest.approx(50)
    assert [title(anim, k) for k in range(5)] == ["phi = 0", "phi = 90", "phi = 180", "phi = 270", "phi = 360"]
    assert sk.Location("rod.end") == pytest.approx(at) and sk.dimensions["phi"] == 30
    assert "<script" in anim._repr_html_()


def test_rows_by_name_and_frames_in_between():
    sk = slider_crank()
    named = sk.Animate({"open": {"phi": 30}, "closed": {"phi": 120}})
    assert [title(named, k) for k in range(2)] == ["open", "closed"]
    smooth = sk.Animate([{"phi": 30}, {"phi": 120}], steps=4, label=lambda s: "slider %.3f" % s.Location("rod.end")[0])
    assert [title(smooth, k) for k in range(5)] == ["phi = 30", "phi = 52.5", "phi = 75", "phi = 97.5", "phi = 120"]
    smooth._draw_frame(2)
    assert any(t.get_text().startswith("slider ") for t in smooth._fig.axes[0].texts)


def test_a_row_without_a_solution_keeps_the_last_good_geometry():
    sk = slider_crank()
    with pytest.warns(px.PlanexWarning, match=r"Animate: 1 row did not solve and show the last good geometry: too short"):
        anim = sk.Animate({"stroke": {"rod_length": 6}, "too short": {"rod_length": 0.5}})
    assert title(anim, 1).startswith("too short: ")


def test_animate_takes_rows_of_its_dimensions():
    sk = slider_crank()
    with pytest.raises(ValueError, match=r"no dimension 'rod' in the sketch; its dimensions: .*'rod_length'"):
        sk.Animate([{"rod": 5}])
    with pytest.raises(ValueError, match="steps=0"):
        sk.Animate([{"phi": 10}], steps=0)
    with pytest.raises(ValueError, match="fps=0"):
        sk.Animate([{"phi": 10}], fps=0)
    with pytest.raises(ValueError, match="different lengths"):
        sk.Animate({"phi": [1, 2], "rod_length": [6]})
    with pytest.raises(ValueError, match="no rows"):
        sk.Animate([])


def test_every_frame_has_the_grid_of_the_view_all_frames_share():
    sk = slider_crank()
    anim = sk.Animate({"rod_length": [3, 6, 12]}, steps=4)
    steps = set()
    for k in range(9):
        anim._draw_frame(k)
        ax = anim._fig.axes[0]
        steps.add(tuple(axis.get_major_locator()._edge.step for axis in (ax.xaxis, ax.yaxis)))
    assert len(steps) == 1

