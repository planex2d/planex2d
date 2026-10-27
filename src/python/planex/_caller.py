# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

import os
import sys
import warnings

PACKAGE = os.path.dirname(os.path.abspath(__file__)) + os.sep


def warn(message, category=UserWarning):
    """warnings.warn at the first frame outside the planex package, as the C++ warnings take it: the warning names the
    caller's line, whatever wrappers lie between"""
    # stacklevel 2 is the frame that called warn
    level, frame = 2, sys._getframe(1)
    while frame is not None and os.path.abspath(frame.f_code.co_filename).startswith(PACKAGE):
        level += 1
        frame = frame.f_back
    warnings.warn(message, category, stacklevel=level)
