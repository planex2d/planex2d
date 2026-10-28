# Changelog

## v0.2.1

### Fixed

- `Sketch.Update` is fast again where it moves geometry without changing a drawn shape (a circle moved by its position
  dimensions, any rigid move): it took thousands of smallest steps, about 0.4 s per update, now under a millisecond

### Packaging

- Windows wheels follow in a later release (not in v0.2.1, as v0.2.0 said)

## v0.2.0

### Packaging

- Wheels for Linux (x86_64, aarch64) and macOS (universal2, macOS 13.3 or newer), Python 3.11 to 3.14; Windows
  follows in v0.2.1
- An OCC-free core: `import planex` works without netgen; a source build without netgen builds the core alone
- The OpenCASCADE bindings in their own package, planex2d-occ: `pip install planex2d[occ]` installs it with netgen
  (Linux x86_64, macOS 13.3 or newer); it holds bindings for the last netgen releases (6.2.2605 on) and loads the ones
  for the installed netgen
- The core is built with pybind11 3.1 from PyPI; building from source needs a C++20 compiler (Xcode 16 on macOS)

### Added

- `px.Ellipse`: exact ellipses and elliptical arcs; DXF ellipses read exactly
- Parametric curves `px.Curve((h, v), (t0, t1))` and `sk.Curve`, from expression strings or a Python function
- `exp`, `log`, `sinh`, `cosh`, `tanh`, `asinh`, `acosh` and `atanh` in curve formulas and expressions
- Curvature-continuous blends: `Fillet(at, r, px.Curvature)`, `sk.Connect`, the `px.Curvature` constraint
- Offset ends and corners: `ends=` (`px.Round`, `px.Flat`, `px.Square`) on `Wire.Offset` and a sketch's `Offset`,
  `corners=` (`px.Round`, `px.Sharp`, `px.Chamfer`) on every offset
- `auto_constraint=` takes a dict of relations; auto constraints also find chain tangents, concentric circles and ends
  on curves
- `px.Auto` for computed defaults and as a drawing flag
- `sk.Add`, `sk.SuggestConstraints`, `sk.AutoConstrain`, `sk.FreeMotions` and `sk.Diagnose`
- Drafting dimensions, `show_diagnostics=` and `show_free_motions=` in sketch plots
- `px.Parameters` shared by sketches: `sk.Use(p)`, `p.Update(...)`
- Dimensioned drawings: `px.Drawing` and `px.Sheet`, written as SVG, PDF and DXF
- DE-9IM relations: `Relate` and its predicates (`Intersects`, `Touches`, `Crosses`, `Overlaps`, `Covers`, ...)
- The symmetric difference: `a ^ b`, `px.SymmetricDifference`
- `Wire.ToNumpy` and `px.FromNumpy`
- The front plane `px.XZ`; `px.Plane` from a plane and an offset or a point, two lines, three points, a line and a
  point, a point and a normal
- Planar STEP and BREP without OpenCASCADE: `px.FromStep`, `px.FromBrep`, `WriteStep`, `WriteBrep`
- `px.WriteSVG` and `px.FromSVG`; `WriteDXF` fills faces and takes lists
- Units for files: the writers take `unit=` (the file's unit) and `scale=`; `FromStep`, `FromDXF` and `FromSVG` return
  meters by default and take `unit=` and `scale=`
- Pad and Pocket: `offset=` with `until=`; `until=` takes a face selection
- A sketch's `Trim` cuts a whole chain or wire in one call
- `Intersect3d` takes a list of OCC shapes

### Changed

- `FromStep` returns meters (before: millimeter numbers); `WriteStep` names the file's unit meters by default (before:
  the numbers as millimeters)
- `FitArcs`, `FitSplines` and the `fit_arcs=` / `fit_splines=` options: `max_edge_length=None` takes the edges shorter
  than 1/20 of the length of their loop or open wire (before: shorter than 1e-4); a number stays an absolute length
- `px.FromNACA` builds the airfoil from parametric curves, one per side, split where the camber line's formula changes;
  `trailing_edge=` takes `px.Sharp` or `px.Flat`; `samples=` is gone
- Pad and Pocket: `until=` ends at the plane of the face given or found, wherever the profile lies; `px.first` and
  `px.last` pass side walls
- Splines are measured, integrated and cut on their exact curve; a closed spline is one edge; cutting splines is faster
- An offset of a spline is one spline per side (a derived curve)
- The solver keeps the drawn shape: a moved point drags its lines and arcs along
- `px.Angle` of two lines is the angle between the lines, not their directions
- A relation a drawing call implies is left out quietly when it follows from the others (`sk.Constrain` still warns
  about a redundant constraint)
- `near=` selects the element nearest to the point itself, not by its center (a circle by its center:
  `kind="circle", near=center`)
- The type stub gives one signature per input form, so type checkers see the precise return types
- Plot arcs are drawn to a tolerance of the object's size; warnings name the caller's line; messages use American
  spelling

### Fixed

- `MaxThickness` finds the largest disc inside, also where it touches only reflex corners
- A curve that crosses itself is cut at every crossing in a sketch's `Shape()` and `Wires()`
- An offset keeps its distance where a curve's parametrization comes to a stop (`FromNACA`'s trailing edge)
- An angle dimension's arc stays near its legs on small drawings
- `px.Fix` and rigid groups take any number of points
- `FromKiCad` fills the zones of converted boards; its docstring and messages name every board format KiCad reads
  (Allegro, EasyEDA, gEDA, ...)

### Removed and renamed

- `sk.Edge(element)` is `sk.Wire(element)`; `congruent=` is `coincident=`
- The ends of the two-point forms are named `start` and `end` in signatures and help (were `from_` and `to`)
- `sk.Circle(h, v, r)` is `sk.Circle((h, v), r)`
- The spline styles "px" and "occ" are "averaged" and "clamped"
- `Plane.Offset` is gone: use `px.Plane(plane, d)`
- `h`, `v` and `t` are reserved and no longer valid ids
