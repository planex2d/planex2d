# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Dimensioned drawings after DIN practice: px.Drawing, one view of constraint sketches with their own
dimensions, and px.Sheet, views at one scale on a DIN sheet, written as SVG and PDF through matplotlib."""
from __future__ import annotations

import datetime
import importlib
import io
import math
import os
from types import SimpleNamespace
from typing import TYPE_CHECKING, Any

import numpy as np

from . import _dimensions as dims
from ._auto import Auto, is_auto
from ._dimensions import ARROW, HEIGHT, NARROW, WIDE

if TYPE_CHECKING:
    from ._planex import Plane

FONT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fonts", "osifont-lgpl3fe.ttf")
TEMPLATE = os.path.join(os.path.dirname(os.path.abspath(__file__)), "templates", "title_block.dxf")
# the sheet sizes in mm, upright (DIN EN ISO 5457); the drawing area leaves 20 mm on the left for filing, 10 mm round
SIZES = {"A4": (210.0, 297.0), "A3": (297.0, 420.0), "A2": (420.0, 594.0), "A1": (594.0, 841.0), "A0": (841.0, 1189.0)}
FILING, BORDER = 20.0, 10.0
# the grid reference zones (DIN EN ISO 5457): their strip this wide outside the frame, fields this long from the
# centring marks outward, their letters (no I, O)
GRID, FIELD = 5.0, 50.0
LETTERS = "ABCDEFGHJKLMNPQRSTUVWXYZ"
# the scales a drawing may take (DIN ISO 5455), largest first
SCALES = (50, 20, 10, 5, 2, 1, 1 / 2, 1 / 5, 1 / 10, 1 / 20, 1 / 50, 1 / 100, 1 / 200, 1 / 500, 1 / 1000)
# the room for dimensions round a view, in mm on paper: three rows
MARGIN = dims.FIRST + 2 * dims.ROW + 6.0
GAP = 12.0
# the height of view names and of the callouts' letters, in mm
NAME = 5.0
# DIN EN ISO 128-2: long-dashed dotted (04) and dashed (02) lines, in line widths
CENTER = (0, (24, 3, 0.5, 3))
HIDDEN = (0, (12, 3))
SYMBOL_CENTER = (0, (8, 1, 0.5, 1))
INK = "#000000"


def _pt(mm):
    return mm * 72 / 25.4


def _need(module, what):
    try:
        return importlib.import_module(module)
    except ImportError as e:
        raise ImportError(f"{what} needs {module} (pip install {module})") from e


class _Lettering:
    """osifont at the size whose capitals are HEIGHT mm high, and text widths in mm"""

    def __init__(self, font=None):
        from matplotlib import font_manager
        from matplotlib.textpath import TextPath

        self.path = font or FONT
        font_manager.fontManager.addfont(self.path)
        self.prop = font_manager.FontProperties(fname=self.path)
        cap = TextPath((0, 0), "H", size=1, prop=self.prop).get_extents().height
        self.points_per_mm = _pt(1.0) / cap
        self.prop.set_size(self.points_per_mm * HEIGHT)
        self._TextPath = TextPath

    def sized(self, height):
        p = self.prop.copy()
        p.set_size(self.points_per_mm * height)
        return p

    def width(self, text, height=HEIGHT):
        extent = self._TextPath((0, 0), text, size=self.points_per_mm * height, prop=self.prop).get_extents().width
        return extent * 25.4 / 72


def _paths(everything):
    from ._svg import _path

    return {i: np.asarray(_path(g), float) for i, g in everything.items() if g["kind"] in dims.CURVES}


class _Sketched:
    """one sketch of a view: its drawing by id, its outline (the curves not construction) in the view's coordinates"""

    def __init__(self, sketch, to_view):
        self.sketch = sketch
        self.everything = {g["id"]: g for g in sketch._Drawing()}
        self.to_view = to_view
        self.flat = to_view is _same
        paths = _paths(self.everything)
        self.outline = [to_view(p) for i, p in paths.items() if not self.everything[i]["construction"]]


def _same(pts):
    return np.asarray(pts, float)


def _mapper(plane, view):
    """a sketch's plane coordinates in a view's plane coordinates (_same on one plane)"""
    if plane is None or view is None or (np.allclose(plane.origin, view.origin) and np.allclose(plane.h, view.h)
                                         and np.allclose(plane.v, view.v)):
        return _same

    def to_view(pts):
        pts = np.atleast_2d(np.asarray(pts, float))
        xyz = [plane.ToXYZ(tuple(p[:2])) for p in pts]
        out = np.array([view.ToHV(tuple(q)) for q in xyz])
        return out if np.ndim(pts) > 1 else out[0]

    return to_view


class Drawing:
    """A dimensioned drawing of a constraint sketch after DIN practice (DIN ISO 129-1).

    The drawing keeps the sketch by reference: its outline, center lines and its own dimension constraints are drawn
    as a drafting drawing (extension and dimension lines, closed arrows, values above the lines with a decimal comma,
    radii and diameters on leaders, equal features once with n×), laid out automatically; Place moves single
    dimensions, Update draws the sketch anew after it changed. Shown in a notebook by itself; ToSVG, WriteSVG and
    WritePDF give it on a sheet of its own (px.Sheet for several views).

    Args:
        sketch: the constraint sketch (px.Sketch) the drawing shows.
        unit: the length one unit of the sketch stands for (px.m, the default, as planex's geometry; px.mm for a
            sketch drawn in millimeter numbers); the values are written in millimeters.
        names: True writes a named dimension as "w = 60" (the plot's way); default the value alone.
        view_name: the view's name written above it, as ViewName's.

    Raises:
        TypeError: sketch is not a px.Sketch.

    Examples:
        dw = px.Drawing(sk, unit=px.mm)
        dw.Place("w", 15).WritePDF("plate.pdf")
    """

    def __init__(self, sketch, /, unit: float | None = None, names: bool = False, view_name: str | None = None):
        import planex as px

        if not isinstance(sketch, px.Sketch):
            raise TypeError("Drawing: a px.Sketch (a QuickSketch has no dimensions; px.Sketch(qs, copy=True) converts it)")
        self.unit = px.m if unit is None else float(unit)
        if not self.unit > 0:
            raise ValueError("Drawing: unit is a positive length (px.mm, px.m, ...)")
        self.names = names
        self._sketches = [sketch]
        self._places, self._centers, self._axes, self._lines, self._measures = {}, {}, [], [], []
        self._view_name, self._callouts, self._hidden = view_name, [], []
        self.Update()

    @property
    def plane(self) -> Plane:
        """The plane the view is seen on: its first sketch's."""
        import planex as px

        return px.Plane(self._sketches[0])

    def Update(self) -> Drawing:
        """Reads the sketches anew (after their dimensions or geometry changed); the places given stay.

        Examples:
            sk.Update({"w": 80}); dw.Update()
        """
        import planex as px

        view = self.plane
        self._sketched = [_Sketched(sk, _mapper(px.Plane(sk), view)) for sk in self._sketches]
        self._dims = []
        for s in self._sketched:
            for d in dims.Dimensions(s.sketch, s.everything):
                for key in ("p", "q", "c"):
                    if getattr(d, key) is not None:
                        setattr(d, key, s.to_view(getattr(d, key)))
                if d.lines != (None, None):
                    d.lines = tuple(None if l is None else (s.to_view(l[0]), s.to_view(l[1])) for l in d.lines)
                self._dims.append(d)
        known = {d.id for d in self._dims}
        for i in self._places:
            if i not in known:
                raise ValueError(f"Drawing: '{i}' is no dimension of the drawing's sketches; they have "
                                 + ", ".join(sorted(known)))
        for key in self._hidden:
            self._hides(key, "Drawing")
        return self

    def Add(self, sketch, /) -> Drawing:
        """Adds another constraint sketch to the view, with its own dimensions.

        Args:
            sketch: the constraint sketch (px.Sketch); one on another plane is seen along the drawing's plane.

        Raises:
            TypeError: sketch is not a px.Sketch.

        Examples:
            dw.Add(holes)
        """
        import planex as px

        if not isinstance(sketch, px.Sketch):
            raise TypeError("Drawing.Add: a px.Sketch")
        self._sketches.append(sketch)
        return self.Update()

    def Place(self, id: str, place: Any = Auto, /, *, leader: bool = False, outside: bool = False, inside: bool = False,
              across: float | None = None, supplement: bool = False, count: int | Auto = Auto, text: str | None = None,
              suffix: str | None = None) -> Drawing:
        """Puts one dimension where it goes instead of where the automatic layout would.

        Args:
            id: the dimension constraint's id.
            place: a length or distance: the offset of its dimension line from the measured points, + away from the
                part's middle, - towards it (measured points on both sides of the middle: + along the drawing's x for
                a vertical one, its y for a horizontal one); a radius or diameter: the direction of its line, an angle
                in degrees from h or "n", "ne", "e", "se", "s", "sw", "w", "nw"; an angle: the radius of its arc.
                px.Auto leaves it as laid out.
            leader: a diameter or radius on a leader out of the part, its value horizontal (a small hole).
            outside: a radius's arrow and value outside the arc.
            inside: a radius's arrow and value on the center's side of the arc.
            across: a diameter from outside, between extension lines at both ends, its dimension line at this offset
                (as a length's).
            supplement: an angle's supplement (180° less the value), the angle on the other side of a line.
            count: the n of "n×" (equal features dimensioned once); 1 writes none; px.Auto from the sketch's patterns
                and px.Equal.
            text: written in place of the value, e.g. "M5" for a thread (no diameter sign) or a letter.
            suffix: written after the value, e.g. "EQS".

        Raises:
            ValueError: id is no dimension of the drawing; a direction is no angle or compass string.

        Examples:
            dw.Place("w", 15)
            dw.Place("hole", "ne", leader=True)
        """
        if id not in {d.id for d in self._dims}:
            raise ValueError(f"Drawing.Place: '{id}' is no dimension of the drawing's sketches; they have "
                             + ", ".join(sorted(d.id for d in self._dims)))
        kind = next(d.kind for d in self._dims if d.id == id)
        count = None if is_auto(count, "Drawing.Place", "count") else count
        p = dims.Place(leader=leader, outside=outside, inside=inside, across=across, supplement=supplement, count=count,
                       text=text, suffix=suffix)
        if not is_auto(place, "Drawing.Place", "place"):
            if kind == "linear":
                p.offset = float(place)
            elif kind == "angle":
                p.radius = float(place)
            else:
                dims.Direction(place)
                p.direction = place
        self._places[id] = p
        return self

    def Hide(self, *ids: str) -> Drawing:
        """Leaves dimensions out of the drawing (one another view shows, or one of equal features); the sketch keeps them.

        Args:
            ids: the dimension constraints' ids, or patterns as the sketch's selections take them (* one part of an
                id, ** any number: "Fillet.*.r").

        Raises:
            ValueError: an id without a pattern is no dimension of the drawing.

        Examples:
            dw.Hide("length")
            dw.Hide("Fillet.2.r", "Fillet.3.r", "Fillet.4.r").Place("Fillet.1.r", count=4)
        """
        for key in ids:
            self._hides(key, "Drawing.Hide")
        self._hidden.extend(ids)
        return self

    def _hides(self, key, call):
        """the ids of the dimensions key leaves out: an id and what lies under it, or a pattern's; a plain id that
        matches none raises"""
        from ._select import _glob, _is_glob

        known = sorted(d.id for d in self._dims)
        if _is_glob(key):
            return {i for i in known if _glob(key).fullmatch(i)}
        found = {i for i in known if i == key or i.startswith(key + ".")}
        if not found:
            raise ValueError(f"{call}: '{key}' is no dimension of the drawing's sketches; they have " + ", ".join(known))
        return found

    def _shown(self):
        """the dimensions drawn: all but the hidden ones"""
        hidden = {i for key in self._hidden for i in self._hides(key, "Drawing")}
        return [d for d in self._dims if d.id not in hidden]

    def Center(self, id: str, /, *, arms: str | None = None, cross: bool = False, pitch: bool = False,
               off: bool = False) -> Drawing:
        """Changes the center lines of one circle or arc.

        Args:
            id: the circle's or arc's id.
            arms: "h" or "v" for one arm of the cross only.
            cross: a short cross at the center instead of lines across the circle.
            pitch: a construction circle drawn as a center line (a bolt circle).
            off: no center lines.

        Examples:
            dw.Center("bolt circle", pitch=True)
        """
        self._centers[id] = {k: v for k, v in dict(arms=arms, cross=cross, pitch=pitch, off=off).items() if v}
        return self

    def Axis(self, a, b=None, /, through=None) -> Drawing:
        """Adds a center line the sketch does not have: an axis of symmetry or of rotation, or a curved one.

        Args:
            a: where it starts, in the drawing's coordinates; or a list of points for a curved center line.
            b: where it ends.
            through: where it crosses another center line with its dash; default its middle.

        Examples:
            dw.Axis((-5, 0), (65, 0))
        """
        self._axes.append(tuple(np.asarray(a, float)) if b is None else
                          (a, b) if through is None else (a, b, through))
        return self

    def Line(self, points, /, hidden: bool = False) -> Drawing:
        """Adds an edge the sketches do not have.

        Args:
            points: the edge as a polyline in the drawing's coordinates.
            hidden: True draws it narrow and dashed (a hidden edge); default wide.

        Examples:
            dw.Line([(0, 0), (0, 10)], hidden=True)
        """
        self._lines.append((np.asarray(points, float), hidden))
        return self

    def ViewName(self, name: str | None, /) -> Drawing:
        """Names the view: the name written 5 mm high, centered above it.

        Args:
            name: the name: the letter of the callout the view is seen from ("A"), a section's ("A-A"), or a text
                ("Flat pattern"); None writes none.

        Examples:
            dw.ViewName("A")
        """
        self._view_name = name
        return self

    def Callout(self, tip, direction, letter: str, /) -> Drawing:
        """Adds a viewing arrow: the direction another view, named letter, is seen from, or one end of a section.

        Drawn after DIN: a wide shaft, a filled head at tip, the letter 5 mm high and upright beyond the tail; a
        section's two ends are two calls with the same letter.

        Args:
            tip: where the arrow points to, in the drawing's coordinates.
            direction: the direction it points in, in the drawing's coordinates.
            letter: the letter of the view it marks.

        Raises:
            ValueError: direction has no length.

        Examples:
            dw.Callout((30, 50), (0, -1), "A")
        """
        d = np.asarray(direction, float)
        if not np.linalg.norm(d) > 0:
            raise ValueError("Drawing.Callout: direction has no length")
        self._callouts.append((np.asarray(tip, float), d / np.linalg.norm(d), str(letter)))
        return self

    def Measure(self, p, q, /, value=None, offset: float = 10.0) -> Drawing:
        """Adds a length no sketch holds, from p to q.

        Args:
            p: one end, in the drawing's coordinates.
            q: the other end.
            value: what it writes: a number or a text ("⌀40"); default the distance in millimeters.
            offset: its dimension line's offset from the points, as Place's.

        Examples:
            dw.Measure((0, 0), (0, 25), value="⌀40", offset=12)
        """
        p, q = np.asarray(p, float), np.asarray(q, float)
        self._measures.append((p, q, value, offset))
        return self

    # drawing on a sheet

    def _outline(self):
        out = [p for s in self._sketched for p in s.outline]
        out += [line for line, hidden in self._lines if not hidden]
        return out

    def _strokes(self, mm, lettering):
        """the dimensions' strokes, the center lines, the callouts and the view's name above everything, in the drawing's
        coordinates, mm sketch units per paper mm"""
        outline = self._outline()
        per_unit = 1e3 * self.unit
        layout = dims.Layout(outline, mm, lambda t: lettering.width(t), self._places, self.names)
        callouts = dims.Strokes("callouts", height=NAME)
        for tip, d, letter in self._callouts:
            n = np.array([-d[1], d[0]])
            tail, base = tip - d * mm * 12, tip - d * mm * 5
            callouts.lines.append((tail, base, "wide"))
            callouts.fills.append(np.array([tip, base + n * mm * 1.5, base - n * mm * 1.5]))
            callouts.texts.append((tail - d * mm * 4 - np.array([0.0, mm * NAME / 2]), 0.0, letter))
        # the callouts are where the caller put them: the leaders and radii laid out keep clear of them
        layout.drawn.append(callouts)
        out = []
        shown = self._shown()
        everything = {i: g for s in self._sketched for i, g in s.everything.items()}
        carried = {r for d in shown for r in d.refs if everything.get(r, {}).get("construction")}
        # the centre lines first: the radii, diameters and leaders keep clear of them
        centres = dims.CenterLines(everything, shown, mm, outline, self._centers, self._axes, carried)
        layout.drawn.append(centres)
        # the rows fill from the part outward: shorter dimensions first
        for d in sorted(shown, key=lambda d: (d.kind != "linear", d.span)):
            if d.kind in ("linear", "radius", "diameter"):
                d = _in_mm(d, per_unit)
            out.append(layout.Draw(d))
        for p, q, value, offset in self._measures:
            d = dims.Dimension("Measure", "linear", float(np.linalg.norm(q - p)) * per_unit if value is None else 0.0)
            d.p, d.q, d.u = p, q, dims._unit(q - p)
            layout.places = {**layout.places, "Measure": dims.Place(offset=offset, text=value if isinstance(value, str) else
                                                                   (None if value is None else dims.Fmt(value)))}
            out.append(layout.Draw(d))
        out.append(centres)
        corners = self._corner_lines(shown, mm)
        if corners.lines:
            out.append(corners)
        if self._callouts:
            out.append(callouts)
        if self._view_name:
            pts = _points(outline, out, mm, lettering)
            middle = (min(p[0] for p in np.concatenate(outline)) + max(p[0] for p in np.concatenate(outline))) / 2 \
                if outline else pts[:, 0].mean()
            s = dims.Strokes("view name", height=NAME)
            s.texts.append((np.array([middle, pts[:, 1].max() + mm * 4]), 0.0, self._view_name))
            out.append(s)
        return out

    def _corner_lines(self, shown, mm):
        """thin lines to the corners a length is measured to that no drawn edge reaches (a fillet's or a chamfer's
        sharp corner): the cut-off pieces (Line.1.trimmed.1) ending there, drawn on a little past the corner"""
        out = dims.Strokes("theoretical corners")
        ends = [s.to_view(g[k]) for s in self._sketched for g in s.everything.values()
                if g["kind"] in ("Line", "Arc") and not g["construction"] for k in ("start", "end")]
        pieces = [(s.to_view(g["start"]), s.to_view(g["end"])) for s in self._sketched for g in s.everything.values()
                  if g["kind"] == "Line" and g["construction"] and ".trimmed." in g["id"]]
        size = max((float(np.linalg.norm(np.ptp(np.concatenate(self._outline()), axis=0))) if self._outline() else 0.0), 1.0)
        # the same point from one solve: equal to the rounding of the part's size
        near = 1e-9 * size
        drawn = set()
        for d in shown:
            if d.kind != "linear":
                continue
            for corner in (d.p, d.q):
                if any(np.linalg.norm(corner - e) <= near for e in ends):
                    continue
                for k, (a, b) in enumerate(pieces):
                    for far, at in ((a, b), (b, a)):
                        if k not in drawn and np.linalg.norm(at - corner) <= near and np.linalg.norm(at - far) > 0:
                            drawn.add(k)
                            out.lines.append((far, at + dims._unit(at - far) * mm * dims.OVER, "thin"))
        return out

    def _extent(self):
        pts = np.concatenate([np.atleast_2d(p) for p in self._outline()]) if self._outline() else np.zeros((1, 2))
        return pts.min(axis=0), pts.max(axis=0)

    def ToSVG(self) -> str:
        """The drawing on a sheet of its own as SVG text.

        Examples:
            svg = dw.ToSVG()
        """
        return Sheet(self).ToSVG()

    def WriteSVG(self, path, /) -> None:
        """Writes the drawing on a sheet of its own as an SVG file, at true scale.

        Args:
            path: the file to write.

        Examples:
            dw.WriteSVG("plate.svg")
        """
        Sheet(self).WriteSVG(path)

    def WritePDF(self, path, /) -> None:
        """Writes the drawing on a sheet of its own as a PDF file, at true scale (it prints 1:1 at 100 %).

        Args:
            path: the file to write.

        Examples:
            dw.WritePDF("plate.pdf")
        """
        Sheet(self).WritePDF(path)

    def WriteDXF(self, path, /) -> None:
        """Writes the drawing on a sheet of its own as a DXF file with real dimension entities, as Sheet.WriteDXF.

        Args:
            path: the file to write.

        Raises:
            ImportError: ezdxf is not installed (pip install ezdxf).

        Examples:
            dw.WriteDXF("plate.dxf")
        """
        Sheet(self).WriteDXF(path)

    def _repr_svg_(self):
        return self.ToSVG()


def _points(outline, strokes, mm, lettering):
    """every point a view's drawing reaches: its outline, its strokes and the corners of its texts"""
    pts = [np.atleast_2d(p) for p in outline]
    for s in strokes:
        pts += [np.array([a, b]) for a, b, _ in s.lines] + [np.atleast_2d(a) for a in s.arcs]
        pts += [np.atleast_2d(f) for f in s.fills]
        for anchor, angle, text in s.texts:
            box = dims._box(anchor, angle, mm * lettering.width(text, s.height), mm * s.height)
            pts += [np.array([a, b]) for a, b in box[:4]]
    return np.concatenate(pts) if pts else np.zeros((1, 2))


def _in_mm(d, per_unit):
    """a dimension's value as the drawing writes it, in millimeters"""
    import copy

    d = copy.copy(d)
    d.value = d.value * per_unit
    return d


class Sheet:
    """Views at one scale on a DIN sheet: px.Drawing objects, placed by their planes in first-angle projection.

    A view on a standard plane takes its place itself, aligned with the front view (the drawing on px.XZ, or the
    first): the top view (px.XY) below it, the view from the left to its right, from the right (px.YZ) to its left;
    Add(drawing, at=...) places any other. The scale is the largest of DIN ISO 5455 at which the views and their
    dimensions fit the sheet's drawing area, or scale=; the sheet is written at true scale.

    Args:
        drawings: the views, px.Drawing objects.
        size: the sheet, "A4" to "A0".
        scale: the drawing's scale as a number (0.5 for 1:2); default the largest that fits.
        frame: True draws the sheet's frame (DIN EN ISO 5457: border, filing margin, centering marks, grid reference
            zones).
        font: the lettering's TrueType file; default osifont (after DIN EN ISO 3098-2), which ships with planex.
        landscape: True lays the sheet on its side, False stands it upright; default A4 upright, A3 and larger on
            their side. The filing margin stays on the left, the title block in the lower right corner.

    Raises:
        ValueError: size is unknown; scale is not positive; font is no file.

    Examples:
        px.Sheet(front, top, side).WritePDF("part.pdf")
        px.Sheet(front, size="A3", scale=0.5, frame=True)
        px.Sheet(front, landscape=True)
    """

    def __init__(self, *drawings: Drawing, size: str = "A4", scale: float | None = None, frame: bool = False,
                 font: str | os.PathLike | None = None, landscape: bool | None = None):
        key = str(size).upper()
        if key not in SIZES:
            raise ValueError(f"Sheet: size '{size}' is unknown; {', '.join(SIZES)}")
        if scale is not None and not scale > 0:
            raise ValueError("Sheet: scale is a positive number, 0.5 for 1:2")
        if font is not None and not os.path.isfile(font):
            raise ValueError(f"Sheet: font '{font}' is no file; a TrueType font file (.ttf)")
        self.font = None if font is None else os.fspath(font)
        w, h = SIZES[key]
        across = key != "A4" if landscape is None else bool(landscape)
        self.size, self.paper = key, ((h, w) if across else (w, h))
        self.scale, self.frame = scale, frame
        self._views, self._tables, self._notes, self._title = [], [], ("", ""), None
        for d in drawings:
            self.Add(d)

    def Add(self, drawing: Drawing, /, at=None) -> Sheet:
        """Adds a view.

        Args:
            drawing: the view, a px.Drawing.
            at: where its origin lies, in the drawing's units from the first view's origin; default a view on a
                standard plane places itself in first-angle projection.

        Raises:
            TypeError: drawing is not a px.Drawing.

        Examples:
            sh.Add(detail, at=(120, 0))
        """
        if not isinstance(drawing, Drawing):
            raise TypeError("Sheet.Add: a px.Drawing")
        self._views.append((drawing, None if at is None else np.asarray(at, float)))
        return self

    def Table(self, rows, /, at=(0, 0), widths=(44, 20), height: float = 7) -> Sheet:
        """Adds a table of data, such as a gear's or a tabulated drawing's sizes.

        Args:
            rows: the rows, each a sequence of cells (texts or numbers).
            at: its upper left corner, in the drawing's units from the first view's origin.
            widths: its columns' widths in mm on paper.
            height: each row's height in mm on paper.

        Examples:
            sh.Table([("Module", 2), ("Teeth", 24)], at=(80, 40))
        """
        self._tables.append((list(rows), np.asarray(at, float), tuple(widths), float(height)))
        return self

    def Title(self, template: str | os.PathLike | None = None, /, **fields: Any) -> Sheet:
        """Adds a title block in the lower right corner of the drawing area, its fields filled in.

        A template is a DXF file whose attribute definitions (ATTDEF, the fields of AutoCAD and BricsCAD title blocks)
        are the block's fields: each keyword fills the field with that tag, in upper or lower case alike. Where the
        template has them and the call does not give them, planex fills SCALE (the sheet's, "1:2"), SIZE ("A4"), DATE
        (today, written 2026-10-25 after DIN EN 28601) and SHEET ("1/1"). The default template follows DIN EN ISO 7200,
        180 mm wide, with the fields OWNER (legal owner), TITLE, SUBTITLE, NUMBER (identification number), REVISION,
        DATE (date of issue), SHEET, SCALE, SIZE, TYPE (document type), STATUS (document status), DEPARTMENT
        (responsible department), REFERENCE (technical reference), AUTHOR (created by) and APPROVER (approved by).

        Args:
            template: the template's DXF file, in mm: its model space, or else its first block with attribute
                definitions, is the title block, the base point the block's lower right corner; default planex's
                template after DIN EN ISO 7200.
            **fields: the fields' values by tag, texts or numbers.

        Raises:
            ImportError: ezdxf is not installed (pip install ezdxf).
            ValueError: a keyword is no field of the template (the error names its fields); the template is no DXF
                file or has no attribute definitions.

        Examples:
            sh.Title(title="Bracket", number="B-100", author="S. Miller")
            sh.Title("company.dxf", title="Bracket")
        """
        block = _TitleBlock(TEMPLATE if template is None else template)
        unknown = [k for k in fields if k.upper() not in block.tags]
        if unknown:
            raise ValueError(f"Sheet.Title: {', '.join(unknown)} is no field of the template; its fields are "
                             + ", ".join(t.lower() for t in block.tags))
        self._title = (block, {k.upper(): v if isinstance(v, str) else dims.Fmt(v) if isinstance(v, (int, float))
                               else str(v) for k, v in fields.items()})
        return self

    def Notes(self, left: str = "", right: str = "") -> Sheet:
        """Writes texts in the sheet's lower corners, 2.5 mm high.

        Args:
            left: the text in the lower left corner.
            right: the text in the lower right corner.

        Examples:
            sh.Notes("All edges deburred", "Exercise 01")
        """
        self._notes = (left, right)
        return self

    def _layout(self, ratio, lettering):
        """the views at the scale ratio: their strokes, their places (origins in the front view's coordinates) and the
        bounds of everything, in the drawing's units"""
        views = [d for d, _ in self._views]
        paper = 1e3 * views[0].unit * ratio
        mm = 1 / paper
        drawn = [d._strokes(mm, lettering) for d in views]
        boxes = []
        for strokes, d in zip(drawn, views):
            pts = _points(d._outline(), strokes, mm, lettering)
            boxes.append((pts.min(axis=0), pts.max(axis=0)))
        places = self._spread(views, boxes, mm)
        lo = np.min([b[0] + p for b, p in zip(boxes, places)], axis=0)
        hi = np.max([b[1] + p for b, p in zip(boxes, places)], axis=0)
        return drawn, places, lo, hi, paper

    def _arrange(self):
        """the scale and where everything goes: the lettering, the scale ratio, each view's strokes and origin (in the
        front view's coordinates), the paper mm per drawing unit and the shift that centers the views in the drawing
        area, on the sheet in mm"""
        if not self._views:
            raise ValueError("Sheet: no views; px.Sheet(drawing) or Add")
        lettering = _Lettering(self.font)
        W, H = self.paper
        area = (FILING, self._footer()[1], W - BORDER, H - BORDER)
        for ratio in ([self.scale] if self.scale else SCALES):
            drawn, places, lo, hi, paper = self._layout(ratio, lettering)
            if (hi - lo)[0] * paper <= area[2] - area[0] and (hi - lo)[1] * paper <= area[3] - area[1]:
                break
        shift = np.array([(area[0] + area[2]) / 2, (area[1] + area[3]) / 2]) - (lo + hi) / 2 * paper
        return SimpleNamespace(lettering=lettering, ratio=ratio, drawn=drawn, places=places, paper=paper, shift=shift)

    def _footer(self):
        """the notes' baseline and the drawing area's bottom above the title block, the notes and the first-angle
        symbol, in mm"""
        base = BORDER + (self._title[0].height if self._title else 0.0)
        notes_at, bottom = base + 2, base + (4.0 if self._title else 0.0)
        if any(self._notes):
            bottom = notes_at + 10
        if len(self._views) > 1:
            bottom = max(bottom, notes_at + (6.5 if self._notes[1] else 0.0) + 2 * HEIGHT + 5)
        return notes_at, bottom

    def _fields(self, a):
        """the title block's fields: the call's, and SCALE, SIZE, DATE and SHEET where the template has them"""
        block, fields = self._title
        ratio = a.ratio
        auto = {"SCALE": f"{dims.Fmt(ratio)}:1" if ratio >= 1 else f"1:{dims.Fmt(1 / ratio)}", "SIZE": self.size,
                "DATE": datetime.date.today().isoformat(), "SHEET": "1/1"}
        return {**{k: v for k, v in auto.items() if k in block.tags}, **fields}

    def _sheet(self, ink, a):
        """what the sheet itself has: its tables, frame, title block, notes and the first-angle symbol"""
        W, H = self.paper
        for rows, origin, widths, height in self._tables:
            ink.table(rows, origin * a.paper + a.shift, widths, height)
        if self.frame:
            ink.frame(W, H, bottom=not self._title)
        if self._title:
            ink.title(self._title[0], (W - BORDER, BORDER), self._fields(a))
        left, right = self._notes
        notes_at = self._footer()[0]
        ink.text(FILING + 2, notes_at, left, 2.5, ha="left")
        ink.text(W - BORDER - 2, notes_at, right, 2.5, ha="right")
        if len(self._views) > 1:
            ink.first_angle(W - BORDER - 2, notes_at + (6.5 if right else 0.0))

    def _figure(self):
        matplotlib = _need("matplotlib", "Sheet and Drawing")
        from matplotlib.figure import Figure

        a = self._arrange()
        matplotlib.rcParams["svg.hashsalt"] = "planex2d"
        W, H = self.paper
        fig = Figure(figsize=(W / 25.4, H / 25.4))
        ax = fig.add_axes((0, 0, 1, 1))
        ax.set_xlim(0, W)
        ax.set_ylim(0, H)
        ax.set_aspect("equal")
        ax.axis("off")
        ink = _Ink(ax, a.lettering)
        for (d, _), strokes, o in zip(self._views, a.drawn, a.places):
            def T(pts, o=o):
                return np.atleast_2d(np.asarray(pts, float)) * a.paper + o * a.paper + a.shift

            for line, hidden in d._lines:
                ink.polyline(T(line), NARROW if hidden else WIDE, HIDDEN if hidden else "-")
            for pts in d._outline():
                ink.polyline(T(pts), WIDE)
            for s in strokes:
                ink.strokes(s, T)
        self._sheet(ink, a)
        fig.planex_scale = a.ratio
        return fig

    def WriteDXF(self, path, /) -> None:
        """Writes the sheet as a DXF file with real dimension entities, editable in a CAD program.

        The views lie in model space at true size in millimeters; the dimensions are DIMENSION entities in a dimension
        style after DIN (decimal comma, closed arrows, osifont lettering) scaled to the sheet's scale, so plotted at
        that scale they come out as on the PDF; center and hidden lines in their DIN EN ISO 128-2 line types; the
        frame, tables and notes at the size the scale gives them.

        Args:
            path: the file to write.

        Raises:
            ImportError: ezdxf is not installed (pip install ezdxf).

        Examples:
            sh.WriteDXF("part.dxf")
        """
        ezdxf = _need("ezdxf", "WriteDXF")
        from ezdxf.enums import TextEntityAlignment

        from ._dxf import AddEdge

        a = self._arrange()
        doc = ezdxf.new("R2013")
        doc.units = 4
        doc.header["$MEASUREMENT"] = 1
        doc.header["$LTSCALE"] = 1 / a.ratio
        # DIN EN ISO 128-2 line types 04 (long-dashed dotted) and 02 (dashed) for the narrow line, in mm on paper
        doc.linetypes.add("DIN_04", pattern=[7.625, 6.0, -0.75, 0.125, -0.75], description="long-dashed dotted (04)")
        doc.linetypes.add("DIN_02", pattern=[3.75, 3.0, -0.75], description="dashed (02)")
        for name, weight, linetype in (("Visible", 50, "Continuous"), ("Thin", 25, "Continuous"), ("Hidden", 25, "DIN_02"),
                                       ("Center", 25, "DIN_04"), ("Dimensions", 25, "Continuous"),
                                       ("Text", 25, "Continuous"), ("Frame", 70, "Continuous")):
            doc.layers.add(name, lineweight=weight, linetype=linetype)
        doc.styles.add("Lettering", font=os.path.basename(a.lettering.path))
        doc.dimstyles.add("DIN", dxfattribs=dict(
            dimtxsty="Lettering", dimtxt=HEIGHT, dimasz=ARROW, dimexe=dims.OVER, dimexo=0.0, dimgap=1.0, dimtad=1,
            dimtih=0, dimtoh=0, dimdec=3, dimzin=8, dimdsep=ord(","), dimadec=3, dimazin=2, dimlfac=1.0,
            dimscale=1 / a.ratio))
        msp = doc.modelspace()
        for (d, _), strokes, o in zip(self._views, a.drawn, a.places):
            u = 1e3 * d.unit

            def at(x, y, o=o, u=u):
                return ((x + o[0]) * u, (y + o[1]) * u)

            def lines(pts, layer):
                msp.add_lwpolyline([at(*p) for p in np.atleast_2d(pts)], dxfattribs={"layer": layer})

            for s in d._sketched:
                if s.flat:
                    for w in s.sketch.Wires():
                        for v in w.edges:
                            AddEdge(msp, v.edge, at, u, {"layer": "Visible"})
                else:
                    for pts in s.outline:
                        lines(pts, "Visible")
            for line, hidden in d._lines:
                lines(line, "Hidden" if hidden else "Visible")
            for s in strokes:
                if s.entity is not None:
                    _dimension(msp, s.entity, at, u)
                    continue
                for p, q, style in s.lines:
                    lines([p, q], {"center": "Center", "wide": "Visible"}.get(style, "Thin"))
                for arc in s.arcs:
                    lines(arc, "Center")
                for f in s.fills:
                    msp.add_solid([at(*p) for p in f], dxfattribs={"layer": "Visible"})
                for anchor, angle, text in s.texts:
                    msp.add_text(text, height=s.height / a.ratio, rotation=angle,
                                 dxfattribs={"layer": "Text", "style": "Lettering"}).set_placement(
                                     at(*anchor), align=TextEntityAlignment.CENTER)
        self._sheet(_DxfInk(msp, a.lettering, a.ratio, a.shift), a)
        doc.saveas(os.fspath(path))

    def _spread(self, views, boxes, mm):
        """each view's origin in the front view's coordinates: a view's own at=, else the views on standard planes
        beside the front view in first-angle projection (sharing the coordinates they have in common), GAP mm apart"""
        kinds = [_side(d.plane) for d in views]
        front = next((k for k, kind in enumerate(kinds) if kind == "front"), 0)
        out = [np.zeros(2) if at is None else np.asarray(at, float).copy() for _, at in self._views]
        gap = mm * GAP
        fb = (boxes[front][0] + out[front], boxes[front][1] + out[front])
        beyond = fb[1][0]
        for k, kind in enumerate(kinds):
            if k == front or self._views[k][1] is not None:
                continue
            b = boxes[k]
            if kind == "top":
                out[k][1] = fb[0][1] - gap - b[1][1]
            elif kind == "bottom":
                out[k][1] = fb[1][1] + gap - b[0][1]
            elif kind == "left":
                out[k][0] = fb[1][0] + gap - b[0][0]
                beyond = max(beyond, out[k][0] + b[1][0])
            elif kind == "right":
                out[k][0] = fb[0][0] - gap - b[1][0]
        for k, kind in enumerate(kinds):
            if k != front and self._views[k][1] is None and kind in ("rear", "other"):
                out[k][0] = beyond + gap - boxes[k][0][0]
                beyond = out[k][0] + boxes[k][1][0]
        return out

    def ToSVG(self) -> str:
        """The sheet as SVG text at true scale.

        Examples:
            svg = sh.ToSVG()
        """
        fig = self._figure()
        buffer = io.StringIO()
        fig.savefig(buffer, format="svg", metadata={"Date": None})
        return buffer.getvalue()

    def WriteSVG(self, path, /) -> None:
        """Writes the sheet as an SVG file at true scale.

        Args:
            path: the file to write.

        Examples:
            sh.WriteSVG("part.svg")
        """
        with open(path, "w", encoding="utf-8") as f:
            f.write(self.ToSVG())

    def WritePDF(self, path, /) -> None:
        """Writes the sheet as a PDF file at true scale: one page the size of the sheet, the lettering embedded.

        It prints 1:1 at 100 %.

        Args:
            path: the file to write.

        Examples:
            sh.WritePDF("part.pdf")
        """
        import matplotlib

        fig = self._figure()
        with matplotlib.rc_context({"pdf.fonttype": 42}):
            fig.savefig(path, format="pdf", metadata={"CreationDate": None})

    def _repr_svg_(self):
        return self.ToSVG()


def _fields(lo, hi, middle):
    """the borders of the grid reference fields between lo and hi: every FIELD from middle outward, the ends' rest a
    field of its own"""
    out = [middle]
    while out[0] - FIELD > lo + 1e-9:
        out.insert(0, out[0] - FIELD)
    while out[-1] + FIELD < hi - 1e-9:
        out.append(out[-1] + FIELD)
    return [lo] + out + [hi]


def _side(plane):
    """which standard view a plane gives, by the direction it is seen from (first-angle projection)"""
    n = np.asarray(tuple(plane.normal), float)
    for name, d in (("front", (0, -1, 0)), ("top", (0, 0, 1)), ("right", (1, 0, 0)), ("left", (-1, 0, 0)),
                    ("bottom", (0, 0, -1)), ("rear", (0, 1, 0))):
        if np.allclose(n, d, atol=1e-9):
            return name
    return "other"


class _Ink:
    """draws on the sheet in mm: lines wide or narrow, center and hidden lines, arrows, the lettering"""

    def __init__(self, ax, lettering):
        self.ax, self.lettering = ax, lettering

    def polyline(self, pts, width, style="-"):
        pts = np.atleast_2d(pts)
        self.ax.plot(pts[:, 0], pts[:, 1], color=INK, lw=_pt(width), linestyle=style, solid_capstyle="butt",
                     dash_capstyle="butt", solid_joinstyle="round")

    def fill(self, pts):
        self.ax.fill(*zip(*pts), color=INK, lw=0)

    def text(self, x, y, text, height=HEIGHT, angle=0.0, ha="center", va="baseline"):
        self.ax.text(x, y, text, fontproperties=self.lettering.sized(height), color=INK, rotation=angle,
                     rotation_mode="anchor", ha=ha, va=va)

    def title(self, block, corner, fields):
        block.Draw(self, corner, fields)

    def strokes(self, s, T):
        for a, b, style in s.lines:
            self.polyline(T([a, b]), WIDE if style == "wide" else NARROW, CENTER if style == "center" else "-")
        for arc in s.arcs:
            self.polyline(T(arc), NARROW, CENTER if s.id == "center lines" else "-")
        for f in s.fills:
            self.fill(T(f))
        for tip, d in s.arrows:
            self.fill(dims.Head(T(tip)[0], d, ARROW))
        for anchor, angle, text in s.texts:
            p = T(anchor)[0]
            self.text(p[0], p[1], text, s.height, angle)

    def table(self, rows, corner, widths, height):
        x0, y1 = corner
        x1, y0 = x0 + sum(widths), y1 - height * len(rows)
        for k in range(len(rows) + 1):
            self.polyline([(x0, y1 - height * k), (x1, y1 - height * k)], NARROW)
        lefts = list(x0 + np.cumsum((0,) + tuple(widths[:-1])))
        for x in lefts + [x1]:
            self.polyline([(x, y0), (x, y1)], NARROW)
        for k, row in enumerate(rows):
            y = y1 - height * (k + 0.5)
            for x, text in zip(lefts, row):
                self.text(x + 1.5, y, dims.Fmt(text), ha="left", va="center")

    def frame(self, W, H, bottom=True):
        """DIN EN ISO 5457: the frame round the drawing area (0.7 mm; 20 mm filing margin on the left, 10 mm round),
        the grid reference border 5 mm outside it (0.35 mm), centering marks at the middle of each side from the grid
        reference border to 10 mm into the drawing area (0.7 mm; the bottom one not into a title block), and in the
        strip between frame and grid reference border the fields, 50 mm long from the centering marks outward (0.35 mm),
        numbered 1, 2, ... from the left along the top and bottom and lettered A, B, ... from the top along both sides
        (on every size; ISO 5457 has A4's on the top and right only)"""
        x0, y0, x1, y1 = FILING, BORDER, W - BORDER, H - BORDER
        g0, h0, g1, h1 = x0 - GRID, y0 - GRID, x1 + GRID, y1 + GRID
        self.polyline([(x0, y0), (x1, y0), (x1, y1), (x0, y1), (x0, y0)], 0.7)
        self.polyline([(g0, h0), (g1, h0), (g1, h1), (g0, h1), (g0, h0)], 0.35)
        for a, b in (((W / 2, h0), (W / 2, y0 + (10 if bottom else 0))), ((W / 2, h1), (W / 2, y1 - 10)),
                     ((g0, H / 2), (x0 + 10, H / 2)), ((g1, H / 2), (x1 - 10, H / 2))):
            self.polyline([a, b], 0.7)
        xs, ys = _fields(x0, x1, W / 2), _fields(y0, y1, H / 2)[::-1]
        for edge, outer in ((y1, h1), (y0, h0)):
            for x in xs[1:-1]:
                self.polyline([(x, edge), (x, outer)], 0.35)
            for k, (a, b) in enumerate(zip(xs, xs[1:])):
                self.text((a + b) / 2, (edge + outer) / 2, str(k + 1), va="center")
        for edge, outer in ((x1, g1), (x0, g0)):
            for y in ys[1:-1]:
                self.polyline([(edge, y), (outer, y)], 0.35)
            for k, (a, b) in enumerate(zip(ys, ys[1:])):
                self.text((edge + outer) / 2, (a + b) / 2, LETTERS[k % len(LETTERS)], va="center")

    def first_angle(self, right, bottom):
        """the symbol of first-angle projection, its lower right corner at (right, bottom)"""
        over, big = 1.5, 2 * HEIGHT
        small, length, gap = 0.54 * big, 1.04 * big, 0.5 * big
        right -= over
        x0, y = right - big - gap - length, bottom + over + big / 2
        self.polyline([(x0, y - small / 2), (x0 + length, y - big / 2), (x0 + length, y + big / 2), (x0, y + small / 2),
                       (x0, y - small / 2)], NARROW)
        c = np.array([right - big / 2, y])
        t = np.linspace(0, 2 * math.pi, 97)
        for r in (big / 2, small / 2):
            self.polyline(np.c_[c[0] + r * np.cos(t), c[1] + r * np.sin(t)], NARROW)
        self.polyline([(x0 - over, y), (right + over, y)], NARROW, SYMBOL_CENTER)
        self.polyline([(c[0], c[1] - big / 2 - over), (c[0], c[1] + big / 2 + over)], NARROW, SYMBOL_CENTER)


class _DxfInk(_Ink):
    """draws the sheet's own lines and texts into a DXF model space, whose drawing lies there at true size in mm: a
    point on the sheet at (p - shift) / ratio"""

    def __init__(self, msp, lettering, ratio, shift):
        self.msp, self.lettering, self.ratio, self.shift = msp, lettering, ratio, shift

    def model(self, pts):
        return (np.atleast_2d(np.asarray(pts, float)) - self.shift) / self.ratio

    def polyline(self, pts, width, style="-"):
        layer = ("Center" if style in (CENTER, SYMBOL_CENTER) else "Hidden" if style == HIDDEN else
                 "Frame" if width > WIDE else "Visible" if width == WIDE else "Thin")
        self.msp.add_lwpolyline([tuple(p) for p in self.model(pts)], dxfattribs={"layer": layer})

    def fill(self, pts):
        self.msp.add_solid([tuple(p) for p in self.model(pts)], dxfattribs={"layer": "Thin"})

    def title(self, block, corner, fields):
        """the template as a block of the file, inserted at the scale's size with its fields as attributes"""
        from ezdxf.addons import Importer

        doc = self.msp.doc
        importer = Importer(block.doc, doc)
        if block.name is None:
            name = next(f"TITLEBLOCK{k or ''}" for k in range(len(doc.blocks) + 1) if f"TITLEBLOCK{k or ''}" not in doc.blocks)
            importer.import_entities(block.layout, target_layout=doc.blocks.new(name, base_point=tuple(block.base)))
        else:
            name = importer.import_block(block.name)
        importer.finalize()
        ref = self.msp.add_blockref(name, tuple(self.model(corner)[0]),
                                    dxfattribs={"xscale": 1 / self.ratio, "yscale": 1 / self.ratio, "layer": "Frame"})
        ref.add_auto_attribs({block.spelling[k]: v for k, v in fields.items()})

    def text(self, x, y, text, height=HEIGHT, angle=0.0, ha="center", va="baseline"):
        from ezdxf.enums import TextEntityAlignment as A

        if not text:
            return
        align = {("left", "baseline"): A.LEFT, ("center", "baseline"): A.CENTER, ("right", "baseline"): A.RIGHT,
                 ("left", "center"): A.MIDDLE_LEFT, ("center", "center"): A.MIDDLE_CENTER,
                 ("right", "center"): A.MIDDLE_RIGHT}[(ha, va)]
        self.msp.add_text(text.replace("⌀", "%%c"), height=height / self.ratio, rotation=angle,
                          dxfattribs={"layer": "Text", "style": "Lettering"}).set_placement(tuple(self.model((x, y))[0]),
                                                                                            align=align)


class _TitleBlock:
    """a title-block template: a DXF file whose model space, or else its first block with attribute definitions
    (ATTDEF), holds the block's lines, texts and fields, its base point the block's lower right corner, in mm"""

    def __init__(self, path):
        ezdxf = _need("ezdxf", "Sheet.Title")
        from ezdxf import bbox

        try:
            self.doc = ezdxf.readfile(os.fspath(path))
        except (OSError, ezdxf.DXFError) as e:
            raise ValueError(f"Sheet.Title: {path} is no DXF file planex can read ({e})") from e
        msp = self.doc.modelspace()
        if len(msp.query("ATTDEF")):
            self.layout, self.name, base = msp, None, self.doc.header.get("$INSBASE", (0, 0, 0))
        else:
            self.layout = next((b for b in self.doc.blocks if not b.is_any_layout and not b.block.is_anonymous
                                and len(b.query("ATTDEF"))), None)
            if self.layout is None:
                raise ValueError(f"Sheet.Title: {path} has no attribute definitions (ATTDEF), the fields of a title "
                                 "block")
            self.name, base = self.layout.name, self.layout.block.dxf.base_point
        self.base = np.array(tuple(base)[:2], float)
        self.spelling = {a.dxf.tag.upper(): a.dxf.tag for a in self.layout.query("ATTDEF")}
        self.tags = list(self.spelling)
        box = bbox.extents(self.layout)
        self.height = float(box.extmax.y - self.base[1]) if box.has_data else 0.0

    def _width(self, e):
        """an entity's line width in mm: its own, else its layer's, else narrow"""
        weight = e.dxf.get("lineweight", -1)
        if weight < 0 and e.dxf.layer in self.doc.layers:
            weight = self.doc.layers.get(e.dxf.layer).dxf.get("lineweight", -1)
        return weight / 100 if weight > 0 else NARROW

    def Draw(self, ink, corner, fields):
        """the block on the sheet in mm, its base point at corner, its fields filled in"""
        def at(p):
            return np.asarray(corner, float) + np.array(tuple(p)[:2], float) - self.base

        for e in self.layout:
            kind = e.dxftype()
            if kind == "LINE":
                ink.polyline([at(e.dxf.start), at(e.dxf.end)], self._width(e))
            elif kind == "LWPOLYLINE":
                pts = [at(p) for p in e.get_points("xy")]
                ink.polyline(pts + pts[:1] if e.closed else pts, self._width(e))
            elif kind in ("CIRCLE", "ARC"):
                t0, t1 = (0.0, 360.0) if kind == "CIRCLE" else (e.dxf.start_angle, e.dxf.end_angle)
                t = np.radians(np.linspace(t0, t1 if t1 > t0 else t1 + 360, 97))
                c = at(e.dxf.center)
                ink.polyline(np.c_[c[0] + e.dxf.radius * np.cos(t), c[1] + e.dxf.radius * np.sin(t)], self._width(e))
            elif kind == "TEXT" or (kind == "ATTDEF" and not e.dxf.flags & 1):
                text = fields.get(e.dxf.tag.upper(), e.dxf.text) if kind == "ATTDEF" else e.dxf.text
                align, p, q = e.get_placement()
                name = align.name
                ha = "left" if "LEFT" in name or name in ("ALIGNED", "FIT") else "right" if "RIGHT" in name else "center"
                va = ("top" if name.startswith("TOP") else "center" if name.startswith("MIDDLE") else
                      "bottom" if name.startswith("BOTTOM") else "baseline")
                where = at(p if q is None or name in ("LEFT", "ALIGNED", "FIT") else q)
                if text:
                    ink.text(where[0], where[1], text, e.dxf.height, e.dxf.rotation, ha, va)


def _dimension(msp, e, at, u):
    """one dimension of the layout (Strokes.entity) as a DIMENSION entity: at(x, y) the model point of a drawing's, u
    the model length of a drawing unit"""
    def P(p):
        return at(*p)

    style = dict(dimstyle="DIN", text=e["text"].replace("⌀", "%%c"), dxfattribs={"layer": "Dimensions"})
    if e["kind"] == "linear":
        dim = msp.add_linear_dim(base=P(e["base"]), p1=P(e["p1"]), p2=P(e["p2"]), angle=e["angle"], **style)
    elif e["kind"] in ("radius", "diameter"):
        add = msp.add_radius_dim if e["kind"] == "radius" else msp.add_diameter_dim
        dim = add(P(e["c"]), radius=e["r"] * u, angle=e["angle"],
                  location=None if e["location"] is None else P(e["location"]), **style)
    else:
        c = e["c"]

        def leg(d, stretch):
            near, far = stretch
            return (P(c + d * near), P(c + d * far)) if far > near else (P(c), P(c + d * e["radius"]))

        # ezdxf measures from the second line counterclockwise to the first
        dim = msp.add_angular_dim_2l(base=P(e["base"]), line1=leg(e["b"], e["legs"][1]), line2=leg(e["a"], e["legs"][0]),
                                     **style)
    dim.render()
