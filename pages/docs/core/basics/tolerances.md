# Tolerances: how exact planex2d is

planex2d avoids tolerances wherever it can:

- Lines and arcs are exact curves, not chains of segments.
- Where edges meet, they share one vertex, a bit-exact copy of one computed point, so values that are equal by
  construction are never compared.
- A tolerance is used only where two independent computations have to agree, for example when deciding whether two
  arcs lie on the same circle. It then reflects the rounding of floating-point numbers.
- A near-coincidence is decided once per pair of edges, and every later step uses that decision.
- Gaps are never closed silently.

The tolerance is `Tol(S) = k · eps · max(S, 1)`, with `k = 10`, `eps = 10⁻¹⁵` and `S` the largest coordinate involved.
Above 1, it is relative to the size, and below 1, it is about 10⁻¹⁴. Every number below is computed on the page.

```python
import math
import numpy as np
import planex as px
```

## Lines and arcs are exact

The example is two discs of radius 1 with their centers 1.2 apart. The areas of their union (Figure 1a) and their
common part (Figure 1b) are checked against the area of the lens the two circles share.

```python
r, d = 1.0, 1.2
a = px.Circle((0, 0), r).Face()
b = px.Circle((d, 0), r).Face()
lens = 2 * r**2 * math.acos(d / (2 * r)) - d / 2 * math.sqrt(4 * r**2 - d**2)
union, common = a + b, a * b
```

```python
Row((union, "The union"), (common, "The common part"), caption="Two discs: union and common part")
```

```text
<IPython.core.display.HTML object>
```

```python
print("union", union.mass, "exact", 2 * math.pi * r**2 - lens)
print("common", common.mass, "exact", lens)
```

```text
union 5.388594871176362 exact 5.388594871176362
common 0.8945904360032246 exact 0.8945904360032246
```

## Shared vertices

The rest of the first disc is a crescent with two arcs. Their four ends are only two points: each corner is one
vertex that both arcs share bit for bit, so the outline is closed exactly and nothing lies in between.

```python
moon = a - b
ends = [tuple(v) for e in moon.edges for v in (e.start, e.end)]
print(len(moon.edges), "edges,", len(ends), "ends,", len(set(ends)), "points")
```

```text
2 edges, 4 ends, 2 points
```

```python
Row((moon, "The crescent: two arcs, two shared vertices", {"marks": Vertices(moon)}))
```

```text
<IPython.core.display.HTML object>
```

## Rounding grows with the coordinates

Floating-point numbers carry about 16 digits, whatever their size. When the same two discs are moved far from the
origin, they lose digits after the decimal point. The error of the common part grows with the coordinates, and so
does `Tol(S)`.

```python
for shift in (0.0, 1e3, 1e6):
    moved = a.Translated((shift, shift)) * b.Translated((shift, shift))
    tol = px.config.k * px.config.eps * max(shift, 1)
    print(f"at {shift:g}: error {moved.mass - lens:.1e}, Tol(S) {tol:.0e}")
```

```text
at 0: error 0.0e+00, Tol(S) 1e-14
at 1000: error -1.6e-13, Tol(S) 1e-11
at 1e+06: error 1.6e-10, Tol(S) 1e-08
```

## Where two curves meet

Two curves that come within `Tol(S)` of each other without crossing are treated as touching. The point where they
meet is placed once, bit-exact on both curves, and the decision is kept for every later step. In the example, two
discs of radius 1 have their centers 2 + gap apart. With a gap of 10⁻¹⁵, the discs touch and share one vertex. With a
gap of 10⁻¹³, which is larger than `Tol(S)` here, they stay apart as two separate circles, and a full circle has no
vertex at all.

```python
for gap in (1e-15, 1e-13):
    pair = a + px.Circle((2 + gap, 0), 1).Face()
    near = {tuple(v) for v in pair.vertices if abs(v.h - 1) < 1e-6}
    print(f"gap {gap:.0e}: {len(near)} vertices where they meet")
print("Tol(S) here:", px.config.k * px.config.eps * 3)
```

```text
gap 1e-15: 1 vertices where they meet
gap 1e-13: 0 vertices where they meet
Tol(S) here: 3.0000000000000005e-14
```

## Splines: an approximation, exported exactly

The core cuts only lines and arcs. To the user, a spline is one edge. For Boolean operations, offsets and areas,
planex2d computes with an arc approximation that stays within `px.config.spline_tol` of the curve, relative to the
spline's size (10⁻⁶ by default). The export passes the exact curve to OpenCASCADE. The example is a closed spline
through twelve points of an ellipse of 4 × 2. Its area in planex2d is compared with the area OpenCASCADE computes on
the exact curve, for three settings.

```python
t = np.radians(np.arange(0, 360, 30))
ellipse = np.column_stack([2 * np.cos(t), np.sin(t)])
default = px.config.spline_tol
for tol in (1e-4, default, 1e-8):
    px.config.spline_tol = tol
    oval = px.Face([px.Spline(ellipse, periodic=True)])
    print(f"spline_tol {tol:.0e}: area off by {oval.mass - oval.ToOCCFace().mass:.1e}")
px.config.spline_tol = default
```

```text
spline_tol 1e-04: area off by 1.3e-04
spline_tol 1e-06: area off by 3.1e-07
spline_tol 1e-08: area off by 7.0e-10
```

## Gaps are never closed silently

A square whose last line stops 10⁻⁵ short of its start stays open, because planex2d does not decide on its own that
the ends belong together. `Close()` ends a drawing at its start, and `CloseGaps(tol)` joins ends that are closer than
a tolerance you choose. The `tol=` argument of the readers does the same.

```python
square = px.QuickSketch().Line(1).Line(0, 1).Line(-1, 0).Line(0, -1 + 1e-5).Wire()
joined = square.Copy()
joined.CloseGaps(1e-4)
closed = px.QuickSketch().Line(1).Line(0, 1).Line(-1, 0).Close().Wire()
print("open drawing closed:", square.IsClosed())
print("after CloseGaps:", joined.IsClosed())
print("with Close():", closed.IsClosed())
```

```text
open drawing closed: False
after CloseGaps: True
with Close(): True
```

## Constraint sketches

A constraint sketch is solved until every constraint holds to 10⁻¹² of the sketch's size. The example is a triangle
given only by its three side lengths, 3, 4 and 5, so it must have a right angle. The dot product of its two legs is
zero up to rounding:

```python
sk = px.Sketch()
sk.Move((0, 0), px.Fix).Line(3.1, 0.2, id="a").Line(-0.1, 3.8, id="b").Close(id="c")
sk.Constrain("a", px.Horizontal)
for side, length in (("a", 3), ("b", 4), ("c", 5)):
    sk.Constrain(side, px.Distance, length)
legs = [np.subtract(tuple(e.end), tuple(e.start)) for e in sk.Wires()[0].edges[:2]]
print("degrees of freedom", sk.dof, "dot product of the legs", np.dot(*legs))
```

```text
degrees of freedom 0 dot product of the legs 1.6639563979149973e-46
```

## OpenCASCADE

OpenCASCADE works with an absolute tolerance of 10⁻⁷ (`Precision::Confusion()`). The export therefore removes
edges and closes gaps below 10⁻⁷ by default (`px.config.remove_edges_smaller`, `px.config.close_gaps_smaller`).
Details smaller than that, in the chosen unit, do not survive the export (see [Units](units.md)).

```python
print("remove_edges_smaller", px.config.remove_edges_smaller)
print("close_gaps_smaller", px.config.close_gaps_smaller)
```

```text
remove_edges_smaller 1e-07
close_gaps_smaller 1e-07
```

## At a glance

| Where | Tolerance | Setting |
|---|---|---|
| Lines, arcs, shared vertices | None: exact, bit-exact copies | |
| Two independent computations must agree | `Tol(S)`: about 10⁻¹⁴ of the size | `px.config.k`, `px.config.eps` |
| Two curves that meet without crossing | `Tol(S)`: one touch point, bit-exact on both | |
| Splines in Boolean operations, offsets, areas | 10⁻⁶ of the spline's size | `px.config.spline_tol` |
| Constraint sketches | Residuals below 10⁻¹² of the size | |
| Gaps | Never closed automatically | `CloseGaps(tol)`, `tol=` of the readers |
| Export to OpenCASCADE | 10⁻⁷, absolute | `px.config.remove_edges_smaller`, `px.config.close_gaps_smaller` |
| Fitting arcs and splines (cleanup) | 10⁻⁵ by default | `px.config.fit_arcs`, `px.config.fit_splines` (`tol` in the dict) |
