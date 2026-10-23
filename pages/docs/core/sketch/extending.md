# Extending edges

`congruent=` draws a line on the carrier of another line, the infinite line it lies on. With a target as its end, the
new line continues the other one until that target. This is how an edge is extended, for example to a point that
exists only on the drawing.

```python
import math
import planex as px
```

## A dovetail's theoretical sharp

A dovetail is dimensioned to its theoretical sharp, the point where its two flanks would meet if they continued above
the surface. The example is the dovetail slide from [Parallel, perpendicular, angle](parallel-perpendicular-angle.md),
drawn there step by step:

```python
s = px.QuickSketch((-30, 0))
s.Line(60, id="bottom")
s.Line(-60, parallel="bottom", distance=30, id="top", construction=True)
s.Line(-60, parallel="bottom", distance=20, id="floor", construction=True)
s.Line(20, angle=(180 + 60, "bottom"), through=(-12, 30), id="left flank line", construction=True)
s.Turn(90).Line(30, id="right side").Turn(90).Line(18, id="right shoulder")
s.Line("floor", angle=(-60, "bottom"), id="right flank")
s.Line("left flank line", parallel="bottom", id="groove floor")
s.Line("top", congruent="left flank line", id="left flank")
s.Line((-30, 30), id="left shoulder").Close(id="left side");
```

The left flank is extended upward by a construction line on its carrier, at 60° through the groove's corner. The right
flank is extended from its start, on its own carrier, until it meets that line. The end of this second line is the
sharp, marked in Figure 1.

```python
s.Line(40, angle=(60, "bottom"), through=(-12, 30), id="left flank up", construction=True)
s.Move("right flank.start")
s.Line("left flank up", congruent="right flank", id="sharp", construction=True)
apex = s.Location("sharp.end")
```

```python
Row((s, "The dovetail's flanks extended to the theoretical sharp", {"fill": False, "marks": [apex]}))
```

```text
<IPython.core.display.HTML object>
```

The sharp lies a/2 · tan α above the surface, with the width a = 24 at the surface and the flank angle α = 60°:

```python
print("apex", apex[1], "exact", 30 + 12 * math.tan(math.radians(60)))
```

```text
apex 50.78460969082654 exact 50.78460969082652
```
