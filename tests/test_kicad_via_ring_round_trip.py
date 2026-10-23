# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""A via's copper written twice, as the via and in a copper polygon whose arcs come from mid points rounded to the
nanometre (WriteKiCad of pic_programmer), reads back as the via's ring: the arcs within the file's resolution of the
via's circle are arcs of that circle."""
import math

import pytest

import planex as px

RING = """(gr_poly
    (pts
      (xy 77.5 77) (arc (start 77.9 77) (mid 78.017157 76.717157) (end 78.3 76.6)) (arc (start 78.3 76.6) (mid 78.7 77) (end 78.3 77.4)) (arc (start 78.3 77.4) (mid 78.017157 77.282843) (end 77.9 77))
      (arc (start 77.5 77) (mid 77.734315 77.565685) (end 78.3 77.8)) (arc (start 78.3 77.8) (mid 79.1 77) (end 78.3 76.2)) (arc (start 78.3 76.2) (mid 77.734315 76.434315) (end 77.5 77))
    )
    (stroke (width 0) (type solid)) (fill yes) (layer "F.Cu") (net 1))"""
VIA = '(via (at 78.3 77) (size 1.6) (drill 0.8) (layers "F.Cu" "B.Cu") (net 1))'
BOARD = """(kicad_pcb (version 20250114) (generator "test") (general (thickness 1.6))
  (layers (0 "F.Cu" signal) (2 "B.Cu" signal) (25 "Edge.Cuts" user))
  (net 0 "") (net 1 "N")
  {items}
  (gr_rect (start 70 70) (end 90 90) (stroke (width 0.1) (type solid)) (fill no) (layer "Edge.Cuts")))"""
RING_MM2 = math.pi * (0.8 ** 2 - 0.4 ** 2)


@pytest.mark.parametrize("items", [RING + VIA, VIA + RING, VIA])
def test_a_via_written_twice_reads_back_as_its_ring(tmp_path, items):
    path = tmp_path / "ring.kicad_pcb"
    path.write_text(BOARD.format(items=items))
    fcu = px.FromKiCad(str(path))["F.Cu"]
    assert 1e6 * fcu.mass == pytest.approx(RING_MM2, rel=1e-6)
    assert len(fcu.faces) == 1
