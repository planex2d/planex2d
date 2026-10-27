# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""The docstrings of the whole public API in their agreed form: the signature lines (calls of the compiled
module), the type line (properties), a summary, every argument under Args, Examples whose lines parse and call what
exists."""
import ast
import inspect
import re

import pytest

import planex as px

def _public():
    """(owner, name, object, is a property, is a member) of every public call and property"""
    out = []
    for name in px.__all__:
        obj = getattr(px, name)
        if inspect.isclass(obj):
            init = obj if inspect.isfunction(getattr(obj, "__init__", None)) else obj.__init__
            # a view has no constructor of its own: pybind's or Python's default text
            doc = init.__doc__ or ""
            if "__init__" in vars(obj) and doc and "No constructor defined" not in doc and not doc.startswith("Initialize self."):
                out.append((name, name, init, False, True))
            for attr, value in vars(obj).items():
                # an enum's members and pybind's name/value carry no docstring of their own
                if attr.startswith("_") or isinstance(value, obj) or hasattr(obj, "__members__") and attr in ("name", "value"):
                    continue
                inner = value.fget if isinstance(value, property) else inspect.unwrap(value) if callable(value) else value
                # the stand-ins of a planex without its OCC module have no docstring of their own
                if getattr(inner, "__module__", None) == "planex._occ":
                    continue
                # a call a Python module attaches belongs to that module
                owner = inner.__module__ if inspect.isfunction(inner) and inner.__module__ != obj.__module__ else name
                out.append((owner, attr, value, isinstance(value, property) or type(value).__name__ == "property", True))
        # px.Auto is a value; its call, px.Auto({...}), has its docstring on its type
        elif callable(obj) and obj.__module__ != "planex._occ" and obj is not px.Auto:
            out.append((obj.__module__, name, obj, False, False))
    out.append(("planex._planex", "__call__", type(px.Auto).__call__, False, True))
    return out


ENTRIES = _public()
KNOWN = {a for n in px.__all__ for a in dir(getattr(px, n))} | set(px.__all__) | set(dir(px.measurements))


def _sections(doc, name=None):
    """the docstring's signature lines (of a property named name: its type line), summary and sections ({"Args": [...], ...})"""
    lines = inspect.cleandoc(doc).splitlines()
    signatures = []
    if name and lines and lines[0].startswith(name + ": "):
        signatures.append(lines.pop(0))
    while lines and re.match(r"^[A-Za-z_]\w*\(.*\)( -> .+)?$", lines[0]):
        signatures.append(lines.pop(0))
    while lines and not lines[0].strip():
        lines.pop(0)
    summary = lines.pop(0) if lines else ""
    sections, current = {}, None
    for line in lines:
        if re.match(r"^(Args|Returns|Raises|Examples):$", line):
            current = sections.setdefault(line[:-1], [])
        elif current is not None and line.strip():
            current.append(line)
    return signatures, summary, sections


def _split(text):
    """text split at the commas outside brackets"""
    parts, depth, start = [], 0, 0
    for k, c in enumerate(text):
        depth += c in "[(" and 1 or (-1 if c in "])" else 0)
        if c == "," and depth == 0:
            parts.append(text[start:k])
            start = k + 1
    return parts + [text[start:]]


def _arguments(signature):
    inside = signature[signature.index("(") + 1:signature.rindex(")")]
    names = []
    for part in _split(inside):
        part = part.strip()
        if part in ("", "/", "*"):
            continue
        names.append(re.split(r"[:=]", part)[0].strip().lstrip("*"))
    return names


@pytest.mark.parametrize("owner, name, obj, is_property, member", ENTRIES, ids=[f"{e[0]}.{e[1]}" for e in ENTRIES])
def test_a_docstring_has_its_form(owner, name, obj, is_property, member):
    doc = getattr(obj, "__doc__", None)
    assert doc, f"{owner}.{name}: no docstring"
    signatures, summary, sections = _sections(doc, name if is_property else None)
    assert summary and summary[0].isupper() and summary.rstrip().endswith("."), f"{owner}.{name}: the summary is one sentence"
    if is_property:
        # its type on the first line, or a Python getter's return annotation
        getter = getattr(obj, "fget", None)
        annotated = inspect.isfunction(getter) and "return" in getattr(getter, "__annotations__", {})
        assert signatures or annotated, f"{owner}.{name}: no type line ({name}: type)"
        return
    function = inspect.unwrap(obj.__init__ if inspect.isclass(obj) else obj)
    if not inspect.isfunction(function):
        assert signatures and all(s.startswith(name + "(") for s in signatures), f"{owner}.{name}: no signature line"
    if signatures:
        arguments = {a for s in signatures for a in _arguments(s)}
    else:
        parameters = list(inspect.signature(function).parameters)
        # a function attached to a class takes the object first, under any name
        if member and "." not in function.__qualname__:
            parameters = parameters[1:]
        arguments = {p for p in parameters if p not in ("self", "args", "kwargs")}
    described = {re.split(r"[ :(]", line.strip())[0].lstrip("*") for line in sections.get("Args", []) if not line.startswith("        ")}
    assert arguments <= described, f"{owner}.{name}: Args lacks {sorted(arguments - described)}"
    examples = sections.get("Examples", [])
    assert examples, f"{owner}.{name}: no example"
    for line in examples:
        code = line.strip()
        try:
            ast.parse(code)
        except SyntaxError as e:
            raise AssertionError(f"{owner}.{name}: the example {code!r} does not parse") from e
        for called in re.findall(r"\.([A-Za-z_]\w*)\(", code):
            assert called in KNOWN, f"{owner}.{name}: the example calls {called}, which planex does not have"
