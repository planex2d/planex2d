# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px


def faces():
    return px.Circle((0, 0), 9).Face() - px.Circle((0, 0), 4).Face(), px.Circle((20, 0), 3).Face()


def test_two_filled_objects_get_an_entry_each_in_matplotlib():
    matplotlib = pytest.importorskip("matplotlib")
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    ring, disc = faces()
    ax = ring.Plot(face_col="lightsteelblue")
    disc.Plot(ax=ax, face_col="khaki")
    assert [t.get_text() for t in ax.get_legend().get_texts()] == [ring.id, disc.id]
    assert ring.Plot().get_legend() is None
    plt.close("all")


def test_two_filled_objects_get_an_entry_each_in_plotly():
    pytest.importorskip("plotly")
    ring, disc = faces()
    fig = ring.Plot(backend="plotly", face_col="lightsteelblue")
    disc.Plot(ax=fig, backend="plotly", face_col="khaki")
    assert [s.name for s in fig.layout.shapes if s.showlegend] == [ring.id, disc.id]
    assert not [s for s in ring.Plot(backend="plotly").layout.shapes if s.showlegend]
