# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

from typing import TYPE_CHECKING

import planex as px

if TYPE_CHECKING:
    from ._planex import Wire


def ClosedOpenWires(wires: Wire | list[Wire], /) -> list[Wire]:
    """Copies of the wires, each open one closed by a straight line from its end back to its start.

    Closed wires are copied as they are.

    Args:
        wires: a wire or a list of wires.

    Returns:
        a list of wires.

    Raises:
        ValueError: the closing line would cross or overlap its wire (a single straight edge cannot be closed).

    Examples:
        px.ClosedOpenWires(u)
        px.Face(px.ClosedOpenWires([outline, u]))
    """
    out = []
    for w in [wires] if isinstance(wires, px.Wire) else wires:
        closed = w.Copy()
        edges = w.edges
        ends = [edges[k].end if at_end else edges[k].start for k, at_end in w.OpenEnds()]
        if len(ends) == 2:
            a, b = ends
            try:
                closed.Add(px.Line(a, b, plane=w))
            except ValueError as e:
                raise ValueError(f"ClosedOpenWires: the straight line from ({a.x:g}, {a.y:g}) to ({b.x:g}, {b.y:g}) "
                                 f"cannot close the wire: {e}") from None
        out.append(closed)
    return out
