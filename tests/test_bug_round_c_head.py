# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest
import planex as px


def box(x0, y0, x1, y1):
    return px.Face([px.FromPolyline([(x0, y0), (x1, y0), (x1, y1), (x0, y1)], closed=True)])


def test_a_boolean_on_a_glued_shape_with_wires_names_both_ways_out():
    glued = px.Glue([box(0, 0, 4, 2), px.FromPolyline([(0, 1), (4, 1)])])
    hole = box(1, 0.5, 2, 1.5)
    with pytest.raises(ValueError, match=r"px\.Unify\(shape\.faces\).*Glue the wires"):
        glued - hole
    assert (px.Unify(glued.faces) - hole).mass == pytest.approx(7, abs=1e-12)


def test_get_transformation_takes_selections_of_edges():
    face = box(0, 0, 4, 2)
    left = [e for e in face.edges if e.start.h == e.end.h == 0]
    right = [e for e in face.edges if e.start.h == e.end.h == 4]
    expected = px.GetTransformation(left, right)
    got = px.GetTransformation(px.Selection(left), px.Selection(right))
    assert repr(got) == repr(expected)


def test_a_selection_measures_the_distance_to_its_nearest_element():
    face = box(0, 0, 4, 2)
    sides = px.Selection([e for e in face.edges if e.start.h == e.end.h])
    assert len(sides) == 2 and sides.Distance((5, 1)) == pytest.approx(1, abs=1e-14)
    with pytest.raises(ValueError, match="empty"):
        px.Selection([]).Distance((5, 1))
