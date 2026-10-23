# Distances and clearances

`Distance` measures how far an object is from points, edges, wires, faces, shapes and OpenCASCADE shapes, for
clearances and wall thicknesses. Inside a face, the distance is 0. `components` returns the shortest connection
itself instead of its length, in the plane with `"hv"` or in 3D with `"xyz"`, and `ClosestPoint` returns where it
starts. All examples on this page use the link from [Tangents](../sketch/tangent.md), two bosses of radius r1 = 1.5
and r2 = 1 with their centers d = 6 apart, with a hole of radius 0.6 and 0.4 in each.

```python
import math
import planex as px

link = px.QuickSketch()
link.Circle(0, 0, 1.5, id="boss1", construction=True)
link.Circle(6, 0, 1.0, id="boss2", construction=True)
link.Line(tangent=("boss1", "boss2"), id="upper")
link.Line(tangent=("boss1", "boss2"), reverse=True, id="lower")
link.Arc("lower", "upper", congruent="boss2", id="end2")
link.Arc("upper", "lower", congruent="boss1", id="end1")
link.Circle(0, 0, 0.6, id="hole1").Circle(6, 0, 0.4, id="hole2")
face = link.Face()
r1, r2, d = 1.5, 1.0, 6.0
```

## Wall thickness around the holes

The distance from each hole to the outline is the wall around it, r2 − 0.4 at the small boss and r1 − 0.6 at the
large one:

```python
print(sorted(h.Distance(face.outer) for h in face.inner), "exact", [r2 - 0.4, r1 - 0.6])
```

```text
[0.5999999999999995, 0.9] exact [0.6, 0.9]
```

## Signed distances

With `signed=True`, a point inside a face gets a negative distance, as in a level set. The example is the middle of
the web, at (3, 0). Its nearest boundary is the two tangent lines, r1 − 3 sin α away, with sin α = (r1 − r2) / d:

```python
print(face.Distance((3, 0), signed=True), "exact", -(r1 - 3 * (r1 - r2) / d))
print(face.Distance((3, 0)), "unsigned, inside the face")
```

```text
-1.25 exact -1.25
0.0 unsigned, inside the face
```

## Clearance and a pin

The example has two shortest connections. Of two links stacked 4 apart, the large bosses come closest, 4 − 2 r1 = 1
apart, dashed in Figure 1a. The shortest connection from the link to a pin at (8, 0.5) runs radially from the small
boss, and `ClosestPoint` returns where it starts, marked together with the pin in Figure 1b.

```python
upper = face.Translated((0, 4))
pin = (8, 0.5)
start = face.ClosestPoint(pin)
```

```python
gap = px.Line((0, r1), (0, 4 - r1))
reach = px.Line(start, pin)
Row((px.Compound([face, upper]), "Two links 4 apart", {"dashed": [gap]}),
    (face, "A pin at (8, 0.5)", {"marks": [pin, start], "dashed": [reach], "view": ((-2, 9), (-2, 2))}),
    caption="Shortest connections: between two links, and to a pin")
```

```text
<IPython.core.display.HTML object>
```

```python
print("links", face.Distance(upper), face.Distance(upper, components="hv"), "exact 1")
r = math.hypot(8 - d, 0.5)
print("pin", face.Distance(pin), "exact", r - r2)
print("connection", face.Distance(pin, components="hv"), "exact", ((8 - d) * (1 - r2 / r), 0.5 * (1 - r2 / r)))
print("closest point", start)
```

```text
links 1.0 (0.0, 1.0) exact 1
pin 1.0615528128088307 exact 1.0615528128088303
connection (1.0298574998546686, 0.25746437496366703) exact (1.0298574998546681, 0.25746437496366703)
closest point (6.970142500145331, 0.24253562503633294)
```

## In 3D and projected

A point 5 above the part is 5 away. The example measures a circle of radius 1 about (10, 0), 5 above the part and to
the right of the small boss, once in 3D, where it is √(2² + 5²) away, and once projected into the plane with
`project=True`, where it is 2 away.

```python
import netgen.occ as occ

hoop = occ.Circle(occ.Pnt(10, 0, 5), occ.Z, 1)
print("point", face.Distance((3, 0, 5)), "exact 5")
print("circle", face.Distance(hoop), "exact", math.sqrt(2**2 + 5**2))
print("circle projected", face.Distance(hoop, project=True), "exact 2")
```

```text
point 5.0 exact 5
circle 5.385164807134504 exact 5.385164807134504
circle projected 2.0 exact 2
```
