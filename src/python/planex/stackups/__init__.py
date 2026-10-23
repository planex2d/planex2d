# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""PCB stackups for the 3d board of FromKiCad(path, stackup).

A stackup is a dict {number of copper layers: [dielectric thicknesses in millimetres between neighbouring copper
layers, from the top]}; FromKiCad takes the entry for the board's number of copper layers and raises if there is none.
The thicknesses are the dielectric alone (the prepreg sheets between two copper layers added, the core as given):
FromKiCad puts the copper layers as sheets between the dielectrics, without a thickness of their own.

kicad: the stackup the board file holds itself (KiCad's Board Setup, Physical Stackup).

The manufacturers' stackups (jlc7628, ... from JLCPCB) name their source and the day it was read; check them against
your order."""

from ._jlcpcb import *  # noqa: F401,F403
from . import _jlcpcb


class _BoardStackup:
    def __repr__(self):
        return "px.stackups.kicad"


kicad = _BoardStackup()
"""the stackup in the board file: the thicknesses of its dielectric layers (with their sublayers) between the copper
layers"""

__all__ = ["kicad"] + _jlcpcb.__all__
