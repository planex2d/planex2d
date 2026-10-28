# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Sketches without expressions solve as before the expressions came: their degrees of freedom, redundant constraints and
ids exactly, their solved points and dimensions within rounding of the sketch's size and their condition, against the
values recorded with the build before (tests/data/constraintsketch_identity.json; `python
tests/test_constraintsketch_identity.py` records them anew). Within rounding, not bit for bit: compilers fuse multiply-adds
differently."""
import json
import pathlib
import warnings

import pytest

import planex as px

pytestmark = pytest.mark.constraints

DATA = pathlib.Path(__file__).parent / "data" / "constraintsketch_identity.json"


def linkage():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(1.0, 0.3, id="crank").Line(3.2, 1.0, id="coupler").Line(-0.2, -1.3, id="rocker")
    sk.Move("crank.start", px.Coincident).Line(4.0, 0.0, id="ground", construction=True)
    sk.Constrain("ground.end", px.Fix)
    sk.Constrain("rocker.end", "ground.end", px.Coincident)
    for name, value in (("crank", 1), ("coupler", 3.5), ("rocker", 3)):
        sk.Constrain(name, px.Distance, value)
    sk.Constrain("ground", "crank", px.Angle, 20, id="phi")
    sk.Constrain("coupler.end", "crank.start", px.Distance, id="span", reference=True)
    sk.Update({"phi": 80}, steps=4)
    sk["phi"] = 120
    sk.Update()
    return sk


def rectangle():
    sk = px.Sketch()
    sk.Move(sk.origin, px.Coincident).Line(4, 0.3, px.Horizontal, id="bottom").Line(0.2, 2, px.Perpendicular) \
        .Line(-4, 0.1, px.Perpendicular, id="top").Close(px.Perpendicular)
    sk.Constrain("bottom", px.Distance, 4, id="w")
    sk.Constrain("bottom.start", "top", px.Distance, 2, id="ht")
    sk.Constrain("top", px.Distance, 4, id="w2")
    sk.Update({"w": 5, "w2": 5})
    return sk


def bends():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line(2, 0, px.Horizontal, id="a").Arc(1, 90, px.Tangent, id="b").Line(1.5, px.Tangent, id="c")
    sk.Constrain("b", px.Radius, 0.8, id="r")
    sk.Constrain("a", px.Distance, 2.5, id="la")
    sk.Circle((4, 4), 0.5, id="hole")
    sk.Constrain("hole", "c", px.Tangent)
    sk.Update({"r": 1.1})
    return sk


def curve():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Spline([(1, 1), (2, 0), (3, 1)], tangents={0: (1, 0)}, id="s")
    sk.Constrain("s.p2", px.Fix)
    sk.Move((1, -1)).Line(2, 3, id="l")
    sk.Constrain("l.start", "s", px.Coincident)
    sk.Constrain("l", px.Distance, 2, id="ll")
    sk.Update({"ll": 2.5})
    return sk


def pattern():
    sk = px.Sketch()
    sk.Move((1, 0)).Circle(0.2, id="bolt")
    sk.CircularPattern("bolt", sk.origin, 4, id="ring")
    sk.Update({"ring.count": 6})
    sk.Update({"ring.angle": 50})
    return sk


def moved():
    sk = px.Sketch()
    sk.Move((1, 0)).Line(1, 1, id="m1").Line(1, -0.5, id="m2")
    sk.Mirrored(["m1", "m2"], px.V)
    sk.Constrain("m1", px.Distance, 1.5, id="lm")
    sk.Constrain("m1.start", px.H, px.Distance, 0.25, id="up")
    sk.SetOrigin((0.5, 0.25))
    sk.Scale((0, 0), 2)
    return sk


def trimmed():
    sk = px.Sketch()
    sk.Move((0, 0)).Line(4, 0, id="base")
    sk.Move((1, -1)).Line(0, 2, id="c1")
    sk.Move((3, -1)).Line(0, 2, id="c2")
    sk.Trim(("base", (2, 0)), ["c1", "c2"])
    sk.Constrain("c1", px.Vertical)
    sk.Constrain("c1", "c2", px.Distance, 2.5, id="gap")
    return sk


SKETCHES = {f.__name__: f for f in (linkage, rectangle, bends, curve, pattern, moved, trimmed)}


def state(sk):
    return {"locations": {ref: list(p) for ref, p in sk.locations.items()}, "dimensions": sk.dimensions, "dof": sk.dof,
            "redundant": sk.redundant, "condition": sk.condition}


def recorded():
    with warnings.catch_warnings():
        warnings.simplefilter("ignore")
        return {name: state(build()) for name, build in SKETCHES.items()}


def test_sketches_without_expressions_solve_as_before():
    expected = json.loads(DATA.read_text())
    got = recorded()
    assert got.keys() == expected.keys()
    for name, want in expected.items():
        have = got[name]
        assert (have["dof"], have["redundant"]) == (want["dof"], want["redundant"]), name
        assert have["locations"].keys() == want["locations"].keys(), name
        assert have["dimensions"].keys() == want["dimensions"].keys(), name
        size = max([1.0] + [abs(c) for p in want["locations"].values() for c in p])
        for ref, p in want["locations"].items():
            assert have["locations"][ref] == pytest.approx(p, rel=0, abs=1e-12 * size), (name, ref)
        for ref, v in want["dimensions"].items():
            assert have["dimensions"][ref] == pytest.approx(v, rel=1e-12, abs=1e-12 * size), (name, ref)
        assert have["condition"] == pytest.approx(want["condition"], rel=1e-9), name


if __name__ == "__main__":
    DATA.write_text(json.dumps(recorded(), indent=1) + "\n")
