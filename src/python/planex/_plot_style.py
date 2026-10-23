# SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
# SPDX-License-Identifier: LGPL-2.1-or-later

import math
import numbers

from ._sketches import Sketch

OVERLAY_COL = "black"
# colormap="default": the docs' map, low values red (the critical end of a thickness map), high values planex blue
COLORMAP = ["#d1495b", "#f2c48d", "#f4ecd8", "#a9d6e5", "#0f7ea8"]



def _css(col):
    """a matplotlib color spec as a CSS color for plotly"""
    try:
        from matplotlib.colors import to_rgba
    except ImportError:
        if isinstance(col, str):
            return col
        r, g, b, a = (list(col) + [1.0])[:4]
        return f"rgba({round(255 * r)},{round(255 * g)},{round(255 * b)},{a})"
    r, g, b, a = to_rgba(col)
    return f"rgba({round(255 * r)},{round(255 * g)},{round(255 * b)},{a})"


def _separated(lines):
    """the x and the y of (m, k, 2) polylines, each one list with None between the polylines: one plotly trace"""
    import numpy as np

    m, k, _ = lines.shape
    out = np.full((2, m, k + 1), None, dtype=object)
    out[:, :, :k] = lines.transpose(2, 0, 1)
    return out[0].ravel().tolist(), out[1].ravel().tolist()


def _limits(values, clim):
    """the colour range: None and None autoscale; one end alone takes the other from the values, never past the given
    one (plotly ignores a zmin without a zmax, matplotlib fails on vmin > vmax)"""
    import numpy as np

    lo, hi = clim
    if (lo is None) == (hi is None):
        return lo, hi
    values = np.asarray(values, dtype=float).ravel()
    values = values[~np.isnan(values)]
    if lo is None:
        return (min(float(values.min()), hi) if values.size else hi), hi
    return lo, (max(float(values.max()), lo) if values.size else lo)


def _default_colours(name):
    return COLORMAP[::-1] if name == "default_r" else COLORMAP


def _colormap(colormap):
    """a matplotlib Colormap: "default" (or "default_r", reversed) planex's map, a matplotlib colormap or its name, or
    a list of colours"""
    import matplotlib
    from matplotlib.colors import Colormap, LinearSegmentedColormap

    if isinstance(colormap, Colormap):
        return colormap
    if isinstance(colormap, str):
        if colormap in ("default", "default_r"):
            colours = _default_colours(colormap)
            return LinearSegmentedColormap.from_list(colormap, colours)
        if colormap in matplotlib.colormaps:
            return matplotlib.colormaps[colormap]
        raise ValueError(f"Plot: colormap {colormap!r} is neither 'default', 'default_r' nor a matplotlib colormap")
    try:
        return LinearSegmentedColormap.from_list("colours", list(colormap))
    except (TypeError, ValueError):
        raise ValueError(f"Plot: colormap is 'default', a matplotlib colormap or its name, or a list of colours, not "
                         f"{colormap!r}") from None



def _colorscale(colormap, stops=32):
    """the colormap as a plotly colorscale; planex's own map without matplotlib"""
    if isinstance(colormap, str) and colormap in ("default", "default_r"):
        colours = _default_colours(colormap)
        return [[k / (len(colours) - 1), c] for k, c in enumerate(colours)]
    from matplotlib.colors import to_hex

    cmap = _colormap(colormap)
    return [[k / (stops - 1), to_hex(cmap(k / (stops - 1)))] for k in range(stops)]


def _title(data):
    label = data.get("label") or ""
    return "%s: %.6g" % (label, data["value"]) if "value" in data else label



def _contours_arg(contours):
    """contours= checked: None, a number of levels, or the levels given, sorted, each once"""
    if contours is None:
        return None
    import numpy as np

    if isinstance(contours, numbers.Integral) and not isinstance(contours, bool) and contours > 0:
        return int(contours)
    if np.ndim(contours) == 1 and len(contours) and all(isinstance(c, numbers.Real) and not isinstance(c, bool)
                                                        for c in contours):
        levels = np.unique(np.asarray(contours, dtype=float))
        if np.isfinite(levels).all():
            return levels
    raise ValueError("Plot: contours= is None, a number of levels (evenly spaced inside the colour range) or a list "
                     "of levels")


def _density(field_lines):
    """field_lines= checked, as the density of matplotlib's streamplot; None when off"""
    if field_lines is True or field_lines is False:
        return 1.0 if field_lines else None
    if isinstance(field_lines, numbers.Real) and math.isfinite(field_lines) and field_lines > 0:
        return float(field_lines)
    raise ValueError("Plot: field_lines= is False, True or a density above 0 (as matplotlib's streamplot: 2 is twice "
                     "as dense)")


def _levels(values, clim, contours):
    """the contour levels: those given, or a number of them evenly spaced inside the colour range (min, max, else the
    values')"""
    import numpy as np

    if not isinstance(contours, int):
        return contours
    lo, hi = _limits(values, clim)
    if lo is None:
        values = np.asarray(values, dtype=float).ravel()
        values = values[np.isfinite(values)]
        if not values.size:
            return np.empty(0)
        lo, hi = float(values.min()), float(values.max())
    return np.linspace(lo, hi, contours + 2)[1:-1] if hi > lo else np.empty(0)


def _offscreen():
    """matplotlib axes outside pyplot: the contour and field lines plotly draws are computed there as matplotlib
    computes them for its own axes, so both backends draw the same lines"""
    from matplotlib.figure import Figure

    return Figure().add_subplot()


def _contour_set(ax, surface, levels):
    """the contour lines of the colours on the faces at the levels, drawn into the matplotlib axes ax; surface is
    ("contour", (xs, ys, z)) for a pixel grid or ("tricontour", (tri, values)); None without levels"""
    if not len(levels):
        return None
    kind, args = surface
    lines = getattr(ax, kind)(*args, levels=levels, colors=OVERLAY_COL, linewidths=0.5, zorder=1)
    lines.set_gid("contours")
    return lines


def _stream(ax, flow, density):
    """the field lines of the part in the plane on the pixels flow = (xs, ys, u, v), NaN outside, as matplotlib's
    streamplot draws them into ax; None on a grid of one row or column"""
    import numpy as np

    xs, ys, u, v = flow
    if min(len(xs), len(ys)) < 2:
        return None
    stream = ax.streamplot(xs, ys, np.ma.masked_invalid(u), np.ma.masked_invalid(v), density=density,
                           color=OVERLAY_COL, linewidth=0.6, arrowsize=0.7, zorder=1.5)
    stream.lines.set_gid("field lines")
    return stream


def _overlays(ax, kind, args, levels, data, density):
    """the contours (kind and args of the surface, levels None for none) and the field lines of data into ax"""
    contour = None if levels is None else _contour_set(ax, (kind, args), levels)
    return contour, (_stream(ax, data["flow"], density) if "flow" in data else None)


def _polylines(lines):
    """the x and the y of polylines (point sequences), each one list with None between the polylines: one plotly trace"""
    import numpy as np

    xs, ys = [], []
    for line in lines:
        line = np.asarray(line, dtype=float)
        xs.extend(line[:, 0].tolist() + [None])
        ys.extend(line[:, 1].tolist() + [None])
    return xs, ys


def _arrowheads(lines):
    """where streamplot puts the arrow of each field line (halfway along it) and its direction, as a plotly marker
    angle"""
    import numpy as np

    heads, angles = [], []
    for line in lines:
        s = np.cumsum(np.hypot(*np.diff(line, axis=0).T))
        n = np.searchsorted(s, s[-1] / 2.0)
        d = line[n + 1] - line[n]
        heads.append(0.5 * (line[n] + line[n + 1]))
        angles.append(-math.degrees(math.atan2(d[1], d[0])))
    return np.reshape(heads, (-1, 2)), angles


def _backend(ax, backend):
    """the backend a plot call draws with: the one of ax if given (a plotly Figure or a matplotlib Axes), else backend,
    by default matplotlib"""
    if backend is None:
        backend = "plotly" if ax is not None and type(ax).__module__.startswith("plotly") else "matplotlib"
    if backend not in ("matplotlib", "plotly"):
        raise ValueError(f"unknown backend {backend!r}")
    return backend


def _plottable(obj):
    """a Sketch in strict mode is drawn while it can still move: a copy in normal mode"""
    if isinstance(obj, Sketch) and obj.mode == "strict":
        obj = obj.Copy()
        obj.mode = "normal"
    return obj
