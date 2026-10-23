# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math

import pytest

import planex as px

ngs = pytest.importorskip("ngsolve")
from netgen.occ import MoveTo, OCCGeometry  # noqa: E402


def square_mesh(maxh=0.1):
    wp = MoveTo(0, 0).Line(1, name="bottom").Rotate(90).Line(1, name="right").Rotate(90)
    wp.Line(1, name="top").Rotate(90).Line(1, name="left").Close()
    return ngs.Mesh(OCCGeometry(wp.Face(), dim=2).GenerateMesh(maxh=maxh))


def disc(mesh, order, r=0.3):
    gf = ngs.GridFunction(ngs.H1(mesh, order=order))
    gf.Set(r**2 - (ngs.x - 0.5) ** 2 - (ngs.y - 0.5) ** 2)
    return gf


def test_disc_inside_the_domain_is_resampled_and_projected():
    p = px.FromLevelSet(disc(square_mesh(), 3), 0.02)
    assert len(p.wires) == 1
    assert abs(p.mass - math.pi * 0.3**2) < 1e-3
    assert {e.name for e in p.edges} == {"interface"}
    lengths = [e.mass for e in p.edges]
    assert 0.01 < min(lengths) and max(lengths) < 0.03
    # every vertex lies on the cubic level set
    for e in p.edges:
        assert abs(0.3**2 - (e.start.x - 0.5) ** 2 - (e.start.y - 0.5) ** 2) < 1e-9


def test_edge_length_none_keeps_the_lattice_resolution():
    p = px.FromLevelSet(disc(square_mesh(), 1), None)
    assert abs(p.mass - math.pi * 0.3**2) < 0.02
    assert max(e.mass for e in p.edges) < 0.15


def test_region_reaching_the_boundary_keeps_the_boundary_with_its_names():
    mesh = square_mesh()
    gf = ngs.GridFunction(ngs.H1(mesh, order=2))
    gf.Set(0.5 - ngs.x)
    p = px.FromLevelSet(gf, 0.05)
    assert abs(p.mass - 0.5) < 1e-9
    names = {e.name for e in p.edges}
    assert names == {"interface", "bottom", "top", "left"}
    assert all(abs(e.start.x - 0.5) < 1e-9 for e in p.edges if e.name == "interface")
    assert abs(sum(e.mass for e in p.edges if e.name != "interface") - 2.0) < 1e-9


def test_hole_and_negative_inside():
    mesh = square_mesh()
    gf = ngs.GridFunction(ngs.H1(mesh, order=2))
    r = ngs.sqrt((ngs.x - 0.5) ** 2 + (ngs.y - 0.5) ** 2)
    gf.Set((r - 0.2) * (0.4 - r))
    ring = px.FromLevelSet(gf, 0.02)
    assert len(ring.wires) == 2
    assert abs(ring.mass - math.pi * (0.4**2 - 0.2**2)) < 2e-3
    rest = px.FromLevelSet(gf, 0.02, inside="negative")
    assert len(rest.wires) == 3
    assert abs(rest.mass + ring.mass - 1.0) < 1e-6
    assert "left" in {e.name for e in rest.edges}


def test_empty_region_warns():
    mesh = square_mesh(0.3)
    gf = ngs.GridFunction(ngs.H1(mesh, order=1))
    gf.Set(-1.0 - ngs.x)
    with pytest.warns(UserWarning):
        p = px.FromLevelSet(gf, 0.1)
    assert len(p.edges) == 0
