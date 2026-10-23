# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""The frozen public API: every public name and its signature as help() shows it, against tests/data/public_api.json.
A change to the API is made on purpose: PLANEX_WRITE_API=1 rewrites the file, and the diff shows what changed."""
import inspect
import json
import os
import re
from pathlib import Path

import planex as px

DATA = Path(__file__).parent / "data" / "public_api.json"


def _signature(obj):
    call = obj.__init__ if inspect.isclass(obj) else obj
    function = inspect.unwrap(call)
    doc = inspect.cleandoc(getattr(call, "__doc__", None) or "")
    lines = []
    for line in doc.splitlines():
        if not re.match(r"^[A-Za-z_]\w*\(.*\)( -> .+)?$", line):
            break
        lines.append(line)
    if lines:
        return lines
    try:
        text = str(inspect.signature(function if inspect.isclass(obj) else obj))
    except (TypeError, ValueError):
        return ["(no signature)"]
    return [re.sub(r" at 0x[0-9a-f]+", "", text)]


def _entry(value):
    if isinstance(value, property) or type(value).__name__ == "property":
        return "property"
    if callable(value):
        return _signature(value)
    return "value"


def _api():
    api = {}
    for module, names in (("px", px.__all__), ("px.measurements", px.measurements.__all__)):
        for name in names:
            obj = getattr(px if module == "px" else px.measurements, name)
            key = module + "." + name
            if inspect.isclass(obj):
                api[key] = _signature(obj)
                for attr, value in sorted(vars(obj).items()):
                    if not attr.startswith("_"):
                        api[key + "." + attr] = "member" if isinstance(value, obj) else _entry(value)
            else:
                api[key] = _entry(obj)
    return api


def test_the_public_api_is_the_frozen_one():
    api = _api()
    if os.environ.get("PLANEX_WRITE_API"):
        DATA.write_text(json.dumps(api, indent=1, sort_keys=True) + "\n")
    frozen = json.loads(DATA.read_text())
    added = sorted(set(api) - set(frozen))
    removed = sorted(set(frozen) - set(api))
    changed = sorted(k for k in set(api) & set(frozen) if api[k] != frozen[k])
    assert not (added or removed or changed), f"added {added}, removed {removed}, changed {changed}"
