# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import re

import pytest
from netgen import occ

import planex as px


def numbered(shape):
    """each edge and vertex named by its first index in shape.edges / shape.vertices, which list them once per face"""
    for items, prefix in ((shape.edges, "u"), (shape.vertices, "v")):
        seen = []
        for k, x in enumerate(items):
            if not any(x == y for y in seen):
                seen.append(x)
                x.name = prefix + str(k)
    return shape


def projected(sk, scope):
    return [i for kind in ("Line", "Arc", "Spline", "Point") for i in sk.elements.get(kind, []) if i.startswith(scope + ".")]


def test_projected_edges_and_vertices_have_the_numbers_of_shape_edges_and_vertices():
    box = numbered(occ.Box((0, 0, 0), (4, 2, 1)))
    sk = px.Sketch()
    sk.Project(box, construction=False)
    ids = projected(sk, "Projection.1")
    edges = [i for i in ids if ".Edge." in i]
    vertices = [i for i in ids if ".Vertex." in i]
    assert len(edges) == 4 and len(vertices) == 4
    for i in edges:
        assert sk._Name(i) == "u" + re.fullmatch(r"Projection\.1\.Edge\.(\d+)", i)[1]
    for i in vertices:
        assert sk._Name(i) == "v" + re.fullmatch(r"Projection\.1\.Vertex\.(\d+)", i)[1]
    assert sorted(sk.wires["Projection.1"]) == sorted(edges)
    assert len(sk("Projection.1")) == len(ids)


def test_a_projected_full_circle_is_one_circle_element():
    cylinder = numbered(occ.Cylinder(occ.Pnt(0, 0, 0), occ.Z, r=1, h=2))
    sk = px.Sketch()
    sk.Project(cylinder, construction=False)
    circles = [i for i in sk.elements.get("Circle", []) if i.startswith("Projection.1.")]
    assert len(circles) == 1 and not [i for i in sk.elements.get("Arc", []) if i.startswith("Projection.1.")]
    assert sk._Name(circles[0]).startswith("u")
    top = max(range(len(cylinder.edges)), key=lambda k: cylinder.edges[k].start.z + cylinder.edges[k].end.z)
    assert circles == ["Projection.1.Edge.%d" % top]


def test_each_projection_has_its_scope_and_id_replaces_it():
    box = occ.Box((0, 0, 0), (4, 2, 1))
    sk = px.Sketch()
    sk.Project(box, id="top").Project(box.Move((10, 0, 0))).Project(box.Move((20, 0, 0)))
    assert projected(sk, "top") and projected(sk, "Projection.1") and projected(sk, "Projection.2")
    assert "top" in sk.wires and not sk.points
    with pytest.raises(ValueError, match="contains '.'"):
        sk.Project(box, id="a.b")


def test_a_section_in_a_sketch_lives_under_intersect3d():
    box = occ.Box((0, 0, -1), (4, 2, 1))
    sk = px.Sketch()
    sk.Intersect3d(box)
    assert len([i for i in sk.elements["Line"] if i.startswith("Intersect3d.1.")]) == 4
