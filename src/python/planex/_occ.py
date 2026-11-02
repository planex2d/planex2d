# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""The OCC part of planex: its bindings add their functions to _planex and their methods to the core's classes. A
development build carries them itself (_planex_occ); else they come from planex2d-occ (the package planex_occ), built
for this planex2d and the installed netgen. Without them the names are stand-ins that raise naming what to install;
bindings that are there but do not fit the installed netgen or planex are left unloaded with a warning."""
import warnings
from importlib.metadata import PackageNotFoundError, version
from importlib.util import find_spec

from . import _planex

FUNCTIONS = ("CheckShape", "Intersect3d", "OCCGeometry", "Pad", "Pocket", "Project", "_Ax2d", "_FromOCC", "_OccBSpline",
             "_ShadedImage", "_Shading", "_StackedPrisms", "_UseSplineFit", "_ViewLines")
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
REBUILD = "pip install --no-build-isolation --force-reinstall planex2d"


def _installed_netgen():
    """the installed netgen-mesher's version, None for a netgen without pip's metadata"""
    try:
        return version("netgen-mesher")
    except PackageNotFoundError:
        return None


def _built_netgen():
    """the netgen a build from source compiled its OCC bindings against (planex/_occbuild.py), None without it"""
    try:
        from ._occbuild import NETGEN
    except ImportError:
        return None
    return NETGEN or None


def _load():
    """None once the OCC bindings are loaded, else why they are not and whether they are there but do not fit (a
    planex2d-occ without a module for the installed netgen or built for another planex2d, a build from source for
    another netgen)"""
    if find_spec("netgen") is None:
        return "netgen is not installed; " + INSTALL, False
    try:
        if find_spec("planex._planex_occ") is not None:
            built, installed = _built_netgen(), _installed_netgen()
            if built and installed and built != installed:
                return (f"this planex's OCC bindings are built for netgen {built}, netgen {installed} is installed: "
                        f"{REBUILD} builds them for it"), True
            import netgen.occ  # noqa: F401  (first: on Windows it loads the DLLs of netgen and OpenCASCADE)
            from . import _planex_occ  # noqa: F401
            return None, False
        if find_spec("planex_occ") is not None:
            import planex_occ  # noqa: F401  (its bindings for the installed netgen)
            return None, False
    except ImportError as e:
        return str(e), True
    return "planex2d-occ is not installed; " + INSTALL, False


why, unfit = _load()
loaded = why is None
if unfit:
    warnings.warn("planex's OCC integration is not loaded: " + why, _planex.PlanexWarning, stacklevel=2)
if not loaded:
    for name in FUNCTIONS:
        setattr(_planex, name, _stand_in(name, why))
    for cls, names in METHODS.items():
        if hasattr(_planex, cls):
            for name in names:
                setattr(getattr(_planex, cls), name, _stand_in(f"{cls}.{name}", why))
