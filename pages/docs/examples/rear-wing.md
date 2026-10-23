# Formula Student rear wing with DRS

The example is the three-element rear wing of a Formula Student car with DRS: on the straights, the top flap swings
open about a hinge at its trailing edge to cut drag. In 3D, the wing is a part with its endplates. In 2D, it is a
section, meshed in a wind tunnel. Lengths are in chords of the main plane.

![A three-element Formula Student rear wing with endplates](rear-wing_files/rear-wing_1.png){.result}

```python
import math
import netgen.occ as occ
import ngsolve as ngs
from ngsolve_webgpu.jupyter import Draw
import planex as px
```

## The elements

The elements are NACA 6412 profiles turned upside down, so that they push the car down:

| Element | Chord | Incidence |
|---|---|---|
| `main` | 1 | 3° |
| `flap1` | 0.45 | 16° |
| `flap2`, the DRS flap | 0.35 | 30° |

Each flap starts 0.06 ahead of the trailing edge of the element before it and above it, by 0.042 and 0.03, so that
the elements overlap with a slot of about 2 % of the main chord between them, as on real wings. Through the slot,
fresh air reaches the flap's suction side. `wing` places the elements one after the other and turns the DRS flap about
its hinge at its trailing edge (the dot in Figure 1).

```python
elements = [("main", 1.00, 3.0), ("flap1", 0.45, 16.0), ("flap2", 0.35, 30.0)]


def element(name, chord, incidence, leading_edge):
    face = px.FromNACA("6412", chord=chord).Mirrored((0, 0), (1, 0)).Rotated((0, 0), incidence)
    face = face.Translated(leading_edge)
    face.edges.name = name
    a = math.radians(incidence)
    return face, (leading_edge[0] + chord * math.cos(a), leading_edge[1] + chord * math.sin(a))


def wing(opening=0.0):
    faces, leading_edge = [], (0.0, 0.0)
    for (name, chord, incidence), gap in zip(elements, (0.042, 0.03, 0.0)):
        face, trailing_edge = element(name, chord, incidence, leading_edge)
        faces.append(face)
        leading_edge = (trailing_edge[0] - 0.06, trailing_edge[1] + gap)
    faces[-1].Rotate(trailing_edge, -opening)
    return faces, trailing_edge
```

Opening the DRS turns the flap from 30° to 17°, by `turn` = 13°. It lifts the flap's nose away from the flap below and
widens the slot between them (Figure 1):

```python
turn = 30 - 17
closed_faces, hinge = wing()
open_faces, _ = wing(turn)
print("slots closed", [round(a.Distance(b), 4) for a, b in zip(closed_faces, closed_faces[1:])])
print("slots open", [round(a.Distance(b), 4) for a, b in zip(open_faces, open_faces[1:])])
```

```text
slots closed [0.0211, 0.0272]
slots open [0.0211, 0.0895]
```

```python
Row((px.Compound(closed_faces), "DRS closed", {"dots": [hinge]}),
    (px.Compound(open_faces), "DRS open", {"dots": [hinge]}),
    caption="The three elements, with the DRS flap's hinge")
```

```text
<IPython.core.display.HTML object>
```

## In 3D

The united section is extruded over a span of 3. The endplate is sketched on the end face of the extrusion, as a
rectangle 0.3 larger on every side than the closed wing, and extruded outwards by 0.03. A copy moved by the span closes
the other side.

```python
(x0, y0), (x1, y1) = px.Unify(closed_faces).bounding_box


def wing3d(opening):
    body = px.Unify(wing(opening)[0]).ToOCCShape().Extrude(3)
    end = px.QuickSketch(body.faces.Max(occ.Z))
    end.Move((x0 - 0.3, y0 - 0.3)).Rectangle(x1 - x0 + 0.6, y1 - y0 + 0.6)
    plate = end.Face().ToOCCFace().Extrude(0.03)
    other = plate.Move((0, 0, -3.03))
    body.faces.col = (0.624, 0.702, 0.784)
    for p in (plate, other):
        p.faces.col = (0.80, 0.12, 0.12)
    return occ.Compound([body, plate, other])

Draw(wing3d(0), euler_angles=[30, 40, 0])
```

```text
<IPython.core.display.HTML object>
```

The same wing with the DRS open:

```python
Draw(wing3d(turn), euler_angles=[30, 40, 0])
```

```text
<IPython.core.display.HTML object>
```

## In the wind tunnel

The 2D section sits in a channel of 9 × 5 without endplates, with uniform inflow from the left, free outflow on the
right, and slip walls on the top and bottom. The mesh sizes:

- the wing's edges get elements of 0.004 and, through hp refinement, layers of thin elements towards them for the
  boundary layers;
- a region of 5.3 × 1.5 around the wing and its wake gets elements of 0.04, fine enough for the vortices;
- the rest of the channel gets elements of 0.25.

The elements keep their edge names through the subtraction, so the wing is a named boundary of the fluid.

```python
def tunnel():
    channel = px.QuickSketch((-2, -2.5))
    channel.Line((7, -2.5), name="bottom").Line((7, 2.5), name="outlet").Line((-2, 2.5), name="top")
    channel.Close(name="inlet")
    wake = px.QuickSketch((-0.3, -0.5)).Rectangle(5.3, 1.5).Face()
    wake.maxh = 0.04
    faces, _ = wing()
    for f in faces:
        f.edges.maxh = 0.004
        f.edges.hpref = 1
    return px.Subtract([px.Glue([channel.Face(), wake]), *faces])

mesh = ngs.Mesh(px.OCCGeometry(tunnel()).GenerateMesh(maxh=0.25))
mesh.RefineHP(levels=2)
mesh.Curve(3)
print(mesh.ne, "elements")
Draw(mesh)
```

```text
24032 elements
```

```text
<IPython.core.display.HTML object>
```
