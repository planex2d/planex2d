# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import time
import warnings

import pytest

import planex as px

pytestmark = pytest.mark.constraints

DEFAULTS = {"coincident": True, "tangent": True, "concentric": True, "point_on_curve": True, "horizontal": False,
            "vertical": False, "perpendicular": False, "parallel": False, "equal_radius": False, "equal_length": False,
            "collinear": False, "named_helpers": True}


@pytest.fixture(autouse=True)
def config():
    on, options = px.config.auto_constraint, px.config.auto_constraint_options
    yield
    px.config.auto_constraint = on
    px.config.auto_constraint_options = options


def kinds(sk):
    return {kind: sorted(repr(c) for c in ids.values()) for kind, ids in sk.constraints.items()}


def count(sk, kind):
    return len(sk.constraints.get(kind, {}))


def quiet(make):
    with warnings.catch_warnings():
        warnings.simplefilter("error")
        return make()


def test_the_config_has_the_defaults():
    assert px.config.auto_constraint is False
    assert px.config.auto_constraint_options == DEFAULTS


def test_a_dict_switches_only_the_relations_it_names():
    px.config.auto_constraint_options = {"horizontal": True}
    assert px.config.auto_constraint_options == DEFAULTS | {"horizontal": True}
    assert px.Sketch(auto_constraint=True).auto_constraint == DEFAULTS | {"horizontal": True}
    assert px.Sketch(auto_constraint={"tangent": False}).auto_constraint == DEFAULTS | {"horizontal": True, "tangent": False}


def test_wrong_keys_and_values_raise():
    with pytest.raises(TypeError, match=r"no relation 'tangency' \(coincident, tangent, concentric, point_on_curve"):
        px.Sketch(auto_constraint={"tangency": True})
    with pytest.raises(TypeError, match=r"'tangent' is True or False, not 1"):
        px.config.auto_constraint_options = {"tangent": 1}
    with pytest.raises(TypeError, match="a dict of relations"):
        px.config.auto_constraint_options = True
    with pytest.raises(TypeError, match=r"True, False or a dict of relations, not 'yes'"):
        px.Sketch(auto_constraint="yes")
    assert px.config.auto_constraint_options == DEFAULTS


def test_none_takes_the_configs_switch():
    assert px.Sketch().auto_constraint is False
    px.config.auto_constraint = True
    assert px.Sketch().auto_constraint == DEFAULTS
    assert px.Sketch(auto_constraint=False).auto_constraint is False
    assert px.QuickSketch((0, 0)).Line(1).elements["Line"] == ["Line.1"]


def test_a_quicksketch_takes_no_relations():
    with pytest.raises(ValueError, match=r"QuickSketch\(auto_constraint=True\): a QuickSketch has no constraints"):
        px.QuickSketch(auto_constraint={"tangent": True})


def test_the_property_reads_false_or_the_dict_and_takes_what_the_argument_takes():
    sk = px.Sketch()
    sk.auto_constraint = {"horizontal": True}
    assert sk.auto_constraint == DEFAULTS | {"horizontal": True}
    px.config.auto_constraint_options = {"vertical": True}
    assert sk.auto_constraint == DEFAULTS | {"horizontal": True}
    sk.auto_constraint = False
    assert sk.auto_constraint is False


def test_a_drawn_end_on_a_line_lies_on_it():
    sk = quiet(lambda: px.Sketch(auto_constraint=True).Line((0, 2), (10, 2)).Line((5, 5), (5, 2)))
    assert kinds(sk)["Coincident"] == ['"Point.4", "Line.1", px.Coincident']


def test_circles_with_one_centre_are_concentric():
    sk = quiet(lambda: px.Sketch(auto_constraint=True).Circle((5, 5), 2).Circle((5, 5), 3))
    assert kinds(sk)["Concentric"] == ['"Circle.1", "Circle.2", px.Concentric']


def test_a_line_turning_back_along_an_arc_is_tangent_too():
    sk = quiet(lambda: px.Sketch(auto_constraint=True).Move((1, 1)).Line(0, 5).Arc(2, 90).Line(3, 0))
    assert kinds(sk)["Tangent"] == ['"Arc.1", "Line.2", px.Tangent', '"Line.1", "Arc.1", px.Tangent']


def test_horizontal_and_vertical_only_when_switched_on():
    def drawn(auto):
        return px.Sketch(auto_constraint=auto).Move((1, 1)).Line(10).Line(0, 5)

    assert "Horizontal" not in drawn(True).constraints
    sk = drawn({"horizontal": True, "vertical": True})
    assert kinds(sk)["Horizontal"] == ['"Line.1", px.Horizontal']
    assert kinds(sk)["Vertical"] == ['"Line.2", px.Vertical']


def test_named_helpers_off_leaves_a_helper_out_unless_a_switch_finds_it():
    def drawn(options):
        sk = px.Sketch(auto_constraint=options)
        sk.Move((1, 1)).Line(10, id="a")
        sk.Move((1, 3)).Line(4, parallel="a", id="b")
        return sk

    assert kinds(drawn(True))["Parallel"] == ['"a", "b", px.Parallel']
    assert "Parallel" not in drawn({"named_helpers": False}).constraints
    assert kinds(drawn({"named_helpers": False, "parallel": True}))["Parallel"] == ['"a", "b", px.Parallel']


def rectangle(angle=0.0):
    c, s = math.cos(math.radians(angle)), math.sin(math.radians(angle))
    return px.QuickSketch((1, 1), dir=(c, s)).Line(10).Turn(90).Line(5).Turn(90).Line(10).Close()


def test_copied_geometry_gets_the_switched_relations_and_nothing_redundant():
    options = {"horizontal": True, "vertical": True, "perpendicular": True}
    sk = quiet(lambda: px.Sketch(rectangle(), copy=True, auto_constraint=options))
    assert count(sk, "Horizontal") == 2 and count(sk, "Vertical") == 2 and count(sk, "Perpendicular") == 0
    assert sk.redundant == [] and sk.dof == 4


def test_of_four_right_angles_three_are_kept():
    sk = quiet(lambda: px.Sketch(rectangle(30), copy=True, auto_constraint={"perpendicular": True, "parallel": True}))
    assert count(sk, "Perpendicular") == 3 and count(sk, "Parallel") == 0
    assert sk.redundant == [] and sk.dof == 5


def test_a_square_gets_three_equal_lengths():
    quick = px.QuickSketch((1, 1)).Line(4).Turn(90).Line(4).Turn(90).Line(4).Close()
    sk = quiet(lambda: px.Sketch(quick, copy=True, auto_constraint={"equal_length": True}))
    assert count(sk, "Equal") == 3 and sk.redundant == []


def test_collinear_lines_meeting_or_apart():
    quick = px.QuickSketch()
    quick.Move((1, 1)).Line(4).Line(3)
    quick.Move((10, 1)).Line(2)
    sk = quiet(lambda: px.Sketch(quick, copy=True, auto_constraint={"collinear": True}))
    assert kinds(sk)["Coincident"] == ['"Line.1", "Line.3", px.Coincident', '"Line.2.end", "Line.1", px.Coincident']
    assert sk.redundant == []


def test_equal_radii():
    quick = px.QuickSketch().Circle((1, 5), 2).Circle((10, 5), 2).Circle((20, 5), 3)
    sk = quiet(lambda: px.Sketch(quick, copy=True, auto_constraint={"equal_radius": True}))
    assert kinds(sk)["Equal"] == ['"Circle.1", "Circle.2", px.Equal']


def test_a_long_outline_copies_in_with_its_relations_quickly():
    quick = px.QuickSketch((0, 0))
    for _ in range(200):
        quick.Line(1).Arc(0.2, 90).Line(1).Arc(0.2, -90)
    start = time.perf_counter()
    sk = quiet(lambda: px.Sketch(quick, copy=True, auto_constraint=True))
    took = time.perf_counter() - start
    assert count(sk, "Tangent") == 799 and sk.redundant == []
    assert took < 30
