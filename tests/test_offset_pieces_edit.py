# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import pytest

import planex as px

pytestmark = pytest.mark.constraints

TOL = 1e-9


def walled(**options):
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line((10, 0), id="s1").Line((10, 6), id="s2").Line((0, 6), id="s3").Close(id="s4")
    sk.Constrain("s1", px.Horizontal)
    sk.Constrain("s2", px.Vertical)
    sk.Constrain("s1", px.Distance, 10)
    sk.Constrain("s2", px.Distance, 6)
    sk.Offset(["s1", "s2", "s3", "s4"], 1, id="wall", **options)
    return sk


def test_a_piece_of_a_live_offset_is_trimmed_alone_and_keeps_following_its_source():
    sk = walled()
    sk.Line((5, -3), (5, 9), id="cut")
    sk.Trim(("wall.s1", (2, -1)), "cut")
    assert "wall.s1.trimmed.1" in sk.elements["Line"]
    sk["wall.d"] = 2
    sk.Update()
    assert sk.Location("wall.s1.start")[1] == pytest.approx(-2, abs=TOL)
    assert sk.Location("wall.s1.end") == pytest.approx((10, -2), abs=TOL)
    assert sk.Location("s1.end") == pytest.approx((10, 0), abs=TOL)


def test_a_piece_of_a_live_offset_is_removed_alone():
    sk = walled()
    sk.Remove("wall.s1")
    assert "wall.s1" not in sk.elements["Line"] and "wall.s2" in sk.elements["Line"]
    sk["wall.d"] = 2
    sk.Update()
    assert sk.Location("wall.s2.start")[0] == pytest.approx(12, abs=TOL)


def test_a_sharp_corner_of_a_live_offset_takes_a_fillet():
    sk = walled(corners=px.Sharp)
    sk.Fillet("wall.s1.end", 0.5)
    sk["wall.d"] = 2
    sk.Update()
    g = sk._Geometry("Fillet.1")
    assert g["r"] == pytest.approx(0.5, abs=TOL) and g["center"] == pytest.approx((11.5, -1.5), abs=TOL)


def curved(d, **options):
    """a block with a spline side, offset by d (d < 0 inside, both ends of the spline's offset where it crosses its
    neighbours')"""
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line((10, 0), id="s1").Arc(3, 90, px.Tangent, id="a1")
    sk.Spline([(11, 7), (13, 11), (8, 14)], id="sp")
    sk.Line((0, 14), id="top").Close(id="left")
    sk.Offset(["s1", "a1", "sp", "top", "left"], d, id="wall", **options)
    return sk


def off_source(sk, pieces, source, d):
    """how far the ends of the pieces lie from d off their source"""
    return max(abs(sk.Measure(f"{p}.{end}", to=source)["distance"] - d) for p in pieces for end in ("start", "end"))


def test_a_spline_s_offset_piece_is_trimmed_alone_and_keeps_following_its_source():
    # inside: both ends of the spline's offset are where it crosses its neighbours' offsets
    sk = curved(-1, corners=px.Sharp)
    corner = sk.Location("wall.sp.end")
    sk.Line((0, 8), (20, 8), id="cut", construction=True)
    sk.Trim(("wall.sp", (12, 10)), "cut")
    assert "wall.sp.trimmed.1" in sk.elements["Spline"]
    assert sk.Location("wall.sp.end")[1] == pytest.approx(8, abs=TOL)
    assert sk.Location("wall.sp.trimmed.1.end") == pytest.approx(corner, abs=TOL)
    sk["wall.d"] = 1.2
    sk.Update()
    assert sk.Measure("wall.sp.end", to="cut")["distance"] == pytest.approx(0, abs=TOL)
    assert off_source(sk, ["wall.sp", "wall.sp.trimmed.1"], "sp", 1.2) < TOL
    assert sk.Location("wall.sp.trimmed.1.end") == pytest.approx(sk.Location("wall.top.start"), abs=TOL)


def test_a_spline_s_offset_piece_is_split_and_named_along_its_own_way():
    sk = curved(1)
    sk.Line((0, 10), (20, 10), id="cut", construction=True)
    sk.Split("wall.sp", "cut")
    assert sk.Location("wall.sp.1.end")[1] == pytest.approx(10, abs=TOL) and sk.Location("wall.sp.1.start")[1] < 10
    sk["wall.d"] = 1.5
    sk.Update()
    assert off_source(sk, ["wall.sp.1", "wall.sp.2"], "sp", 1.5) < TOL
    # an offset that runs against its spline (the chain takes it backwards): its pieces still count from its start
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Line((10, 0), id="s1")
    sk.Move((10, 10)).Spline([(13, 5), (10, 0)], id="sp")
    sk.Constrain("sp.end", "s1.end", px.Coincident)
    sk.Offset(["s1", "sp"], 1, id="wall")
    start = sk.Location("wall.right.sp.start")
    sk.Line((0, 5), (20, 5), id="cut", construction=True)
    sk.Split("wall.right.sp", "cut")
    assert sk.Location("wall.right.sp.1.start") == pytest.approx(start, abs=TOL)


def test_a_spline_s_offset_piece_is_removed_alone():
    sk = curved(1)
    sk.Remove("wall.sp")
    assert "wall.sp" not in sk.elements["Spline"]
    sk["wall.d"] = 1.5
    sk.Update()
    assert off_source(sk, ["wall.top"], "top", 1.5) < TOL


def test_a_closed_spline_s_offset_is_cut_across_its_seam():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Spline([(6, -1), (9, 4), (4, 8), (-2, 5)], periodic=True, id="loop")
    sk.Offset(["loop"], 1, id="ring")
    sk.Line((-5, 3), (12, 3), id="cut", construction=True)
    sk.Split("ring.loop", "cut")
    assert {"ring.loop.1", "ring.loop.2"} <= set(sk.elements["Spline"])
    sk["ring.d"] = 1.5
    sk.Update()
    assert off_source(sk, ["ring.loop.1", "ring.loop.2"], "loop", 1.5) < TOL
    assert all(sk.Measure(f"ring.loop.{k}.{end}", to="cut")["distance"] == pytest.approx(0, abs=TOL) for k in (1, 2) for end in ("start", "end"))


def test_a_sharp_corner_of_a_spline_s_offset_takes_a_fillet():
    sk = curved(-1, corners=px.Sharp)
    sk.Fillet("wall.top.start", 0.5)
    sk["wall.d"] = 1.2
    sk.Update()
    assert sk.Measure("Fillet.1.center", to="wall.sp")["distance"] == pytest.approx(0.5, abs=TOL)
    assert sk.Measure("Fillet.1.center", to="wall.top")["distance"] == pytest.approx(0.5, abs=TOL)
    assert off_source(sk, ["wall.sp"], "sp", 1.2) < TOL


def test_a_closed_spline_s_offset_trimmed_across_its_seam_keeps_its_length_also_frozen():
    sk = px.Sketch()
    sk.Move((0, 0), px.Fix).Spline([(6, -1), (9, 4), (4, 8), (-2, 5)], periodic=True, id="loop")
    sk.Offset(["loop"], 1, id="ring")
    whole = sk.Measure("ring.loop")["length"]
    sk.Line((-5, 3), (12, 3), id="cut", construction=True)
    # the piece below the cut runs over the seam (the ring starts below the loop's first point)
    sk.Trim(("ring.loop", (5, -2)), "cut")
    pieces = ("ring.loop", "ring.loop.trimmed.1")
    assert sum(sk.Measure(p)["length"] for p in pieces) == pytest.approx(whole, rel=1e-6)
    frozen = px.QuickSketch(sk, copy=True)
    assert sum(frozen.Measure(p)["length"] for p in pieces) == pytest.approx(whole, rel=1e-6)
