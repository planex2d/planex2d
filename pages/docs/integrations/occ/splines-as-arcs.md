# Splines as arcs

planex2d computes with splines through a chain of arcs, with lines where the curve is straight.
`approximation=True` exports that chain instead of the B-spline, for tools that accept only lines and arcs. Figure 1
shows a spline through four marked points and a magnified piece of it with the joints of the chain that planex2d
computes with:

```python
import planex as px

curve = px.Spline([(0, 0), (1, 0.6), (2, 0), (3, 0.8)])
chain = curve.edges[0].edge.approximation
print(len(curve.ToOCCWire().edges), "edge,", len(curve.ToOCCWire(approximation=True).edges), "with approximation=True")
```

```text
1 edge, 142 with approximation=True
```

```python
joints = [(a.start.h, a.start.v) for a in chain] + [(chain[-1].end.h, chain[-1].end.v)]
p = curve.edges[0].PointAt(0.15)
view = ((p.h - 0.12, p.h + 0.12), (p.v - 0.09, p.v + 0.09))
Row((curve, "A spline", {"fill": False, "marks": [(0, 0), (1, 0.6), (2, 0), (3, 0.8)]}),
    (curve, f"Magnified: joints of its {len(chain)} pieces", {"fill": False, "marks": joints, "view": view}),
    caption="A spline and the chain of arcs it is computed with")
```

```text
<IPython.core.display.HTML object>
```

```python
print(sorted({type(e).__name__ for e in chain}), len(chain), "pieces")
```

```text
['ArcEdge', 'LineEdge'] 142 pieces
```
