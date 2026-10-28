# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

from ._planex import Auto, AutoType

__all__ = ["Auto", "AutoType", "is_auto"]


def is_auto(value, call, name):
    """True for px.Auto, the default worked out from the situation; None and px.Auto({...}) raise"""
    if value is None:
        raise TypeError(f"{call}({name}=None): None is no value here; the default px.Auto works it out")
    if isinstance(value, AutoType):
        if value.relations:
            raise TypeError(f"{call}({name}=px.Auto({{...}})): there is nothing to switch here; px.Auto alone works it out")
        return True
    return False
