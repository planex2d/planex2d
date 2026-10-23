# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""Lengths in metres, planex's unit, to multiply with: ``sk.Line(40 * px.mm)``, ``face.mass / px.mm**2``.

A drawing itself has no unit; these factors only say which number means what. There is deliberately nothing below a
micrometre: exports to OCC work with absolute tolerances (1e-7), so geometry should stay at sizes around a metre."""

m = 1.0
cm = 1e-2
mm = 1e-3
um = 1e-6
inch = 0.0254
mil = 2.54e-5
