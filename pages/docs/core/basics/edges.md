# Lines and arcs

planex2d has three exact edge types: lines, circular arcs and full circles. Each constructor returns a wire of one
edge, so the result can go straight into a face, a Boolean operation or an export, and `wire.edges[0]` gives the
edge itself. Smooth curves through points are described in [Splines](splines.md).

```python
import math
import planex as px
```

## Lines and arcs

A line runs between two points. An arc runs from its start to its end around a center, counterclockwise unless
`reverse=True` is given. Figure 1 shows a line from (0, 0) to (30, 40) (a), a quarter circle of radius 10 from
(10, 0) to (0, 10) (b), and the other three quarters between the same points, drawn clockwise (c). The ends are
marked.

```python
line = px.Line((0, 0), (30, 40))
quarter = px.Arc((10, 0), (0, 10), center=(0, 0))
rest = px.Arc((10, 0), (0, 10), center=(0, 0), reverse=True)
```

```python
circle = px.Circle((0, 0), 10)
Row((line, "A line", {"marks": Vertices(line)}),
    (quarter, "Counterclockwise: a quarter", {"marks": Vertices(quarter), "dashed": [circle]}),
    (rest, "reverse=True: three quarters", {"marks": Vertices(rest), "dashed": [circle]}),
    caption="A line and two arcs between the same points")
```

```text
<IPython.core.display.HTML object>
```

The lengths are 50, 5π and 15π, the last one up to the rounding of its final digit:

```python
print("line:", line.mass)
print("quarter / π:", quarter.mass / math.pi)
print("three quarters / π:", rest.mass / math.pi)
```

```text
line: 50.0
quarter / π: 5.0
three quarters / π: 14.999999999999998
```

## Full circles

`px.Circle(center, r)` makes a full circle. It is one exact edge with no vertices. Nothing splits it into arcs until
another edge crosses or touches it, and a face with a circular hole keeps the hole as one edge.

```python
ring = px.Circle((0, 0), 10)
edge = ring.edges[0]
print(len(ring.edges), "edge,", len(ring.vertices), "vertices")
print("center", tuple(edge.center), "radius", edge.edge.radius)
print("length / π:", ring.mass / math.pi)
```

```text
1 edge, 0 vertices
center (0.0, 0.0) radius 10.0
length / π: 20.0
```

## What an edge knows

Every edge provides its start, its end and its midpoint. An arc also provides its center, its radius and its sweep in
degrees. The sweep is negative when the arc runs clockwise. Figure 2 shows these points for both arcs.

```python
e = quarter.edges[0]
print("start", tuple(e.start), "end", tuple(e.end), "midpoint", tuple(e.midpoint))
print("center", tuple(e.center), "radius", e.edge.radius, "sweep", e.edge.sweep)
print("sweep of the three quarters:", rest.edges[0].edge.sweep)
```

```text
start (10.0, 0.0) end (0.0, 10.0) midpoint (7.0710678118654755, 7.071067811865475)
center (0.0, 0.0) radius 10.0 sweep 90.0
sweep of the three quarters: -270.0
```

```python
def points(arc):
    e = arc.edges[0]
    return [("start", e.start), ("end", e.end), ("mid", e.midpoint), ("center", e.center)]


def labels(arc):
    out = []
    for text, p in points(arc):
        r = math.hypot(p.x, p.y)
        out.append((text, (p.x * (1 + 2.0 / r), p.y * (1 + 2.0 / r)) if r else (0, -1.8)))
    return out


Row((quarter, "The quarter", {"marks": [p for _, p in points(quarter)], "labels": labels(quarter),
                              "dashed": [circle], "view": ((-13.5, 13.5), (-13.5, 13.5))}),
    (rest, "The three quarters", {"marks": [p for _, p in points(rest)], "labels": labels(rest),
                                  "dashed": [circle], "view": ((-13.5, 13.5), (-13.5, 13.5))}),
    caption="The points every arc provides")
```

```text
<IPython.core.display.HTML object>
```

The edge data that belongs to the edge's geometry, such as the radius and the sweep, sits on `e.edge`. The points,
the length and the properties sit on `e` itself.
