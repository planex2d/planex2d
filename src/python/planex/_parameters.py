# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later


from __future__ import annotations

import math
import numbers
import weakref
from typing import TYPE_CHECKING

from . import _planex

if TYPE_CHECKING:
    from ._planex import Sketch


def _values(call, values, keywords):
    """the parameters given as a dict and as keywords, each name checked against the id rules and each value a number"""
    if values is not None and not isinstance(values, dict):
        raise TypeError(f"{call}: the parameters as a dict or as keywords, not {values!r}")
    out = dict(values or {})
    for name in keywords:
        if name in out:
            raise ValueError(f"{call}: {name!r} is given twice")
    out.update(keywords)
    for name, value in out.items():
        if not isinstance(name, str):
            raise TypeError(f"{call}: a parameter's name is a str, not {name!r}")
        try:
            _planex._CheckUserId(name)
        except ValueError as e:
            raise ValueError(f"{call}: {e}") from None
        if isinstance(value, bool) or not isinstance(value, numbers.Real) or not math.isfinite(value):
            raise ValueError(f"{call}: {name} = {value!r}: a parameter is a finite number")
        out[name] = float(value)
    return out


class Parameters:
    """Named values several sketches share: a sketch that uses the set (sk.Use) names them in its expressions and curve formulas, and Update sets them in all of those sketches at once.

    Inside a sketch its dimensions stay its parameters; a global parameter is set only by Update, and a sketch dimension
    or element may not have a parameter's name.

    Args:
        values: the parameters as a dict, {"th": 3, "w": 40}.
        kwargs: the parameters as keywords, th=3, w=40.

    Raises:
        TypeError: values is no dict, a name is no str.
        ValueError: a name the id rules refuse (h, v and t are reserved), a value that is no finite number, a name given
            twice.

    Examples:
        p = px.Parameters(th=3, w=40)
        p = px.Parameters({"th": 3, "w": 40})
    """

    def __init__(self, values: dict[str, float] | None = None, /, **kwargs: float) -> None:
        self._values = _values("Parameters", values, kwargs)
        self._users = []

    def __getitem__(self, name):
        return self._values[name]

    def __iter__(self):
        return iter(self._values)

    def __len__(self):
        return len(self._values)

    def __contains__(self, name):
        return name in self._values

    def __repr__(self):
        return "Parameters(" + ", ".join(f"{name}={value:g}" for name, value in self._values.items()) + ")"

    def _sketches(self):
        """the sketches that use the set and still exist"""
        self._users = [ref for ref in self._users if ref() is not None]
        return [ref() for ref in self._users]

    def _add(self, sketch):
        if all(s is not sketch for s in self._sketches()):
            self._users.append(weakref.ref(sketch))

    def Update(self, values: dict[str, float] | None = None, /, **kwargs: float) -> Parameters:
        """Set parameters to new values and solve every sketch that uses the set with them.

        If one of those sketches has no solution with the new values, all of them stay as they were and the set keeps
        its values.

        Args:
            values: the new values as a dict, {"th": 4}.
            kwargs: the new values as keywords, th=4.

        Raises:
            TypeError: values is no dict.
            ValueError: a name the set does not have, a value that is no finite number; a sketch with no solution (named
                in the message).

        Examples:
            p.Update(th=4)
            p.Update({"th": 4, "w": 45})
        """
        new = _values("Update", values, kwargs)
        unknown = [name for name in new if name not in self._values]
        if unknown:
            raise ValueError(f"Update: no parameter {', '.join(map(repr, unknown))}; the set has {', '.join(self._values) or 'none'}")
        old = {name: self._values[name] for name in new}
        done = []
        for sketch in self._sketches():
            try:
                sketch._UpdateParameters(new)
            except ValueError as e:
                for s in reversed(done):
                    s._UpdateParameters(old)
                raise ValueError(f"Update: {sketch.id}: {e}; every sketch stays as it was") from None
            done.append(sketch)
        self._values.update(new)
        return self


def use(sketch: Sketch, parameters: Parameters, /) -> Sketch:
    """Use a set of global parameters: the sketch names them in its expressions and curve formulas, and the set's Update sets them here too.

    Args:
        parameters: the px.Parameters set.

    Raises:
        TypeError: parameters is no px.Parameters.
        ValueError: the sketch has an element or a constraint with a parameter's name.

    Examples:
        sk.Use(p)
        sk.Use(p).Expression("Distance.2 = th")
    """
    if not isinstance(parameters, Parameters):
        raise TypeError(f"Use: a px.Parameters set, not {parameters!r}")
    sketch._UseParameters(dict(parameters._values))
    parameters._add(sketch)
    return sketch
