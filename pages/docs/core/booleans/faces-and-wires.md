# Faces and wires

Faces combine with faces, and wires with wires. `face + wire` raises a `TypeError`. A union of wires cuts them at
every crossing and keeps overlapping stretches once, while gluing cuts them the same way but keeps every wire as its
own part. See [Splitting and trimming](split-trim.md) for cutting a face along a wire.

```python
import math
import planex as px
```

## A truss from its member center lines

The example is a Pratt truss with a span of 12 in four panels of 3 and a height of 2, its members drawn as single
lines: the two chords, five verticals and four diagonals that slope down toward the middle. The union cuts the chords
at the panel points, so every edge of the result runs from one joint to the next (Figure 1, with the joints marked).

```python
members = [px.Line((0, 0), (12, 0)), px.Line((0, 2), (12, 2))]
members += [px.Line((3 * k, 0), (3 * k, 2)) for k in range(5)]
members += [px.Line((0, 2), (3, 0)), px.Line((3, 2), (6, 0)), px.Line((6, 0), (9, 2)), px.Line((9, 0), (12, 2))]
truss = px.Unify(members)
```

```python
Row((truss, "A Pratt truss, its joints marked", {"fill": False, "marks": Vertices(truss)}),
    caption="The union of the member center lines")
```

```text
<IPython.core.display.HTML object>
```

The members add up to two chords of 12, five verticals of 2 and four diagonals of √13. The `mass` of a shape is the
area of its faces, so the length is summed over its wires:

```python
length = sum(wire.mass for wire in truss.wires)
print(len(truss.edges), "edges, length", length, "exact", 2 * 12 + 5 * 2 + 4 * math.sqrt(13))
```

```text
17 edges, length 48.42220510185596 exact 48.42220510185596
```

## Glued, the members stay separate

`px.Glue` cuts the wires against each other as the union does, but every member remains its own wire:

```python
glued = px.Glue(members)
print(len(glued.wires), "wires from", len(members), "members")
```

```text
11 wires from 11 members
```

## Overlapping stretches are kept once

The example is two lines on the x axis, from 0 to 2 and from 1 to 3. They unite into a single edge of length 3:

```python
merged = px.Line((0, 0), (2, 0)) + px.Line((1, 0), (3, 0))
print(len(merged.edges), "edge, length", merged.wires[0].mass)
```

```text
1 edge, length 3.0
```
