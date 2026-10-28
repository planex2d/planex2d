# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px

PEANUT = [(-6, 0), (-3, 3), (0, 1), (3, 3), (6, 0), (3, -3), (0, -1), (-3, -3)]
OVAL = [(-5, 0), (0, 3), (5, 0), (0, -3)]


def pyplot():
    pytest.importorskip("matplotlib").use("Agg")
    import matplotlib.pyplot as plt

    return plt


def test_legend_does_not_cover_the_face():
    plt = pyplot()
    sk = px.QuickSketch()
    sk.Slot(10, 2)
    fig, ax = plt.subplots()
    px.Plot(sk.Face(), ax=ax, line_col="type")
    fig.canvas.draw()
    face = ax.patches[0].get_window_extent()
    legend = ax.get_legend().get_window_extent()
    assert not face.overlaps(legend)
    plt.close(fig)


def test_mesh_overlay_adds_no_fill():
    ngsolve = pytest.importorskip("ngsolve")
    plt = pyplot()
    from netgen.occ import OCCGeometry, WorkPlane

    mesh = ngsolve.Mesh(OCCGeometry(WorkPlane().Rectangle(10, 2).Face(), dim=2).GenerateMesh(maxh=0.5))
    sk = px.QuickSketch()
    sk.Slot(10, 2)
    face = sk.Face()
    fig, ax = plt.subplots()
    px.PlotField(ngsolve.x, mesh, face, ax=ax)
    before = len(ax.patches)
    px.PlotMesh(mesh, face, ax=ax)
    assert len(ax.patches) == before
    plt.close(fig)


def test_closed_spline_offset_crossing_itself_raises():
    messages = []
    for cls in (px.Sketch, px.QuickSketch):
        sk = cls()
        sk.Spline(PEANUT, periodic=True, id="s")
        with pytest.raises(ValueError, match="cross itself") as raised:
            sk.Offset("s", -0.3)
        messages.append(str(raised.value))
    assert messages[0] == messages[1]


def test_closed_spline_offset_within_spline_tol():
    d = 0.3
    sk = px.QuickSketch()
    sk.Spline(OVAL, periodic=True, id="s")
    sk.Offset("s", d, id="o")
    source = sk.Wire("s")
    box = 10.0
    worst = 0.0
    for edge in sk.Wire("o").edges:
        for p in edge.Sample(1e-3):
            worst = max(worst, abs(min(e.Distance(p) for e in source.edges) - d))
    assert worst <= 4 * px.config.spline_tol * box


def test_spline_split_next_to_a_joint_ends_exactly_on_the_split_point():
    x = 5.0 + 0.5 * 10.0 * 1e-15 * 5.0
    sk = px.QuickSketch()
    sk.Spline([(5, 5), (10, 0)], id="s")
    sk.Line((x, -1), (x, 6), id="l")
    sk.Split("s", "l")
    first, second = (sk.Wire(i).edges[0] for i in ("s.1", "s.2"))
    assert first.end.x == x
    assert second.start.x == x
