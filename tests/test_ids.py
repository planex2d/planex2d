# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import gc

import pytest

import planex as px


@pytest.fixture(autouse=True)
def fresh_counters():
    px.ResetIdCounters()


@pytest.fixture(autouse=True)
def no_leftovers():
    gc.collect()


def square(x=0.0, s=1.0):
    return px.QuickSketch().Move((x, 0)).Rectangle(s, s).Face()


@pytest.mark.constraints
def test_sketches_count_together_and_take_an_id():
    assert px.Sketch().id == "Sketch.1"
    assert px.QuickSketch().id == "Sketch.2"
    assert px.Sketch(id="bracket").id == "bracket"
    assert px.QuickSketch(id="plate").id == "plate"
    assert px.QuickSketch().id == "Sketch.3"


def test_an_object_gets_its_number_when_first_asked_and_its_edges_follow_it():
    a, b = square(), square(2)
    assert b.id == "Face.1" and a.id == "Face.2"
    assert [e.id for e in a.edges] == ["Face.2.Line.1", "Face.2.Line.2", "Face.2.Line.3", "Face.2.Line.4"]
    a.id = "plate"
    assert a.edges[2].id == "plate.Line.3"


def test_copies_are_other_objects_of_their_kind_with_the_same_local_ids():
    f = square()
    local = [e.id.split(".", 2)[2] for e in f.edges]
    for copy, kind in ((f.Mirrored((0, 0), (0, 1)), "Mirror"), (f.Translated((1, 0)), "Translate"), (f.Rotated((0, 0), 30), "Rotate"),
                       (f.Scaled((0, 0), 2), "Scale"), (f.Copy(), "Copy")):
        assert copy.id == kind + ".1"
        assert [e.id for e in copy.edges] == [kind + ".1." + l for l in local]
    f.Translate((5, 0))
    assert f.id == "Face.1" and f.edges[0].id == "Face.1." + local[0]


def test_combining_makes_a_new_shape_numbered_anew():
    a, b = square(), square(0.5)
    a.id, b.id = "a", "b"
    for r in (a + b, a - b, a * b, px.Glue([a, b]), px.Unify([a, b])):
        ids = [e.id for e in r.edges]
        assert r.id.startswith("Shape.") and all(i.startswith(r.id + ".") for i in ids)
        assert len(set(ids)) == len(ids)
    g = px.Glue([a, b])
    assert [f.id for f in g.faces] == [g.id + ".Face.1", g.id + ".Face.2", g.id + ".Face.3"]
    pieces = a.Split(px.Line((0.5, -1), (0.5, 2)))
    assert len({p.id for p in pieces}) == 2
    assert all(e.id.startswith(p.id + ".") for p in pieces for e in p.edges)


def test_the_same_inputs_give_the_same_ids():
    def build():
        px.ResetIdCounters()
        g = px.Glue([square(), square(0.5)])
        return g.id, [e.id for e in g.edges]
    assert build() == build()


def test_ids_do_not_keep_edges_from_merging():
    w = px.QuickSketch().Move((0, 0)).Line((1, 0)).Line((2, 0)).Line((2, 1)).Line((0, 1)).Close().Wire()
    assert len({e.id for e in w.edges}) == len(w.edges)
    w.MergeCollinearEdges()
    assert len(w.edges) == 4


@pytest.mark.constraints
def test_calls_that_make_objects_take_an_id():
    f = square()
    assert f.Mirrored((0, 0), (0, 1), id="left").id == "left"
    assert px.Glue([f, square(2)], id="pair").id == "pair"
    assert px.Line((0, 0), (1, 0), id="cut").edges[0].id == "cut.Line.1"
    pieces = f.Split(px.Line((0.5, -1), (0.5, 2)), id="half")
    assert [p.id for p in pieces] == ["half.1", "half.2"]
    sk = px.Sketch()
    sk.Move((0, 0)).Rectangle(2, 1)
    assert sk.Face(id="plate").edges[0].id == "plate.Line.1"
    assert px.Compound([f]).id == "Compound.1" and px.Compound([f], id="c").id == "c"


def test_imports_count_in_their_own_kind(tmp_path):
    pytest.importorskip("ezdxf")
    px.WriteDXF(square(), tmp_path / "s.dxf")
    [a], [b] = px.FromDXF(tmp_path / "s.dxf"), px.FromDXF(tmp_path / "s.dxf", id="drawing")
    assert a.id == "DXF.1" and [e.id for e in a.edges] == ["DXF.1.Line.%d" % k for k in range(1, 5)]
    assert b.id == "drawing.1"


@pytest.mark.constraints
def test_find_by_id():
    f = square()
    f.id = "plate"
    assert px.Find("plate") is f
    assert px.Find("plate.Line.2").ids == ["plate.Line.2"]
    sk = px.Sketch()
    sk.Circle((1, 1), 0.3, id="hole")
    assert px.Find(sk.id) is sk and px.Find(sk.id + ".hole").ids == [sk.id + ".hole"]
    c = px.Compound([f], id="both")
    assert px.Find("both") is c
    with pytest.raises(ValueError, match="no object"):
        px.Find("nothing.Line.1")
    pytest.importorskip("matplotlib")
    import matplotlib
    matplotlib.use("Agg")
    assert px.Plot("plate.Line.2") is not None and px.Plot("plate") is not None


def test_vertices_have_ids_that_live_on_the_edge_ends():
    f = square()
    f.id = "plate"
    ids = [v.id for v in f.vertices]
    assert ids == ["plate.Vertex.%d" % k for k in range(1, 5)]
    f.vertices[2].name = "corner"
    assert f.vertices[2].id == ids[2] and f.vertices[2].name == "corner"
    m = f.Mirrored((0, 0), (0, 1))
    assert [v.id for v in m.vertices] == ["Mirror.1.Vertex.%d" % k for k in range(1, 5)]
    assert f("Vertex.3").ids == ["plate.Vertex.3"]
    g = px.Glue([f, square(1)])
    assert len({v.id for v in g.vertices}) == len(g.vertices) == 6
    assert all(v.id.startswith(g.id + ".Vertex.") for v in g.vertices)


def test_a_plain_id_finds_the_object_outside_any_compound():
    f = square()
    f.id = "plate_outside"
    c = px.Compound([f], id="both_outside")
    c.Translate((5, 0))
    assert px.Find("plate_outside") is f
    assert px.Find("plate_outside.Line.2")[0].start.x < 5


def test_a_part_is_found_through_its_compound():
    f = square()
    f.id = "plate_through"
    inner = px.Compound([f], id="inner_through")
    c = px.Compound([f, inner], id="both_through")
    c.Translate((5, 0))
    assert px.Find("both_through.plate_through") is c[0]
    edge = px.Find("both_through.plate_through.Line.2")
    assert edge.ids == ["plate_through.Line.2"] and edge[0].start.x >= 5
    assert px.Find("both_through.inner_through.plate_through") is c[1][0]
    m = f.Mirrored((0, 0), (0, 1))
    m.id = "Mirror_through_1"
    d = px.Compound([m], id="d_through")
    assert px.Find("d_through.Mirror_through_1") is d[0]
    assert px.Find("d_through.Mirror_through_1.Line.1").ids == ["Mirror_through_1.Line.1"]
    with pytest.raises(ValueError, match="no object"):
        px.Find("both_through.nothing")


def test_without_the_original_its_one_copy_in_a_compound_is_found():
    f = square()
    f.id = "plate_alone"
    c = px.Compound([f], id="both_alone")
    del f
    gc.collect()
    assert px.Find("plate_alone") is c[0]


def test_two_compounds_with_parts_of_one_id_make_a_plain_id_ambiguous():
    f = square()
    f.id = "plate_twice"
    a, b = px.Compound([f], id="a_twice"), px.Compound([f], id="b_twice")
    del f
    gc.collect()
    with pytest.raises(ValueError, match="2 objects have the id 'plate_twice'"):
        px.Find("plate_twice")
    assert px.Find("a_twice.plate_twice") is a[0] and px.Find("b_twice.plate_twice") is b[0]


@pytest.mark.parametrize("bad", ["a.b", "1a", "_a", "a+b", "a-b", "a  b", " a", "a ", "Länge", "φ", "a²"])
def test_a_user_given_id_follows_the_id_rule(bad):
    sk = px.QuickSketch()
    with pytest.raises(ValueError, match="id '"):
        sk.Move((0, 0)).Line((1, 0), id=bad)
    with pytest.raises(ValueError, match="id '"):
        sk.Vertex(bad, (0, 0))
    with pytest.raises(ValueError, match="id '"):
        px.Move((0, 0)).Rectangle(1, 1, id=bad)
    with pytest.raises(ValueError, match="id '"):
        px.Move((0, 0)).Line((1, 0)).Line((1, 1)).Finish(id_wire=bad)
    face = px.Move((0, 0)).Rectangle(1, 1).Face()
    with pytest.raises(ValueError, match="id '"):
        face.id = bad
    with pytest.raises(ValueError, match="id '"):
        px.QuickSketch(id=bad)
    with pytest.raises(ValueError, match="id '"):
        px.Compound([face], id=bad)


def test_good_ids_and_the_reason_a_bad_one_gives():
    sk = px.QuickSketch()
    sk.Move((0, 0)).Line((1, 0), id="top edge").Line((1, 1), id="Bolt_2")
    assert {"top edge", "Bolt_2"} <= set(sk.elements["Line"])
    with pytest.raises(ValueError, match=r"contains '\.'.*the dot separates scopes and references"):
        sk.Vertex("p.q", (0, 0))
    with pytest.raises(ValueError, match="non-ASCII"):
        sk.Vertex("Länge", (0, 0))
    with pytest.raises(ValueError, match="must start with a letter"):
        sk.Vertex("2nd", (0, 0))


def test_single_result_accessors_raise_value_error_naming_the_fix():
    two = px.Glue([px.Line((0, 0), (1, 0)), px.Line((0, 1), (1, 1))])
    with pytest.raises(ValueError, match=r"Wire\(\): the edges form 2 wires; \.wires gives them"):
        two.Wire()
    with pytest.raises(ValueError, match=r"Face\(\): the shape has 0 faces and 2 wires; \.faces"):
        two.Face()
    holed = (px.Move((0, 0)).Rectangle(4, 4).Face() - px.Move((1, 1)).Rectangle(1, 1).Face()).Face()
    with pytest.raises(ValueError, match=r"Wire\(\): the face has 2 loops.*\.outer the outline"):
        holed.Wire()
