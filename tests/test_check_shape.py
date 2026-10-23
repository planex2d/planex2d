# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

from netgen.occ import Box, Pnt

import planex as px


def test_the_shell_of_a_box_is_valid_closed_and_has_no_free_edges():
    assert px.debug.CheckShape(Box(Pnt(0, 0, 0), Pnt(1, 2, 3)).shells[0]) == {"valid": True, "closed": True, "free_edges": 0}


def test_one_face_of_a_box_has_four_free_edges():
    face = Box(Pnt(0, 0, 0), Pnt(1, 2, 3)).faces[0]
    assert px.debug.CheckShape(face) == {"valid": True, "closed": False, "free_edges": 4}
