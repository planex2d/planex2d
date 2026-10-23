# Cycloidal drive: the disc cut by its pins

The example is a cycloidal drive, the gearbox in many robot joints. A disc with N − 1 lobes rolls inside a ring of N
pins. The
input crank moves the disc's center on a small circle, and for every turn of the crank, the disc turns back by one
lobe, so the output turns N − 1 times slower and in the opposite direction. The disc's outline is not drawn; it is
what is left of a blank when every pin, at every crank angle, is cut away. Lengths are in mm.

![A cycloidal disc wobbling inside its ring of pins](cycloidal-drive_files/cycloidal-drive_1.gif){.result}

```python
import math
import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from IPython.display import HTML
import planex as px
```

## The dimensions

The ring has `N` = 12 pins of radius `pin` = 3 on a circle of `R` = 30, and the crank's eccentricity is `e` = 1.5.
The output has six pins of radius 2.5 on a circle of 15. The disc has N − 1 = 11 lobes, so the ratio is 11 to 1:

```python
N = 12
R = 30.0
pin = 3.0
e = 1.5
lobes = N - 1
ring = [(R * math.cos(2 * math.pi * k / N), R * math.sin(2 * math.pi * k / N)) for k in range(N)]
print(N, "pins,", lobes, "lobes")


def disc(x, y, r):
    return px.QuickSketch().Circle(x, y, r).Face()
```

```text
12 pins, 11 lobes
```

## The disc

Seen from the disc, the pins move. At crank angle θ, the disc's center is at e (cos θ, sin θ), and the disc has
turned by −θ / (N − 1). Cutting all pins at 360 crank angles out of a blank leaves the cycloidal outline. The disc also
gets a bore of 16 for the eccentric and holes for the output pins, e wider than the pins so that they can follow the
wobble (Figure 1).

```python
def from_disc(theta):
    turn = -math.degrees(theta) / lobes
    cx, cy = e * math.cos(theta), e * math.sin(theta)
    return [disc(x, y, pin).Translated((-cx, -cy)).Rotated((0, 0), -turn) for x, y in ring]


cuts = px.Unify([p for k in range(360) for p in from_disc(2 * math.pi * k / 360)])
holes = [disc(15 * math.cos(math.pi * k / 3), 15 * math.sin(math.pi * k / 3), 2.5 + e) for k in range(6)]
cam = px.Subtract([disc(0, 0, R), cuts, disc(0, 0, 8.0), *holes]).faces[0]
print("disc area", round(cam.mass, 3), "mm², outline", round(cam.outer.mass, 3), "mm long")
```

```text
disc area 1820.236 mm², outline 187.036 mm long
```

```python
Row((cam, "The disc inside its ring of pins", {"fills": [(disc(x, y, pin), LIGHT) for x, y in ring]}),
    caption="The cycloidal disc, cut by every pin at every crank angle")
```

```text
<IPython.core.display.HTML object>
```

**Check:** the outline crosses the circle halfway between its tips and its valleys twice per lobe:

```python
radii = [math.hypot(x, y) for x, y in cam.outer.ToShapely(0.01).coords]
middle = (max(radii) + min(radii)) / 2
crossings = cam.outer.Intersections(px.QuickSketch().Circle(middle).Wire())
print(len(crossings), "crossings,", len(crossings) // 2, "lobes")
```

```text
22 crossings, 11 lobes
```

## Running

The disc placed at a crank angle must touch the pins without overlapping them. With pins 0.01 thinner, the ones
within 0.02 of the disc count as in contact. In a perfect cycloidal drive, every pin touches the disc at every moment,
and half of them push. The output pins must stay free inside their holes. **Checks** every 10° of the crank:

```python
def placed(theta):
    turn = -math.degrees(theta) / lobes
    return cam.Rotated((0, 0), turn).Translated((e * math.cos(theta), e * math.sin(theta)))


def output_pins(theta):
    turn = -theta / lobes
    return [disc(15 * math.cos(turn + math.pi * k / 3), 15 * math.sin(turn + math.pi * k / 3), 2.5)
            for k in range(6)]


worst, touching = 0.0, []
for k in range(0, 360, 10):
    theta = 2 * math.pi * k / 360
    body = placed(theta)
    pins = [disc(x, y, pin - 0.01) for x, y in ring]
    worst = max([worst] + [(body * p).mass for p in pins] + [(body * p).mass for p in output_pins(theta)])
    touching.append(sum(1 for p in pins if body.Distance(p) < 0.02))
print("largest overlap", worst, "mm²")
print("pins in contact", min(touching), "to", max(touching), "of", N)
```

```text
largest overlap 0.0 mm²
pins in contact 12 to 12 of 12
```

## In motion

During two turns of the crank, the disc wobbles, and the output with its pins turns by two lobes the other way.

```python
fig, ax = plt.subplots(figsize=(6, 6))
pins_drawn = [disc(x, y, pin) for x, y in ring]


def frame(k):
    theta = 2 * math.pi * k / 36
    ax.clear()
    for p in pins_drawn:
        p.Plot(ax=ax, face_col="dimgray", legend=False)
    placed(theta).Plot(ax=ax, face_col="tab:orange", legend=False)
    for p in output_pins(theta):
        p.Plot(ax=ax, face_col="tab:blue", legend=False)
    ax.plot([0, e * math.cos(theta)], [0, e * math.sin(theta)], "o-", color="tab:red")
    ax.set_xlim(-36, 36)
    ax.set_ylim(-36, 36)
    ax.set_aspect("equal", adjustable="box")
    ax.axis("off")


animation = FuncAnimation(fig, frame, frames=72, interval=60)
plt.close(fig)
HTML(animation.to_jshtml())
```

```text
<IPython.core.display.HTML object>
```
