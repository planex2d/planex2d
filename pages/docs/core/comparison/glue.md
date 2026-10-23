# Gluing many faces

The task is a fiber composite cross-section: 200 fibers of radius 0.6 glued into a matrix 30 × 30, so that every
fiber and the matrix share the fiber's outline. The fibers are named `fiber` and the matrix `matrix`. For the polygon
libraries, the task is to cut the fibers, as polygons of 2048 segments per quarter circle, out of the matrix, which
gives the face of the matrix. Figure 1 shows the result of `Glue` in planex2d and in OpenCASCADE, and
Figure 2 the times.

```python
import math
import random
import netgen.occ as occ
import shapely
import planex as px
from comparison_libraries import best, chart, difference
```

```python
random.seed(2)
L, r, n = 30.0, 0.6, 200
centers = []
while len(centers) < n:
    p = (random.uniform(r, L - r), random.uniform(r, L - r))
    if all(math.dist(p, q) > 2.2 * r for q in centers):
        centers.append(p)

matrix = px.QuickSketch().Rectangle(L, L).Face()
matrix.name = "matrix"
px_fibers = [px.QuickSketch().Circle(x, y, r).Face() for x, y in centers]
for fiber in px_fibers:
    fiber.name = "fiber"
occ_matrix = occ.WorkPlane().Rectangle(L, L).Face()
occ_fibers = [occ.WorkPlane().Circle(x, y, r).Face() for x, y in centers]
polygons = [shapely.Point(x, y).buffer(r, quad_segs=2048) for x, y in centers]
```

```python
t_px, a = best(lambda: px.Glue([matrix, *px_fibers]))
t_occ, b = best(lambda: occ.Glue([occ_matrix, *occ_fibers]), repeats=1)
others = difference([(0, 0), (L, 0), (L, L), (0, L)], polygons)

matrix_area = sum(f.mass for f in a.faces(name="matrix"))
occ_area = sum(f.mass for f in b.faces)
print(f"planex2d   {t_px:.4f} s, {len(a.faces)} faces, matrix {matrix_area:.6f}")
print(f"OCC        {t_occ:.4f} s, {len(b.faces)} faces, area {occ_area:.6f}")
for name, (t, area) in others.items():
    relative = (area - matrix_area) / matrix_area
    print(f"{name:10} {t:.4f} s, matrix {area:.6f}, relative to planex2d {relative:+.0e}")
```

```text
planex2d   0.0009 s, 201 faces, matrix 673.805329
OCC        0.0156 s, 201 faces, area 900.000000
shapely    0.8414 s, matrix 673.805351, relative to planex2d +3e-08
pyclipper  0.5683 s, matrix 673.805351, relative to planex2d +3e-08
gdstk      0.6588 s, matrix 673.805351, relative to planex2d +3e-08
manifold3d 0.6726 s, matrix 673.805351, relative to planex2d +3e-08
```

```python
Row((a, "planex2d", {"fill": False}), (b, "OpenCASCADE"), caption="200 fibers glued into a matrix")
```

```text
<IPython.core.display.HTML object>
```

```python
Times(chart(t_px, others, t_occ), title="Gluing 200 fibers into a matrix", note=POLYGONS)
```

```text
<IPython.core.display.HTML object>
```
