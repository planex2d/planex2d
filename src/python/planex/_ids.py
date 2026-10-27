# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""ids for what the calls make: id= on every call that returns new objects, the kinds of the imports"""
import functools
import inspect
import re

from . import _planex as px

ID_TEXT = ("the id of what it returns (of a list's items id.1, id.2, ..., of a dict's id.key); default the kind that made "
           "it and a number, given when first asked for.")
ID_DOC = "\n\nid: " + ID_TEXT

# the calls that return new objects, per class
METHODS = {
    "Wire": ["Copy", "Rotated", "Translated", "Mirrored", "Scaled", "Face", "Offset", "Simplified", "Trim", "Filleted",
             "Chamfered"],
    "Face": ["Copy", "Rotated", "Translated", "Mirrored", "Scaled", "Offset", "Simplified", "Split", "Trim", "Filleted",
             "Chamfered"],
    "Shape": ["Copy", "Rotated", "Translated", "Mirrored", "Scaled", "Face", "Wire", "Offset", "Simplified", "Split",
              "Trim", "Filleted", "Chamfered"],
    "Sketch": ["Copy", "Rotated", "Translated", "Scaled", "Face", "Shape", "Wire", "Wires", "Construction"],
    "QuickSketch": ["Copy", "Rotated", "Translated", "Scaled", "Face", "Shape", "Wire", "Wires", "Construction"],
    "EdgeView": ["Wire", "Rotated", "Translated", "Mirrored", "Scaled"],
    "FaceView": ["Copy"],
}
FUNCTIONS = ["Line", "Arc", "Circle", "FromPolyline", "Spline", "BSpline", "Curve", "Wires", "Glue", "Unify", "Subtract", "Intersect",
             "Move"]
# the imports: their results count in a kind of their own
IMPORTS = {"FromDXF": "DXF", "FromKiCad": "KiCad", "FromOCC": "OCC", "Project": "OCC", "Intersect3d": "OCC",
           "FromShapely": "Shapely", "FromLevelSet": "LevelSet", "FromNACA": "NACA"}


def _each(obj, f):
    if isinstance(obj, dict):
        for x in obj.values():
            _each(x, f)
    elif isinstance(obj, (list, tuple)):
        for x in obj:
            _each(x, f)
    elif hasattr(obj, "parts") and not hasattr(obj, "_SetIdKind"):
        f(obj)
        for x in obj.parts:
            _each(x, f)
    else:
        f(obj)


def _named(obj, id):
    if isinstance(obj, dict):
        for key, x in obj.items():
            _named(x, "%s.%s" % (id, key))
    elif isinstance(obj, (list, tuple)):
        for k, x in enumerate(obj, 1):
            _named(x, "%s.%d" % (id, k))
    elif hasattr(obj, "_SetId"):
        obj._SetId(id)
    else:
        obj._id = id


def with_id(f, kind=None):
    """f with id=; kind: the kind its results' auto ids count in"""

    @functools.wraps(f)
    def call(*args, id=None, **kwargs):
        if id is not None:
            px._CheckUserId(id)
        r = f(*args, **kwargs)
        if kind is not None:
            _each(r, lambda x: x._SetIdKind(kind) if hasattr(x, "_SetIdKind") else None)
        if id is not None:
            _named(r, id)
        return r

    call.__doc__ = _with_id_doc(f.__doc__ or "")
    signature = inspect.signature(f) if inspect.isfunction(f) else None
    if signature is not None and "id" not in signature.parameters:
        parameters = list(signature.parameters.values())
        kinds = [p.kind for p in parameters]
        at = kinds.index(inspect.Parameter.VAR_KEYWORD) if inspect.Parameter.VAR_KEYWORD in kinds else len(parameters)
        parameters.insert(at, inspect.Parameter("id", inspect.Parameter.KEYWORD_ONLY, default=None, annotation="str | None"))
        call.__signature__ = signature.replace(parameters=parameters)
    return call


def _with_id_doc(doc):
    """doc with id=: in its signature lines (keyword-only) and under Args (the docstring form), else appended"""
    lines = inspect.cleandoc(doc).split("\n")
    if not any(line.strip() in ("Args:", "Raises:", "Examples:") for line in lines):
        return doc + ID_DOC
    k = 0
    while k < len(lines) and re.match(r"^[A-Za-z_]\w*\(.*\)( -> .+)?$", lines[k]):
        head, sep, tail = lines[k].rpartition(")")
        inside = head[head.index("(") + 1:]
        # a signature line that has id already (written by the C++ docstring) keeps it once
        if re.search(r"(^|, )id(:|=| =)", inside):
            k += 1
            continue
        if "**" in inside:
            head = head.replace("**", "id: str | None = None, **", 1)
        elif re.search(r"(^|, )\*($|,|\w)", inside):
            head += ", id: str | None = None"
        else:
            head += (", " if inside.strip() else "") + "*, id: str | None = None"
        lines[k] = head + sep + tail
        k += 1
    entry = "    id: " + ID_TEXT
    if any(line.startswith("    id:") for line in lines):
        return "\n".join(lines)
    if "Args:" in lines:
        at = lines.index("Args:") + 1
        while at < len(lines) and lines[at].startswith("    "):
            at += 1
        lines.insert(at, entry)
    else:
        at = next(i for i, line in enumerate(lines) if line.strip() in ("Raises:", "Examples:"))
        lines[at:at] = ["Args:", entry, ""]
    return "\n".join(lines)


def add_id_arguments(namespace):
    """id= on the calls of the classes and functions in namespace (the planex module's), the imports' kinds"""
    for cls, names in METHODS.items():
        c = namespace[cls] if cls in namespace else getattr(px, cls)
        for name in names:
            if name in c.__dict__:
                setattr(c, name, with_id(getattr(c, name)))
    for name in FUNCTIONS:
        if name in namespace:
            namespace[name] = with_id(namespace[name])
    for name, kind in IMPORTS.items():
        if name in namespace:
            namespace[name] = with_id(namespace[name], kind)
