# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""The bindings in two modules: the OCC module adds its functions to the core module and its methods to the core's
classes; without it planex has the same names, stand-ins that raise naming what is missing; with it the core's
settings, warnings and hooks are the OCC module's too."""
import json
import subprocess
import sys

import pytest

import planex as px
from planex import _occ

WITHOUT_OCC = "import sys\nsys.modules['netgen'] = sys.modules['ngsolve'] = sys.modules['planex._planex_occ'] = None\n"
NAMES = """
import json
import planex as px
names = {"px": [n for n in dir(px) if n != "_planex_occ"], "_planex": dir(px._planex)}
for cls in px._occ.METHODS:
    if hasattr(px._planex, cls):
        names[cls] = dir(getattr(px._planex, cls))
print(json.dumps(names))
"""


def _run(code):
    return subprocess.run([sys.executable, "-c", code], capture_output=True, text=True, check=True).stdout


@pytest.mark.occ
def test_the_occ_module_adds_its_functions_to_the_core_module_and_its_methods_to_the_classes():
    for name in _occ.FUNCTIONS:
        f = getattr(px._planex, name)
        assert type(f).__name__ == "builtin_function_or_method" and f.__module__ == "planex._planex", name
    for cls, names in _occ.METHODS.items():
        for name in names:
            assert type(vars(getattr(px._planex, cls))[name]).__name__ == "instancemethod", (cls, name)


@pytest.mark.occ
def test_without_the_occ_module_planex_has_the_same_names():
    assert json.loads(_run(WITHOUT_OCC + NAMES)) == json.loads(_run(NAMES))


@pytest.mark.subprocess
def test_without_the_occ_module_an_occ_call_says_what_is_missing():
    out = _run(WITHOUT_OCC + """
import planex as px
a = px.Face([px.FromPolyline([(0, 0), (1, 0), (1, 1)], closed=True)])
b = px.Face([px.FromPolyline([(0, 0), (1, 0), (1, 1)], closed=True, plane=px.XZ)])
for call in (lambda: px.Pad(a, 1), lambda: a.ToOCCFace(), lambda: px.XY.ToOCCAxes(), lambda: a.Distance(b)):
    try:
        call()
    except ImportError as e:
        print(type(e).__name__, e)
""")
    install = "pip install planex2d[occ] (planex2d-occ, with netgen)"
    missing = "netgen is not installed; " + install
    assert out.splitlines() == [
        "_OCCMissing Pad needs planex's OCC integration: " + missing,
        "_OCCMissing Face.ToOCCFace needs planex's OCC integration: " + missing,
        "_OCCMissing Plane.ToOCCAxes needs planex's OCC integration: " + missing,
        "ImportError a distance to something in another plane needs planex's OCC integration (netgen and OCC), which is "
        "not loaded; " + install]


@pytest.mark.occ
def test_a_warning_of_the_occ_module_is_a_planex_warning():
    import netgen.occ as occ
    with pytest.warns(px.PlanexWarning, match="1 point left out"):
        px.Project(occ.Segment(occ.Pnt(0, 0, 0), occ.Pnt(0, 0, 1)))


@pytest.mark.occ
def test_the_occ_exports_take_the_settings_of_px_config():
    w = px.FromPolyline([(0, 0), (1, 0), (1, 1e-4), (2, 1e-4)])
    before = px.config.remove_edges_smaller
    try:
        px.config.remove_edges_smaller = 1e-3
        coarse = len(w.ToOCCWire().edges)
    finally:
        px.config.remove_edges_smaller = before
    assert (len(w.ToOCCWire().edges), coarse) == (3, 2)


# netgen.occ first, as planex's loaders do: on Windows it loads the DLLs of netgen and OpenCASCADE the module needs
REFUSE = """
import sys
sys.modules['{module}'] = None
import planex
planex._planex.{name} = {value}
del sys.modules['{module}']
import netgen.occ
try:
    import {module}
except ImportError as e:
    print(e)
"""


def test_the_module_carries_the_installed_version():
    assert px._planex._version == px.__version__


@pytest.mark.occ
def test_the_occ_bindings_refuse_a_core_of_another_version_or_other_sources():
    version = px._planex._version
    module = "planex._planex_occ" if "planex._planex_occ" in sys.modules else "planex_occ"
    assert _run(REFUSE.format(module=module, name="_version", value='"9.9.9"')).strip() == (
        f"planex's OCC bindings are built for planex2d {version}, this planex2d is 9.9.9: pip install --upgrade "
        "planex2d[occ] installs the OCC bindings for it")
    assert _run(REFUSE.format(module=module, name="_layout", value="(1,)")).strip() == (
        f"planex's OCC bindings (planex2d {version}) are built from other sources than this planex2d's core: build both "
        "from the same sources")
