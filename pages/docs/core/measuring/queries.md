# Boxes, crossings and open ends

Queries find things in an object without changing it: its bounding box, its edges by name, the points where a line
crosses it, and its outline and holes. All examples on this page use the link from [Tangents](../sketch/tangent.md),
two bosses of radius r1 = 1.5 and r2 = 1 with their centers d = 6 apart, joined by tangent lines, with a hole in
each boss.

```python
import math
import planex as px

link = px.QuickSketch()
link.Circle(0, 0, 1.5, id="boss1", construction=True)
link.Circle(6, 0, 1.0, id="boss2", construction=True)
link.Line(tangent=("boss1", "boss2"), id="upper", name="upper")
link.Line(tangent=("boss1", "boss2"), reverse=True, id="lower", name="lower")
link.Arc("lower", "upper", congruent="boss2", id="end2")
link.Arc("upper", "lower", congruent="boss1", id="end1")
link.Circle(0, 0, 0.6, id="hole1").Circle(6, 0, 0.4, id="hole2")
face = link.Face()
r1, r2, d = 1.5, 1.0, 6.0
```

## Box, edges by name, crossings

The example makes three queries on the link (Figure 1):

1. The bounding box: the end arc of the large boss reaches over its top and bottom, and that of the small boss over
   its far side.
2. The two tangent lines, found by their names with `edges(name="upper|lower")`. Each is √(d² − (r1 − r2)²) long.
3. The points where the center line crosses the outline and the holes. The differences of the first two and of the
   last two crossings are the wall thicknesses around the holes.

```python
box = face.bounding_box
tangents = face.edges(name="upper|lower")
center_line = px.Line((-2, 0), (8, 0))
xs = sorted(p.h for p in face.Intersections(center_line))
```

```python
(x0, y0), (x1, y1) = box
frame = px.QuickSketch((x0, y0)).Rectangle(x1 - x0, y1 - y0).Wire()
Row((face, "The bounding box", {"dashed": [frame]}), (face, "The tangent lines", {"highlight": list(tangents)}),
    (face, "The center line's crossings", {"dashed": [center_line], "marks": [(x, 0) for x in xs]}),
    caption="Queries on the link: its box, edges by name, and crossings")
```

```text
<IPython.core.display.HTML object>
```

```python
print("box", box, "exact", ((-r1, -r1), (d + r2, r1)))
print([(e.name, e.mass) for e in tangents], "exact", math.sqrt(d**2 - (r1 - r2) ** 2))
print("crossings", xs)
print("walls", xs[1] - xs[0], xs[5] - xs[4])
```

```text
box ((-1.5, -1.5), (7.0, 1.5)) exact ((-1.5, -1.5), (7.0, 1.5))
[('upper', 5.979130371550699), ('lower', 5.979130371550699)] exact 5.979130371550699
crossings [-1.5, -0.6, 0.6, 5.6, 6.4, 7.0]
walls 0.9 0.5999999999999996
```

## Outline and holes

`outer` and `inner` are the loops of a face. The outline of the link consists of the two tangents, the arc of the
large boss over π + 2α and the arc of the small boss over π − 2α. Its holes are two full circles (Figure 2).

```python
alpha = math.asin((r1 - r2) / d)
tangents = 2 * math.sqrt(d**2 - (r1 - r2) ** 2)
outline = tangents + r1 * (math.pi + 2 * alpha) + r2 * (math.pi - 2 * alpha)
print("outline", face.outer.mass, "exact", outline)
holes = sorted(h.mass for h in face.inner)
print("holes", holes, "exact", [2 * math.pi * 0.4, 2 * math.pi * 0.6])
```

```text
outline 19.895672463686495 exact 19.895672463686495
holes [2.5132741228718367, 3.7699111843077517] exact [2.5132741228718345, 3.7699111843077517]
```

```python
Row((face, "The outline", {"highlight": face.outer.edges}),
    (face, "The holes", {"highlight": [e for h in face.inner for e in h.edges]}),
    caption="outer and inner: the loops of a face")
```

```text
<IPython.core.display.HTML object>
```

## Open ends

`IsClosed()` tells whether the wires of a sketch are closed, before they are made into a face:

```python
print([w.IsClosed() for w in link.Wires()])
```

```text
[True, True, True]
```
