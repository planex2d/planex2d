# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import re
import warnings

import pytest

import planex as px

pytestmark = pytest.mark.constraints


def recorded(call):
    with warnings.catch_warnings(record=True) as caught:
        warnings.simplefilter("always")
        call()
    return [str(w.message) for w in caught if issubclass(w.category, px.PlanexWarning)]


def test_a_redundant_constraint_names_what_it_follows_from():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line((10, 0), id="l")
    sk.Constrain("l.end", px.Fix)
    assert recorded(lambda: sk.Constrain("l", px.Horizontal)) == [
        'Horizontal.1 ("l", px.Horizontal) follows from Fix.1, Fix.2; remove one of them']
    assert sk.redundant == ["Horizontal.1"] and sk.dependencies == {"Horizontal.1": ["Fix.1", "Fix.2"]}


def test_each_redundancy_names_only_its_own_partners():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line((10, 0), id="l")
    sk.Constrain("l.end", px.Fix)
    sk.Circle((20, 0), 3, id="c")
    sk.Constrain("c", px.Radius, 3)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        sk.Constrain("l", px.Horizontal)
        sk.Constrain("c", px.Diameter, 6)
    # a radius and a diameter of one circle follow from each other: both are redundant
    assert sk.dependencies == {"Horizontal.1": ["Fix.1", "Fix.2"], "Radius.1": ["Diameter.1"], "Diameter.1": ["Radius.1"]}


def test_a_coincidence_of_points_already_merged_names_the_merges_between_them():
    sk = px.Sketch(auto_constraint=False)
    sk.Move((0, 0)).Line((10, 0), id="a")
    sk.Move((10, 0)).Line((10, 5), id="b")
    sk.Move((10, 0)).Line((15, 0), id="c")
    sk.Constrain("a.end", "b.start", px.Coincident)
    sk.Constrain("b.start", "c.start", px.Coincident)
    messages = recorded(lambda: sk.Constrain("a.end", "c.start", px.Coincident))
    assert len(messages) == 1 and messages[0].startswith('Coincident.3 ("a.end", "c.start", px.Coincident) follows from ')
    assert sorted(sk.dependencies["Coincident.3"]) == ["Coincident.1", "Coincident.2"]


def test_a_sketch_without_redundancies_has_no_dependencies():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line((10, 0), px.Horizontal, id="l")
    assert sk.dependencies == {} and sk.redundant == []


def test_an_update_without_a_solution_names_what_contradicts_it():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line((3, 0), px.Horizontal, id="a").Line((0, 4), id="b").Close(id="c")
    sk.Constrain("a", px.Distance, 3, id="da")
    sk.Constrain("b", px.Distance, 4, id="db")
    sk.Constrain("c", px.Distance, 5, id="dc")
    with pytest.raises(ValueError, match=re.escape("no solution, nothing changed; dc contradicts da, db")):
        sk.Update({"dc": 10})
    assert sk["dc"] == 5
