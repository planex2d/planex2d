# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

from typing import Any

from . import _planex as px
from ._sketches import sketch_types
from ._plot_style import _plottable
from ._auto import Auto, is_auto


def _occ_shapes(obj, out, planes):
    """the OCC shapes of obj into out and its planes into planes: planex objects exported, of a Sketch its drawn edges,
    OCC shapes as they are, lists and tuples item by item"""
    from netgen import occ

    from ._compound import Compound

    if isinstance(obj, occ.TopoDS_Shape):
        out.append(obj)
    elif isinstance(obj, px.Plane):
        planes.append(obj)
    elif isinstance(obj, sketch_types):
        obj = _plottable(obj)
        if obj.Wires():
            out.append(obj.ToOCCWires())
    elif isinstance(obj, (px.Face, px.Shape, px.Wire, Compound)):
        out.append(obj.ToOCCShape())
    elif isinstance(obj, (list, tuple)):
        for item in obj:
            _occ_shapes(item, out, planes)
    else:
        raise TypeError("Plot3d: expected Wires, Faces, Shapes, Compounds, Sketches, Planes or OCC shapes, got %s"
                        % type(obj).__name__)


def _plane_outline(plane, size):
    """a square of side size around the plane's origin and a line from the origin along u"""
    from netgen import occ

    h = size / 2
    corners = [occ.Pnt(*plane.ToXYZ((u, v))) for u, v in ((-h, -h), (h, -h), (h, h), (-h, h))]
    edges = [occ.Segment(corners[k], corners[(k + 1) % 4]) for k in range(4)]
    edges.append(occ.Segment(occ.Pnt(*plane.ToXYZ((0, 0))), occ.Pnt(*plane.ToXYZ((h, 0)))))
    return occ.Compound(edges)


def _plot3d_compound(objects, plane_size=None):
    """the one OCC compound Plot3d draws; plane_size None: worked out"""
    from netgen import occ

    shapes, planes = [], []
    _occ_shapes(objects, shapes, planes)
    if planes:
        if plane_size is None:
            plane_size = 1.0
            if shapes:
                pmin, pmax = occ.Compound(shapes).bounding_box
                plane_size = max(pmax[i] - pmin[i] for i in range(3)) or 1.0
        shapes += [_plane_outline(p, plane_size) for p in planes]
    if not shapes:
        raise ValueError("Plot3d: nothing to draw")
    return occ.Compound(shapes)


def Plot3d(objects: Any, /, plane_size: float | Auto = Auto, backend: str = "webgpu", **kwargs: Any) -> Any:
    """Draws planex objects, planes and OCC shapes in one 3d view.

    Everything is exported to OCC, put into one compound and drawn; a sketch contributes its drawn edges. A plane is
    drawn as a square of side plane_size with a line along its u axis. backend "webgpu" draws with
    ngsolve_webgpu.jupyter.Draw: it needs ngsolve_webgpu (pip install ngsolve_webgpu, which brings the full ngsolve)
    and a browser with WebGPU, with the kernel on the same machine as the browser or its websocket port forwarded.
    backend "webgui" draws with netgen.webgui.Draw and needs only netgen.

    Args:
        objects: a Wire, Face, Shape, Compound, sketch, Plane or OCC shape, or a list of them.
        plane_size: the side of a plane's square; px.Auto takes the largest extent of everything else, 1 without.
        backend: 'webgpu' or 'webgui'.
        kwargs: go to the backend's Draw (width, height; settings, euler_angles only for webgui).

    Returns:
        the backend's scene.

    Raises:
        ValueError: backend is unknown, or there is nothing to draw.
        ImportError: backend 'webgpu' without ngsolve_webgpu installed.

    Examples:
        px.Plot3d([face, px.XY], plane_size=2)
        shape.Plot3d(backend="webgui", height=400)
    """
    if backend not in ("webgpu", "webgui"):
        raise ValueError(f"Plot3d: unknown backend {backend!r}, use 'webgpu' or 'webgui'")
    compound = _plot3d_compound(objects, None if is_auto(plane_size, "Plot3d", "plane_size") else plane_size)
    if backend == "webgui":
        from netgen.webgui import Draw

        return Draw(compound, **kwargs)
    try:
        from ngsolve_webgpu.jupyter import Draw
    except ImportError as e:
        raise ImportError("Plot3d(backend='webgpu') needs ngsolve_webgpu (pip install ngsolve_webgpu), or use "
                          "backend='webgui'") from e
    return Draw(compound, **kwargs)
