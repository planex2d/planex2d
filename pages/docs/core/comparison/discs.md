# Union of many discs

500 random discs of radius 0.3 to 1.2 in a square of 30 are united: in planex2d with `Unify`, in OpenCASCADE with
`Fuse` and `UnifySameDomain`, and in the polygon libraries as polygons of 2048 segments per quarter circle. Figure 1
shows the union from planex2d and from OpenCASCADE, and Figure 2 the times.

```python
import random
import netgen.occ as occ
import shapely
import planex as px
from comparison_libraries import best, chart, union
```

```python
random.seed(1)
n = 500
data = []
for _ in range(n):
    x, y = random.uniform(0, 30), random.uniform(0, 30)
    data.append((x, y, random.uniform(0.3, 1.2)))
px_discs = [px.QuickSketch().Circle(x, y, r).Face() for x, y, r in data]
occ_discs = [occ.WorkPlane().Circle(x, y, r).Face() for x, y, r in data]
polygons = [shapely.Point(x, y).buffer(r, quad_segs=2048) for x, y, r in data]
```

```python
t_px, a = best(lambda: px.Unify(px_discs))
t_occ, b = best(lambda: occ.Fuse(occ_discs).UnifySameDomain(), repeats=1)
others = union(polygons)

print(f"planex2d   {t_px:.4f} s, area {a.mass:.6f}, {len(a.edges)} edges")
occ_area = sum(f.mass for f in b.faces)
print(f"OCC        {t_occ:.4f} s, area {occ_area:.6f}, {len(b.edges)} edges")
for name, (t, area) in others.items():
    relative = (area - a.mass) / a.mass
    print(f"{name:10} {t:.4f} s, area {area:.6f}, relative to planex2d {relative:+.0e}")
```

```text
planex2d   0.0024 s, area 612.803230, 640 edges
OCC        9.5292 s, area 612.803230, 640 edges
shapely    1.0983 s, area 612.803194, relative to planex2d -6e-08
pyclipper  1.6787 s, area 612.803194, relative to planex2d -6e-08
gdstk      1.4541 s, area 612.803194, relative to planex2d -6e-08
manifold3d 1.2514 s, area 612.803194, relative to planex2d -6e-08
```

```python
Row((a, "planex2d", {"fill": False}), (b, "OpenCASCADE"), caption="The union of 500 discs")
```

```text
<IPython.core.display.HTML object>
```

```python
Times(chart(t_px, others, t_occ), title="Uniting 500 discs", note=POLYGONS)
```

```text
<IPython.core.display.HTML object>
```
