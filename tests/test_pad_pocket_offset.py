# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

occ = pytest.importorskip("netgen.occ")

TOL = 1e-9


def plate():
    return px.QuickSketch().Move((0, 0)).Rectangle(2, 1).Face()


def z_range(shape):
    (_, _, z0), (_, _, z1) = shape.bounding_box
    return round(z0, 6), round(z1, 6)


@pytest.mark.parametrize("offset, height", [(1, 6), (-1.5, 3.5), (0, 5)])
def test_an_offset_moves_the_end_past_or_short_of_the_plane(offset, height):
    pad = px.Pad(plate(), until=px.Plane(origin=(0, 0, 5)), offset=offset)
    assert pad.mass == pytest.approx(2 * height, rel=TOL)
    assert z_range(pad) == (0, height)


def test_the_offset_is_measured_along_the_normal_of_a_tilted_plane():
    a, d = math.radians(30), 1.0
    normal = (0, math.sin(a), math.cos(a))
    pad = px.Pad(plate(), until=px.Plane(origin=(0, 0, 5), normal=normal), offset=d)
    # the end plane: cos(a) z + sin(a) y = 5 cos(a) + d, so z = 5 + d / cos(a) - tan(a) y over the 2 x 1 plate
    assert pad.mass == pytest.approx(2 * (5 + d / math.cos(a) - 0.5 * math.tan(a)), rel=TOL)


def test_a_pocket_stops_short_of_the_last_face():
    box = occ.Box((0, 0, 0), (10, 10, 4))
    bore = px.QuickSketch(px.Plane(origin=(0, 0, 4))).Circle((5, 5), 1).Face()
    assert px.Pocket(box, bore, until=px.last, offset=-1).mass == pytest.approx(400 - 3 * math.pi, rel=TOL)


@pytest.mark.parametrize("call, message", [
    (lambda f: px.Pad(f, 3, offset=1), r"^Pad\(offset=\): offset= moves the end given by until="),
    (lambda f: px.Pad(f, until=px.Plane(origin=(0, 0, 5)), offset=-6), r"^Pad\(until=, offset=-6\.0+\): the end offset"),
])
def test_offset_errors(call, message):
    with pytest.raises(ValueError, match=message):
        call(plate())
