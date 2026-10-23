# Nearly coincident edges

The task is the union of two unit squares, the second shifted by `d` along x. The exact area of the union is 1 + d,
and the output lists the errors of both kernels for d from 10⁻⁴ down to 10⁻¹⁰:

```python
import netgen.occ as occ
import planex as px
```

```python
ds = [1e-4, 1e-6, 1e-8, 1e-10]
for d in ds:
    square = px.QuickSketch().Rectangle(1, 1).Face()
    moved = px.QuickSketch((d, 0)).Rectangle(1, 1).Face()
    a = square + moved
    first = occ.WorkPlane().Rectangle(1, 1).Face()
    second = occ.WorkPlane().MoveTo(d, 0).Rectangle(1, 1).Face()
    b = (first + second).UnifySameDomain()
    err_px = a.mass - (1 + d)
    err_occ = sum(f.mass for f in b.faces) - (1 + d)
    print(f"d = {d:.0e}   planex2d error {err_px:+.1e}   OCC error {err_occ:+.1e}")
```

```text
d = 1e-04   planex2d error +0.0e+00   OCC error +2.2e-16
d = 1e-06   planex2d error +0.0e+00   OCC error +2.2e-16
d = 1e-08   planex2d error +0.0e+00   OCC error -1.0e-08
d = 1e-10   planex2d error +0.0e+00   OCC error -1.0e-10
```

Figure 1 shows the last case, d = 10⁻¹⁰. At this scale, the two squares look like one square, so a lens magnifies
the lower right corner, where their right sides run 10⁻¹⁰ apart. Next to them are the unions from planex2d and from
OpenCASCADE, with their vertices marked.

```python
lens = {"spot": (1, 0), "r": 0.07, "at": (0.48, 0.5), "R": 0.36, "half": 3e-10}
Row((px.Compound([square, moved]), "two squares 10⁻¹⁰ apart", {"fill": False, "zoom": lens}),
    (a, f"planex2d: {len(a.edges)} edges", {"fill": False, "marks": Vertices(a)}),
    (b, f"OpenCASCADE: {len(b.edges)} edges", {"marks": Vertices(b)}),
    caption="The union of two unit squares 10⁻¹⁰ apart")
```

```text
<IPython.core.display.HTML object>
```
