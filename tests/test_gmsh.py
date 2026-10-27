# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import re

import pytest

import planex as px


def plate():
    f = px.QuickSketch((0, 0)).Rectangle(2, 1).Face()
    f.name = "plate"
    for e in f.edges:
        m = e.midpoint
        e.name = {0.0: "left", 2.0: "right"}.get(m.x, "wall")
    return f


def commands(path, kind):
    return [line for line in path.read_text().splitlines() if line.startswith(kind + "(") or line.startswith(kind + " ")]


def test_a_plate_with_a_hole(tmp_path):
    hole = px.QuickSketch().Circle((1, 0.5), 0.2).Face()
    for e in hole.edges:
        e.name, e.maxh = "hole", 0.05
    (plate() - hole).WriteGeo(tmp_path / "p.geo")
    p = tmp_path / "p.geo"
    assert len(commands(p, "Line")) == 4 and len(commands(p, "Circle")) == 4
    assert len(commands(p, "Curve Loop")) == 2 and commands(p, "Plane Surface") == ["Plane Surface(1) = {1, 2};"]
    text = p.read_text()
    for name in ("left", "right", "wall", "hole"):
        assert re.search(rf'Physical Curve\("{name}", \d+\) = {{[-\d, ]+}};', text)
    assert re.search(r'Physical Surface\("plate", \d+\) = \{1\};', text)
    sized = [line for line in commands(p, "Point") if line.count(",") == 3]
    assert len(sized) == 4 and all(line.endswith(", 0.05};") for line in sized)


def test_unnamed_edges_and_faces_share_the_groups_unnamed(tmp_path):
    a = px.QuickSketch((-0.5, -0.5)).Rectangle(1, 1).Face()
    b = px.QuickSketch((2.5, -0.5)).Rectangle(1, 1).Face()
    b.name = "b"
    px.Glue([a, b]).WriteGeo(tmp_path / "g.geo")
    text = (tmp_path / "g.geo").read_text()
    assert re.search(r'Physical Curve\("unnamed", \d+\) = {[\d, ]+};', text)
    assert re.search(r'Physical Surface\("b", \d+\) = {\d};', text) and re.search(r'Physical Surface\("unnamed", \d+\) = {\d};', text)


def test_identifications_become_periodic_curves(tmp_path):
    f = plate()
    f.Identify("left", "right")
    f.WriteGeo(tmp_path / "f.geo")
    periodic = commands(tmp_path / "f.geo", "Periodic Curve")
    assert len(periodic) == 1
    m = re.match(r"Periodic Curve {(-?\d+)} = {(-?\d+)} Affine {(.*)};", periodic[0])
    assert [float(x) for x in m[3].split(",")] == [1, 0, 0, 2, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1]


def test_circles_split_at_half_a_circle_and_splines_are_bsplines(tmp_path):
    px.QuickSketch().Circle((0, 0), 1).Face().WriteGeo(tmp_path / "c.geo")
    assert len(commands(tmp_path / "c.geo", "Circle")) == 4
    blob = px.Move((0, 0)).Spline([(1, 0.5), (2, 0), (1, -1)]).Close().Face()
    blob.WriteGeo(tmp_path / "s.geo")
    assert len(commands(tmp_path / "s.geo", "BSpline")) == 1 and len(commands(tmp_path / "s.geo", "Line")) == 1


def test_wires_and_compounds(tmp_path):
    w = px.Line((0, 0), (1, 0))
    w.name = "cut"
    px.Compound([w, plate()]).WriteGeo(tmp_path / "w.geo")
    text = (tmp_path / "w.geo").read_text()
    assert 'Physical Curve("cut"' in text and 'Physical Surface("plate"' in text
    with pytest.raises(TypeError):
        px.WriteGeo(3, tmp_path / "x.geo")


GMSH = r"""
import json, sys
import gmsh
gmsh.initialize()
gmsh.option.setNumber("General.Terminal", 0)
gmsh.open(sys.argv[1])
gmsh.model.mesh.generate(2)
groups = sorted(gmsh.model.getPhysicalName(d, t) or "#" for d, t in gmsh.model.getPhysicalGroups())
moves = set()
for dim, tag in gmsh.model.getEntities(1):
    master, slaves, masters, affine = gmsh.model.mesh.getPeriodicNodes(dim, tag)
    for s, m in zip(slaves, masters):
        ps, pm = gmsh.model.mesh.getNode(int(s))[0], gmsh.model.mesh.getNode(int(m))[0]
        moves.add((round(ps[0] - pm[0], 9), round(ps[1] - pm[1], 9)))
gmsh.finalize()
print(json.dumps({"groups": groups, "moves": sorted(moves)}))
"""


def test_gmsh_meshes_what_is_written(tmp_path):
    import json
    import subprocess
    import sys

    # gmsh brings its own OpenCASCADE: it runs in a process of its own
    if subprocess.run([sys.executable, "-c", "import gmsh; gmsh.initialize()"], capture_output=True).returncode != 0:
        pytest.skip("gmsh does not run here")
    f = plate()
    f.Identify("left", "right")
    hole = px.QuickSketch().Circle((1, 0.5), 0.2).Face()
    (f - hole).WriteGeo(tmp_path / "f.geo")
    out = subprocess.run([sys.executable, "-c", GMSH, str(tmp_path / "f.geo")], capture_output=True, text=True, check=True)
    result = json.loads(out.stdout)
    assert result["groups"] == ["left", "plate", "right", "unnamed", "wall"]
    assert result["moves"] == [[2.0, 0.0]]
