# Splines

In a sketch, `Spline(points)` runs from the pen through the given points, and lines and arcs join it like any other
edge. `periodic=True` closes it smoothly, and `tangents=` sets its direction at chosen points. A spline through points
interpolates the curve the points come from, so it is close to that curve but not exactly on it. The checks below show
how close. See [Splines](../basics/splines.md) for how planex2d handles splines in general, including the two
interpolation styles, closed splines and points.

```python
import math
import numpy as np
import planex as px
```

## A NACA 0012 airfoil

The airfoil has a chord of 100 and a thickness of 12 %, in the form with a closed trailing edge, with 40 points per
side and cosine spacing. The angle θ runs once around the profile, from the trailing edge over the upper side to the
leading edge and back along the lower side, so the spline starts and ends at the trailing edge. `px.FromNACA("0012")`
creates the same profile in one call. Here it is drawn point by point to show `Spline` in a sketch.

A construction line at 75 % chord runs from the lower to the upper surface. It is the hinge line of a flap and is as
long as the profile is thick there (Figure 1). Its ends are given as `(target, point)`, which selects the
intersection with the profile nearest to the point.

```python
def thickness(x):
    return 60 * (0.2969 * np.sqrt(x) - 0.1260 * x - 0.3516 * x**2 + 0.2843 * x**3 - 0.1036 * x**4)


theta = np.linspace(0, 2 * np.pi, 81)[1:]
x = (1 + np.cos(theta)) / 2
points = np.column_stack([100 * x, np.sign(np.sin(theta)) * thickness(x)])

wing = px.QuickSketch((100, 0)).Spline(points, id="profile")
wing.Move((75, -100), dir=(0, 1))
wing.Line(("profile", (75, -100)), parallel=px.V, until=("profile", (75, 100)), id="hinge", construction=True)
profile = wing.Face()
```

```python
Row((wing, "NACA 0012 from one spline, the hinge line at 75 % chord dashed"))
```

```text
<IPython.core.display.HTML object>
```

The area is checked against the integral of the thickness formula, and the hinge line against the thickness:

```python
exact = 12000 * (0.2969 * 2 / 3 - 0.1260 / 2 - 0.3516 / 3 + 0.2843 / 4 - 0.1036 / 5)
print("area", profile.mass, "exact", exact, "relative", (profile.mass - exact) / exact)
print("hinge", wing.Edge("hinge").mass, "thickness", 2 * thickness(0.75))
```

```text
area 817.0611516356596 exact 817.0599999999997 relative 1.4094872590512479e-06
hinge 6.240877677195613 thickness 6.240878086031979
```

## A closed cam

The cam is a periodic spline through the lift curve r = R + h (1 − cos a) / 2 with R = 20 and h = 8 at 36 points,
with a bore of 10 (Figure 2). The area of the exact lift curve is π (R + h/2)² + π h²/8, and the spline matches it
within the interpolation error:

```python
a = np.radians(np.arange(0, 360, 10))
r = 20 + 8 * (1 - np.cos(a)) / 2
lift = px.Spline(np.column_stack([r * np.cos(a), r * np.sin(a)]), periodic=True)
cam = px.Face([lift, px.Circle((0, 0), 5)])
```

```python
Row((cam, "A cam: one periodic spline through 36 points, and the bore"))
```

```text
<IPython.core.display.HTML object>
```

```python
exact = math.pi * 24**2 + math.pi * 8**2 / 8 - math.pi * 10**2 / 4
print("area", cam.mass, "exact", exact, "relative", (cam.mass - exact) / exact)
```

```text
area 1756.1465567096234 exact 1756.1502933566942 relative -2.127749022930798e-06
```
