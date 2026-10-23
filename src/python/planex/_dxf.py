# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

import math
import os
import warnings
from typing import TYPE_CHECKING

from . import _planex as px
from ._geom import edge_views, read_name
from ._units import cm, inch, m, mil, mm, um

if TYPE_CHECKING:
    from ._compound import Compound
    from ._planex import Face, QuickSketch, Shape, Sketch, Wire
    from .types import Plane

ELLIPSE_TOL = 1e-4  # relative to the major axis; planex has no ellipse, it becomes a spline through sampled points


def _rgb(ent, doc):
    if ent.dxf.hasattr("true_color"):
        import ezdxf.colors
        return tuple(c / 255 for c in ezdxf.colors.int2rgb(ent.dxf.true_color))
    aci = ent.dxf.color
    if aci == 256 and ent.dxf.layer in doc.layers:  # BYLAYER
        aci = abs(doc.layers.get(ent.dxf.layer).color)
    if 1 <= aci <= 255:
        import ezdxf.colors
        return tuple(c / 255 for c in ezdxf.colors.aci2rgb(aci))
    return None


def _arc_from_bulge(p, q, bulge, plane):
    # bulge = tan(sweep / 4), positive counterclockwise; the center sits r - sagitta left of the chord midpoint
    chord = (q[0] - p[0], q[1] - p[1])
    length = math.hypot(*chord)
    sagitta = abs(bulge) * length / 2
    radius = length * (1 + bulge * bulge) / (4 * abs(bulge))
    n = (-chord[1] / length, chord[0] / length)
    h = (radius - sagitta) * (1 if bulge > 0 else -1)
    center = ((p[0] + q[0]) / 2 + n[0] * h, (p[1] + q[1]) / 2 + n[1] * h)
    return px._Arc(p, q, center, bulge > 0, plane=plane)


def _polyline(pts, closed, plane):
    """lines and bulge arcs between consecutive (x, y, bulge) points"""
    out = []
    pairs = list(zip(pts, pts[1:] + (pts[:1] if closed else [])))
    for (x0, y0, b), (x1, y1, _) in pairs:
        if (x0, y0) == (x1, y1):
            continue
        out.append(_arc_from_bulge((x0, y0), (x1, y1), b, plane) if b else px.Line((x0, y0), (x1, y1), plane=plane))
    return out


def _bspline_pieces(poles, knots, weights, degree):
    """(poles, knots, weights) of the pieces between interior knots of multiplicity degree + 1: SVG conversions store
    chains of Bezier pieces as one spline, which is not one curve"""
    p = degree
    pieces = []
    a = 0
    i = p + 1
    while i < len(knots) - (p + 1):
        j = i
        while j < len(knots) and knots[j] == knots[i]:
            j += 1
        if j - i >= p + 1:
            pieces.append((a, i, i + p + 1))
            a = j - (p + 1)
            i = a + p + 1
        else:
            i = j
    pieces.append((a, len(knots) - p - 1, len(knots)))
    return [(poles[a:b], knots[a:c], weights[a:b] if weights else None) for a, b, c in pieces]


def _splines(bs, s, plane):
    """one wire per piece; a piece whose poles all coincide is nothing, one OCC rejects is interpolated through samples"""
    poles = [(p.x * s, p.y * s) for p in bs.control_points]
    weights = list(bs.weights())
    out = []
    for pp, kk, ww in _bspline_pieces(poles, list(bs.knots()), weights, bs.degree):
        extent = max(abs(p[i] - pp[0][i]) for p in pp for i in (0, 1))
        if extent <= 1e-12 * max(1.0, max(abs(c) for p in pp for c in p)):
            continue
        try:
            out.append(px._BSpline(pp, kk, bs.degree, ww, plane=plane))
        except (ValueError, RuntimeError):
            piece = type(bs)(pp, order=bs.degree + 1, knots=kk, weights=ww)
            out.append(px._Spline([(p.x, p.y) for p in piece.approximate(16 * len(pp))], plane=plane))
    return out


def _edges(ent, s, plane):
    kind = ent.dxftype()
    if kind == "LINE":
        return [px.Line((ent.dxf.start.x * s, ent.dxf.start.y * s), (ent.dxf.end.x * s, ent.dxf.end.y * s), plane=plane)]
    if kind in ("ARC", "CIRCLE", "LWPOLYLINE") or (kind == "POLYLINE" and ent.is_2d_polyline):
        # in the entity's own coordinates (OCS): CAD writes mirrored arcs with extrusion -Z, x mirrored and the
        # turning sense reversed
        z = ent.dxf.extrusion.normalize().z
        if abs(abs(z) - 1.0) > 1e-12:
            warnings.warn(f"FromDXF: a {kind} in a plane tilted to the drawing (extrusion {tuple(ent.dxf.extrusion)}) "
                          "is skipped", px.PlanexWarning, stacklevel=3)
            return []
        ocs, ccw = ent.ocs(), z > 0

        def to(x, y):
            p = ocs.to_wcs((x, y, 0))
            return (p.x * s, p.y * s)
    if kind in ("ARC", "CIRCLE"):
        cx, cy, r = ent.dxf.center.x, ent.dxf.center.y, ent.dxf.radius
        c = to(cx, cy)
        if kind == "CIRCLE":
            return [px.Circle(c, r * s, reverse=not ccw, plane=plane)]
        a0 = math.radians(ent.dxf.start_angle)
        a1 = math.radians(ent.dxf.end_angle)
        if a1 <= a0:
            a1 += 2 * math.pi
        start = to(cx + r * math.cos(a0), cy + r * math.sin(a0))
        end = to(cx + r * math.cos(a1), cy + r * math.sin(a1))
        return [px._Arc(start, end, c, ccw, plane=plane)]
    if kind == "LWPOLYLINE":
        return _polyline([(*to(x, y), b if ccw else -b) for x, y, b in ent.get_points("xyb")], ent.closed, plane)
    if kind == "POLYLINE" and ent.is_2d_polyline:
        pts = [(*to(v.dxf.location.x, v.dxf.location.y), v.dxf.bulge if ccw else -v.dxf.bulge) for v in ent.vertices]
        return _polyline(pts, ent.is_closed, plane)
    if kind == "SPLINE":
        return _splines(ent.construction_tool(), s, plane)
    if kind == "ELLIPSE":
        pts = [(p.x * s, p.y * s) for p in ent.flattening(ELLIPSE_TOL * ent.dxf.major_axis.magnitude * s)]
        closed = abs(ent.dxf.end_param - ent.dxf.start_param - 2 * math.pi) < 1e-9
        if closed:
            pts = pts[:-1]
        return [px._Spline(pts, periodic=closed, plane=plane)] if len(pts) > 2 else []
    return []


# the length in meters of every $INSUNITS code a drawing can count in (DXF reference); 21-24 are US survey units
_METERS = {1: inch, 2: 12 * inch, 3: 63360 * inch, 4: mm, 5: cm, 6: m, 7: 1000 * m, 8: 1e-6 * inch, 9: mil, 10: 36 * inch,
           11: 1e-10, 12: 1e-9, 13: um, 14: 0.1, 15: 10.0, 16: 100.0, 17: 1e9,
           21: 1200 / 3937, 22: 100 / 3937, 23: 3600 / 3937, 24: 6336000 / 3937}


def FromDXF(path: str | os.PathLike, /, tol: float = 1e-7, layers: list[str] | None = None, unit: float | None = None,
            plane: Plane | None = None) -> list[Wire]:
    """Reads the lines, arcs, circles, polylines, splines and ellipses of a DXF file's model space as clean wires.

    The coordinates are scaled to metres; the wires are built as by px.Wires: ends within tol (after scaling) joined,
    the edges cut against each other (crossings, T-junctions, an overlapping stretch kept once), then the chains.
    Blocks (INSERT) are read as their entities. The layer becomes the edges' name, the colour their col; an ellipse
    becomes a spline through sampled points. px.Glue(wires) makes them one Shape.

    Args:
        path: the DXF file.
        tol: ends closer than this (after scaling) are joined, a length.
        layers: the names of the layers to read; None reads all.
        unit: the length the file counts in (px.mm, px.inch, ...); None takes the unit its header ($INSUNITS) names,
            metres with a warning if it names none.
        plane: the plane of the wires; None is the xy plane.

    Returns:
        a list of wires; [] for an empty drawing.

    Raises:
        ValueError: unit is not a positive length.

    Examples:
        px.FromDXF("part.dxf", unit=px.mm)
        px.Glue(px.FromDXF("part.dxf", layers=["outline"]))
    """
    import ezdxf
    plane = px.AxesOf(plane, "FromDXF")

    doc = ezdxf.readfile(str(path))
    if unit is None:
        code = doc.header.get("$INSUNITS", 0)
        unit = _METERS.get(code)
        if unit is None:
            warnings.warn(f"FromDXF: {path} names no length unit ($INSUNITS {code}); read as meters, unit=px.mm or "
                          f"another sets it", px.PlanexWarning, stacklevel=2)
            unit = m
    elif not unit > 0:
        raise ValueError(f"FromDXF(unit={unit}): a unit is a positive length, e.g. px.mm")
    scale = unit
    edges = []
    for e in doc.modelspace():
        for ent in (e.virtual_entities() if e.dxftype() == "INSERT" else [e]):
            if layers is not None and ent.dxf.layer not in layers:
                continue
            col = _rgb(ent, doc)
            for w in _edges(ent, scale, plane):
                w.name = read_name(ent.dxf.layer)
                if col is not None:
                    w.col = col
                edges.append(w)
    return px.Wires(edges, tol)


# the DXF $INSUNITS code of each unit factor; another factor writes "unitless"
_INSUNITS = {m: 6, cm: 5, mm: 4, um: 13, inch: 1, mil: 9}


def WriteDXF(obj: Face | Shape | Wire | Sketch | QuickSketch | Compound, path: str | os.PathLike, /,
             unit: float = m) -> None:
    """Writes the edges as LINE, ARC and SPLINE entities of a DXF file, each edge once.

    An edge's name becomes its layer (the characters a layer name cannot have, such as / and :, become _, with a
    warning), its col the true colour. The coordinates are divided by unit and $INSUNITS names it.

    Args:
        obj: the Face, Shape, Wire, sketch or Compound to write.
        path: the file to write.
        unit: the length the file counts in (px.m, px.mm, px.inch, ...).

    Raises:
        TypeError: unit is a name, not a length.
        ValueError: unit is not a positive finite length.

    Examples:
        face.WriteDXF("part.dxf", unit=px.mm)
        px.WriteDXF(shape, "part.dxf")
    """
    import ezdxf
    import ezdxf.colors

    if isinstance(unit, str):
        raise TypeError(f"WriteDXF(unit={unit!r}): a unit is a length, e.g. px.mm, not a name")
    if not (unit > 0 and math.isfinite(unit)):
        raise ValueError(f"WriteDXF(unit={unit}): a unit is a positive length, e.g. px.mm")
    k = 1.0 / unit

    def p(v):
        return (v.x * k, v.y * k)

    from ezdxf.lldxf.validator import INVALID_LAYER_NAME_CHARACTERS

    doc = ezdxf.new()
    doc.units = _INSUNITS.get(unit, 0)
    msp = doc.modelspace()
    renamed = {}
    for v in edge_views(obj):
        e = v.edge
        layer = v.name or "0"
        if INVALID_LAYER_NAME_CHARACTERS.intersection(layer):
            renamed[layer] = layer = "".join("_" if ch in INVALID_LAYER_NAME_CHARACTERS else ch for ch in layer)
        if layer not in doc.layers:
            doc.layers.add(layer)
        attribs = {"layer": layer}
        if v.col is not None:
            attribs["true_color"] = ezdxf.colors.rgb2int(tuple(round(255 * c) for c in v.col[:3]))
        if isinstance(e, px.LineEdge):
            msp.add_line(p(e.start), p(e.end), dxfattribs=attribs)
        elif isinstance(e, px.CircleEdge):
            msp.add_circle(p(e.center), e.radius * k, dxfattribs=attribs)
        elif isinstance(e, px.ArcEdge):
            a, b = (e.start, e.end) if e.ccw else (e.end, e.start)
            c = e.center
            start_angle = math.degrees(math.atan2(a.y - c.y, a.x - c.x))
            end_angle = math.degrees(math.atan2(b.y - c.y, b.x - c.x))
            msp.add_arc(p(c), e.radius * k, start_angle, end_angle, dxfattribs=attribs)
        else:
            b = e.bspline
            poles = [(x * k, y * k) for x, y in b["poles"]]
            if b["weights"] is None:
                msp.add_open_spline(poles, b["degree"], b["knots"], dxfattribs=attribs)
            else:
                msp.add_rational_spline(poles, b["weights"], b["degree"], b["knots"], dxfattribs=attribs)
    if renamed:
        warnings.warn("WriteDXF: names a DXF layer cannot have were written as " +
                      ", ".join(f"{a!r} -> {b!r}" for a, b in renamed.items()), px.PlanexWarning, stacklevel=2)
    doc.saveas(str(path))
