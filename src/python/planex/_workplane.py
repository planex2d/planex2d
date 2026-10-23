# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""px.WorkPlane: netgen's occ.WorkPlane drawing on a px.QuickSketch"""
from __future__ import annotations

import math
import warnings
from typing import Any

from . import _planex

# netgen's: an end this close to the start closes the wire, a line shorter than this is not drawn
CLOSE = 1e-10
# OCC's vertex tolerance: an edge meets a wire at a vertex this close
MEET = 1e-7
# the points of an ellipse's spline: a periodic cubic through them is within 1e-9 of the ellipse
ELLIPSE_POINTS = 128
EDGE_PROPERTIES = ("name", "maxh", "minh", "hpref", "layer", "col", "partition")
# the chord tolerance of the polyline that gives a wire's orientation
ORIENTATION_TOL = 1e-4


class WorkPlane:
    """Drop-in for netgen's occ.WorkPlane: the same calls, arguments, defaults and cursor, drawn on a px.QuickSketch.

    The results are planex objects: Face() a px.Face, Wire() and Last() a px.Wire, Wires() a list of px.Wire; the
    drawing calls return the WorkPlane. Names (name=, NameVertex) are the planex edge and vertex names and reach OCC
    through ToOCCFace, ToOCCWire, ToOCCWires, ToOCCShape (the export of what Face(), Wire(), Wires() return). Face() puts the
    finished wires into one face as netgen does, the first the outer wire (counterclockwise), the others holes
    (clockwise, so Reverse() after drawing them); planex finds holes by nesting, so Face() takes a wire inside another as a
    hole and a clockwise outer wire as the outer wire, each with a PlanexWarning. Ellipse is a closed spline through
    points of the ellipse.

    Wires are grouped as in netgen: MoveTo and Move only move the cursor, a wire stays open until Close() or Finish(),
    and a later edge joins it where it meets one of its vertices. An edge that meets none, and Circle or Ellipse while
    a wire is open, raise ValueError. Wires() gives each such wire as clean px.Wire chains (a branch gives several),
    Wire() and Last() the last one when it is a single chain; ToOCCWire() and ToOCCWires() export the grouping 1:1.

    Args:
        axes: what to draw on, as px.QuickSketch's first argument: netgen's occ.Axes, a px.Plane, a planex or planar OCC
            object or a list of them on one plane (their axes); a point (h, v): the XY plane, the cursor there; default
            the XY plane.
        pos: netgen's gp_Ax2d: the start position and direction of the cursor; default (0, 0), direction (1, 0).
        copy: True copies the geometry of axes in (OCC: projected); each copied wire is a finished wire of the
            WorkPlane, so Face() and Wires() take it.

    Attributes:
        cur_loc: the cursor's position, a px.Vertex.
        cur_dir: the cursor's direction, a px.Vertex.
        start_pnt: where the wire started, the point Close() goes back to, a px.Vertex.
        sketch: the px.QuickSketch drawn on.

    Raises:
        TypeError: wrong arguments in a call (the message names the call).

    Examples:
        wp = px.WorkPlane().Rectangle(4, 2).MoveTo(2, 1).Circle(0.5).Reverse()
        face = wp.Face()
        px.WorkPlane().Line(2, name="bottom").Rotate(90).Line(1).Close().ToOCCFace()
        px.WorkPlane(solid.faces.Max(occ.Z), copy=True).MoveTo(2, 1).Circle(0.5).Face()
    """

    def __init__(self, axes: Any = None, pos: Any = None, *, copy: bool = False) -> None:
        start = None
        if isinstance(axes, tuple) and len(axes) == 2 and all(isinstance(c, (int, float)) for c in axes):
            start, axes = (float(axes[0]), float(axes[1])), None
        if copy:
            self._sketch = _planex.QuickSketch(_planex.Plane() if axes is None else axes, copy=True)
        else:
            self._sketch = _planex.QuickSketch() if axes is None else _planex.QuickSketch(axes)
        self._start = (0.0, 0.0) if start is None else start
        self._loc, self._dir = self._start, (1.0, 0.0)
        if pos is not None:
            self._loc, self._dir = _planex._Ax2d(pos)
        self._chain = False
        self._started = False
        self._edges = []
        self._ends = []
        self._wires = list(self._sketch.Wires()) if copy else []
        self._vertex = None

    @property
    def cur_loc(self) -> _planex.Vertex:
        """The cursor's position."""
        return _planex.Vertex(self._loc)

    @property
    def cur_dir(self) -> _planex.Vertex:
        """The cursor's direction, a unit vector."""
        return _planex.Vertex(self._dir)

    @property
    def start_pnt(self) -> _planex.Vertex:
        """Where the wire started, the point Close() goes back to."""
        return _planex.Vertex(self._start)

    @property
    def sketch(self) -> _planex.QuickSketch:
        """The px.QuickSketch drawn on."""
        return self._sketch

    @property
    def _axes(self):
        return self._sketch._axes

    def MoveTo(self, h: float, v: float) -> WorkPlane:
        """Moves the cursor to (h, v); the next edge starts there, and joins the open wire if it meets it.

        Args:
            h: the position along the plane's first axis.
            v: the position along the plane's second axis.

        Examples:
            wp.MoveTo(1, 2)
        """
        self._break_chain()
        self._started = False
        self._start = self._loc = (float(h), float(v))
        return self

    def Move(self, l: float) -> WorkPlane:
        """Moves the cursor l along its direction, as MoveTo.

        Args:
            l: the distance, negative backwards.

        Examples:
            wp.Move(2)
        """
        return self.MoveTo(self._loc[0] + l * self._dir[0], self._loc[1] + l * self._dir[1])

    def Direction(self, dirh: float, dirv: float) -> WorkPlane:
        """Sets the cursor's direction to (dirh, dirv).

        Args:
            dirh: the direction's component along the plane's first axis.
            dirv: the direction's component along the plane's second axis.

        Raises:
            ValueError: the direction has zero length.

        Examples:
            wp.Direction(0, 1)
        """
        norm = math.hypot(dirh, dirv)
        if not norm > 0:
            raise ValueError("Direction(%g, %g): the direction has zero length; give a nonzero (dirh, dirv)" % (dirh, dirv))
        self._dir = (dirh / norm, dirv / norm)
        return self

    def Rotate(self, ang: float) -> WorkPlane:
        """Turns the cursor's direction by ang degrees, counterclockwise.

        Args:
            ang: the angle in degrees.

        Examples:
            wp.Rotate(90)
        """
        self._dir = _rotated(self._dir, ang * math.pi / 180)
        return self

    def LineTo(self, h: float, v: float, name: str | None = None) -> WorkPlane:
        """Draws a line to (h, v); the direction stays.

        Args:
            h: the end along the plane's first axis.
            v: the end along the plane's second axis.
            name: the line's name.

        Raises:
            ValueError: the line meets no vertex of the open wire; call Finish() first.

        Examples:
            wp.LineTo(3, 0)
            wp.LineTo(3, 2, name="side")
        """
        old, new = self._loc, (float(h), float(v))
        if math.dist(new, old) < CLOSE:
            self._loc = new
            return self
        closing = math.dist(new, self._start) < CLOSE
        self._meet("LineTo", old, new)
        self._loc = new
        before = self._begin(old)
        self._sketch.Line(self._start if closing else new, **_named(name))
        return self._drawn(before, closing, old, new)

    def Line(self, *args: Any, **kwargs: Any) -> WorkPlane:
        """Draws a line l along the direction, or by (dx, dy); the direction stays.

        Args:
            args: l and name, or dx, dy and name: netgen's Line(l, name=None) and Line(dx, dy, name=None).
            kwargs: l, dx, dy, name.

        Raises:
            TypeError: too many arguments, a missing or doubled one, or another keyword.

        Examples:
            wp.Line(2)
            wp.Line(1, 0.5, name="slope")
        """
        numbers = "dx" in kwargs or "dy" in kwargs or len(args) == 3
        numbers = numbers or (len(args) == 2 and not isinstance(args[1], (str, type(None))))
        if "l" in kwargs or not numbers:
            l, name = _bound("Line", ("l", "name"), args, kwargs)
            return self.LineTo(self._loc[0] + l * self._dir[0], self._loc[1] + l * self._dir[1], name)
        dx, dy, name = _bound("Line", ("dx", "dy", "name"), args, kwargs)
        return self.LineTo(self._loc[0] + dx, self._loc[1] + dy, name)

    def ArcTo(self, h: float, v: float, t: Any, name: str | None = None, maxh: float | None = None) -> WorkPlane:
        """Draws an arc to (h, v) that leaves the cursor's position along t; the direction turns by the arc's turn.

        Args:
            h: the end along the plane's first axis.
            v: the end along the plane's second axis.
            t: the start tangent, (dh, dv), a px.Vertex or netgen's gp_Vec2d.
            name: the arc's name.
            maxh: the arc's mesh size.

        Raises:
            ValueError: the end is the cursor's position, or t runs along the chord; the arc meets no vertex of the
                open wire (call Finish() first).

        Examples:
            wp.ArcTo(2, 2, (1, 0))
        """
        (x, y), (h, v), t = self._loc, (float(h), float(v)), _xy(t)
        if x == h and y == v:
            raise ValueError("ArcTo(%g, %g): points P1 and P2 must not be congruent; give an end other than the cursor" % (h, v))
        mx, my = (x + h) / 2, (y + v) / 2
        n = (-(my - y), mx - x)
        across = t[0] * n[0] + t[1] * n[1]
        if across == 0:
            raise ValueError("ArcTo(%g, %g): the tangent runs along the chord; give a t that is not parallel to it" % (h, v))
        k = ((my - y) * n[0] + (x - mx) * n[1]) / across
        cx, cy = x - k * t[1], y + k * t[0]
        closing = math.dist((h, v), self._start) < CLOSE
        self._meet("ArcTo", (x, y), (h, v))
        before = self._begin((x, y))
        self._sketch.Arc(self._start if closing else (h, v), tangent=t, **_named(name, maxh=maxh))
        self._loc = (h, v)
        t2 = (v - cy, -(h - cx)) if _angle(t, (x - h, y - v)) >= 0 else (-(v - cy), h - cx)
        self.Rotate(-_angle(t2, t) * 180 / math.pi)
        return self._drawn(before, closing, (x, y), (h, v))

    def Arc(self, r: float, ang: float, name: str | None = None, maxh: float | None = None) -> WorkPlane:
        """Draws an arc tangent to the cursor's direction, to the left for a positive angle, else to the right.

        Args:
            r: the radius.
            ang: the angle in degrees, not a multiple of 360.
            name: the arc's name.
            maxh: the arc's mesh size.

        Raises:
            ValueError: ang is a multiple of 360.

        Examples:
            wp.Arc(1, 90)
            wp.Arc(0.5, -180, name="notch")
        """
        turn = math.fmod(ang, 360) * math.pi / 180
        if -1e-16 < turn < 1e-16:
            raise ValueError("Arc(%g, %g): angle must not be an integer multiple of 360; give another angle" % (r, ang))
        x, y = self._dir
        n = (-y, x) if turn >= 0 else (y, -x)
        cx, cy = self._loc[0] + r * n[0], self._loc[1] + r * n[1]
        n = _rotated(n, turn - math.pi)
        return self.ArcTo(cx + r * n[0], cy + r * n[1], (x, y), name, maxh)

    def Spline(self, points: list, periodic: bool = False, tol: float = 1e-8, tangents: dict | None = None,
               start_from_localpos: bool = True, name: str | None = None) -> WorkPlane:
        """Draws a spline through the points (OCC's interpolation, as netgen); the direction becomes its end tangent's.

        Args:
            points: the points (h, v) to pass through.
            periodic: True closes the spline smoothly.
            tol: the first point may not lie this close to the cursor's position; points this close count as one.
            tangents: {index: (dh, dv)}: the tangent at a point; 0 is the start.
            start_from_localpos: True starts at the cursor's position, added before the points.
            name: the spline's name.

        Raises:
            ValueError: the first point lies within tol of the cursor's position; the spline meets no vertex of the
                open wire (call Finish() first).

        Examples:
            wp.Spline([(1, 1), (2, 0)])
            wp.Spline([(1, 1), (2, 0)], tangents={0: (1, 0)})
        """
        pts = [_xy(p) for p in points]
        first = self._loc if start_from_localpos else pts[0]
        if start_from_localpos and math.dist(pts[0], first) <= tol:
            raise ValueError("Spline(): the first of the points is too close to the cursor's position (distance <= tol); "
                             "leave it out or move the cursor")
        end = pts[-1]
        closing = periodic or math.dist(end, self._start) < CLOSE
        if closing and not periodic:
            pts[-1] = self._start
        self._meet("Spline", first, end)
        before = self._begin(first)
        self._sketch.Spline(pts, periodic=periodic, tangents={k: _xy(t) for k, t in (tangents or {}).items()},
                            start_from_localpos=start_from_localpos, style="occ", tol=tol, **_named(name))
        self._loc = end
        self.Rotate(_angle(self._dir, tuple(self._sketch.pen.direction)) * 180 / math.pi)
        return self._drawn(before, closing, first, end)

    def Rectangle(self, l: float, w: float, name: str | None = None) -> WorkPlane:
        """Draws a rectangle with the cursor's position as a corner, l along the direction and w to its left.

        Args:
            l: the length along the direction.
            w: the width.
            name: the sides' name.

        Examples:
            wp.Rectangle(3, 2)
        """
        for side in (l, w, l, w):
            self.Line(side, name).Rotate(90)
        return self

    def RectangleC(self, l: float, w: float, name: str | None = None) -> WorkPlane:
        """Draws a rectangle centred on the cursor's position, l along the direction.

        Args:
            l: the length along the direction.
            w: the width.
            name: the sides' name.

        Examples:
            wp.RectangleC(3, 2)
        """
        self.Move(-l / 2).Rotate(-90).Move(w / 2).Rotate(90)
        self.Rectangle(l, w, name)
        return self.Rotate(-90).Move(-w / 2).Rotate(90).Move(l / 2)

    def Circle(self, *args: float) -> WorkPlane:
        """Draws a circle as a wire of its own: Circle(h, v, r) around (h, v), Circle(r) around the cursor's position.

        Args:
            args: h, v, r, or r.

        Raises:
            TypeError: not one or three numbers.
            ValueError: a wire is open; call Finish() first.

        Examples:
            wp.Circle(0, 0, 1)
            wp.Circle(0.5)
        """
        if len(args) not in (1, 3):
            raise TypeError("Circle() takes (r) or (h, v, r), %d arguments given" % len(args))
        h, v, r = (*self._loc, args[0]) if len(args) == 1 else args
        self._require_no_open_wire("Circle")
        self._sketch.Circle(h, v, r)
        self._wires.append(self._sketch._Wire([self._sketch._LastId()]))
        return self

    def Ellipse(self, major: float, minor: float) -> WorkPlane:
        """Draws an ellipse around the cursor's position, major along the direction, as a wire of its own.

        It is a closed spline through points of the ellipse; the cursor stays.

        Args:
            major: the semi-axis along the direction.
            minor: the semi-axis across it.

        Raises:
            ValueError: a wire is open; call Finish() first.

        Examples:
            wp.Ellipse(2, 1)
        """
        self._require_no_open_wire("Ellipse")
        loc, direction = self._loc, self._dir
        along, across = direction, (-direction[1], direction[0])
        angles = [2 * math.pi * i / ELLIPSE_POINTS for i in range(ELLIPSE_POINTS)]
        points = [(loc[0] + major * math.cos(a) * along[0] + minor * math.sin(a) * across[0],
                   loc[1] + major * math.cos(a) * along[1] + minor * math.sin(a) * across[1]) for a in angles]
        self._begin(points[0])
        self._sketch.Spline(points, periodic=True, start_from_localpos=False, style="px")
        self._wires.append(self._sketch._Wire([self._sketch._LastId()]))
        self._break_chain()
        return self

    def NameVertex(self, name: str) -> WorkPlane:
        """Names the vertex where the last line, arc or spline ended.

        Args:
            name: the vertex's name.

        Examples:
            wp.Line(2).NameVertex("corner")
        """
        if self._vertex is not None:
            self._sketch._SetProperty([self._vertex], "name", name)
        return self

    def Close(self, name: str | None = None) -> WorkPlane:
        """Draws a line back to the wire's start point and finishes the wire.

        Args:
            name: the line's name.

        Examples:
            wp.Close()
            wp.Close(name="bottom")
        """
        if math.dist(self._start, self._loc) > CLOSE:
            return self.LineTo(*self._start, name)
        return self.Finish()

    def Finish(self) -> WorkPlane:
        """Finishes the wire being drawn without closing it.

        Examples:
            wp.Finish()
        """
        if self._started:
            self._wires.append(self._sketch._Wire(self._edges))
            self._edges, self._ends, self._started = [], [], False
            self._break_chain()
        return self

    def Reverse(self) -> WorkPlane:
        """Reverses the last finished wire: its edges and their order; a hole of a face is a clockwise wire.

        Raises:
            ValueError: there is no finished wire.

        Examples:
            wp.Circle(0.5).Reverse()
        """
        if not self._wires:
            raise ValueError("Reverse(): no finished wire; draw one and Close() or Finish() it first")
        self._wires[-1] = self._reversed(self._wires[-1])
        return self

    def Offset(self, d: float) -> WorkPlane:
        """Replaces the last wire by its offset at distance d, running the way the wire did; a wire being drawn is finished first.

        A positive d moves the wire to its left as netgen does: a counterclockwise wire grows, a reversed one shrinks.

        Args:
            d: the offset distance, as Face.Offset for a closed wire and Wire.Offset for an open one.

        Raises:
            ValueError: there is no finished wire, or the offset has more than one loop.

        Examples:
            wp.Rectangle(2, 1).Offset(0.25)
        """
        self.Finish()
        if not self._wires:
            raise ValueError("Offset(%g): no finished wire; draw one and Close() or Finish() it first" % d)
        w = self._wires[-1]
        loops = self._offset_shape(w, d).wires
        if len(loops) != 1:
            raise ValueError("Offset(%g): the offset has %d loops, a wire is one; use a smaller |d|" % (d, len(loops)))
        self._wires[-1] = self._oriented_as(loops[0], w)
        return self

    def _offset_shape(self, wire: _planex.Wire, d: float) -> Any:
        if not wire.IsClosed():
            return wire.Offset(d)
        return _planex.Face([wire]).Offset(d if _counterclockwise(wire, 0) else -d)

    def _oriented_as(self, wire: _planex.Wire, model: _planex.Wire) -> _planex.Wire:
        if model.IsClosed() and _counterclockwise(wire, 0) != _counterclockwise(model, 0):
            return self._reversed(wire)
        return wire

    def Wire(self) -> _planex.Wire | None:
        """The last finished wire, None if there is none; also called Last.

        Raises:
            ValueError: the wire is not one chain (it branches or crosses itself); Wires() gives its chains.

        Examples:
            wp.Wire()
        """
        if not self._wires:
            return None
        chains = _chains(self._wires[-1])
        if len(chains) != 1:
            raise ValueError("Wire(): the last wire is %d chains (it branches or crosses itself); Wires() gives them" % len(chains))
        return chains[0]

    Last = Wire

    def Wires(self) -> list[_planex.Wire]:
        """All finished wires, each as clean chains: a wire that branches or crosses itself gives several.

        Examples:
            wp.Wires()
        """
        return [chain for wire in self._wires for chain in _chains(wire)]

    def Face(self) -> _planex.Face | _planex.Shape:
        """The face of all finished wires, which are then forgotten; a px.Shape when they make several regions.

        Regions follow planex's rule for drawn edges (even-odd), as netgen's one face holds them: a wire inside one
        other wire is a hole, a wire inside a hole an island (a face of its own), a wire outside the others another
        region, and where two wires overlap is a hole. A hole drawn without Reverse(), a clockwise outer wire and
        crossing wires give a PlanexWarning.

        Raises:
            ValueError: there is no finished wire; a wire is not closed or not one chain.

        Examples:
            wp.Rectangle(4, 2).MoveTo(2, 1).Circle(0.5).Reverse().Face()
        """
        face = self._face()
        self._wires = []
        return face

    def ToOCCFace(self, *args: Any, **kwargs: Any) -> Any:
        """The OCC export of Face()'s px.Face, an occ.Face; the wires are not forgotten.

        Args:
            args: the arguments of px.Face.ToOCCFace.
            kwargs: its keyword arguments.

        Raises:
            ValueError: as Face().

        Examples:
            wp.Rectangle(4, 2).ToOCCFace()
        """
        return self._face().ToOCCFace(*args, **kwargs)

    def ToOCCShape(self, *args: Any, **kwargs: Any) -> Any:
        """The OCC export of Face()'s px.Face as px.Face.ToOCCShape; the wires are not forgotten.

        Args:
            args: the arguments of px.Face.ToOCCShape.
            kwargs: its keyword arguments.

        Raises:
            ValueError: as Face().

        Examples:
            wp.Rectangle(4, 2).ToOCCShape()
        """
        return self._face().ToOCCShape(*args, **kwargs)

    def ToOCCWire(self, *args: Any, **kwargs: Any) -> Any:
        """The OCC export of Wire(), an occ.Wire.

        Args:
            args: the arguments of px.Wire.ToOCCWire.
            kwargs: its keyword arguments.

        Raises:
            ValueError: there is no finished wire.

        Examples:
            wp.Line(2).Rotate(90).Line(1).Finish().ToOCCWire()
        """
        if not self._wires:
            raise ValueError("ToOCCWire(): no finished wire; draw one and Close() or Finish() it first")
        return _occ_wire(self._wires[-1], args, kwargs)

    def ToOCCWires(self, *args: Any, **kwargs: Any) -> Any:
        """The OCC export of the finished wires as netgen groups them, a compound of occ.Wire, one per wire.

        Args:
            args: the arguments of px.Wire.ToOCCWires.
            kwargs: its keyword arguments.

        Raises:
            ValueError: there is no finished wire.

        Examples:
            wp.Rectangle(4, 2).MoveTo(2, 1).Circle(0.5).ToOCCWires()
        """
        if not self._wires:
            raise ValueError("ToOCCWires(): no finished wire; draw one and Close() or Finish() it first")
        from netgen import occ
        return occ.Compound([_occ_wire(w, args, kwargs) for w in self._wires])

    def _face(self) -> _planex.Face | _planex.Shape:
        if not self._wires:
            raise ValueError("Face(): no finished wire; draw one and Close() or Finish() it first")
        wires = self.Wires()
        ccw = [_counterclockwise(w, k + 1) for k, w in enumerate(wires)]
        solo = [_planex.Face([w]) for w in wires]
        n = len(wires)
        crossing = {k for k in range(n) for j in range(n)
                    if j != k and _partly_inside(wires[k], solo[j]) and not _inside(wires[k], solo[j])}
        if crossing:
            warnings.warn("WorkPlane.Face(): wires %s cross. planex takes the region of all drawn edges (even-odd), so "
                          "where they overlap is a hole; for the union use px.Unify of their faces."
                          % ", ".join(str(k + 1) for k in sorted(crossing)), _planex.PlanexWarning, stacklevel=3)
        around = [[j for j in range(n) if j != k and _inside(wires[k], solo[j])] for k in range(n)]
        for k in range(n):
            if k in crossing:
                continue
            outer = [j for j in around[k] if len(around[j]) == len(around[k]) - 1]
            if len(around[k]) % 2:
                self._warn_drawn_the_other_way(k, not ccw[k], outer[0] if outer else around[k][0])
            else:
                self._warn_drawn_the_other_way(k, ccw[k], None)
        shape = _planex.Shape(wires)
        faces = list(shape.faces)
        return faces[0] if len(faces) == 1 else shape

    def _warn_drawn_the_other_way(self, k: int, as_drawn: bool, outer: int | None) -> None:
        if as_drawn:
            return
        if outer is not None:
            warnings.warn("WorkPlane.Face(): wire %d lies inside wire %d and is taken as a hole. planex finds holes by "
                          "nesting, so Reverse() is not needed; the face is the one netgen returns when the hole is "
                          "reversed. A face cannot lie inside another face: to keep the inner region as a face of its "
                          "own, use px.Glue([outer, inner])." % (k + 1, outer + 1), _planex.PlanexWarning, stacklevel=4)
        else:
            warnings.warn("WorkPlane.Face(): wire %d runs clockwise and is taken as an outer wire. planex finds holes by "
                          "nesting, so Reverse() is not needed; the face is the one netgen returns when the wire is not "
                          "reversed." % (k + 1), _planex.PlanexWarning, stacklevel=4)

    def _reversed(self, wire: _planex.Wire) -> _planex.Wire:
        return _planex.Wire([self._reversed_edge(e) for e in reversed(list(wire.edges))])

    def _reversed_edge(self, view: Any) -> _planex.Wire:
        edge, axes = view.edge, self._axes
        if isinstance(edge, _planex.LineEdge):
            piece = _planex.Line(edge.end, edge.start, plane=axes)
        elif isinstance(edge, _planex.ArcEdge):
            piece = _planex.Arc(edge.end, edge.start, edge.center, reverse=edge.ccw, plane=axes)
        elif isinstance(edge, _planex.CircleEdge):
            piece = _planex.Circle(edge.center, edge.radius, reverse=edge.ccw, plane=axes)
        else:
            piece = _reversed_bspline(edge.bspline, axes)
        for key in EDGE_PROPERTIES:
            if getattr(view, key) is not None:
                setattr(piece.edges[0], key, getattr(view, key))
        return piece

    def Plot(self, *args: Any, **kwargs: Any) -> Any:
        """Draws the sketch drawn on, as its Plot.

        Args:
            args: the arguments of the sketch's Plot.
            kwargs: its keyword arguments.

        Examples:
            wp.Plot()
        """
        return self._sketch.Plot(*args, **kwargs)

    def _repr_svg_(self):
        return self._sketch._repr_svg_()

    def _begin(self, at):
        if not self._chain:
            self._sketch.Move(at)
            self._chain = True
        return self._sketch._LastId()

    def _break_chain(self):
        if self._chain:
            self._sketch.Finish()
            self._chain = False

    def _meet(self, call, old, new):
        if self._edges and not any(math.dist(p, q) < MEET for p in (old, new) for q in self._ends):
            raise ValueError("%s(): the edge from (%g, %g) does not meet the open wire: call Finish() first to start a new wire"
                             % (call, old[0], old[1]))

    def _require_no_open_wire(self, call):
        if self._edges:
            raise ValueError("%s(): a wire is open: call Finish() first, the %s is a wire of its own" % (call, call.lower()))

    def _drawn(self, before, closing, old, end):
        last = self._sketch._LastId()
        if last != before:
            self._edges.append(last)
            self._ends += [old, end]
            self._vertex = last + ".end"
            self._started = True
        if closing:
            self.Finish()
        return self


def _chains(wire):
    """a netgen wire as clean chains: the wire itself if it is one"""
    clean = _planex.Wires(wire)
    if len(clean) == 1 and len(list(clean[0].edges)) == len(list(wire.edges)):
        return [wire]
    return clean


def _occ_wire(wire, args, kwargs):
    """one OCC wire of all the edges of a netgen wire, joined at their vertices as BRepBuilderAPI_MakeWire does"""
    from netgen import occ
    pieces = wire.ToOCCWires(*args, **{"merge_collinear_closer": 0, **kwargs}).wires
    return pieces[0] if len(pieces) == 1 else occ.Wire([e for piece in pieces for e in piece.edges])


def _reversed_bspline(bspline, axes):
    """the exact b-spline run backwards: poles and weights reversed, the knots mirrored"""
    knots, weights = bspline["knots"], bspline["weights"]
    return _planex.BSpline(list(reversed(bspline["poles"])), [knots[0] + knots[-1] - k for k in reversed(knots)],
                           bspline["degree"], None if weights is None else list(reversed(weights)), plane=axes)


def _inside(wire, face):
    return all(face.Contains(p) for p in wire.ToPolyline(ORIENTATION_TOL))


def _partly_inside(wire, face):
    return any(face.Contains(p, boundary=False) for p in wire.ToPolyline(ORIENTATION_TOL))


def _counterclockwise(wire, number):
    """whether a closed wire runs counterclockwise (its edges in their order and direction): the sign of its area"""
    if not wire.IsClosed():
        raise ValueError("Face(): wire %d is not closed: Close() it, or leave it out" % number)
    return sum(_area(e.edge) for e in wire.edges) > 0


def _area(edge):
    """the edge's term of the wire's signed area: half the integral of x dy - y dx"""
    if isinstance(edge, _planex.CircleEdge):
        return math.pi * edge.radius ** 2 * (1 if edge.ccw else -1)
    if isinstance(edge, _planex.ArcEdge):
        c, s, e = edge.center, edge.start, edge.end
        return (c.x * (e.y - s.y) - c.y * (e.x - s.x) + edge.radius ** 2 * math.radians(edge.sweep)) / 2
    if isinstance(edge, _planex.SplineEdge):
        p = edge.Sample(ORIENTATION_TOL)
        return sum(p[i][0] * p[i + 1][1] - p[i + 1][0] * p[i][1] for i in range(len(p) - 1)) / 2
    return (edge.start.x * edge.end.y - edge.end.x * edge.start.y) / 2


def _xy(p):
    """a 2d point or vector: netgen's gp_Pnt2d or gp_Vec2d, a px.Vertex or a pair"""
    if hasattr(p, "x") and hasattr(p, "y"):
        return float(p.x), float(p.y)
    x, y = p
    return float(x), float(y)


def _rotated(v, ang):
    """gp_Dir2d.Rotate by ang radians"""
    c, s = math.cos(ang), math.sin(ang)
    return c * v[0] + -s * v[1], s * v[0] + c * v[1]


def _angle(a, b):
    """gp_Vec2d.Angle: the turn from a to b in radians, in [-pi, pi]"""
    d = math.hypot(*a) * math.hypot(*b)
    if not d > 0:
        raise ValueError("a direction has zero length")
    cos, sin = (a[0] * b[0] + a[1] * b[1]) / d, (a[0] * b[1] - a[1] * b[0]) / d
    # OCC's switch between acos and asin, where each is accurate
    if -0.70710678118655 < cos < 0.70710678118655:
        return math.acos(cos) if sin > 0 else -math.acos(cos)
    if cos > 0:
        return math.asin(sin)
    return math.pi - math.asin(sin) if sin > 0 else -math.pi - math.asin(sin)


def _bound(call, names, args, kwargs):
    """the values of names (a trailing name defaults to None) from args, then from kwargs"""
    if len(args) > len(names):
        raise TypeError("%s() takes at most %d arguments (%d given)" % (call, len(names), len(args)))
    out = list(args)
    for k in names[len(args):]:
        if k in kwargs:
            out.append(kwargs.pop(k))
        elif k == "name":
            out.append(None)
        else:
            raise TypeError("%s() missing argument '%s'" % (call, k))
    for k in names[:len(args)]:
        if k in kwargs:
            raise TypeError("%s() got multiple values for argument '%s'" % (call, k))
    if kwargs:
        raise TypeError("%s() got an unexpected keyword argument '%s'" % (call, next(iter(kwargs))))
    return out


def _named(name, **given):
    """the keywords of a sketch drawing call: name and the given ones that are set"""
    out = {k: v for k, v in given.items() if v is not None}
    if name is not None:
        out["name"] = name
    return out
