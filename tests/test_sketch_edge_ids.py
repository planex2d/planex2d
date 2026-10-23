import pathlib

import pytest

import planex as px


def test_the_stub_compiles():
    stub = pathlib.Path(__file__).parents[1] / "src" / "python" / "planex" / "_planex.pyi"
    compile(stub.read_text(), str(stub), "exec")


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_edge_takes_the_element_by_position(make):
    sk = make().Move((0, 0)).Line((1, 0), id="a")
    assert sk.Edge("a").mass == pytest.approx(1)
    with pytest.raises(TypeError):
        sk.Edge(element="a")


@pytest.mark.parametrize("make", [px.QuickSketch, px.Sketch])
def test_both_sketch_kinds_name_what_their_calls_return(make):
    sk = make().Move((0, 0)).Line((1, 0), id="a").Line((0, 1)).Close()
    assert sk.Edge("a", id="side").id == "side"
    assert sk.Face(id="plate").id == "plate"
    assert sk.Wire(id="outline").id == "outline"
    assert sk.Copy(id="twin").id == "twin"
