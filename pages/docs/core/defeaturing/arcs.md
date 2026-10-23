# Fitting arcs

Chains of short edges are replaced by arcs, or by lines where they are straight, as a tessellated or measured outline
needs before it is exported or meshed. The points do not have to lie exactly on a circle: an arc runs through the two
ends of the chain it replaces, and its center is computed from the points between them by least squares. Every fit
is then checked against the edges it replaces:

- each point has to lie within the tolerance of the arc;
- the edges have to stay within the tolerance of it, plus an allowance where the chain bends at a vertex, because the
  chords of a tessellated circle lie inside it.

A fit that deviates more is rejected, and the edges stay as they are.

```python
import math
import random
import planex as px
```

## A measured bore

The example is a bore of radius 10, measured at 256 points that each lie up to 0.03 off the circle at random, as a
scan or a coordinate measuring machine delivers them, and joined by lines with `px.FromPolyline` (Figure 1a). Fitted
with a tolerance of 0.1, it becomes three arcs with radii within 0.07 of 10 (Figure 1b). The magnified view shows the
measured points in gray around the fitted arc, which runs between them, not through them.

```python
random.seed(1)
pts = []
for k in range(256):
    t = 2 * math.pi * k / 256
    r = 10 + random.uniform(-0.03, 0.03)
    pts.append((r * math.cos(t), r * math.sin(t)))
bore = px.Face([px.FromPolyline(pts, closed=True)])
fitted = bore.Copy().FitArcs(tol=0.1, max_edge_length=0.5)
```

```python
spot = (10 * math.cos(math.radians(30)), 10 * math.sin(math.radians(30)))
lens = {"spot": spot, "r": 0.7, "at": (-0.5, -1.5), "R": 7.0, "half": 0.45, "points": pts}
Row((bore, f"{len(bore.edges)} measured points joined"),
    (fitted, f"Fitted: {len(fitted.edges)} arcs", {"zoom": lens}),
    caption="A measured bore fitted with arcs, tolerance 0.1")
```

```text
<IPython.core.display.HTML object>
```

```python
print("radii", [round(e.edge.radius, 4) for e in fitted.edges])
print("area", fitted.mass, "circle", math.pi * 100)
```

```text
radii [10.0066, 10.0603, 9.9984]
area 314.21891028047474 circle 314.1592653589793
```

## Tolerance

A fit needs a tolerance above the noise of its points. The example is a unit circle of 64 points with their
coordinates rounded to three digits. Below the rounding, the fits deviate from the edges by more than allowed and are
rejected, so the 64 edges remain. Just above it, only some chains fit, and well above it, the circle becomes two arcs.

```python
rounded = [(round(math.cos(2 * math.pi * k / 64), 3), round(math.sin(2 * math.pi * k / 64), 3)) for k in range(64)]
circle = px.Face([px.FromPolyline(rounded, closed=True)])
for tol in (1e-5, 1e-4, 1e-3):
    f = circle.Copy().FitArcs(tol=tol, max_edge_length=0.2)
    radii = [e.edge.radius for e in f.edges if hasattr(e.edge, "radius")]
    error = max(abs(r - 1) for r in radii) if radii else "none, no arcs fitted"
    print("tol", tol, ":", len(f.edges), "edges, largest radius error", error)
```

```text
tol 1e-05 : 64 edges, largest radius error none, no arcs fitted
tol 0.0001 : 40 edges, largest radius error 0.01307012691401388
tol 0.001 : 2 edges, largest radius error 2.294391787671657e-09
```

## On export

The exports take the same options, and the exported face of the rounded circle has 2 edges instead of 64:

```python
exported = circle.ToOCCShape(fit_arcs=dict(max_edge_length=0.2, tol=1e-3))
print(len(exported.edges), "edges")
```

```text
2 edges
```
