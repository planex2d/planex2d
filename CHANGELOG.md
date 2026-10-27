# Changelog

## v0.2.0

- Ship wheels for Linux (x86_64, aarch64) and macOS (universal2, macOS 13.3 or newer), Python 3.11 to 3.14; Windows
  follows in v0.2.1
- Add an OCC-free core: `import planex` works without netgen; a source build without netgen builds the core alone
- Move the OpenCASCADE bindings into their own package, planex2d-occ: `pip install planex2d[occ]` installs it with
  netgen; it holds bindings for the last netgen releases (6.2.2605 on) and loads the ones for the installed netgen
- Build the core with pybind11 3.1 from PyPI; building from source needs a C++20 compiler (Xcode 16 on macOS)
- Add `px.Ellipse`: exact ellipses and elliptical arcs; read DXF ellipses exactly
- Add parametric curves `px.Curve((h, v), (t0, t1))` and `sk.Curve`, from expression strings or a Python function
- Draw `px.FromNACA` airfoils as exact curves; remove `samples=`; `trailing_edge=` takes `px.Sharp` or `px.Flat`
- Add curvature-continuous blends: `Fillet(at, r, px.Curvature)`, `sk.Connect`, the `px.Curvature` constraint
- Add offset ends and corners: `ends=`, `corners=` with `px.Round`, `px.Flat`, `px.Square`, `px.Sharp`, `px.Chamfer`
- Offset a spline as one spline per side (derived curve)
- Let `auto_constraint=` take a dict of relations; record chain tangents, concentric circles and ends on curves
- Add `px.Auto` for computed defaults and as a drawing flag
- Add `sk.Add`, `sk.SuggestConstraints`, `sk.AutoConstrain`, `sk.FreeMotions` and `sk.Diagnose`
- Add drafting dimensions, `show_diagnostics=` and `show_free_motions=` to sketch plots
- Add `px.Parameters` shared by sketches: `sk.Use(p)`, `p.Update(...)`
- Add dimensioned drawings: `px.Drawing` and `px.Sheet`, written as SVG, PDF and DXF
- Add DE-9IM relations: `Relate` and its predicates (`Intersects`, `Touches`, `Crosses`, `Overlaps`, `Covers`, ...)
- Add the symmetric difference: `a ^ b`, `px.SymmetricDifference`
- Add `Wire.ToNumpy` and `px.FromNumpy`
- Add the front plane `px.XZ` and `px.Plane` from a plane and an offset or a point, two lines, three points, a line
  and a point, a point and a normal
- Read and write planar STEP and BREP without OpenCASCADE: `px.FromStep`, `px.FromBrep`
- Add `px.WriteSVG` and `px.FromSVG`; `WriteDXF` fills faces and takes lists
- Return meters from every reader; writers take `unit=` and `scale=`; `WriteStep` writes meters by default
- Pad and Pocket: `until=` ends at the plane of the face and takes a face selection; add `offset=`
- Measure, integrate and cut splines on their exact curve; make a closed spline one edge; cut splines faster
- Keep the drawn shape when solving: a moved point drags its lines and arcs along
- Make `px.Angle` of two lines the angle between the lines
- Leave out quietly a relation a drawing call implies when it follows from the others (`sk.Constrain` still warns
  about a redundant constraint)
- Select with `near=` the element nearest to the point itself, not by its center (a circle by its center:
  `kind="circle", near=center`)
- Find the largest disc inside for `MaxThickness`, also where it touches only reflex corners
- Cut a curve that crosses itself at every crossing in a sketch's `Shape()` and `Wires()`
- Keep an offset's distance where a curve's parametrization comes to a stop (`FromNACA`'s trailing edge)
- Keep an angle dimension's arc near its legs on small drawings
- Let `px.Fix` and rigid groups take any number of points
- FromKiCad: fill the zones of converted boards; name every program KiCad imports
- Draw plot arcs to a tolerance of the object's size; name the caller's line in warnings; American spelling in messages
- Rename `sk.Edge(element)` to `sk.Wire(element)` and `congruent=` to `coincident=`
- Make `start` and `end` positional in the two-point forms (no `from_`, `to`)
- Change `sk.Circle(h, v, r)` to `sk.Circle((h, v), r)`
- Rename the spline styles "px" and "occ" to "averaged" and "clamped"
- Remove `Plane.Offset`: use `px.Plane(plane, d)`
- Reserve `h`, `v` and `t`: no longer valid ids
