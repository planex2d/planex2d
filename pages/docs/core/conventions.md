# API conventions

planex2d uses a few conventions throughout its Python API.

## Chaining and copies

Methods that change an object in place return that object, so calls can be chained. The method of the same name
ending in -ed returns a new object and leaves the original unchanged: `Rotate` and `Rotated`, `Translate` and
`Translated`.

## Reading the arguments

Positional arguments read from … to …. `Line(a, b)` draws a line from a to b, and
`Constrain("Line.1", "Line.2", px.Tangent)` makes Line.1 tangent to Line.2. Sizes follow the ends, as in
`Slot(l, w)`, and flags such as `px.Horizontal` come last, in any order. Keywords are options with values, such as
`name=`, `id=` or `until=`.

A drawing call in a sketch starts at the pen when the start is left out. The pen's direction is used only where
nothing else fixes the curve:

| Call | Reads as |
|---|---|
| `Line(4)` | 4 along the pen's direction |
| `Line(3, 4)` | From the pen, 3 along h and 4 along v |
| `Line((3, 4))` | From the pen to the point (3, 4) |
| `Line((1, 0), (3, 4))` | From (1, 0) to (3, 4) |
| `Arc((3, 4))` | From the pen to (3, 4), tangent to the pen's direction |
| `Line("Line.2", through=(5, 3))` | From the pen through (5, 3) to where that line meets Line.2 |
| `Line((0, 2), parallel="Line.1", until="Line.2")` | From (0, 2), parallel to Line.1, up to Line.2 |
| `Arc((5, 0), sweep=90, center=(0, 0))` | From (5, 0), a quarter turn about the origin |

A helper that fixes the carrier, such as `through=`, `parallel=`, `perpendicular=`, `angle=` or `tangent=`, leaves
both readings as they are. A helper that gives the end, `until=` or `sweep=`, takes the place of the end, so a single
positional argument is then the start. An element as an end needs a helper that fixes the carrier. Without one, the
call raises and names the helpers it accepts.

## Points

A point in the plane is a pair (h, v), given as a tuple, a list or a NumPy row, and a set of points is an array of
shape `(n, 2)`. A 3D point (x, y, z) is accepted as well when it lies in the plane; off the plane, the call raises. In
a sketch, a point can also be a point id such as `"A"` or `"Line.1.end"`, or the pen, `sk.pen`. Where a call needs an
end, an element or another target stands for the point where the carrier meets it, and `(target, point)` picks the
intersection nearest that point.

## Planes

Every argument that takes a plane accepts a `px.Plane` or anything that has a plane: a sketch, a face, a shape, a
wire, a compound or a planar OpenCASCADE shape. `None` means the xy plane, `px.XY`. netgen's `occ.Axes` is accepted
wherever a plane is, so code written for netgen passes its axes unchanged, and 3D points and directions also take
netgen's `Pnt`, `Vec` and `Dir`.

## Angles and units

Angles are in degrees. planex2d does not impose a unit system: lengths use whatever unit you choose, as long as you
use it consistently. See [Units](basics/units.md).

## Queries

Query methods return plain values: numbers, dictionaries or arrays, depending on the query.

## Errors and warnings

A call that cannot perform the requested operation raises an exception that explains why. A valid result that may be
unexpected produces a `px.PlanexWarning`. The library does not silently correct such cases. To make warnings fail
explicitly, for example in automated studies, use Python's warnings module:

```python
import warnings
import planex as px

warnings.simplefilter("error", px.PlanexWarning)
```

For installation requirements and optional dependencies, see [Installation](installation.md).
