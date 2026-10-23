# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import numpy as np
import planex as px


def signed_area(points):
    x, y = points[:, 0], points[:, 1]
    return 0.5 * float(np.sum(x[:-1] * y[1:] - x[1:] * y[:-1]))


def test_circle_polyline_follows_direction():
    assert signed_area(px.Circle((0, 0), 1).ToPolyline(0.01)) > 0
    assert signed_area(px.Circle((0, 0), 1, reverse=True).ToPolyline(0.01)) < 0


def test_rectangle_polyline_follows_edge_order():
    ccw = px.FromPolyline([(0, 0), (2, 0), (2, 1), (0, 1)], closed=True)
    cw = px.FromPolyline([(0, 0), (0, 1), (2, 1), (2, 0)], closed=True)
    assert signed_area(ccw.ToPolyline(0.01)) == 2.0
    assert signed_area(cw.ToPolyline(0.01)) == -2.0

