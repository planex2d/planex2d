# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

"""sk.Animate: a matplotlib animation of a constraint sketch through rows of its dimensions"""
from __future__ import annotations

import math
from typing import Any, Callable

from ._auto import Auto, is_auto
from ._caller import warn
from . import _planex as px

# the in-between frames over the last real design, and a failed row's last good geometry
MUTED = "#9fb3c8"
GREY = "#b0b0b0"


def _rows(rows):
    """[(name, {dimension: value})] of the three forms: {name: {dimension: value}}, [{dimension: value}, ...] or
    {dimension: [values]} (one frame per value, the arrays of equal length)"""
    if isinstance(rows, dict) and rows and all(isinstance(v, dict) for v in rows.values()):
        return [(str(name), dict(values)) for name, values in rows.items()]
    if isinstance(rows, dict):
        lengths = {len(v) for v in rows.values()}
        if len(lengths) != 1:
            raise ValueError("Animate({dimension: values}): the arrays have different lengths %s" % sorted(lengths))
        n = lengths.pop()
        return [(None, {k: float(v[i]) for k, v in rows.items()}) for i in range(n)]
    rows = list(rows)
    if not all(isinstance(r, dict) for r in rows):
        raise TypeError("Animate: rows are {name: {dimension: value}}, a list of {dimension: value} or "
                        "{dimension: [values]}")
    return [(None, dict(r)) for r in rows]


def _frames(rows, steps, counts):
    """the frames as (row index, its name, the values it sets, True for a row's own frame); with steps > 1 the
    values in between each two rows, linear (counts, the patterns', whole)"""
    full, last = [], {}
    for name, values in rows:
        last = {**last, **values}
        full.append((name, dict(last)))
    frames = []
    for k, (name, values) in enumerate(full):
        if k > 0 and steps > 1:
            before = full[k - 1][1]
            for j in range(1, steps):
                t = j / steps
                frames.append((k, None, {d: _between(before.get(d, v), v, t, d in counts) for d, v in values.items()}, False))
        frames.append((k, name, values, True))
    return frames


def _between(a, b, t, count):
    """a value t of the way from a to b; a pattern's count stays whole"""
    value = a + (b - a) * t
    return round(value) if count else value


def _title(name, values, changing):
    if name is not None:
        return name
    return ", ".join("%s = %.4g" % (d, values[d]) for d in changing)


def _animation_class():
    """a FuncAnimation shown in Jupyter as its HTML player (made on first use: matplotlib is optional)"""
    from matplotlib.animation import FuncAnimation

    class Animation(FuncAnimation):
        def _repr_html_(self):
            return self.to_jshtml()

    return Animation


def Animate(sketch: Any, rows: Any, /, steps: int = 1, fps: float = 10, label: Callable | None = None,
            keep_designs: bool | Auto = Auto, show_constraints: bool | None = None) -> Any:
    """A matplotlib animation of the sketch through rows of its dimensions, each frame the sketch solved and drawn.

    Every frame is solved first, on a copy, so the sketch stays as it is, and all frames share one view. A row that
    does not solve keeps the last good geometry, greyed, its reason in the title; one warning lists those rows.

    Args:
        rows: {name: {dimension: value}} (sk.ReadSpreadsheet's rows), a list of {dimension: value}, or
            {dimension: [values]} with arrays of equal length; a row keeps the values the rows before it set.
        steps: frames per row: steps - 1 more in between each two rows, the values linear between them, each solved.
        fps: frames per second, for showing and for anim.save.
        label: f(sketch) giving a text for each frame, e.g. lambda s: "gap %.2f" % s.Measure("pin", to="slot")["distance"].
        keep_designs: with steps > 1, True draws each row's design while the frames in between move over it muted;
            px.Auto: True when steps > 1.
        show_constraints: as Plot.

    Returns:
        a matplotlib FuncAnimation: shown in Jupyter, anim.save("x.gif") or anim.save("x.mp4") writes it.

    Raises:
        ImportError: matplotlib is missing.
        ValueError: no rows, a row names no dimension of the sketch, steps below 1, fps not positive, or arrays of
            different lengths.
        TypeError: rows of another form.

    Examples:
        sk.Animate({"crank": [0, 90, 180, 270, 360]})
        sk.Animate([{"w": 10}, {"w": 14}], steps=10, fps=20)
    """
    try:
        import matplotlib.pyplot as plt
        from matplotlib.ticker import MultipleLocator
    except ImportError as e:
        raise ImportError("Animate needs matplotlib (pip install matplotlib)") from e
    from ._plot_sketch import _grid_steps
    if not (isinstance(steps, int) and steps >= 1):
        raise ValueError("Animate(steps=%r): a whole number of at least 1" % (steps,))
    if not (fps > 0 and math.isfinite(fps)):
        raise ValueError("Animate(fps=%r): frames per second, > 0" % (fps,))
    keep = steps > 1 if is_auto(keep_designs, "Animate", "keep_designs") else bool(keep_designs)
    rows = _rows(rows)
    if not rows:
        raise ValueError("Animate: no rows")
    known = sketch.dimensions
    unknown = sorted({d for _, values in rows for d in values if d not in known})
    if unknown:
        raise ValueError("Animate: no dimension %s in the sketch; its dimensions: %s"
                         % (", ".join(map(repr, unknown)), ", ".join(map(repr, known))))
    changing = [d for d in dict.fromkeys(k for _, values in rows for k in values)
                if len({values.get(d) for _, values in rows if d in values}) > 1]
    work = sketch.Copy()
    shots, failed, good, design = [], [], work.Copy(), None
    for row, name, values, real in _frames(rows, steps, {d for d, v in known.items() if isinstance(v, int)}):
        try:
            work.Update(values)
            good = work.Copy()
            reason = None
        except (ValueError, RuntimeError) as e:
            work = good.Copy()
            reason = str(e).split(";")[0]
            if real:
                failed.append(name or _title(None, values, changing))
        if real:
            design = good if reason is None else None
        shots.append((good, design, _title(name, values, changing), reason, real))
    boxes = [s.bounding_box for s, *_ in shots]
    (x0, y0), (x1, y1) = (min(b[0][0] for b in boxes), min(b[0][1] for b in boxes)), (max(b[1][0] for b in boxes), max(b[1][1] for b in boxes))
    pad = 0.08 * max(x1 - x0, y1 - y0, 1e-9)
    step, minor = _grid_steps(x1 - x0 + 2 * pad, y1 - y0 + 2 * pad)
    width = 6.0
    fig, ax = plt.subplots(figsize=(width, max(2.0, width * (y1 - y0 + 2 * pad) / (x1 - x0 + 2 * pad))))

    def draw(k):
        shot, design, title, reason, real = shots[k]
        ax.clear()
        if keep and not real and design is not None:
            design.Plot(ax=ax, show_constraints=show_constraints)
        before = set(ax.get_children())
        shot.Plot(ax=ax, show_constraints=show_constraints)
        if reason is not None or (keep and not real):
            colour = GREY if reason is not None else MUTED
            for artist in set(ax.get_children()) - before:
                for setter in ("set_color", "set_edgecolor"):
                    if hasattr(artist, setter):
                        try:
                            getattr(artist, setter)(colour)
                        except (TypeError, ValueError):
                            pass
        ax.set_xlim(x0 - pad, x1 + pad)
        ax.set_ylim(y0 - pad, y1 + pad)
        for axis in (ax.xaxis, ax.yaxis):
            axis.set_major_locator(MultipleLocator(step))
            axis.set_minor_locator(MultipleLocator(minor))
        ax.set_aspect("equal")
        ax.set_title(title + (": " + reason if reason is not None else ""))
        if label is not None and reason is None:
            ax.text(0.02, 0.98, str(label(shot)), transform=ax.transAxes, va="top", ha="left")
        return []

    animation = _animation_class()(fig, draw, frames=len(shots), interval=1000.0 / fps, blit=False)
    plt.close(fig)
    if failed:
        warn("Animate: %d row%s did not solve and show the last good geometry: %s"
             % (len(failed), "" if len(failed) == 1 else "s", ", ".join(failed)), px.PlanexWarning)
    return animation
