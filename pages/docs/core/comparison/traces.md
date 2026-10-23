# Union of traces

500 random strokes, 2 long and 0.2 wide with round ends, lie in a square of 30. OpenCASCADE gets the faces as
exported by planex2d, and the polygon libraries get the strokes buffered with 512 segments per quarter circle.
Figure 1 shows the union from planex2d and from OpenCASCADE, and Figure 2 the times.

```python
import math
import random
import netgen.occ as occ
import shapely
import planex as px
from comparison_libraries import best, chart, union
```

```python
random.seed(2)
strokes = []
for _ in range(500):
    x, y = random.uniform(0, 30), random.uniform(0, 30)
    strokes.append((x, y, random.uniform(0, 360)))
px_traces = [px.QuickSketch((x, y)).Turn(a).Line(2).Wire().Offset(0.1)
             for x, y, a in strokes]
occ_traces = [t.ToOCCShape() for t in px_traces]
polygons = []
for x, y, a in strokes:
    end = (x + 2 * math.cos(math.radians(a)), y + 2 * math.sin(math.radians(a)))
    polygons.append(shapely.LineString([(x, y), end]).buffer(0.1, quad_segs=512))
```

```python
t_px, a = best(lambda: px.Unify(px_traces))
t_occ, b = best(lambda: occ.Fuse(occ_traces).UnifySameDomain(), repeats=1)
others = union(polygons)

print(f"planex2d   {t_px:.4f} s, area {a.mass:.6f}, {len(a.edges)} edges")
occ_area = sum(f.mass for f in b.faces)
print(f"OCC        {t_occ:.4f} s, area {occ_area:.6f}, {len(b.edges)} edges")
for name, (t, area) in others.items():
    relative = (area - a.mass) / a.mass
    print(f"{name:10} {t:.4f} s, area {area:.6f}, relative to planex2d {relative:+.0e}")
```

```text
planex2d   0.0062 s, area 191.735856, 3023 edges
```

```text
OCC        11.1362 s, area 191.735856, 3023 edges
shapely    0.7077 s, area 191.735837, relative to planex2d -1e-07
pyclipper  0.5864 s, area 191.735837, relative to planex2d -1e-07
gdstk      0.5129 s, area 191.735837, relative to planex2d -1e-07
manifold3d 0.4997 s, area 191.735837, relative to planex2d -1e-07
```

```python
Row((a, "planex2d", {"fill": False}), (b, "OpenCASCADE"), caption="The union of 500 traces")
```

```text
<IPython.core.display.HTML object>
```

```python
Times(chart(t_px, others, t_occ), title="Uniting 500 traces", note=POLYGONS)
```

```text
<IPython.core.display.HTML object>
```
