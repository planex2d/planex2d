# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_a_line_on_the_carrier_of_another_takes_coincident(make):
    sk = make().Move((0, 0)).Line((4, 0), id="base").Move((6, 1)).Line((6, 3), id="post")
    sk.Move((5, 0)).Line(2, coincident="base", id="more")
    assert sk.Wire("more").edges[0].end.y == pytest.approx(0, abs=1e-12)


@pytest.mark.parametrize("call", ["Line", "Arc", "Circle", "Slot"])
def test_congruent_names_coincident(call):
    sk = px.QuickSketch().Move((0, 0)).Line((4, 0), id="base")
    # a Slot passes its helpers on to Line, which draws the spine
    with pytest.raises(TypeError, match=r"\(congruent=\.\.\.\): congruent= is now coincident="):
        getattr(sk, call)(1, congruent="base")


@pytest.mark.parametrize("key", ["start", "end", "from_", "to"])
def test_the_ends_are_positional(key):
    with pytest.raises(TypeError, match=r"the ends are positional: Line\(start, end, \.\.\.\)"):
        px.QuickSketch().Line(**{key: (1, 0)})


def test_the_two_end_forms_name_start_and_end():
    for call in (px.Sketch.Line, px.Sketch.Arc, px.Sketch.Slot, px.QuickSketch.ArcSlot, px.QuickSketch.Keyhole):
        doc = call.__doc__
        assert "from_" not in doc and "congruent" not in doc
        assert "(start: Point | Id, end: Point | Id, " in doc
