# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""The OCC part of planex: its bindings add their functions to _planex and their methods to the core's classes. A
development build carries them itself (_planex_occ); else they come from planex2d-occ (the package planex_occ), built
for this planex2d and the installed netgen. Without them the names are stand-ins that raise naming what to install."""
from importlib.util import find_spec

from . import _planex

FUNCTIONS = ("CheckShape", "Intersect3d", "OCCGeometry", "Pad", "Pocket", "Project", "_Ax2d", "_FromOCC", "_OccBSpline",
             "_StackedPrisms", "_UseSplineFit")
_EXPORTS = ("ToOCCFace", "ToOCCShape", "ToOCCWire", "ToOCCWires")
METHODS = {"Face": _EXPORTS + ("Project",), "Shape": ("Project", "ToOCCFace", "ToOCCShape", "ToOCCWires"),
           "Wire": _EXPORTS + ("Project",), "Plane": ("ToOCCAxes",), "QuickSketch": _EXPORTS + ("Intersect3d", "Project"),
           "Sketch": _EXPORTS + ("Intersect3d", "Project")}


class _OCCMissing(ImportError):
    """A call that needs planex's OCC integration, which is not loaded."""


def _stand_in(name, why):
    def missing(*args, **kwargs):
        raise _OCCMissing(f"{name} needs planex's OCC integration: {why}")
    missing.__name__ = name.rpartition(".")[2]
    missing.__qualname__ = name
    return missing


INSTALL = "pip install planex2d[occ] (planex2d-occ, with netgen)"


def _load():
    """None once the OCC bindings are loaded, else why they are not; a module that is there but does not fit raises"""
    if find_spec("netgen") is None:
        return "netgen is not installed; " + INSTALL
    if find_spec("planex._planex_occ") is not None:
        from . import _planex_occ  # noqa: F401
        return None
    if find_spec("planex_occ") is not None:
        import planex_occ  # noqa: F401  (its bindings for the installed netgen)
        return None
    return "planex2d-occ is not installed; " + INSTALL


why = _load()
loaded = why is None
if not loaded:
    for name in FUNCTIONS:
        setattr(_planex, name, _stand_in(name, why))
    for cls, names in METHODS.items():
        if hasattr(_planex, cls):
            for name in names:
                setattr(getattr(_planex, cls), name, _stand_in(f"{cls}.{name}", why))
