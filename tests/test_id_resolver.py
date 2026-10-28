# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px


def plate():
    sk = px.QuickSketch()
    sk.Move((0, 0)).Line((4, 0), id="a").Line((4, 3), id="b").Line((0, 3), id="c").Close(id="d")
    return sk


# an id as each call takes it: local, the sketch's global id, the selected element, a one-item selection
FORMS = {"local": lambda sk, i: i, "global": lambda sk, i: sk.id + "." + i, "element": lambda sk, i: sk(i)[0],
         "selection": lambda sk, i: sk(i)}


@pytest.mark.parametrize("form", FORMS)
def test_every_form_of_an_id_names_the_same_element(form):
    sk = plate()
    ref = lambda i: FORMS[form](sk, i)
    assert sk.Location(ref("a.end")) == pytest.approx((4, 0))
    assert sk.Geometry(ref("b"))["length"] == pytest.approx(3)
    assert len(sk.Wire(ref("c")).edges) == 1
    assert sk.Measure(ref("a"))["length"] == pytest.approx(4)
    assert sk.Measure(ref("a"), to=ref("c"))["distance"] == pytest.approx(3)
    sk.Move((2, -1)).Line((2, 4), id="cut")
    lines = len(sk.elements["Line"])
    sk.Split(ref("a"), ref("cut"))
    assert len(sk.elements["Line"]) == lines + 1
    sk.Offset(ref("b"), 0.5, id="o")
    assert "o.left" in sk.wires
    sk.Remove(FORMS[form](sk, "o") if form in ("local", "global") else "o")
    assert "o.left" not in sk.wires


@pytest.mark.parametrize("form", FORMS)
@pytest.mark.constraints
def test_a_constraint_sketch_takes_every_form_in_constrain_and_its_dimensions(form):
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Line(4, 0.5, px.Horizontal, id="a").Line(0.2, 3, px.Vertical, id="b")
    ref = lambda i: FORMS[form](sk, i)
    sk.Constrain(ref("a"), px.Distance, 5, id="w")
    assert sk.Geometry("a")["length"] == pytest.approx(5)
    if form in ("local", "global"):
        sk[ref("w")] = 6
        sk.Update()
        assert sk[ref("w")] == pytest.approx(6) and sk.Geometry("a")["length"] == pytest.approx(6)


def test_another_sketchs_id_or_element_raises():
    a, b = plate(), plate()
    with pytest.raises(ValueError, match="belongs to " + a.id.replace(".", r"\.") + ", this is " + b.id.replace(".", r"\.")):
        b.Location(a.id + ".a.end")
    with pytest.raises(ValueError, match="belongs to another sketch"):
        b.Wire(a("a")[0])


def test_unknown_ids_raise_with_the_closest_ids():
    sk = plate()
    with pytest.raises(ValueError, match=r"no element 'bb' \(did you mean 'b'\?\)"):
        sk.Wire("bb")
    with pytest.raises(ValueError, match=r"no element 'bb' here \(did you mean 'b'\?\)"):
        sk("bb")
    with pytest.raises(ValueError, match=r"Measure\('bb'\): no element, point or edge name 'bb' \(did you mean 'b'\?\)"):
        sk.Measure("bb")
    with pytest.raises(ValueError, match=r"^Wire\('zz'\): no element 'zz'$"):
        sk.Wire("zz")


def test_a_shape_measures_by_name_id_or_selected_face_or_edge():
    s = plate().Shape()
    e, f = s.edges[0], s.faces[0]
    local = e.id[len(s.id) + 1:]
    length = s.Measure(e)["length"]
    assert length > 0 and s.Measure(local)["length"] == s.Measure(e.id)["length"] == length
    assert s.Measure(f)["area"] == pytest.approx(12) and s.Measure(f.id)["area"] == pytest.approx(12)
    e.name = "bottom"
    assert s.Measure("bottom")["length"] == length
    with pytest.raises(ValueError, match="no element 'zz9'"):
        s.Measure("zz9")
