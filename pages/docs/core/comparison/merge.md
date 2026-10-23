# Merging split edges

The task is the union of a grid of 20 × 20 unit squares. The planex2d union merges the pieces of each side into one
edge by itself. OpenCASCADE's `Fuse` leaves them split until `UnifySameDomain` runs, and after that, both have the four
edges of the outline. The squares have no arcs, so the polygon libraries get them exactly. Figure 1 shows the squares
and the unions from planex2d and from OpenCASCADE, with their vertices marked, and Figure 2 shows the times.

```python
import netgen.occ as occ
import shapely
import planex as px
from comparison_libraries import best, chart, union
```

```python
n = 20
cells = [(i, j) for i in range(n) for j in range(n)]
px_squares = [px.QuickSketch((i, j)).Rectangle(1, 1).Face() for i, j in cells]
occ_squares = [occ.WorkPlane().MoveTo(i, j).Rectangle(1, 1).Face() for i, j in cells]
polygons = [shapely.box(i, j, i + 1, j + 1) for i, j in cells]
```

```python
t_px, a = best(lambda: px.Unify(px_squares))
t_occ, b = best(lambda: occ.Fuse(occ_squares).UnifySameDomain(), repeats=1)
others = union(polygons)

print(f"planex2d   {t_px:.4f} s, {len(a.edges)} edges")
print(f"OCC        {t_occ:.4f} s, {len(b.edges)} edges")
for name, (t, area) in others.items():
    print(f"{name:10} {t:.4f} s, area {area:.6f}")
```

```text
planex2d   0.0010 s, 4 edges
OCC        1.7253 s, 4 edges
shapely    0.0057 s, area 400.000000
pyclipper  0.0006 s, area 400.000000
gdstk      0.0006 s, area 400.000000
manifold3d 0.0004 s, area 400.000000
```

```python
Row((px.Compound(px_squares), f"{len(px_squares)} unit squares", {"fill": False}),
    (a, f"planex2d: {len(a.edges)} edges", {"fill": False, "marks": Vertices(a)}),
    (b, f"OpenCASCADE: {len(b.edges)} edges", {"marks": Vertices(b)}),
    caption="The union of a grid of unit squares")
```

```text
<IPython.core.display.HTML object>
```

```python
Times(chart(t_px, others, t_occ), title="Uniting 400 unit squares", note=POLYGONS)
```

```text
<IPython.core.display.HTML object>
```
