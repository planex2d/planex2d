# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

occ = pytest.importorskip("netgen.occ")

TOL = 1e-9


def plate():
    sk = px.QuickSketch().Move((0, 0))
    for end, name in [((2, 0), "s"), ((2, 1), "e"), ((0, 1), "n"), ((0, 0), "w")]:
        sk.Line(end, name=name)
    f = sk.Face()
    f.name, f.col, f.maxh = "plate", (1, 0, 0), 0.2
    for k, v in enumerate(f.vertices):
        v.name = "v%d" % k
    return f


def z_range(shape):
    (_, _, z0), (_, _, z1) = shape.bounding_box
    return round(z0, 6), round(z1, 6)


def test_a_pad_is_the_profile_extruded_along_its_normal():
    s = px.Pad(plate(), 3)
    assert isinstance(s, occ.Solid)
    assert s.mass == pytest.approx(6, rel=TOL) and len(s.faces) == 6 and z_range(s) == (0, 3)
    assert [x.name for x in s.solids] == ["plate"]


def test_the_pads_faces_and_edges_carry_the_profiles_properties():
    s = px.Pad(plate(), 3)
    sides = sorted(f.name for f in s.faces if f.name)
    assert sides == ["e", "n", "s", "w"]
    caps = [f for f in s.faces if not f.name]
    assert len(caps) == 2 and all(tuple(f.col) == (1, 0, 0, 1) and f.maxh == pytest.approx(0.2) for f in caps)
    lateral = {e.name for e in s.edges if e.name}
    assert lateral == {"v0", "v1", "v2", "v3"}


def test_length2_reverse_and_dir():
    f = plate()
    both = px.Pad(f, 3, length2=1)
    assert both.mass == pytest.approx(8, rel=TOL) and z_range(both) == (-1, 3)
    assert sorted(x.name for x in both.faces if x.name) == ["e", "n", "s", "w"]
    down = px.Pad(f, 3, reverse=True)
    assert down.mass == pytest.approx(6, rel=TOL) and z_range(down) == (-3, 0)
    tilted = px.Pad(f, 3, dir=(1, 0, 1))
    assert tilted.mass == pytest.approx(2 * 3 / math.sqrt(2), rel=TOL)


def test_a_pad_up_to_a_plane():
    f = plate()
    assert px.Pad(f, until=px.Plane(origin=(0, 0, 5))).mass == pytest.approx(10, rel=TOL)
    assert px.Pad(f, length2=1, until=px.Plane(origin=(0, 0, 5))).mass == pytest.approx(12, rel=TOL)


def block():
    box = occ.Box((0, 0, 0), (10, 10, 4))
    box.solids[0].name = "block"
    bore = px.QuickSketch(px.Plane(origin=(0, 0, 4))).Circle(5, 5, 1, name="bore").Face()
    bore.col = (0, 0, 1)
    return box, bore


def test_a_pocket_goes_into_the_target():
    box, bore = block()
    p = px.Pocket(box, bore, 2)
    assert p.mass == pytest.approx(400 - 2 * math.pi, rel=TOL)
    assert [s.name for s in p.solids] == ["block"]
    assert [f.name for f in p.faces if f.name] == ["bore"]
    assert any(f.col is not None and tuple(f.col) == (0, 0, 1, 1) for f in p.faces)


@pytest.mark.parametrize("reach", [px.last, px.first])
def test_a_pocket_through_the_target(reach):
    box, bore = block()
    p = px.Pocket(box, bore, until=reach)
    assert p.mass == pytest.approx(400 - 4 * math.pi, rel=TOL) and len(p.faces) == 7


@pytest.mark.parametrize("call, message", [
    (lambda f: px.Pad(f), r"^Pad: give a length or until="),
    (lambda f: px.Pad(f, 3, until=px.Plane(origin=(0, 0, 5))), r"^Pad: give a length or until=, not both"),
    (lambda f: px.Pad(f, 3, dir=(1, 0, 0)), r"^Pad\(dir\): the direction runs parallel to the profile's plane"),
    (lambda f: px.Pad(f, until=px.first), r"^Pad\(until=px.first\): a Pad has no target"),
    (lambda f: px.Pad(f, until=occ.Box((0, 0, 0), (1, 1, 1))), r"^Pad\(until=\): an OCC SOLID is no end"),
    (lambda f: px.Pad(f, until=px.Plane(origin=(0, 0, -1))), r"^Pad\(until=\): the plane does not lie ahead"),
    (lambda f: px.Pad(f, -1), r"^Pad\(length, length2\): lengths are finite and not negative"),
])
def test_pad_errors(call, message):
    with pytest.raises(ValueError, match=message):
        call(plate())


def test_a_wire_is_no_profile():
    with pytest.raises(TypeError, match=r"^Pad\(profile\): the profile is a Face, Shape, Sketch or QuickSketch"):
        px.Pad(px.FromPolyline([(0, 0), (1, 0), (1, 1)], closed=True), 3)


def test_a_profile_that_does_not_land_fully_raises():
    small = px.QuickSketch(px.Plane(origin=(0, 0, 5))).Move((0, 0)).Rectangle(1, 1).Face()
    with pytest.raises(ValueError, match=r"^Pad\(until=\): the profile does not land fully"):
        px.Pad(plate(), until=small)
