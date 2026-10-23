# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""for debugging: planex's timers (Timers, ResetTimers), a trace of them (StartTrace, StopTrace) and CheckShape, OCC's
check of a shape"""
from ._planex import CheckShape, ResetTimers, StartTrace, StopTrace, Timers

__all__ = ["CheckShape", "ResetTimers", "StartTrace", "StopTrace", "Timers"]
