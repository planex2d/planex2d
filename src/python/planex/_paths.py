# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

from __future__ import annotations

import os


def with_extension(path: str | os.PathLike, extension: str) -> str:
    """path as a str, with the format's extension (".step") where its file name has none; another extension stays"""
    p = os.fspath(path)
    return p if os.path.splitext(p)[1] else p + extension
