# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import matplotlib

matplotlib.use("Agg")
from matplotlib.collections import LineCollection
from netgen import occ

import planex as px


def styles(ax):
    out = []
    for c in ax.collections:
        if isinstance(c, LineCollection):
            dashed = any(offset is not None and dash is not None for offset, dash in c.get_linestyle())
            out.extend([dashed] * len(c.get_segments()))
    return out


def test_a_projected_construction_edge_is_dashed_and_a_projected_drawn_edge_solid():
    box = occ.Box((0, 0, 0), (4, 2, 1))
    reference = px.Sketch()
    reference.Project(box, construction=True)
    drawn = px.Sketch()
    drawn.Project(box, construction=False)
    assert styles(reference.Plot()) and all(styles(reference.Plot()))
    assert styles(drawn.Plot()) and not any(styles(drawn.Plot()))


def test_plotly_dashes_a_projected_construction_edge():
    sk = px.Sketch()
    sk.Project(occ.Box((0, 0, 0), (4, 2, 1)), construction=True)
    dashes = {t.line.dash for t in sk.Plot(backend="plotly").data if t.mode == "lines" and t.line.dash}
    assert dashes == {"dash"}
