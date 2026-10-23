# Model rocket: von Kármán nose cone and swept fins

The example is a model rocket with a body tube of radius `R` = 20 and 300 long, a von Kármán nose cone `nose` = 120
long, a boat tail of 30 with the nozzle, and four swept fins. The nose is the Haack curve
r = R/√π √(θ − sin 2θ / 2), x = L (1 − cos θ)/2, drawn as a spline on the plane of symmetry and revolved. The fins
are sketched on that plane, turned about the axis. Lengths are in mm.

![A model rocket with a von Kármán nose cone and four swept fins](rocket_files/rocket_1.png){.result}

```python
import math
import netgen.occ as occ
from ngsolve_webgpu.jupyter import Draw
import planex as px
```

## Nose cone

Every part is drawn on `side`, the plane of symmetry, and revolved about the x axis. The points of the nose sit more
densely towards the pointed tip, where the curve bends most. The spline leaves the tip along the axis of symmetry
and meets the tube level by itself (Figure 1).

```python
R = 20.0
nose = 120.0
side = px.Plane(normal=(0, -1, 0), axis=(1, 0, 0))
axis = occ.Axis((0, 0, 0), occ.X)


def haack(x):
    theta = math.acos(1 - 2 * x / nose)
    return R / math.sqrt(math.pi) * math.sqrt(theta - math.sin(2 * theta) / 2)


points = [(nose * (k / 40) ** 2, haack(nose * (k / 40) ** 2)) for k in range(1, 41)]
profile = px.QuickSketch(side).Spline(points, tangents={0: (0, 1)}, name="haack").Line((nose, 0)).Close()
cone = profile.Face().ToOCCFace().Revolve(axis, 360)
```

```python
Row((profile, "The half section of the nose cone"), caption="The Haack curve as a spline")
```

```text
<IPython.core.display.HTML object>
```

**Check:** the von Kármán nose has the volume π R² L / 2, and the revolved spline comes close to it:

```python
print("nose volume", cone.mass, "exact", math.pi * R**2 * nose / 2)
```

```text
nose volume 75396.23397473701 exact 75398.22368615503
```

## Body, boat tail, nozzle

The tube is a rectangle of 300 × `R`, revolved. The boat tail narrows to 0.7 `R` over its 30, and the nozzle bell is
a wall of two splines, from 4 inside the boat tail to 22 beyond it (Figure 2).

```python
end = nose + 300
tube = px.QuickSketch(side).Move((nose, 0)).Rectangle(300, R).Face().ToOCCFace().Revolve(axis, 360)
taper = px.QuickSketch(side).Move((end, 0)).Line((end, R)).Line((end + 30, 0.7 * R)).Line((end + 30, 0)).Close()
boat_tail = taper.Face().ToOCCFace().Revolve(axis, 360)
nozzle = px.QuickSketch(side).Move((end + 26, 5)).Spline([(end + 38, 7), (end + 52, 12)]).Line((end + 52, 13))
nozzle.Spline([(end + 38, 8), (end + 26, 6)]).Close()
bell = nozzle.Face().ToOCCFace().Revolve(axis, 360)
```

```python
Row((taper, "The boat tail and the nozzle bell", {"fills": [(nozzle.Face(), LIGHT)]}),
    caption="The half sections of the tail")
```

```text
<IPython.core.display.HTML object>
```

## Fins

Each fin is a trapezoid with a root chord of `c_root` = 60, a tip chord of `c_tip` = 25 and a span of `span` = 40,
swept back by 35. It is drawn on the plane of symmetry, moved half the fin thickness of 2 off it and extruded
through, with its root 1 inside the tube, and it ends 5 before the boat tail. The four fins sit on that plane, turned
by 90° each (Figure 3).

```python
c_root = 60.0
c_tip = 25.0
span = 40.0
x_f = end - c_root - 5


def fin(angle):
    plane = side.Rotated((0, 0, 0), (1, 0, 0), angle).Offset(-1)
    sketch = px.QuickSketch(plane).Move((x_f, R - 1)).Line((x_f + 35, R + span)).Line((x_f + 35 + c_tip, R + span))
    return sketch.Line((x_f + c_root, R - 1)).Close().Face()


fins = [fin(90 * k) for k in range(4)]
fin_solids = [f.ToOCCFace().Extrude(2) for f in fins]
```

```python
Row((fins[0], "A fin, the surface of the tube dashed", {"dashed": [px.FromPolyline([(x_f - 10, R), (end, R)])]}),
    caption="A fin, with its root 1 inside the tube")
```

```text
<IPython.core.display.HTML object>
```

**Check:** the fin is a trapezoid of the mean chord times its span plus the 1 inside the tube:

```python
print("fin area", fins[0].mass, "trapezoid", (c_root + c_tip) / 2 * (span + 1))
```

```text
fin area 1742.5 trapezoid 1742.5
```

## The rocket

The nose and the fins are red, the tube white, the boat tail gray and the nozzle dark:

```python
parts = [(cone, (0.80, 0.12, 0.12)), (tube, (0.95, 0.95, 0.96)), (boat_tail, (0.6, 0.6, 0.62)),
         (bell, (0.25, 0.25, 0.28))]
parts += [(f, (0.80, 0.12, 0.12)) for f in fin_solids]
for shape, color in parts:
    shape.faces.col = color
Draw(occ.Compound([shape for shape, color in parts]), euler_angles=[-24.97, 10.41, 2.95])
```

```text
<IPython.core.display.HTML object>
```
