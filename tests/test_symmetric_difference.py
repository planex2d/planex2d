# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px


def square(x, y, s=2.0, name=None):
    f = px.Move((x, y)).Rectangle(s, s).Face()
    if name:
        f.name = name
    return f


def test_two_overlapping_squares_leave_two_l_shapes():
    a, b = square(0, 0), square(1, 1)
    r = a ^ b
    assert len(r.faces) == 2
    assert r.mass == pytest.approx(6, abs=1e-12)
    assert r.mass == pytest.approx((a + b).mass - (a * b).mass, abs=1e-12)


def test_each_region_keeps_the_name_of_its_part():
    r = square(0, 0, name="a") ^ square(1, 1, name="b")
    assert sorted((f.name, round(f.mass, 12)) for f in r.faces) == [("a", 3.0), ("b", 3.0)]


def test_adjacent_squares_stay_two_faces_on_one_shared_edge():
    r = square(0, 0, name="a") ^ square(2, 0, name="b")
    assert len(r.faces) == 2 and r.mass == pytest.approx(8, abs=1e-12)
    assert len(r.edges) == 7


def test_a_part_with_itself_leaves_nothing_and_disjoint_parts_both():
    a = square(0, 0)
    assert (a ^ a).mass == pytest.approx(0, abs=1e-12)
    assert (a ^ square(5, 0)).mass == pytest.approx(8, abs=1e-12)


def test_a_list_keeps_what_an_odd_number_of_parts_cover():
    strip = [px.Move((x, 0)).Rectangle(3, 1).Face() for x in (0, 1, 2)]
    r = px.SymmetricDifference(strip)
    # covered once by [0, 1] and [4, 5], three times by [2, 3]
    assert r.mass == pytest.approx(3, abs=1e-12)
    assert sorted(round(f.mass, 12) for f in r.faces) == [1.0, 1.0, 1.0]
    assert px.SymmetricDifference(strip, multithreaded=False).mass == pytest.approx(3, abs=1e-12)


def test_shapes_faces_and_views_mix():
    a, b = square(0, 0), square(1, 1)
    s = px.Shape(a)
    assert (s ^ b).mass == pytest.approx(6, abs=1e-12)
    assert (a ^ px.Shape(b)).mass == pytest.approx(6, abs=1e-12)
    assert (s.faces[0] ^ b).mass == pytest.approx(6, abs=1e-12)


def test_wires_and_compounds_have_no_symmetric_difference():
    a = square(0, 0)
    with pytest.raises(TypeError, match="face \\^ wire is not supported"):
        a ^ px.Line((0, 0), (1, 1))
    with pytest.raises(TypeError, match="a Compound has no booleans"):
        px.Compound([a]) ^ a
    with pytest.raises(ValueError, match="faces and wires is not defined"):
        px.SymmetricDifference([px.Shape(a), px.Glue([px.Line((0, 0), (1, 1))])])
    with pytest.raises(ValueError, match="only faces have an intersection, a difference or a symmetric difference"):
        px.SymmetricDifference([px.Glue([px.Line((0, 0), (1, 1))]), px.Glue([px.Line((0, 1), (1, 0))])])
