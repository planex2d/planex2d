# Fully, under- and over-constrained

A sketch reports how much it can still move. `sk.dof` counts the remaining degrees of freedom, and `sk.status` is
`"full"`, `"under"` or `"over"`. The pictures color each element by its state: green when nothing about it can move,
black when something can, and pink where constraints conflict or repeat. This page shows what each state looks like
and how a sketch is made fully constrained.

```python
import warnings
import numpy as np
import planex as px
```

## Under-constrained

The example is a rectangle drawn with flags only: horizontal, with three right angles, starting at the origin. Its
width and height are free, which leaves two degrees of freedom (Figure 1a). With both as dimensions, it is fully
constrained and green (Figure 1b).

```python
sk = px.Sketch()
sk.Move(sk.origin, px.Coincident).Line(40, 1, px.Horizontal, id="bottom")
sk.Line(1, 20, px.Perpendicular, id="right").Line(-40, 1, px.Perpendicular, id="top")
sk.Close(px.Perpendicular, id="left")
free = sk.Copy()
sk.Constrain("bottom", px.Distance, 40, id="w")
sk.Constrain("bottom", "top", px.Distance, 20, id="h")
print(free.status, free.dof, "->", sk.status, sk.dof)
```

```text
under 2 -> full 0
```

```python
Row((free, f"{free.status}: {free.dof} degrees of freedom", {"constraints": True}),
    (sk, f"{sk.status}: width and height", {"constraints": True}),
    caption="A rectangle under-constrained and fully constrained")
```

```text
<IPython.core.display.HTML object>
```

## Redundant constraints

The left and right sides are already parallel because of the right angles. A `Parallel` constraint between them
repeats that. It is consistent and the sketch still solves, so planex2d keeps it, warns, and lists it in
`sk.redundant` together with the constraints it repeats. Its sign turns pink (Figure 2). `status` counts only degrees
of freedom, so it stays `"full"`. `Remove` takes the repetition back.

```python
with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter("always")
    sk.Constrain("right", "left", px.Parallel)
print(caught[0].message)
print(sk.status, sk.redundant)
repeated = sk.Copy()
sk.Remove("Parallel.1")
print(sk.status, sk.redundant)
```

```text
Parallel.1 ("right", "left", px.Parallel) is redundant (consistent) with Perpendicular.2, Perpendicular.3
full ['Perpendicular.2', 'Perpendicular.3', 'Parallel.1']
full []
```

```python
Row((repeated, "Parallel repeats the right angles: pink", {"constraints": True}))
```

```text
<IPython.core.display.HTML object>
```

## Over-constrained

A constraint that contradicts the others cannot be enforced. In the example, a diagonal of 50 disagrees with a width
of 40 and a height of 20. planex2d keeps the constraint but suspends it: a `PlanexWarning` names the constraints it
contradicts, the status is `"over"`, and the geometry stays at its last solution. The suspended constraint and the
ones it contradicts are drawn in pink. `Face()` and the exports still work on the last solution, with a warning.

```python
with warnings.catch_warnings(record=True) as caught:
    warnings.simplefilter("always", px.PlanexWarning)
    sk.Constrain("bottom.start", "top.start", px.Distance, 50, id="diagonal")
print(caught[0].message)
print(sk.status, sorted(sk.dimensions))
```

```text
"bottom.start", "top.start", px.Distance, 50: no solution, it contradicts the constraints there are (Perpendicular.1, Perpendicular.2, w, h); diagonal is kept but not solved, the sketch is over-constrained
over ['diagonal', 'h', 'w']
```

Every later `Update` tries the suspended constraint again. A rectangle 40 × 30 has a diagonal of exactly 50, so with
the height changed to 30, the diagonal fits and is enforced, and the sketch is fully constrained again. The diagonal
now repeats what the width and the height already fix, so it is redundant, and a warning says so. Removing the
suspended constraint, or one it contradicts, also ends the conflict.

```python
sk["h"] = 30
sk.Update()
print(sk.status, sk.Measure("bottom.start", "top.start")["distance"])
```

```text
full 50.0
```

```text
stderr:
/tmp/ipykernel_834917/2222836129.py:2: PlanexWarning: diagonal ("bottom.start", "top.start", px.Distance, 50) is redundant (consistent) with Perpendicular.1, Perpendicular.2, w, h
  sk.Update()
```

## Strict mode

Exporting an under-constrained sketch is usually a mistake, because the part then has whatever size the drawing
happened to have. `Sketch(mode="strict")` refuses every export while the sketch can still move, and the error states
what is free. Reading remains allowed, including positions, measurements and pictures. Strict mode also turns a
change of branch into an error (see [changing dimensions](update.md)), so an unattended loop never produces a silently
flipped part.

```python
sk = px.Sketch(mode="strict")
sk.Move(sk.origin, px.Coincident).Line(40, 0, px.Horizontal, id="bottom")
sk.Line(0, 20, px.Perpendicular).Line(-40, 0, px.Perpendicular, id="top")
sk.Close(px.Perpendicular)
try:
    sk.Face()
except ValueError as e:
    print(e)
sk.Constrain("bottom", px.Distance, 40, id="w")
sk.Constrain("bottom", "top", px.Distance, 20, id="h")
print(sk.status, sk.Face().mass)
```

```text
Face(): mode='strict' and the sketch is not fully constrained (2 degrees of freedom; free: bottom, Line.1, top, Line.2)
full 800.0
```

## Condition

`sk.condition` states how strongly a small change of the dimensions can move the geometry. A well-posed part, like
this rectangle, stays at a few units. A large value means that some point depends on nearly parallel lines or nearly
tangent circles, so a tiny change of a dimension moves it far. planex2d warns above
`px.config.sensitivity_warning`.

```python
print(f"condition {sk.condition:.3f}")
```

```text
condition 2.864
```

## The Jacobian

`sk.jacobian` is the matrix the solver works with. It has one row per equation and one column per unknown, and
each entry is the derivative of that equation with respect to that unknown at the solved geometry. Rows are labeled
by their source, `(constraint id, row)`, and columns by what they move, `(reference, coordinate)`. The matrix is a
scipy sparse array. `scale` holds the scaling of its columns, and the raw derivatives are `matrix[:, k] / scale[k]`.
The plate from the [section overview](index.md) has eleven unknowns and eleven equations, and every equation involves
only a few of the unknowns (Figure 3).

```python
from scipy.sparse.linalg import spsolve

plate = px.Sketch(id="plate")
plate.Move(plate.origin, px.Coincident)
plate.Line(37, 2.5, px.Horizontal, id="bottom").Arc(9, 95, px.Tangent, id="fillet")
plate.Line(-1, 17, px.Vertical, px.Tangent, id="right")
plate.Line(-46, 3, px.Perpendicular, id="top").Close(px.Perpendicular, id="left")
plate.Constrain("fillet", px.Radius, 10, id="r")
plate.Constrain("bottom.start", "right", px.Distance, 50, id="width")
plate.Constrain("bottom", "top", px.Distance, 30, id="height")

J = plate.jacobian
print(J["shape"], J["matrix"].nnz, "of", J["shape"][0] * J["shape"][1], "entries")
print(J["rows"][:3], "...")
print(J["columns"][:3], "...")
```

```text
(11, 11) 63 of 121 entries
[('Horizontal.1', 0), ('Tangent.1', 0), ('Vertical.1', 0)] ...
[('bottom.end', 'x'), ('bottom.end', 'y'), ('fillet.end', 'x')] ...
```

```python
import io
import html
import matplotlib.pyplot as plt
from IPython.display import HTML, display
from docs_figures import INK, PLANEX, RED, _figure, _source


def matrix_figure(J, caption):
    A = J["matrix"].toarray()
    count = {}
    for id, _ in J["rows"]:
        count[id] = count.get(id, 0) + 1
    rows = [id if count[id] == 1 else f"{id} {k}" for id, k in J["rows"]]
    columns = [f"{ref} {c}" for ref, c in J["columns"]]
    fig, ax = plt.subplots(figsize=(0.3 * len(columns) + 2.2, 0.3 * len(rows) + 1.6))
    big = np.abs(A).max()
    for i, j in zip(*np.nonzero(A)):
        ax.add_patch(plt.Rectangle((j - 0.42, i - 0.42), 0.84, 0.84, lw=0,
                                   color=PLANEX if A[i, j] > 0 else RED,
                                   alpha=0.25 + 0.75 * abs(A[i, j]) / big))
    ax.set_xlim(-0.6, A.shape[1] - 0.4)
    ax.set_ylim(A.shape[0] - 0.4, -0.6)
    ax.set_xticks(range(len(columns)), columns, rotation=90, fontsize=8, color=INK)
    ax.set_yticks(range(len(rows)), rows, fontsize=8, color=INK)
    ax.set_xticks(np.arange(-0.5, A.shape[1]), minor=True)
    ax.set_yticks(np.arange(-0.5, A.shape[0]), minor=True)
    ax.grid(which="minor", color="#eef1f5", lw=0.6)
    ax.tick_params(which="both", length=0)
    ax.set_aspect("equal")
    for side in ax.spines.values():
        side.set_color("#e3e8ee")
    fig.tight_layout()
    buffer = io.BytesIO()
    fig.savefig(buffer, format="svg", transparent=True)
    plt.close(fig)
    src = _source(buffer.getvalue(), "svg")
    display(HTML(_figure(f'<div style="width: 46%; margin: 4px auto 8px; text-align: center">'
                         f'<img src="{src}" alt="{html.escape(caption)}" style="width: 100%"></div>', caption)))


matrix_figure(J, "The sk.jacobian of the plate, with positive entries in blue and negative ones in red")
```

```text
<IPython.core.display.HTML object>
```

### What a dimension moves

The equation of a dimension states that the measured length equals the set value, so changing the dimension by one
puts a one on the right-hand side of its row. Solving J dx = e for that row gives how far every unknown moves per unit
of the dimension. For the fillet's radius `r`, the bottom line ends one unit earlier, the right side starts one unit
higher, and the arc's center moves one unit to the left and one up (Figure 4). A finite difference of two `Update`s
gives the same result.

```python
rows, columns = J["rows"], J["columns"]
unit = np.zeros(J["shape"][0])
unit[rows.index(("r", 0))] = 1
dr = J["scale"] * spsolve(J["matrix"].tocsc(), unit)
for (ref, c), d in zip(columns, dr):
    if abs(d) > 1e-12:
        print(f"d {ref}.{c} / dr = {d:+.12f}")

h = 1e-4
up, down = plate.Copy().Update({"r": 10 + h}), plate.Copy().Update({"r": 10 - h})
fd = (up.Location("fillet.center")[1] - down.Location("fillet.center")[1]) / (2 * h)
k = columns.index(("fillet.center", "y"))
print(f"fillet.center.y: Jacobian {dr[k]:.12f}, finite difference {fd:.12f}")
```

```text
d bottom.end.x / dr = -1.000000000000
d fillet.end.y / dr = +1.000000000000
d fillet.center.x / dr = -1.000000000000
d fillet.center.y / dr = +1.000000000000
d fillet.r / dr = +1.000000000000
fillet.center.y: Jacobian 1.000000000000, finite difference 0.999999999944
```

```python
import math
from docs_figures import INK, PLANEX


def arrow(p, d, s, w):
    x, y = p[0], p[1]
    u, v = d[0] * s, d[1] * s
    n = math.hypot(u, v)
    if n < 1e-9:
        return None
    ux, uy = u / n, v / n
    head = min(4 * w, 0.45 * n)
    bx, by = x + ux * (n - head), y + uy * (n - head)
    corners = [(x - uy * w / 2, y + ux * w / 2), (bx - uy * w / 2, by + ux * w / 2),
               (bx - uy * 1.6 * w, by + ux * 1.6 * w), (x + u, y + v),
               (bx + uy * 1.6 * w, by - ux * 1.6 * w), (bx + uy * w / 2, by - ux * w / 2),
               (x + uy * w / 2, y - ux * w / 2)]
    outline = px.QuickSketch().Move(tuple(corners[0]))
    for q in corners[1:]:
        outline.Line(q)
    return outline.Close().Face()


def motion(sk, columns, d, caption, view, s=10, w=0.9):
    moves = {}
    for (ref, c), v in zip(columns, d):
        if c in ("x", "y"):
            moves.setdefault(ref, [0.0, 0.0])["xy".index(c)] = v
    points = [sk.Location(ref) for ref in moves]
    arrows = [arrow(p, m, s, w) for p, m in zip(points, moves.values())]
    return (sk.Face(), caption, {"fills": [(a, PLANEX) for a in arrows if a is not None],
                                 "dots": [p[:2] for p in points], "dot": INK, "names": False,
                                 "view": view})


Row(motion(plate, columns, dr, "How the plate's points move with r, drawn ten times larger", ((-6, 62), (-6, 38))))
```

```text
<IPython.core.display.HTML object>
```

### Free motions

An under-constrained sketch has fewer independent rows than columns. `sk.dof` is the number of columns minus the
rank of J, and the null space of J holds the motions that change no equation. For the rectangle drawn with flags only,
at the top of this page, these are its free width and its free height (Figure 5).

```python
from scipy.linalg import null_space

Jf = free.jacobian
raw = Jf["matrix"].toarray() / Jf["scale"]
print(free.dof, "=", raw.shape[1], "columns -", np.linalg.matrix_rank(raw), "rank")
motions = null_space(raw)
print(motions.shape[1], "free motions")
```

```text
2 = 6 columns - 4 rank
2 free motions
```

```python
cf = Jf["columns"]
pick = [cf.index(("bottom.end", "x")), cf.index(("top.end", "y"))]
basis = motions @ np.linalg.inv(motions[pick, :])
view = ((-6, 52), (-6, 30))
Row(motion(free, cf, basis[:, 0], "Free motion: the width", view, s=8, w=0.8),
    motion(free, cf, basis[:, 1], "Free motion: the height", view, s=8, w=0.8),
    caption="The two free motions of the rectangle drawn with flags only")
```

```text
<IPython.core.display.HTML object>
```

A redundant constraint adds a row but no rank. With the `Parallel` that repeats the right angles, the plate's matrix
has twelve rows but a rank of eleven, and `sk.redundant` names the rows that depend on each other.

```python
with warnings.catch_warnings():
    warnings.simplefilter("ignore")
    plate.Constrain("right", "left", px.Parallel)
Jr = plate.jacobian
print(Jr["shape"], "rank", np.linalg.matrix_rank(Jr["matrix"].toarray()))
print(plate.redundant)
```

```text
(12, 11) rank 11
['Perpendicular.1', 'Perpendicular.2', 'Parallel.1']
```
