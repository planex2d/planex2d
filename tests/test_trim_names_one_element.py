# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px


def test_trim_of_a_chain_says_it_is_a_wire_and_names_an_element():
    sk = px.QuickSketch().Circle(10, id="bore")
    sk.Move((-3, 0)).Line((3, 0)).Line((3, 12.8)).Line((-3, 12.8)).Close()
    with pytest.raises(ValueError, match=r"'Chain\.1' is a wire of 4 elements, not one element; name one of them, such as '"):
        sk.Trim(("Chain.-1", (0, 0)), "bore")


def test_trim_of_a_rectangle_says_it_is_a_group_and_names_an_element():
    sk = px.QuickSketch().Circle(10, id="bore")
    sk.Move((-3, 0)).Rectangle(6, 12.8, id="keyway")
    with pytest.raises(ValueError, match=r"'keyway' is a (wire|group) of 4 elements, not one element; name one of them, "
                                         r"such as 'keyway\."):
        sk.Trim(("keyway", (0, 0)), "bore")


def test_an_unknown_id_still_gets_the_suggestion():
    sk = px.QuickSketch().Move((0, 0)).Line((1, 0), id="edge")
    with pytest.raises(ValueError, match=r"no element 'edgf' \(did you mean 'edge'\?\)"):
        sk.Trim("edgf")
