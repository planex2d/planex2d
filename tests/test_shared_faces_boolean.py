# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""a shape whose faces share edges is cut face by face: the faces' booleans compute the point where the other operand
crosses a shared edge on their own, the joined faces still meet there exactly"""
import pytest

import planex as px

H = float.fromhex
# two cells of the docs' honeycomb (glued unit hexagons) as the comb stores them: the window's bottom line y = 8
# crosses their shared edge, each face's boolean put the crossing a rounding apart
UPPER = [(("0x1.021676a049927p+2", "0x1.01e5939ccb4b8p+3"), ("0x1.15f0ce853b8a0p+2", "0x1.e16846be4848ap+2")),
         (("0x1.15f0ce853b8a0p+2", "0x1.e16846be4848ap+2"), ("0x1.3da57e4f1f792p+2", "0x1.e16846be4848ap+2")),
         (("0x1.3da57e4f1f792p+2", "0x1.e16846be4848ap+2"), ("0x1.517fd6341170bp+2", "0x1.01e5939ccb4b8p+3")),
         (("0x1.3da57e4f1f792p+2", "0x1.131703da7272bp+3"), ("0x1.517fd6341170bp+2", "0x1.01e5939ccb4b8p+3")),
         (("0x1.15f0ce853b8a0p+2", "0x1.131703da7272bp+3"), ("0x1.3da57e4f1f792p+2", "0x1.131703da7272bp+3")),
         (("0x1.021676a049927p+2", "0x1.01e5939ccb4b8p+3"), ("0x1.15f0ce853b8a0p+2", "0x1.131703da7272bp+3"))]
LOWER = [(("0x1.3da57e4f1f792p+2", "0x1.e16846be4848ap+2"), ("0x1.517fd6341170bp+2", "0x1.bf056642f9fa6p+2")),
         (("0x1.517fd6341170bp+2", "0x1.bf056642f9fa6p+2"), ("0x1.793485fdf55fdp+2", "0x1.bf056642f9fa5p+2")),
         (("0x1.793485fdf55fdp+2", "0x1.bf056642f9fa5p+2"), ("0x1.8d0edde2e7577p+2", "0x1.e16846be4848ap+2")),
         (("0x1.793485fdf55fdp+2", "0x1.01e5939ccb4b8p+3"), ("0x1.8d0edde2e7577p+2", "0x1.e16846be4848ap+2")),
         (("0x1.517fd6341170bp+2", "0x1.01e5939ccb4b8p+3"), ("0x1.793485fdf55fdp+2", "0x1.01e5939ccb4b8p+3")),
         (("0x1.3da57e4f1f792p+2", "0x1.e16846be4848ap+2"), ("0x1.517fd6341170bp+2", "0x1.01e5939ccb4b8p+3"))]


def cell(edges):
    return px.Wire([px.Line((H(a[0]), H(a[1])), (H(b[0]), H(b[1]))) for a, b in edges]).Face()


@pytest.mark.parametrize("op, window", [
    (lambda a, b: a * b, px.QuickSketch((4, 8)).Rectangle(9, 6).Face()),
    (lambda a, b: a - b, px.QuickSketch((4, 2)).Rectangle(9, 6).Face())])
def test_the_faces_cut_one_by_one_still_meet_where_the_cut_crosses_their_shared_edge(op, window):
    upper, lower = cell(UPPER), cell(LOWER)
    cut = op(px.Glue([upper, lower]), window)
    assert len(cut.faces) == 2
    assert all(w.IsClosed() for f in cut.faces for w in f.wires)
    assert cut.mass == pytest.approx(op(upper, window).mass + op(lower, window).mass, rel=1e-12)
    crossing = [v for v in cut.vertices if v.y == 8.0 and 5.2 < v.x < 5.3]
    assert len(crossing) == 1
