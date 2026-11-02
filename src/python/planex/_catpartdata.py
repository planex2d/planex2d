# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

""".CATPart files read into plain data with Python's standard library only: read(path) returns dicts and lists, lengths
in millimeters, angles in degrees."""


from __future__ import annotations

import struct

MAGIC = b"V5_CFV2\0"
SELECTOR_BASE = 0x34
CONSTRAINT_KINDS = {0x20: "fix", 0x21: "distance", 0x22: "coincidence", 0x23: "concentric", 0x24: "tangent",
                    0x25: "length", 0x26: "angle", 0x28: "parallel", 0x2A: "perpendicular", 0x2C: "radius",
                    0x2D: "symmetry", 0x2E: "equidistant", 0x31: "horizontal", 0x32: "vertical", 0x33: "midpoint"}
_KIND_AT = 25
_LIST_TYPES = {3864, 3867, 4123, 4876, 4888, 4891}
_NUMBERS = 4872
_FRAME = 3848


class CATPartError(ValueError):
    """a file that is no .CATPart, or one this reader cannot follow"""


def read(path) -> dict:
    """The sketches, parameters and formulas of a .CATPart file.

    Returns:
        {"sketches": [sketch], "parameters": [parameter], "formulas": [formula]}, where
        sketch = {"id", "name", "origin", "h", "v", "elements": [element], "constraints": [constraint]} (origin, h, v the
        sketch plane in 3d, mm); element = {"id", "kind" ("point", "line", "circle", "arc", "spline", "axis"),
        "construction", ...}: a point "at" (h, v); a line "start", "end" (point ids); a circle "center" (a point id),
        "r"; an arc also "start", "end", "ccw"; a spline "points" (ids), "degree", "closed", "tangents" ({index of a
        point: its arrow}), "curvatures" ({index: 1 / the radius, a left turn positive}, only where a tangent is); an axis
        "role" ("origin", "h", "v"); constraint = {"id", "kind", "elements" (ids, as stored), "value", "parameter", "reference"};
        parameter = {"id", "name", "type", "value", "formula"}; formula = {"id", "text", "inputs" (parameter ids for #1_,
        #2_, ...), "output" (a parameter id), "signature"}.

    Raises:
        CATPartError: not a .CATPart file, or one this reader cannot follow.
    """
    with open(path, "rb") as f:
        data = f.read()
    stream = _feature_stream(data)
    names = _catalog(stream)
    objects = _objects(stream, names)
    return _model(objects, names)


def _u32be(data, at):
    return struct.unpack_from(">I", data, at)[0]


def _streams(data):
    if data[:8] != MAGIC:
        raise CATPartError("not a .CATPart file")
    start, length = _u32be(data, 8), _u32be(data, 12)
    out = []
    for ds in range(start, start + length - 0x54):
        k, total = _u32be(data, ds + 0x50), _u32be(data, ds + 0x0C)
        if not 0 < k < 4096 or ds + 0x54 + 20 * k > start + length:
            continue
        extents, log = [], 0
        for i in range(k):
            phys, plen, llen, loff, _ = struct.unpack_from(">5I", data, ds + 0x54 + 20 * i)
            if plen == 0 or llen != plen or loff != log or phys + plen > len(data):
                break
            extents.append((phys, plen))
            log += llen
        else:
            if log == total:
                out.append((_name_before(data, ds), b"".join(data[p:p + n] for p, n in extents)))
    return out


def _name_before(data, ds):
    if data[ds - 3:ds] != b"\0\0\0":
        return ""
    chars, i = [], ds - 3
    while i >= 2 and data[i - 1] == 0 and 32 <= data[i - 2] < 127:
        chars.append(chr(data[i - 2]))
        i -= 2
    return "".join(reversed(chars))


def _feature_stream(data):
    for name, stream in _streams(data):
        if name[1:].startswith("Cont_") and b"MechanicalPart" in stream and b"\x7c\x02" in stream:
            return stream
    raise CATPartError("no part features in the file (is it a .CATPart?)")


def _atom(b, i):
    c = b[i]
    if 0x80 <= c <= 0xD0:
        return c - 0x80, i + 1
    if 0xD1 <= c <= 0xE4 and i + 1 < len(b):
        return (c - 0xD1) * 256 + b[i + 1] + 1, i + 2
    return None


def _catalog(stream):
    at = 0
    while (at := stream.find(b"\x7c\x02", at)) >= 0:
        total = struct.unpack_from("<I", stream, at + 2)[0]
        frame = stream[at:at + total]
        if b"MechanicalPart" in frame and _atom(stream, at + 6):
            count, i = _atom(stream, at + 6)
            names = []
            while i < at + total and len(names) < count - 1:
                n = stream[i]
                names.append(stream[i + 1:i + n].decode("utf-8", "replace"))
                i += n
            return names
        at += 2
    raise CATPartError("no class catalog in the part's feature stream")


def _frames(stream, marker):
    out, at = [], 0
    while (at := stream.find(marker, at)) >= 0:
        total = struct.unpack_from("<I", stream, at + 2)[0]
        rec = stream[at:at + total]
        inner = b"\x7c\x06" if marker == b"\x7c\x05" else b"\x7c\x0a"
        if 6 < total < 1 << 20 and rec.find(inner) > 0:
            out.append((at, rec))
            at += total
        else:
            at += 2
    return out


def _objects(stream, names):
    entities = [e for e in _frames(stream, b"\x7c\x05") if e[1].find(b"\x7c\x06") == 7]
    graph = _frames(stream, b"\x7c\x09")
    graph = graph[len(graph) - len(entities):] if len(graph) >= len(entities) else [(0, b"")] * len(entities)
    objects = {}
    for (_, rec), (_, grec) in zip(entities, graph):
        d = rec.find(b"\x7c\x06")
        dlen = struct.unpack_from("<I", rec, d + 2)[0]
        definition = rec[d + 6:d + dlen]
        v = rec.find(b"\x7c\x07", d + dlen - 1)
        vlen = struct.unpack_from("<I", rec, v + 2)[0]
        value, suffix = rec[v + 6:v + vlen], rec[v + vlen:]
        ea = 4 if definition[4:5] == b"\xea" else definition.find(b"\xea", 4)
        if ea < 0 or len(definition) < ea + 5:
            continue
        ident = struct.unpack_from("<I", definition, ea + 1)[0]
        fields = _value_fields(value, names)
        p = grec.find(b"\x7c\x0a")
        payload = grec[p + 6:p + struct.unpack_from("<I", grec, p + 2)[0]] if p > 0 else b""
        objects[ident] = {"id": ident, "lead": rec[6], "class": _name(names, definition[2]),
                          "role": _name(names, definition[3]), "fields": fields, "suffix": suffix,
                          "graph": _graph_fields(payload, names, fields)}
    return objects


def _name(names, c):
    k = c - SELECTOR_BASE
    return names[k] if 0 <= k < len(names) else ""


def _value_fields(value, names):
    out, i = {}, 0
    while i < len(value) and value[i] != 0xFE:
        name = _name(names, value[i])
        if i + 6 <= len(value) and value[i + 1] == 0x80:
            i += 6
        else:
            a = _atom(value, i + 1) if i + 1 < len(value) else None
            if not a or a[1] >= len(value):
                break
            i = a[1]
        typ = None
        if value[i] == 0xE8 and i + 1 < len(value) and _atom(value, i + 1):
            typ, i = _atom(value, i + 1)
        elif value[i] == 0xE9:
            a = _atom(value, i + 1) if i + 1 < len(value) else None
            typ = a[0] if a else None
            j = value.find(b"\x37", i + 1, i + 6)
            i = j if j > 0 else i + 1
        if i < len(value) and value[i] == 0x37:
            i += 1
        body, i = _body(value, i, names)
        if i < len(value) and value[i] == 0xFE:
            i += 1
        out.setdefault(name, (typ, body))
    return out


def _body(b, i, names):
    out = []
    while i < len(b) and b[i] != 0xFE:
        c = b[i]
        if c == 0x80 and i + 5 <= len(b):
            out.append(("word", struct.unpack_from("<I", b, i + 1)[0], ""))
            i += 5
        elif c == 0xE6 and i + 9 <= len(b):
            out.append(struct.unpack_from("<d", b, i + 1)[0])
            i += 9
        elif c == 0xE8:
            out.append(0.0)
            i += 1
        elif c == 0xE7:
            out.append(1.0)
            i += 1
        elif c == 0xE9:
            out.append(-1.0)
            i += 1
        elif _atom(b, i):
            n, j = _atom(b, i)
            out.append(("atom", n, _name(names, c) if j == i + 1 else ""))
            i = j
        else:
            out.append(("byte", c, _name(names, c)))
            i += 1
    return out, i + 1


def _graph_fields(payload, names, fields):
    out, i = {}, 0
    while i < len(payload) and payload[i] != 0xFE:
        name = _name(names, payload[i])
        i += 1
        if i >= len(payload):
            break
        if payload[i] == 0xE5:
            n = struct.unpack_from("<I", payload, i + 1)[0]
            out[name] = payload[i + 5:i + 5 + n]
            i += 5 + n
            if i < len(payload) and payload[i] != 0xFE and _atom(payload, i):
                i = _atom(payload, i)[1]
            continue
        a = _atom(payload, i)
        if not a:
            i += 1
            continue
        n, j = a
        typ = fields.get(name, (None,))[0]
        listed = _list(payload, j, n) if typ in _LIST_TYPES or typ is None else None
        if listed:
            out[name], i = listed
        else:
            out[name] = n - 1
            i = j
    return out


def _separator(b, j):
    if j < len(b) and b[j] == 0x81:
        return j + 1
    if j + 5 <= len(b) and b[j] == 0x80:
        return j + 5
    return None


def _list(b, j, n):
    ids = []
    for _ in range(n - 1):
        j = _separator(b, j)
        if j is None or j >= len(b):
            return None
        if b[j] == 0x05:
            j += 1
        a = _atom(b, j) if j < len(b) else None
        if not a:
            return None
        ids.append(a[0] - 1)
        j = a[1]
    end = _separator(b, j)
    return (ids, end) if end is not None and n >= 1 else None


def _numbers(obj, field="DataBloc"):
    body = obj["fields"].get(field, (None, []))[1]
    for k in range(len(body) - 1):
        if isinstance(body[k], tuple) and body[k][0] == "atom" and body[k + 1] == ("atom", 1, body[k + 1][2] if isinstance(body[k + 1], tuple) else ""):
            return [x for x in body[k + 2:] if isinstance(x, float)]
    return [x for x in body if isinstance(x, float)]


def _atom_value(obj, field):
    body = obj["fields"].get(field, (None, []))[1]
    atoms = [x[1] for x in body if isinstance(x, tuple) and x[0] == "atom"]
    return atoms[-1] if atoms else None


def _string(obj, field, names):
    body = obj["fields"].get(field, (None, []))[1]
    for x in body:
        if isinstance(x, tuple) and x[2]:
            return x[2]
    return None


def _suffix_value(obj):
    s = obj["suffix"]
    if len(s) >= 13 and s[4] == 0xE6:
        return struct.unpack_from("<d", s, 5)[0]
    return {0xE8: 0.0, 0xE7: 1.0, 0xE9: -1.0}.get(s[4]) if len(s) > 4 else None


def _model(objects, names):
    parameters, formulas = [], []
    relations = {}
    for o in objects.values():
        if o["class"] == "RelationExpFct":
            g = o["graph"]
            expr = objects.get(g.get("expression"))
            out = g.get("paramout")
            formulas.append({"id": o["id"], "text": _string(expr, "body", names) if expr else None,
                             "signature": _string(expr, "param", names) if expr else None,
                             "inputs": g.get("paramin", []) if isinstance(g.get("paramin"), list) else [g.get("paramin")],
                             "output": out[0] if isinstance(out, list) and out else out})
            relations[o["id"]] = formulas[-1]
    for o in objects.values():
        if o["lead"] == 2 and o["class"] in ("LENGTH", "ANGLE", "Real", "Integer", "Boolean", "String"):
            valby = o["graph"].get("Valby")
            parameters.append({"id": o["id"], "name": _string(o, "name", names), "type": o["class"],
                               "value": _suffix_value(o), "formula": valby if valby in relations else None,
                               "measured": valby is not None and valby not in relations})
    by_id = {p["id"]: p for p in parameters}
    sketches = []
    for o in objects.values():
        if o["class"] == "PRTSketch":
            sketches.append(_sketch(o, objects, by_id))
    features = []
    for o in objects.values():
        if o["class"] == "HybridBody" and isinstance(o["graph"].get("_FtrList"), list):
            features += [{"id": x, "class": objects[x]["class"]} for x in o["graph"]["_FtrList"]
                         if x in objects and objects[x]["class"] != "PRTSketch"]
    return {"sketches": sketches, "parameters": [p for p in parameters if p["name"]], "formulas": formulas,
            "features": features, "dimension_parameters": parameters}


def _sketch(sk, objects, parameters):
    members = sk["graph"].get("_FtrList", [])
    members = members if isinstance(members, list) else [members]
    out = {"id": sk["id"], "name": _string(sk, "MMAlias", None), "origin": (0.0, 0.0, 0.0), "h": (1.0, 0.0, 0.0),
           "v": (0.0, 1.0, 0.0),
           "elements": [], "constraints": []}
    seen = set()
    for m in members:
        o = objects.get(m)
        if not o:
            continue
        if o["class"] == "SketchSupport":
            frame = [x for x in o["fields"].get("DataBloc", (None, []))[1][2:] if isinstance(x, float)]
            if len(frame) >= 9:
                out["h"], out["v"], out["origin"] = tuple(frame[0:3]), tuple(frame[3:6]), tuple(frame[6:9])
            for axis in o["graph"].get("_FtrList", []) if isinstance(o["graph"].get("_FtrList"), list) else []:
                a = objects.get(axis)
                if a and a["class"].startswith("2DAxis_"):
                    role = {"2DAxis_Origin": "origin", "2DAxis_HDirection": "h", "2DAxis_VDirection": "v"}[a["class"]]
                    out["elements"].append({"id": axis, "kind": "axis", "role": role, "construction": True})
                    seen.add(axis)
        elif o["class"].startswith("2D") and o["class"] != "2DFixTogether":
            _element(o, objects, out, seen)
        elif o["class"] == "R7ProjectedGeometrySet":
            items = o["graph"].get("_FtrList", [])
            for q in items if isinstance(items, list) else [items]:
                if q in objects and objects[q]["class"] == "R7Imported3DGeometry":
                    _projected(objects[q], objects, out, seen)
        elif o["class"] == "ConstraintDYS":
            out["constraints"].append(_constraint(o, objects, parameters))
        elif o["class"] == "2DFixTogether":
            members = o["graph"].get("elts", [])
            out["constraints"].append({"id": o["id"], "kind": "fix together", "parameter": None, "value": None,
                                       "elements": members if isinstance(members, list) else [members], "reference": False})
    return out


def _element(o, objects, sketch, seen):
    if o["id"] in seen:
        return
    seen.add(o["id"])
    g, cls = o["graph"], o["class"]
    flags = [x[1] for x in o["fields"].get("MngFlgs", (None, []))[1] if isinstance(x, tuple) and x[0] == "byte"]
    e = {"id": o["id"], "name": _string(o, "MMAlias", None), "construction": bool(flags and flags[-1] & 3)}
    for ref in ("StartPoint", "EndPoint", "Center"):
        if isinstance(g.get(ref), int) and g[ref] in objects:
            _element(objects[g[ref]], objects, sketch, seen)
    n = _numbers(o)
    if cls in ("2DPoint", "2DCstPoint"):
        e.update(kind="point", at=(n[0], n[1]) if len(n) >= 2 else (0.0, 0.0))
    elif cls == "2DLine":
        e.update(kind="line", start=g.get("StartPoint"), end=g.get("EndPoint"))
    elif cls == "2DCircle":
        arc = isinstance(g.get("StartPoint"), int)
        e.update(kind="arc" if arc else "circle", center=g.get("Center"), r=n[3] if len(n) > 3 else None)
        if arc:
            e.update(start=g.get("StartPoint"), end=g.get("EndPoint"), ccw=(n[6] if len(n) > 6 else 1.0) > 0)
    elif cls == "2DEllipse":
        e.update(kind="ellipse", center=g.get("Center"), angle=n[2] if len(n) > 2 else 0.0, a=n[3] if len(n) > 3 else None,
                 b=n[4] if len(n) > 4 else None)
    elif cls == "2DSplineCurve":
        pts = next((v for v in g.values() if isinstance(v, list)), [])
        for q in pts:
            if q in objects:
                _element(objects[q], objects, sketch, seen)
        degree = [x[1] for x in o["fields"].get("Degree", (None, []))[1] if isinstance(x, tuple)]
        closed = [x[1] for x in o["fields"].get("Closed", (None, []))[1] if isinstance(x, tuple)]
        tangents, curvatures = {}, {}
        for k, q in enumerate(pts):
            g = objects[q]["graph"] if q in objects else {}
            listed = g.get("_FtrList", [])
            for t in listed if isinstance(listed, list) else [listed]:
                if t in objects and objects[t]["class"] == "2DCstPoint_Tangent":
                    tangents[k] = tuple(_numbers(objects[t])[2:4])
            c = g.get("CurvaturePara")
            if k in tangents and isinstance(c, int) and c in objects and _suffix_value(objects[c]) is not None:
                curvatures[k] = _suffix_value(objects[c])
        e.update(kind="spline", points=pts, degree=degree[-1] - 1 if degree else None, closed=bool(closed and closed[-1] == 2),
                 tangents=tangents, curvatures=curvatures)
    else:
        e.update(kind=cls)
    sketch["elements"].append(e)


_PROJECTED = {2: "point", 3: "line"}


def _projected(o, objects, sketch, seen):
    if o["id"] in seen:
        return
    seen.add(o["id"])
    kind = _PROJECTED.get(_atom_value(o, "DataType"))
    g = o["graph"]
    e = {"id": o["id"], "construction": True, "projected": True}
    if kind == "point":
        n = _numbers(o)
        e.update(kind="point", at=(n[0], n[1]) if len(n) >= 2 else (0.0, 0.0))
    elif kind == "line":
        for ref in ("StartPoint", "EndPoint"):
            if isinstance(g.get(ref), int) and g[ref] in objects:
                _element(objects[g[ref]], objects, sketch, seen)
                sketch["elements"][-1]["projected"] = True
        e.update(kind="line", start=g.get("StartPoint"), end=g.get("EndPoint"))
    else:
        e.update(kind="unread projection", type=_atom_value(o, "DataType"))
    sketch["elements"].append(e)


def _constraint(o, objects, parameters):
    g = o["graph"]
    block = g.get("ftype")
    code = block[_KIND_AT] if isinstance(block, (bytes, bytearray)) and len(block) > _KIND_AT else None
    kind = CONSTRAINT_KINDS.get(code, "unknown 0x%02x" % code if code is not None else "unknown")
    values = g.get("values", [])
    pid = values[0] if isinstance(values, list) and values else None
    p = parameters.get(pid, {})
    value = p.get("value")
    size = objects.get(g.get("CstAttr_Dimension"))
    if kind == "radius" and size and len(size["suffix"]) >= 3 and size["suffix"][-3] == 0x82:
        kind, value = "diameter", None if value is None else 2.0 * value
    elements = g.get("elts", [])
    return {"id": o["id"], "name": _string(o, "MMAlias", None), "kind": kind,
            "elements": elements if isinstance(elements, list) else [elements], "parameter": pid, "value": value,
            "reference": bool(p.get("measured"))}
