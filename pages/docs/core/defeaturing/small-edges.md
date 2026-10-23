# Small edges

Edges shorter than a tolerance are removed, and their two ends meet in the middle. Splines are kept.

```python
import planex as px
```

## A step of 10⁻⁵

The example is an outline 2 × 1 with a step of 10⁻⁵ in its bottom (Figure 1a). Magnified there, the tiny edge runs
between the two marked vertices (Figure 1b). `SmallEdges` lists the edges shorter than a given length:

```python
outline = [(0, 0), (1, 0), (1, 1e-5), (2, 1e-5), (2, 1), (0, 1)]
face = px.Face([px.FromPolyline(outline, closed=True)])
print(face.SmallEdges(1e-4))
```

```text
[1]
```

```python
Row((face, "The outline"), (face, "Magnified at (1, 0)", {"marks": [(1, 0), (1, 1e-5)], "view": ((1 - 3e-5, 1 + 3e-5), (-1.5e-5, 2.5e-5))}),
    caption="A step of 10⁻⁵")
```

```text
<IPython.core.display.HTML object>
```

Once the step is removed, its two ends meet in one vertex, marked in the same view (Figure 2):

```python
face.RemoveSmallEdges(1e-4)
print(len(face.edges), face.SmallEdges(1e-4))
```

```text
5 []
```

```python
Row((face, "Magnified at (1, 0)", {"marks": Vertices(face), "view": ((1 - 3e-5, 1 + 3e-5), (-1.5e-5, 2.5e-5))}),
    caption="The step removed")
```

```text
<IPython.core.display.HTML object>
```

## On export

The exports take `remove_edges_smaller` for the same step. The example exports the outline with its step, once with
the default and once with a tolerance of 10⁻⁴:

```python
raw = px.Face([px.FromPolyline(outline, closed=True)])
print(len(raw.ToOCCShape().edges), len(raw.ToOCCShape(remove_edges_smaller=1e-4).edges))
```

```text
6 5
```
