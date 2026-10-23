# Solar panel shading over a day

The example is a solar panel of 3 × 2 on a roof, with a chimney beside it and a neighbor's wall to the south. Seen
from the sun, everything is flat. Projected along the sun's direction onto a plane, the panel and the obstacles become
planex2d faces, and the part of the panel covered by obstacles is in the shade. The example follows this over a day,
hour by hour, at the solstices and the equinox. Lengths are in m.

![The shaded share of a roof-mounted solar panel over a day](solar-shading_files/solar-shading_1.png){.result}

```python
import math
import warnings
import matplotlib.pyplot as plt
import netgen.occ as occ
from ngsolve_webgpu.jupyter import Draw
import planex as px
```

## The scene

The roof is inclined 35° towards the south (x east, y north, z up). `on_roof` gives a point on the roof, u along the
eaves and v up the slope, lifted off it by `lift`. The scene:

- the roof, 12 wide and 8 up the slope;
- the panel, 3 × 2, 1 up the slope and 0.05 above the roof;
- the chimney, 0.6 × 0.6, on the roof east of the panel;
- the wall, 10 long and 0.5 thick, 8 m to the south.

The roof and the panel are planex2d rectangles on planes along the roof, and the chimney and the wall are boxes. Only
the parts of the obstacles above the roof can shade the panel.

```python
tilt = math.radians(35)
up_slope = (0.0, math.cos(tilt), math.sin(tilt))
normal = (0.0, -math.sin(tilt), math.cos(tilt))


def on_roof(u, v, lift=0.0):
    return (u, v * up_slope[1] + lift * normal[1], v * up_slope[2] + lift * normal[2])


roof = px.QuickSketch(px.Plane(origin=on_roof(-4, 0), normal=normal, axis=(1, 0, 0))).Rectangle(12, 8)
roof = roof.Face().ToOCCFace()
panel = px.QuickSketch(px.Plane(origin=on_roof(0, 1, 0.05), normal=normal, axis=(1, 0, 0))).Rectangle(3, 2)
panel = panel.Face().ToOCCFace()
base = on_roof(4.0, 2.0)
chimney = occ.Box(occ.Pnt(base[0], base[1], base[2] - 0.5), occ.Pnt(base[0] + 0.6, base[1] + 0.6, base[2] + 1.4))
wall = occ.Box(occ.Pnt(-3, -8.5, -6), occ.Pnt(7, -8, 4.0))
above = occ.Box(occ.Pnt(-50, -50, 0), occ.Pnt(50, 50, 50)).Rotate(occ.Axis((0, 0, 0), occ.X), 35)
obstacles = [chimney * above, wall * above]
roof.faces.col = (0.6, 0.3, 0.25)
panel.faces.col = (0.1, 0.2, 0.5)
Draw(occ.Compound([roof, panel, chimney, wall]))
```

```text
<IPython.core.display.HTML object>
```

## The sun

The location is Vienna, at latitude 48.2°. The sun's elevation and direction follow from the day's declination and
the hour angle (solar time).

```python
latitude = math.radians(48.2)

def sun(day, hour):
    declination = math.radians(23.44) * math.sin(2 * math.pi * (284 + day) / 365)
    H = math.radians(15 * (hour - 12))
    elevation = math.asin(math.sin(latitude) * math.sin(declination)
                          + math.cos(latitude) * math.cos(declination) * math.cos(H))
    east = -math.cos(declination) * math.sin(H)
    north = math.cos(latitude) * math.sin(declination) - math.sin(latitude) * math.cos(declination) * math.cos(H)
    return (east, north, math.sin(elevation)), math.degrees(elevation)
```

## Seen from the sun

The view plane faces the sun. `flat` projects every face of an obstacle onto it and unites them, and the share of the
panel's projection that the obstacles cover is the shaded share of the panel.

```python
def view(direction):
    d = direction
    h = (-d[1], d[0], 0.0) if abs(d[2]) < 0.99 else (1.0, 0.0, 0.0)
    return px.Plane(normal=d, axis=h)


def flat(shape, axes):
    pieces = []
    for f in shape.faces:
        try:
            with warnings.catch_warnings():
                warnings.simplefilter("ignore", px.PlanexWarning)
                piece = px.Face(px.Project(f, axes))
        except Exception:
            continue
        if piece.mass > 1e-9:
            pieces.append(piece)
    return px.Unify(pieces) if pieces else None


def shade(direction):
    axes = view(direction)
    target = px.Face(px.Project(panel, axes))
    covers = [c for c in (flat(o, axes) for o in obstacles) if c is not None]
    covered = (target * px.Unify(covers)).mass if covers else 0.0
    return target, covers, covered / target.mass
```

**Check:** projected onto the view plane, the panel keeps its area of 6 m² times the cosine of the sun's angle to the
panel. At the equinox at 10:00 (solar time):

```python
direction, elevation = sun(80, 10.0)
target, covers, shaded = shade(direction)
cosine = sum(a * b for a, b in zip(direction, normal)) / math.hypot(*direction)
print("sun", round(elevation, 1), "degrees high, panel seen from the sun", target.mass, "m², 6 m² times cos",
      6 * cosine, "m²")
print("shaded", round(100 * shaded, 1), "%")
```

```text
sun 34.9 degrees high, panel seen from the sun 5.049090637440053 m², 6 m² times cos 5.0490906374400515 m²
shaded 0.0 %
```

At 10:00 at the equinox, the obstacles miss the panel. At the winter solstice, with the sun low, the wall's shadow
covers much of it in the morning (Figure 1).

```python
winter = [h / 2 for h in range(16, 33) if sun(355, h / 2)[1] > 5]
low = min(winter, key=lambda h: abs(shade(sun(355, h)[0])[2] - 0.4))
panels = []
for title, (day, hour) in (("Equinox", (80, 10.0)), ("Winter solstice", (355, low))):
    seen, shadows, share = shade(sun(day, hour)[0])
    fills = [(c, "#c3cbd3") for c in shadows]
    if shadows:
        fills.append((seen * px.Unify(shadows), GUIDE))
    (x0, y0), (x1, y1) = seen.bounding_box
    cx, cy, r = (x0 + x1) / 2, (y0 + y1) / 2, 0.9 * max(x1 - x0, y1 - y0)
    label = f"{title}, {int(hour)}:{round(60 * (hour % 1)):02d}: {100 * share:.0f} % shaded"
    panels.append((seen, label, {"fills": fills, "view": ((cx - r, cx + r), (cy - r, cy + r))}))
Row(*panels, caption="The panel seen from the sun, the obstacles grey and the shaded part orange")
```

```text
<IPython.core.display.HTML object>
```

## Over the day

Every quarter of an hour from 4:00 to 20:00 while the sun shines on the panel, the shaded share and the sunlight that
the panel catches are recorded: the cosine of the sun's angle times the unshaded share. The loss compares that with
the same panel without obstacles:

```python
days = {"winter solstice": 355, "equinox": 80, "summer solstice": 172}
curves = {}
for label, day in days.items():
    hours, shares, light, clear = [], [], [], []
    for quarter in range(4 * 4, 4 * 20 + 1):
        hour = quarter / 4
        direction, elevation = sun(day, hour)
        cosine = sum(a * b for a, b in zip(direction, normal))
        if elevation <= 0 or cosine <= 0:
            continue
        share = shade(direction)[2]
        hours.append(hour)
        shares.append(share)
        light.append(cosine * (1 - share))
        clear.append(cosine)
    curves[label] = (hours, shares, light)
    print(label, round(100 * (1 - sum(light) / sum(clear)), 1), "% of the day's direct light lost to shade")
```

```text
winter solstice 22.5 % of the day's direct light lost to shade
equinox 0.9 % of the day's direct light lost to shade
summer solstice 1.6 % of the day's direct light lost to shade
```

```python
Chart("solar time [h]", "shaded share", lines={label: (h, s) for label, (h, s, l) in curves.items()},
      title="The shaded share of the panel")
Chart("solar time [h]", "direct light", lines={label: (h, l) for label, (h, s, l) in curves.items()},
      title="The direct light on the panel: cosine times the unshaded share")
```

```text
<IPython.core.display.HTML object>
```

```text
<IPython.core.display.HTML object>
```
