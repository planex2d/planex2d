# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""KiCad boards (.kicad_pcb, KiCad 9 and later): every layer as a planex shape, or the whole board stacked up in 3d"""

from __future__ import annotations

import glob
import math
import ntpath
import os
import re
import shutil
import subprocess
import tempfile
import warnings
from typing import TYPE_CHECKING, Any

from . import _planex as px
from . import stackups
from ._geom import read_name

if TYPE_CHECKING:
    from ._planex import Shape

# KiCad works in millimetres, the results are in metres
MM = 1e-3
_PTH, _NPTH = "pth", "npth"
# the file versions of KiCad 9.0 (the oldest read) and of KiCad 10 (the newest tested); KiCad 10's first (its sources'
# pcb_io_kicad_sexpr.h, "Start of 10.0 development")
_OLDEST, _NEWEST, _KICAD10 = 20241229, 20260206, 20250210
# the board as solder mask green, the copper (tracks, pads, zones, vias and the drilled walls) gold
BOARD_COL = (0.106, 0.369, 0.125)
COPPER_COL = (0.831, 0.686, 0.216)
_WILDCARDS = {"*.Adhes": ("F.Adhes", "B.Adhes"), "*.Paste": ("F.Paste", "B.Paste"), "*.Mask": ("F.Mask", "B.Mask"),
              "*.SilkS": ("F.SilkS", "B.SilkS"), "*.Fab": ("F.Fab", "B.Fab"), "*.CrtYd": ("F.CrtYd", "B.CrtYd")}
_MASKS = {"F.Cu": "F.Mask", "B.Cu": "B.Mask"}
_TEXTS = ("gr_text", "gr_text_box", "fp_text", "fp_text_box", "property")
# what KiCad prints that is left out, in the order of the warning: (element or fill, singular, plural)
_LEFT_OUT = (("text", "text", "texts"), ("dimension", "dimension", "dimensions"), ("table", "table", "tables"),
             ("barcode", "barcode", "barcodes"), ("hatch", "hatched fill", "hatched fills"))

# the formats kicad-cli pcb import reads (KiCad 10's importers), by extension, for the messages
_IMPORTED = {".pcbdoc": "Altium Designer", ".cspcbdoc": "Altium CircuitStudio", ".cmpcbdoc": "Altium CircuitMaker",
             ".swpcbdoc": "SolidWorks PCB", ".brd": "Eagle", ".asc": "PADS", ".cpa": "CADSTAR", ".txt": "Fabmaster",
             ".fab": "Fabmaster", ".pcb": "P-CAD"}
# where KiCad installs kicad-cli, after $PLANEX_KICAD_CLI and the PATH (on Windows one folder per version)
_MAC_KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
_WINDOWS_KICAD_CLI = ntpath.join(os.environ.get("ProgramFiles", r"C:\Program Files"), "KiCad", "*", "bin", "kicad-cli.exe")

# KiCad writes these characters of a net name as {name}
_ESCAPES = {"slash": "/", "backslash": "\\", "lt": "<", "gt": ">", "colon": ":", "dblquote": '"', "quote": "'",
            "bar": "|", "space": " ", "dollar": "$", "tab": "\t", "return": "\n", "brace": "{"}


def _unescaped(name):
    return re.sub(r"\{(\w+)\}", lambda m: _ESCAPES.get(m.group(1), m.group(0)), name)


_TOKEN = re.compile(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()"]+')


class _Quoted(str):
    """a string that was quoted in the file: (net "5") names a net, (net 5) refers to the net table"""


def _parse(text):
    """the S-expression as nested lists: [head, child, ...]; quoted strings unquoted"""
    stack = [[]]
    for m in _TOKEN.finditer(text):
        t = m.group()
        if t == "(":
            stack.append([])
        elif t == ")":
            node = stack.pop()
            stack[-1].append(node)
        elif t[0] == '"':
            stack[-1].append(_Quoted(re.sub(r"\\(.)", r"\1", t[1:-1])))
        else:
            stack[-1].append(t)
    return stack[0][0]


def _all(node, key):
    return [c for c in node[1:] if isinstance(c, list) and c and c[0] == key]


def _first(node, key):
    return next((c for c in node[1:] if isinstance(c, list) and c and c[0] == key), None)


def _value(node, key, default=None):
    c = _first(node, key)
    return c[1] if c is not None and len(c) > 1 else default


def _number(node, key, default=0.0):
    v = _value(node, key)
    return float(v) if v is not None else default


def _pair(node, key):
    c = _first(node, key)
    return (float(c[1]), float(c[2])) if c is not None else None


def _width(node):
    """the line width: KiCad 7 and later in (stroke (width w)), before as (width w)"""
    stroke = _first(node, "stroke")
    return _number(stroke if stroke is not None else node, "width")


def _filled(node):
    return _value(node, "fill", "no") in ("yes", "solid")


def _hidden(node):
    """(hide yes) or (hide), or the bare hide of older files, on a text or its effects"""
    effects = _first(node, "effects")
    return any(_value(n, "hide", "yes") == "yes" if _first(n, "hide") is not None else
               any(c == "hide" and not isinstance(c, _Quoted) for c in n[1:])
               for n in (node, effects) if n is not None)


def _tented(node, inherit):
    """(front, back) of a (tenting ...): KiCad 9 lists the tented sides (or none), KiCad 10 writes (front yes)
    (back no), a side it leaves out or sets to none keeps inherit"""
    if node is None:
        return inherit
    if not any(isinstance(c, list) for c in node[1:]):
        return ("front" in node[1:], "back" in node[1:])
    sides = {"front": inherit[0], "back": inherit[1]}
    for c in node[1:]:
        if isinstance(c, list) and len(c) > 1 and c[0] in sides and c[1] in ("yes", "no"):
            sides[c[0]] = c[1] == "yes"
    return (sides["front"], sides["back"])


class _Frame:
    """KiCad's coordinates (millimetres, y down, angles counterclockwise on screen) to planex's (metres, y up): a
    footprint's local point is rotated by its angle and moved to its position"""

    def __init__(self, at=(0.0, 0.0), angle=0.0):
        self.x, self.y = at[0] * MM, -at[1] * MM
        self.c, self.s = math.cos(math.radians(angle)), math.sin(math.radians(angle))

    def __call__(self, p):
        x, y = p[0] * MM, -p[1] * MM
        return (self.x + self.c * x - self.s * y, self.y + self.s * x + self.c * y)


def _circle_through(a, m, b):
    """the center of the circle through three points"""
    ax, ay = a
    bx, by = m
    cx, cy = b
    d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by))
    ux = ((ax * ax + ay * ay) * (by - cy) + (bx * bx + by * by) * (cy - ay) + (cx * cx + cy * cy) * (ay - by)) / d
    uy = ((ax * ax + ay * ay) * (cx - bx) + (bx * bx + by * by) * (ax - cx) + (cx * cx + cy * cy) * (bx - ax)) / d
    return (ux, uy)


def _arc(a, m, b):
    """the arc from a through m to b; a line where the three lie on one (KiCad writes such arcs)"""
    turn = (m[0] - a[0]) * (b[1] - a[1]) - (m[1] - a[1]) * (b[0] - a[0])
    if abs(turn) <= 1e-9 * math.dist(a, b) ** 2:
        return px.Line(a, b)
    return px._Arc(a, b, _circle_through(a, m, b), turn > 0)


def _disc(c, r):
    return px.QuickSketch().Circle(c[0], c[1], r).Face()


def _stroke(a, b, w):
    """a straight track or line of width w with round ends"""
    if math.dist(a, b) <= 1e-12:
        return _disc(a, w / 2)
    return px.Line(a, b).Offset(w / 2)


def _polygon(pts):
    s = px.QuickSketch().Move(tuple(pts[0]))
    last = pts[0]
    for p in pts[1:]:
        if math.dist(p, last) > 1e-12:
            s.Line(p)
            last = p
    return s.Close().Face()


def _region(edges):
    return px._RegionOfEdges(edges, chains=False)


def _drawn(edges, filled, half):
    """what KiCad prints of a drawing: the region if filled, the edges stroked to 2 half wide; a negative half shrinks
    the region"""
    shapes = [_region(edges)] if filled and edges else []
    if half > 0:
        shapes.extend(e.Offset(half) for e in edges)
    elif half < 0 and shapes:
        shapes = [shapes[0].Offset(half)]
    return shapes


# the file's resolution: KiCad writes millimetres to six decimals
RESOLUTION = 1e-6 * MM


def _round_copper(shape):
    """(centre, radius) of a copper part that is one full circle (a via, a round pad), else None"""
    edges = list(shape.edges)
    if len(edges) != 1 or not isinstance(edges[0].edge, px.CircleEdge):
        return None
    c = edges[0].edge
    return tuple(c.center), c.radius


def _on_circles(edge, circles):
    """the arc edge as an arc around the centre of the circle among circles (a grid {cell: [(centre, radius)]}) that
    its start, middle and end lie on within the file's resolution, else edge: an arc in the file has its circle from
    a mid point rounded to the nanometre, and the same copper as a via's or pad's exact circle would not meet it"""
    if not isinstance(edge.edges[0].edge, px.ArcEdge):
        return edge
    arc = edge.edges[0].edge
    points = [tuple(arc.start), tuple(edge.edges[0].PointAt(0.5)), tuple(arc.end)]
    cx, cy = (round(v / _CELL) for v in arc.center)
    for i in (cx - 1, cx, cx + 1):
        for j in (cy - 1, cy, cy + 1):
            for c, r in circles.get((i, j), ()):
                if all(abs(math.dist(p, c) - r) <= RESOLUTION for p in points):
                    return px._Arc(points[0], points[2], c, arc.ccw)
    return edge


# the cells of the circle grid: far wider than an arc's centre is off when its mid point is rounded
_CELL = 1e-5


def _drilled(hole, name):
    for e in hole.edges:
        e.name = name
        e.col = COPPER_COL
    return hole


def _points(pts, frame):
    """a (pts ...) list of (xy x y) and (arc (start) (mid) (end)) as a wire through the points"""
    edges = []
    last = None
    for item in pts[1:]:
        if item[0] == "xy":
            p = frame((float(item[1]), float(item[2])))
            if last is not None and math.dist(last, p) > 1e-12:
                edges.append(px.Line(last, p))
            last = p
        elif item[0] == "arc":
            a, m, b = (frame(_pair(item, k)) for k in ("start", "mid", "end"))
            if last is not None and math.dist(last, a) > 1e-12:
                edges.append(px.Line(last, a))
            edges.append(_arc(a, m, b))
            last = b
    return edges, last


def _closed_region(pts, frame):
    """the region a closed (pts ...) outline bounds; KiCad's filled zones join holes to the outline by slits, which
    cancel"""
    edges, last = _points(pts, frame)
    first = _start(edges[0]) if edges else None
    if first is not None and math.dist(first, last) > 1e-12:
        edges.append(px.Line(last, first))
    return _region(edges)


def _start(wire):
    e = wire.edges[0]
    return tuple(e.start)


def _kicad_cli(path, what):
    """kicad-cli of KiCad 10 or later: $PLANEX_KICAD_CLI, else the one on the PATH, else where KiCad installs it"""
    found = os.environ.get("PLANEX_KICAD_CLI") or shutil.which("kicad-cli")
    if not found and os.path.isfile(_MAC_KICAD_CLI):
        found = _MAC_KICAD_CLI
    if not found:
        versions = {p: [int(n) for n in re.findall(r"\d+", p.split("\\")[-3])] for p in glob.glob(_WINDOWS_KICAD_CLI)}
        found = max(versions, key=versions.get, default=None)
    missing = "FromKiCad(%r): KiCad >= 10 (kicad-cli) is not installed; it converts %s boards" % (path, what)
    if not found:
        raise RuntimeError(missing)
    try:
        run = subprocess.run([found, "version"], capture_output=True, text=True, errors="replace")
    except OSError:
        raise RuntimeError("%s (%s cannot be run)" % (missing, found)) from None
    version = run.stdout.strip()
    number = re.match(r"(\d+)\.(\d+)", version)
    # KiCad 10 development builds call themselves 9.99
    if run.returncode != 0 or number is None or tuple(int(n) for n in number.groups()) < (9, 99):
        raise RuntimeError("%s (found kicad-cli %s)" % (missing, version or "of unknown version"))
    return found


def _converted(path, folder):
    """a non-KiCad board converted by kicad-cli (KiCad 10's pcb import) into a .kicad_pcb in folder"""
    what = _IMPORTED.get(os.path.splitext(os.fspath(path))[1].lower(), "non-KiCad")
    cli = _kicad_cli(path, what)
    out = os.path.join(folder, "board.kicad_pcb")
    run = subprocess.run([cli, "pcb", "import", "--format", "auto", "-o", out, os.fspath(path)], capture_output=True,
                         text=True, errors="replace")
    if run.returncode != 0 or not os.path.isfile(out):
        raise ValueError("FromKiCad(%r): kicad-cli could not convert the %s board: %s" %
                         (path, what, (run.stderr or run.stdout).strip()))
    return out


class _Board:
    def __init__(self, path, name=None):
        with open(path, encoding="utf-8") as f:
            root = _parse(f.read())
        path = path if name is None else name
        if not root or root[0] != "kicad_pcb":
            raise ValueError("FromKiCad(%r): not a KiCad board (.kicad_pcb)" % (path,))
        self.path = path
        version = _value(root, "version")
        if version is None or not version.isdigit() or int(version) < _OLDEST:
            raise ValueError("FromKiCad(%r): KiCad file version %s is older than KiCad 9 (%d); open the board in "
                             "KiCad 9 or later and save it" % (path, version, _OLDEST))
        if int(version) > _NEWEST:
            warnings.warn("FromKiCad(%r): file version %s is newer than the newest tested (%d)" %
                          (path, version, _NEWEST), px.PlanexWarning, stacklevel=3)
        self.order = [c[1] for c in _first(root, "layers")[1:] if isinstance(c, list) and len(c) > 1]
        names = [n for n in self.order if n.endswith(".Cu")]
        inner = sorted((n for n in names if re.fullmatch(r"In\d+\.Cu", n)), key=lambda n: int(n[2:-3]))
        self.copper = [n for n in ("F.Cu",) if n in names] + inner + [n for n in ("B.Cu",) if n in names]
        self.version = int(version)
        setup = _first(root, "setup") or ["setup"]
        self.mask_margin = _number(setup, "pad_to_mask_clearance")
        self.mask_min_width = _number(setup, "solder_mask_min_width")
        self.paste_margin = _number(setup, "pad_to_paste_clearance")
        self.paste_ratio = _number(setup, "pad_to_paste_clearance_ratio")
        tenting = _first(setup, "tenting")
        self.tented = _tented(tenting, (False, False)) if tenting is not None else (True, True)
        self.stackup = _first(setup, "stackup")
        self.nets = {c[1]: c[2] for c in _all(root, "net") if len(c) > 2}
        self.parts = {n: {} for n in self.copper}
        # the copper circles by layer, on a grid, and the copper drawings, made once all circles are known
        self.circles = {n: {} for n in self.copper}
        self.drawings = []
        self.drawn = {}
        self.left_out = {}
        self.holes = []
        self.outline = []
        self.unfilled = 0
        for node in root[1:]:
            if not isinstance(node, list):
                continue
            kind = node[0]
            if kind == "segment" or kind == "arc":
                self._track(node)
            elif kind == "via":
                self._via(node)
            elif kind == "zone":
                self._zone(node)
            elif kind == "footprint" or kind == "module":
                self._footprint(node)
            elif kind in _TEXTS:
                self._text(node)
            elif kind in ("dimension", "table", "barcode"):
                self._leave_out(_value(node, "layer"), kind)
            elif kind.startswith("gr_"):
                self._graphic(node, _Frame(), self._net(node))
        for layers, net, edges, filled, half in self.drawings:
            circles = {}
            for layer in layers:
                for cell, found in self.circles[layer].items():
                    circles.setdefault(cell, []).extend(found)
            for s in _drawn([_on_circles(e, circles) for e in edges], filled, half):
                self._add(layers, net, s)

    def _net(self, node):
        """the net name of an element: (net 3) refers to the table, (net "GND") or (net 3 "GND") names it"""
        c = _first(node, "net")
        if c is None or len(c) < 2:
            return None
        name = c[-1] if len(c) > 2 or isinstance(c[1], _Quoted) else self.nets.get(c[1])
        return _unescaped(name) if name else None

    def _layers(self, node):
        """the copper layers an element lies on: (layer x) or (layers ...), "*.Cu", "*In.Cu" and "F&B.Cu" spelled out"""
        names = []
        single = _value(node, "layer")
        if single is not None:
            names.append(single)
        layers = _first(node, "layers")
        if layers is not None:
            names.extend(layers[1:])
        out = []
        for n in names:
            if n == "*.Cu":
                out.extend(self.copper)
            elif n == "*In.Cu":
                out.extend(x for x in self.copper if x not in ("F.Cu", "B.Cu"))
            elif n == "F&B.Cu":
                out.extend(x for x in ("F.Cu", "B.Cu") if x in self.copper)
            elif n in self.copper:
                out.append(n)
        return list(dict.fromkeys(out))

    def _mask_of(self, node):
        """the solder mask layer a track or copper drawing opens: KiCad 9 writes (layers "F.Cu" "F.Mask")"""
        layers = _first(node, "layers")
        copper = self._layers(node)
        if layers is None or len(copper) != 1 or not any(n in ("F.Mask", "B.Mask") for n in layers[1:]):
            return None
        return _MASKS.get(copper[0])

    def _span(self, a, b):
        """the copper layers from a to b in stack order"""
        i, j = sorted((self.copper.index(a), self.copper.index(b)))
        return self.copper[i:j + 1]

    def _add(self, layers, net, shape):
        round_ = _round_copper(shape)
        for layer in layers:
            self.parts[layer].setdefault(net, []).append(shape)
            if round_ is not None:
                cell = tuple(round(v / _CELL) for v in round_[0])
                self.circles[layer].setdefault(cell, []).append(round_)

    def _copper(self, node, shape):
        self._add(self._layers(node), self._net(node), shape)

    def _draw(self, layer, shape):
        self.drawn.setdefault(layer, []).append(shape)

    def _leave_out(self, layer, kind):
        if layer is not None:
            counts = self.left_out.setdefault(layer, {})
            counts[kind] = counts.get(kind, 0) + 1

    def _text(self, node):
        if not _hidden(node):
            self._leave_out(_value(node, "layer"), "text")

    def _track(self, node):
        """a track segment or arc: copper of its width, and on the solder mask the opening its mask margin (else the
        board's) widens"""
        w = _number(node, "width") * MM
        if node[0] == "segment":
            a, b = (_Frame()(_pair(node, k)) for k in ("start", "end"))
            self._copper(node, _stroke(a, b, w))
        else:
            a, m, b = (_Frame()(_pair(node, k)) for k in ("start", "mid", "end"))
            path = _arc(a, m, b)
            self._copper(node, path.Offset(w / 2))
        mask = self._mask_of(node)
        if mask is None:
            return
        opening = w + 2 * max(_number(node, "solder_mask_margin", self.mask_margin) * MM, -w / 2)
        if opening > 0:
            self._draw(mask, _stroke(a, b, opening) if node[0] == "segment" else path.Offset(opening / 2))

    def _via(self, node):
        layers = [n for n in _first(node, "layers")[1:] if n in self.copper]
        span = self._span(layers[0], layers[-1]) if len(layers) >= 2 else layers
        at = _Frame()(_pair(node, "at"))
        self._add(span, self._net(node), _disc(at, _number(node, "size") * MM / 2))
        drill = _number(node, "drill") * MM
        if drill > 0:
            self.holes.append((_drilled(_disc(at, drill / 2), _PTH), span))
        tented = _tented(_first(node, "tenting"), self.tented)
        opening = (_number(node, "size") + 2 * self.mask_margin) * MM
        for copper, mask, closed in (("F.Cu", "F.Mask", tented[0]), ("B.Cu", "B.Mask", tented[1])):
            if copper in span and not closed and opening > 0:
                self._draw(mask, _disc(at, opening / 2))

    def _zone(self, node):
        net = self._net(node) or (_unescaped(_value(node, "net_name")) if _value(node, "net_name") else None)
        filled = _all(node, "filled_polygon")
        if not filled and _first(node, "keepout") is None and _first(node, "rule_area") is None:
            self.unfilled += 1
        for poly in filled:
            layer = _value(poly, "layer") or _value(node, "layer")
            region = _closed_region(_first(poly, "pts"), _Frame())
            if layer in self.copper:
                self._add([layer], net, region)
            elif layer is not None and layer != "Edge.Cuts":
                self._draw(layer, region)

    def _graphic(self, node, frame, net):
        """a line, arc, circle, rectangle or polygon: copper (and its solder mask opening, widened by its mask margin,
        else the board's), the outline on Edge.Cuts, or a drawing on another layer"""
        layer = _value(node, "layer")
        kind = node[0][3:]
        edges, filled = _outline(node, kind, frame)
        if edges is None:
            return
        if layer == "Edge.Cuts":
            self.outline.extend(edges)
            return
        w = _width(node) * MM
        layers = self._layers(node)
        if _value(node, "fill") in ("hatch", "reverse_hatch", "cross_hatch"):
            self._leave_out(layer if layer is not None or not layers else layers[0], "hatch")
        if not layers:
            if layer is not None:
                for s in _drawn(edges, filled, w / 2):
                    self._draw(layer, s)
            return
        self.drawings.append((layers, net, edges, filled, w / 2))
        mask = self._mask_of(node)
        if mask is not None:
            margin = _number(node, "solder_mask_margin", self.mask_margin) * MM
            for s in _drawn(edges, filled, w / 2 + (margin if filled else max(margin, -w / 2))):
                self._draw(mask, s)

    def _footprint(self, node):
        at = _first(node, "at")
        frame = _Frame((float(at[1]), float(at[2])), float(at[3]) if len(at) > 3 else 0.0)
        for item in node[1:]:
            if not isinstance(item, list):
                continue
            if item[0] == "pad":
                self._pad(item, frame, node)
            elif item[0] in _TEXTS:
                self._text(item)
            elif item[0] in ("dimension", "table", "barcode"):
                self._leave_out(_value(item, "layer"), item[0])
            elif item[0].startswith("fp_"):
                self._graphic(item, frame, self._net(item))

    def _pad(self, node, frame, footprint):
        """the pad's copper, its drill hole, and its shape on the other layers it lists: on a solder mask grown by its
        mask margin, on a paste layer by its paste margin, as KiCad 9 sets them (the pad's, else the footprint's, else
        the board's; a pad without copper has none)"""
        kind, shape = node[2], node[3]
        at = _first(node, "at")
        center = frame((float(at[1]), float(at[2])))
        angle = float(at[3]) if len(at) > 3 else 0.0
        size = _pair(node, "size") or (0.0, 0.0)
        listed = _first(node, "layers")
        listed = listed[1:] if listed is not None else []
        spelled = dict.fromkeys(x for item in listed for x in _WILDCARDS.get(item, (item,)))
        others = [n for n in spelled if not n.endswith(".Cu") and n != "Edge.Cuts"]
        drill = _first(node, "drill")
        offset = _pair(drill, "offset") if drill is not None else None
        shift = (offset[0] * MM, -offset[1] * MM) if offset else (0.0, 0.0)
        pad = _pad_shape(node, shape, size) if kind != "np_thru_hole" or others else None
        if kind != "np_thru_hole" and pad is not None:
            self._add(self._layers(node), self._net(node), _placed(pad, angle, center, shift))
        if drill is not None and kind in ("thru_hole", "np_thru_hole"):
            hole = _drill(drill, _NPTH if kind == "np_thru_hole" else _PTH)
            if hole is not None:
                self.holes.append((_placed(hole, angle, center), list(self.copper)))
        on_copper = any(n.endswith(".Cu") for n in listed)
        plotted = "kicad10" if self.version >= _KICAD10 else "kicad9"
        for name in others:
            mx = my = 0.0
            rule = plotted
            if on_copper and name in ("F.Mask", "B.Mask"):
                margin = _number(node, "solder_mask_margin", _number(footprint, "solder_mask_margin", self.mask_margin))
                mx = my = max(margin, -min(size) / 2)
                if self.mask_min_width > 0:
                    rule = "closing"
            elif on_copper and name in ("F.Paste", "B.Paste"):
                mx, my = self._paste_margins(node, footprint, shape, size)
            opening = _pad_opening(node, shape, size, mx, my, pad, rule)
            if opening is not None:
                self._draw(name, _placed(opening, angle, center, shift))

    def _paste_margins(self, node, footprint, shape, size):
        """KiCad's paste margin of a pad in x and y (mm): the margin plus the ratio times the pad's size, each the
        pad's, else the footprint's, else the board's; not below half the size, except for custom pads"""
        margin = _number(node, "solder_paste_margin", _number(footprint, "solder_paste_margin", self.paste_margin))
        ratio = _number(node, "solder_paste_margin_ratio", _number(
            footprint, "solder_paste_margin_ratio", _number(footprint, "solder_paste_ratio", self.paste_ratio)))
        mx, my = margin + size[0] * ratio, margin + size[1] * ratio
        if shape == "custom":
            return mx, my
        return max(mx, -size[0] / 2), max(my, -size[1] / 2)

    def dielectrics(self):
        """the thicknesses (mm) of the dielectric layers (their sublayers added) between the copper layers of the
        stackup in the board file"""
        if self.stackup is None:
            raise ValueError("FromKiCad(%r, px.stackups.kicad): the board file has no stackup (KiCad: Board Setup, "
                             "Physical Stackup)" % (self.path,))
        gaps, gap = [], None
        for item in _all(self.stackup, "layer"):
            if item[1] in self.copper:
                if gap is not None:
                    gaps.append(gap)
                gap = 0.0
            elif gap is not None and item[1].startswith("dielectric"):
                gap += sum(float(c[1]) for c in _all(item, "thickness"))
        if len(gaps) != len(self.copper) - 1:
            raise ValueError("FromKiCad(%r, px.stackups.kicad): the board file's stackup has %d of the %d copper "
                             "layers" % (self.path, len(gaps) + (gap is not None), len(self.copper)))
        return gaps

    def names(self):
        """the copper layers from the top, Edge.Cuts, then the other layers of the layer table holding something"""
        rest = [n for n in self.order if n in self.drawn and n not in self.copper and n != "Edge.Cuts"]
        return self.copper + ["Edge.Cuts"] + rest

    def shape(self, name):
        if name in self.copper:
            return self.layer(name)
        if name == "Edge.Cuts":
            return self.profile()
        parts = self.drawn.get(name)
        if not parts:
            return px.Shape()
        openings = px.Unify(parts)
        if name in ("F.Mask", "B.Mask") and self.mask_min_width > 0:
            # KiCad grows by half the minimum width less one nanometre, so openings exactly that far apart stay apart
            grow = (self.mask_min_width / 2 - 1e-6) * MM
            return px.Unify([openings.Offset(grow).Offset(-grow), openings])
        return openings

    def layer(self, name):
        """the copper of one layer: united per net (faces named by net), drilled"""
        nets = []
        for net, parts in self.parts[name].items():
            if not parts:
                continue
            u = px.Unify(parts)
            u.name = read_name(net)
            nets.append(u)
        if not nets:
            return px.Shape()
        shape = px.Glue(nets) if len(nets) > 1 else nets[0]
        shape.col = COPPER_COL
        shape.edges.col = COPPER_COL
        holes = [h for h, span in self.holes if name in span]
        return shape - px.Unify(holes) if holes else shape

    def profile(self, between=None):
        """the board outline with the holes through it, their edges named "pth" or "npth"; between: the two copper
        layers a hole must join to count, default all of them"""
        if not self.outline:
            raise ValueError("FromKiCad(%r): the board has no outline on Edge.Cuts" % (self.path,))
        shape = px._RegionOfEdges(self.outline, chains=False)
        shape.edges.col = BOARD_COL
        shape.name = "profile"
        shape.col = BOARD_COL
        needed = between or (self.copper[0], self.copper[-1])
        holes = [h for h, span in self.holes if all(n in span for n in needed)]
        return shape - px.Unify(holes) if holes else shape


def _outline(node, kind, frame):
    """the edges of a graphic item and whether it is filled; None for kinds without geometry (text)"""
    filled = _filled(node)
    if kind == "line":
        a, b = (frame(_pair(node, k)) for k in ("start", "end"))
        return ([px.Line(a, b)] if math.dist(a, b) > 1e-12 else []), False
    if kind == "arc":
        a, m, b = (frame(_pair(node, k)) for k in ("start", "mid", "end"))
        return [_arc(a, m, b)], False
    if kind == "circle":
        c, e = frame(_pair(node, "center")), frame(_pair(node, "end"))
        return ([px._Circle(c, e)] if math.dist(c, e) > 1e-12 else []), filled
    if kind == "rect":
        (x0, y0), (x1, y1) = _pair(node, "start"), _pair(node, "end")
        r = min(_number(node, "radius"), abs(x1 - x0) / 2, abs(y1 - y0) / 2)
        if r > 0:
            return _rounded_rect_edges(min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1), r, frame), filled
        corners = [frame(p) for p in ((x0, y0), (x1, y0), (x1, y1), (x0, y1))]
        return [px.Line(corners[k], corners[(k + 1) % 4]) for k in range(4)], filled
    if kind == "curve":
        poles = [frame((float(p[1]), float(p[2]))) for p in _first(node, "pts")[1:]
                 if isinstance(p, list) and p[0] == "xy"]
        if len(poles) != 4 or all(math.dist(p, poles[0]) <= 1e-12 for p in poles):
            return [], False
        return [px.BSpline(poles, [0.0, 0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 1.0], 3)], False
    if kind == "poly":
        edges, last = _points(_first(node, "pts"), frame)
        if edges and math.dist(_start(edges[0]), last) > 1e-12:
            edges.append(px.Line(last, _start(edges[0])))
        return edges, filled
    return None, False


def _rounded_rect_edges(xl, yt, xr, yb, r, frame):
    """the edges of a rectangle from (xl, yt) to (xr, yb) in KiCad's coordinates with corners of radius r (KiCad 10)"""
    q = r * (1 - math.sqrt(0.5))
    loop = [((xl + r, yt), (xr - r, yt), None), ((xr - r, yt), (xr, yt + r), (xr - q, yt + q)),
            ((xr, yt + r), (xr, yb - r), None), ((xr, yb - r), (xr - r, yb), (xr - q, yb - q)),
            ((xr - r, yb), (xl + r, yb), None), ((xl + r, yb), (xl, yb - r), (xl + q, yb - q)),
            ((xl, yb - r), (xl, yt + r), None), ((xl, yt + r), (xl + r, yt), (xl + q, yt + q))]
    edges = []
    for a, b, m in loop:
        a, b = frame(a), frame(b)
        if m is not None:
            edges.append(_arc(a, frame(m), b))
        elif math.dist(a, b) > 1e-12:
            edges.append(px.Line(a, b))
    return edges


def _rounded_rect(w, h, r):
    """a w x h rectangle around the origin with corners of radius r"""
    r = min(r, w / 2, h / 2)
    if r <= 0:
        return _polygon([(-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)])
    x, y = w / 2 - r, h / 2 - r
    s = px.QuickSketch().Move((-x, -h / 2))
    if x > 0:
        s.Line((x, -h / 2))
    s.Arc((w / 2, -y), tangent=(1, 0))
    if y > 0:
        s.Line((w / 2, y))
    s.Arc((x, h / 2), tangent=(0, 1))
    if x > 0:
        s.Line((-x, h / 2))
    s.Arc((-w / 2, y), tangent=(-1, 0))
    if y > 0:
        s.Line((-w / 2, -y))
    s.Arc((-x, -h / 2), tangent=(0, -1))
    return s.Close().Face()


def _chamfered(w, h, c, corners):
    """a w x h rectangle with the corners named in corners cut by c"""
    pts = []
    for (sx, sy), name in (((-1, -1), "bottom_left"), ((1, -1), "bottom_right"), ((1, 1), "top_right"), ((-1, 1), "top_left")):
        x, y = sx * w / 2, sy * h / 2
        if name in corners:
            # going counterclockwise: first the point on the side before the corner, then the one after it
            first = (x, y - sy * c) if sx * sy > 0 else (x - sx * c, y)
            second = (x - sx * c, y) if sx * sy > 0 else (x, y - sy * c)
            pts.extend([first, second])
        else:
            pts.append((x, y))
    return _polygon(pts)


def _pad_shape(node, shape, size):
    """the copper of a pad around the origin, unrotated, in metres (KiCad's y down turned up)"""
    w, h = size[0] * MM, size[1] * MM
    if shape == "circle":
        return _disc((0.0, 0.0), w / 2)
    if shape == "oval":
        if abs(w - h) <= 1e-12:
            return _disc((0.0, 0.0), w / 2)
        if w > h:
            return px.Line((-(w - h) / 2, 0.0), ((w - h) / 2, 0.0)).Offset(h / 2)
        return px.Line((0.0, -(h - w) / 2), (0.0, (h - w) / 2)).Offset(w / 2)
    if shape in ("rect", "roundrect"):
        corners = _chamfer_corners(node)
        if corners:
            pad = _chamfered(w, h, _number(node, "chamfer_ratio") * min(w, h), corners)
            r = _number(node, "roundrect_rratio") * min(w, h) if shape == "roundrect" else 0.0
            return pad.Offset(-r).Offset(r) if r > 0 else pad
        if shape == "roundrect":
            return _rounded_rect(w, h, _number(node, "roundrect_rratio") * min(w, h))
        return _rounded_rect(w, h, 0.0)
    if shape == "trapezoid":
        dx, dy = (v * MM / 2 for v in (_pair(node, "rect_delta") or (0.0, 0.0)))
        # PAD::BuildPadPolygon's corners in KiCad's y down, then flipped
        pts = [(-w / 2 - dy, h / 2 + dx), (w / 2 + dy, h / 2 - dx), (w / 2 - dy, -h / 2 + dx), (-w / 2 + dy, -h / 2 - dx)]
        return _polygon([(x, -y) for x, y in pts])
    if shape == "custom":
        options = _first(node, "options")
        anchor = _value(options, "anchor", "rect") if options is not None else "rect"
        parts = [_disc((0.0, 0.0), w / 2) if anchor == "circle" else _rounded_rect(w, h, 0.0)]
        primitives = _first(node, "primitives")
        for item in primitives[1:] if primitives is not None else []:
            if isinstance(item, list) and item[0].startswith("gr_"):
                edges, filled = _outline(item, item[0][3:], _Frame())
                if edges is None:
                    continue
                # a pad's polygon without a fill entry is filled (KiCad before 7)
                if filled or (item[0] == "gr_poly" and _value(item, "fill") is None):
                    parts.append(_region(edges))
                width = _width(item) * MM
                if width > 0:
                    parts.extend(e.Offset(width / 2) for e in edges)
        return px.Unify(parts)
    return None


def _chamfer_corners(node):
    """the corners a chamfered pad cuts, none if it is not chamfered"""
    chamfer = _first(node, "chamfer")
    corners = set(chamfer[1:]) if chamfer is not None else set()
    return corners if _number(node, "chamfer_ratio") > 0 else set()


def _pad_opening(node, shape, size, mx, my, pad, rule):
    """a pad's opening in a solder mask or paste layer, the pad's size grown by mx, my (mm) on each side; None where
    nothing is left. rule "kicad9", as KiCad 9 plots it: a rectangle gets corners of radius mx > 0, a round rectangle
    keeps its corner ratio, a chamfered one its ratios unless it grows alike in x and y, a trapezoid or custom pad is
    offset by mx. "kicad10": the same, but a round rectangle growing alike in x and y is offset exactly (its radius grows
    by mx). "closing", KiCad's mask with a minimum width (FOOTPRINT::TransformPadsToPolySet): offset exactly by mx >= 0,
    resized for mx < 0 (a custom pad offset)"""
    w, h = size[0] + 2 * mx, size[1] + 2 * my
    if pad is None or (shape != "custom" and (w <= 0 or h <= 0)):
        return None
    if rule == "closing":
        if mx >= 0 or shape == "custom":
            return pad.Offset(mx * MM) if mx else pad
        return _pad_shape(node, shape, (w, h))
    if shape in ("trapezoid", "custom"):
        return pad.Offset(mx * MM) if mx else pad
    if mx == 0 and my == 0:
        return pad
    if _chamfer_corners(node) and mx > 0 and mx == my:
        return pad.Offset(mx * MM)
    if shape == "rect" and not _chamfer_corners(node):
        return _rounded_rect(w * MM, h * MM, max(mx, 0.0) * MM)
    if shape == "roundrect" and rule == "kicad10" and mx == my and not _chamfer_corners(node):
        return _rounded_rect(w * MM, h * MM, max(_number(node, "roundrect_rratio") * min(size) + mx, 0.0) * MM)
    return _pad_shape(node, shape, (w, h))


def _drill(node, name):
    """the hole of a (drill d) or (drill oval w h), its edges named name"""
    values = [v for v in node[1:] if not isinstance(v, list)]
    if values and values[0] == "oval":
        w, h = float(values[1]) * MM, float(values[2] if len(values) > 2 else values[1]) * MM
        if abs(w - h) <= 1e-12:
            hole = _disc((0.0, 0.0), w / 2)
        elif w > h:
            hole = px.Line((-(w - h) / 2, 0.0), ((w - h) / 2, 0.0)).Offset(h / 2)
        else:
            hole = px.Line((0.0, -(h - w) / 2), (0.0, (h - w) / 2)).Offset(w / 2)
    elif values:
        d = float(values[0]) * MM
        if d <= 0:
            return None
        hole = _disc((0.0, 0.0), d / 2)
    else:
        return None
    return _drilled(hole, name)


def _placed(shape, angle, center, shift=(0.0, 0.0)):
    """moved by shift (in the pad's frame), rotated by the pad's (absolute) angle, moved to its center"""
    if shift != (0.0, 0.0):
        shape = shape.Translated(shift)
    if angle:
        shape = shape.Rotated((0.0, 0.0), angle)
    return shape.Translated(center)


def FromKiCad(path: str | os.PathLike, /, layer: str | list[float] | dict | Any | None = None,
              remove_faces_smaller: float | None = 5e-8, merge_small_faces_instead: bool | None = True,
              remove_edges_smaller: float | None = None, close_gaps_smaller: float | None = None,
              remove_free_edges: bool | None = None,
              merge_collinear_closer: float | None = None) -> dict[str, Shape] | Shape | Any:
    """Reads a KiCad board (.kicad_pcb, KiCad 9 and later) in metres, y up: its layers as Shapes, or the board in 3d.

    A board of another program (Altium, Eagle, PADS, CADSTAR, Fabmaster, P-CAD, SolidWorks PCB: a file that is no
    .kicad_pcb) is converted first with KiCad 10's kicad-cli (pcb import), found as $PLANEX_KICAD_CLI, on the PATH or
    where KiCad installs it.

    A copper layer is every track, arc, via, pad, filled zone and copper drawing on it, united per net with the faces
    named by the net; the drill holes through it are cut out, the edges of plated holes (vias and plated pads) named
    "pth", of unplated ones "npth". The outline face is named "profile", the holes through the whole board are cut out
    of it. Every other layer is what KiCad prints there: lines, arcs and Bezier curves (as exact splines) as wide as
    drawn (dashed ones solid), filled shapes and zones as regions, the drawings of footprints too. The solder mask and
    paste layers are their openings: the pads on them grown by their solder mask or paste margin (the pad's, else the
    footprint's, else the board's setup) as the file's KiCad plots them (a round rectangle keeps its corner ratio in a
    KiCad 9 file, grows exactly in a KiCad 10 one), what is drawn there, the vias the board's (or the via's own) tenting
    leaves open and the tracks and copper drawings marked for an opening; with the setup's solder mask minimum width,
    openings closer than it are joined as KiCad joins them. Text, dimensions, tables, barcodes and hatched fills are
    left out, with one warning giving their counts; zones need to be filled in KiCad (a warning says when they are
    not). Copper and drill holes are coloured gold (col), the board solder mask green, in 2d and in 3d. Files newer
    than the newest tested (KiCad 10, version 20260206) warn.

    In 3d every dielectric is the outline extruded between its two copper layers (solids named dielectric_1, ... from
    the top), drilled where a hole joins both, so the hole walls are faces named "pth" or "npth"; the copper layers are
    faces at their heights (B.Cu at z = 0), named by net.

    The cleanup options are those of the OCC exports (ToOCCShape): the copper layers and the outline are cleaned with
    them (Simplify), the other layers keep what KiCad prints, the 3d board is exported with them.

    Args:
        path: the board file.
        layer: None: a dict {KiCad layer name: Shape}: the copper layers from the top (F.Cu, In1.Cu, ..., B.Cu), each
            even when empty, then the board outline "Edge.Cuts", then every other layer of the board's layer table
            that holds something, in the table's order. A layer name ("F.Cu", "F.SilkS", ...): that layer's Shape
            alone. A stackup: the board in 3d as one glued OCC shape: a list of dielectric thicknesses in millimetres
            from the top, as the manufacturer gives it (e.g. [0.2, 1.3, 0.2] for four copper layers), a stackup of
            px.stackups (a dict {number of copper layers: such a list}, e.g. px.stackups.jlc7628), or
            px.stackups.kicad (the stackup in the board file).
        remove_faces_smaller: faces of less area than this (m^2) are united with the neighbour they share the longest
            edge with, such as the slivers a zone fill's polygon leaves next to an exact drill circle (merged, the
            hole keeps its exact circle); None takes px.config.remove_faces_smaller.
        merge_small_faces_instead: False throws such faces away instead; None takes
            px.config.merge_small_faces_instead.
        remove_edges_smaller: removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.
        close_gaps_smaller: closes gaps between ends up to this; None takes px.config.close_gaps_smaller.
        remove_free_edges: removes the edges that touch no other edge at either end; None takes
            px.config.remove_free_edges.
        merge_collinear_closer: merges two lines whose common vertex is this close to the line through their far ends;
            None takes px.config.merge_collinear_closer.

    Returns:
        layer None: a dict of Shapes; a layer name: a Shape; a stackup: a netgen.occ shape.

    Raises:
        ValueError: a legacy board (before KiCad 6) or a file older than KiCad 9 (version 20241229): open and save it
            in KiCad 9 or later; kicad-cli could not convert the board; a layer name the board's layer table does not
            have; a stackup without an entry for the board's number of copper layers, with another number of
            thicknesses than copper layers - 1, or with a thickness not positive; px.stackups.kicad and the file has
            no stackup or one that does not fit; the board has no outline on Edge.Cuts where one is needed.
        RuntimeError: a board of another program and no kicad-cli of KiCad 10 or later can be run.

    Examples:
        px.FromKiCad("board.kicad_pcb", layer="F.Cu")
        px.FromKiCad("board.kicad_pcb", layer=px.stackups.jlc7628)
    """
    cleanup = {"remove_faces_smaller": remove_faces_smaller, "merge_small_faces_instead": merge_small_faces_instead,
               "remove_edges_smaller": remove_edges_smaller, "close_gaps_smaller": close_gaps_smaller,
               "remove_free_edges": remove_free_edges, "merge_collinear_closer": merge_collinear_closer}
    with open(path, "rb") as f:
        start = f.read(4096).lstrip()
    if start.startswith(b"PCBNEW-BOARD"):
        raise ValueError("FromKiCad(%r): a legacy KiCad board (before KiCad 6); open it in KiCad 9 or later and save it"
                         % (path,))
    if start.startswith(b"(kicad_pcb"):
        board = _Board(path)
    else:
        with tempfile.TemporaryDirectory() as folder:
            board = _Board(_converted(path, folder), path)
    if board.unfilled:
        warnings.warn("FromKiCad: %d zone%s not filled: fill the zones in KiCad (B) and save" %
                      (board.unfilled, "s" if board.unfilled > 1 else ""), px.PlanexWarning, stacklevel=2)
    if isinstance(layer, str) and layer not in board.order and layer != "Edge.Cuts":
        raise ValueError("FromKiCad(%r, layer=%r): the board's layers are %s" % (path, layer, ", ".join(board.order)))
    where = None if layer is None else [layer] if isinstance(layer, str) else board.copper
    counts = {}
    for name, found in board.left_out.items():
        if where is None or name in where:
            for kind, n in found.items():
                counts[kind] = counts.get(kind, 0) + n
    listed = ["%d %s" % (counts[kind], one if counts[kind] == 1 else many) for kind, one, many in _LEFT_OUT
              if counts.get(kind)]
    if listed:
        warnings.warn("FromKiCad: %s left out (not supported yet)" % ", ".join(listed), px.PlanexWarning, stacklevel=2)

    def cleaned(name):
        shape = board.shape(name)
        return shape.Simplify(**cleanup) if name in board.copper or name == "Edge.Cuts" else shape

    if layer is None:
        return {name: cleaned(name) for name in board.names()}
    if isinstance(layer, str):
        return cleaned(layer)
    if layer is stackups.kicad:
        layer = board.dielectrics()
    elif isinstance(layer, dict):
        n = len(board.copper)
        if n not in layer:
            counts = [str(k) for k in sorted(layer)]
            listed = " and ".join([", ".join(counts[:-1]), counts[-1]] if len(counts) > 1 else counts)
            raise ValueError("FromKiCad(%r, stackup): the stackup has entries for %s copper layers, the board has %d" %
                             (path, listed, n))
        layer = layer[n]
    return _stackup(board, [float(t) for t in layer], cleanup)


def _stackup(board, thicknesses, cleanup):
    n = len(board.copper)
    if len(thicknesses) != n - 1:
        raise ValueError("FromKiCad(%r, stackup): %d copper layers need %d dielectric thicknesses, got %d" %
                         (board.path, n, n - 1, len(thicknesses)))
    if any(not t > 0 for t in thicknesses):
        raise ValueError("FromKiCad(%r, stackup): the thicknesses must be positive" % (board.path,))
    z = [sum(thicknesses[k:]) * MM for k in range(n)]
    names = ["dielectric_%d" % (i + 1) for i in range(n - 1)]
    if all(set(board.copper) <= set(span) for _, span in board.holes):
        # every hole goes through the whole board: the copper is glued into the outline in planex and the dielectrics
        # are the outline extruded between these interfaces, without an OCC boolean
        region = board.profile()
        region.name = None
        interfaces = []
        for name in board.copper:
            copper = board.layer(name)
            interfaces.append(px.Glue([copper, region]) if copper.faces else region)
        return px._StackedPrisms(interfaces, region, z, names, **cleanup)
    return _glued_stackup(board, thicknesses, z, names, cleanup)


def _glued_stackup(board, thicknesses, z, names, cleanup):
    """blind and buried holes: the dielectrics drilled where a hole joins both their copper layers, glued with the
    copper sheets by OCC"""
    from netgen import occ

    sheets = []
    for k, name in enumerate(board.copper):
        copper = board.layer(name)
        if copper.faces:
            sheets.append(copper.Translated((0.0, 0.0, z[k])).ToOCCShape(**cleanup))
    solids = []
    for i, t in enumerate(thicknesses):
        prism = board.profile((board.copper[i], board.copper[i + 1]))
        prism.name = None
        solid = prism.Translated((0.0, 0.0, z[i + 1])).ToOCCShape(**cleanup).Extrude(t * MM)
        solid.mat(names[i])
        for f in solid.faces:
            f.col = COPPER_COL if f.name in (_PTH, _NPTH) else BOARD_COL
        solids.append(solid)
    return occ.Glue(sheets + solids)
