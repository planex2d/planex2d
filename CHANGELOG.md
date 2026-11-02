# Changelog

## v0.3.1

### Fixed

- `px.FromCATPart` reads a sketch with a circle, an arc or a spline point made coincident with the sketch's origin:
  it lies on planex's `origin` and is held there by a coincidence; before, the file raised a `KeyError`
- `px.FromCATPart` keeps the sketch's coincidences when its dimensions change: a line or an arc that starts or ends
  on the sketch's origin, and two points made coincident in the .CATPart file (a line's end on a circle's center),
  stay together; before, they were drawn at the same place but came apart on `sk.Update`
- `px.FromCATPart` reads a spline made tangent to a curve where the .CATPart file gives it no tangent arrow with the
  tangent its points give it: the tangency fixes where the spline meets the curve, and the sketch reads as constrained
  as it was saved; before, the spline got a tangent of its own there and its end could slide along the curve
- `Trim` and `Split` leave no constraint redundant: where a cut ties a cut point to a cutter's end, the coincidence
  that put that point on the element cut (the user's or an earlier cut's) repeats the tie and is removed, and the
  sketch's degrees of freedom stay as they were; before, that coincidence stayed and was drawn as over-constrained
  (`sk.Trim(("bore", (0, 10)))` after `sk.Constrain("key.end", "bore", px.Coincident)`)
- `Mirrored` leaves no constraint redundant: a point on the mirror axis joins its copy, and the coincidence that put
  that point on the axis repeats the join and is removed; before, it stayed and was drawn as over-constrained
  (`sk.Mirrored(["base", "flank", "eye"], px.V)` after `sk.Constrain("eye.end", "V", px.Coincident)`)
- `Drawing.Center(id)` marks the center of a construction circle as asked, with lines across it or with `cross=True`
  a short cross; before, it drew nothing for one (`px.Drawing(hook).Center("throat").Center("back", cross=True)`)

### Changed

- The sketch plots tell a repeated constraint from a contradicting one: a constraint that repeats others (redundant,
  the sketch still solves) is drawn light pink, one that contradicts others and the elements of an over-constrained
  sketch red; before, both in one deep pink (`sk.Plot(show_constraints=True)`, `px.ToSVG(sk, show_constraints=True)`)

## v0.3.0

### Added

- `standard="ASME"` on `px.Sheet` and `px.Drawing`, and `px.config.drawing_standard` (`"DIN"` by default): the views
  in third-angle projection with its symbol, and a decimal point in the dimensions, tables, title block and DXF
  (a dimension style "ASME"); the dimension text along the line, the sheet sizes, the title block and millimeters
  stay as DIN's in this version (`px.Sheet(front, top, side, standard="ASME")`)
- `px.FixH` and `px.FixV` hold one coordinate as `px.Fix` holds both, as constraints and as drawing flags
  (`sk.Line(0, 5, px.FixH)`: a vertical line that keeps its h and may slide and stretch vertically)
- Selections propagate: every selection call and `sk.Wire(id)` take `propagate=px.Coincident`, `px.Tangent` or
  `px.Curvature`, which adds the edges that continue the chosen ones across their ends (meeting there, tangentially, or
  with equal curvature too) up to where two or more would; `px.Minimal`, the default, takes them alone
  (`face.edges(near=(2, 0), propagate=px.Tangent)`: a side and the fillets and sides tangent to it)
- `Measure(at=...)` on edges, wires, selections of edges and sketch elements or chains: the place at an arc length (a
  number) or nearest a point, on the exact curves: its point, tangent, normal, curvature (a left turn positive), arc
  length, distance from the point given and the edge it lies on (`wire.Measure(at=12.5)["point_hv"]`)
- `wire.Reverse()` and `wire.Reversed()`: the wire run the other way
- `px.FromCATPart(path, scale=px.Auto)`: the sketches of a .CATPart file (tested with V5R21 files) as planex
  sketches on their planes, with their constraints and dimensions, the part's parameters and formulas; its splines in
  style `'quintic'` with their tangents and curvature radii; what planex has no counterpart for is named in a
  warning: ellipses come as fixed curves, equidistant-point constraints, projected face outlines, constraints on a
  spline's tangent arrow and the part's 3D features are left out (`part = px.FromCATPart("bracket.CATPart");
  part["parameters"].Update(w=0.06)`)
- Spline style `'quintic'`: the quintic through the points with the least change of curvature, smooth to its fourth
  derivative at the points (`px.Spline(points, style="quintic")`); `px.Spline` takes
  `tensions=` ({index: tension}, scaling a given tangent's own length, every style) and `curvature_radii=` ({index:
  radius}, positive bending left of the tangent, `math.inf` straight; quintic only) at points with a tangent; a
  tangent is an arrow the quintic runs along; `sk.Spline` takes them too, a curvature radius a new dimension
  `px.CurvatureRadius` (`'CurvatureRadius.1'`, changed by `sk.Update`, `sk.Constrain("Spline.1.p1", px.CurvatureRadius,
  -30)` adds one, a mirror's copy bends the other way; not drawn in sketch plots and drawings yet); `px.Curvature`
  where a quintic meets another element gives the spline its curvature there when the constraints leave it free
- `sk.Animate(rows)`: a matplotlib animation of a constraint sketch through rows of its dimensions
  (`sk.Animate({"crank": np.linspace(0, 360, 73)})`), each frame solved; `steps=` frames in between with the rows'
  designs kept, `fps=`, `label=`; a row without a solution shows the last good geometry greyed; shown in Jupyter,
  `anim.save("x.gif")`
- `sk.AddToSpreadsheet(path, name=, values=)` appends the current solution as a row of a CSV or .xlsx file (every
  dimension, a header row first), and `sk.ReadSpreadsheet(path)` reads the rows back as driving dimensions for
  `sk.Update(row)` or `sk.Animate(rows)`; CSV with Python's csv module, .xlsx with openpyxl
- `px.measurements.ConvexHull()`, `EnclosingCircle()` and `EnclosingRectangle()`: the convex hull as a Face, exact
  with arcs; the smallest circle and the rectangle of least area around the edges, at any angle, with their center,
  radius or length, width and angle (`part.Measure(px.measurements.EnclosingRectangle())["length"]`)
- `sk.BSpline(poles, knots=None, degree=3, periodic=False)` in a `px.Sketch` and a `px.QuickSketch`: a spline by its
  control points, of its own kind `BSpline` (ids `'BSpline.1'`, `sk.elements["BSpline"]`), the poles point elements
  (`'s.p1'`) and the control polygon's legs construction lines (`'s.leg.0'` from the first pole), so constraints on them
  shape it and move only the poles they need (a leg held horizontal: the tangent at that end horizontal); `px.Tangent`
  joins it to the element before it, `periodic=True` closes it; copies get their own legs, and `Remove` takes the poles
  and legs along
- `silhouette=True` on `px.Project`, the Project of a face, shape or wire, and `sk.Project`: the outline of the shadow a
  solid casts on the plane instead of its edges, with the fold lines of curved faces where they bound it (a cylinder
  seen side-on: its two lines; a sphere: its circle) and the holes one sees through; `px.Shape(...)` of it is the
  region, and a sketch takes it as construction, the fold lines named `Projection.N.Fold.K`
  (`px.Shape(px.Project(shaft, plane=px.XZ, silhouette=True))`)
- Views of solids in drawings: `px.Drawing(part, px.Front)` draws an OCC solid seen along a plane (`px.Front`,
  `px.Top`, `px.Left`, `px.Right`, `px.Rear`, `px.Bottom` or any `px.Plane`): its visible edges and the fold lines of
  curved faces wide, hidden edges narrow and dashed (`hidden=False` leaves them out), smooth edges where faces meet
  tangentially narrow and short of the outline; `px.Drawing(part, px.Isometric)` is a shaded pictorial. Each view
  names itself ("Front view", "Top view", ..., "Isometric view"; `view_name=` replaces the name, `""`
  removes it), takes a sketch's dimensions with `dw.Add(sk)` and its place on a `px.Sheet` in first-angle projection
  like any drawing; exact edges in DXF. The lines come from OpenCASCADE's exact hidden-line algorithm; the fold lines it
  traces only approximately are refined onto the exact surface (`silhouette=True` takes the same), and the visibility
  of every line is checked by rays
- Sections: `px.Drawing(part, plane, section=True, view_name="Section view B-B")` cuts the solid at the plane, hatches
  the cut faces and leaves the hidden edges out; `dw.Section(section)` marks the cut on the view it is taken from (the
  cutting line, wide at its ends, and an arrow with the section's letter at each end)
- `sk.Extend`: a line along its line or an arc along its circle, to where it first meets a target
  (`sk.Extend("Line.3", "Arc.2")`, the end nearer it moves) or by a length (`sk.Extend("Line.3.end", 5)`); in a
  `px.Sketch` a constraint that keeps the end where it is raises, and with auto_constraint the new end stays on the target
- `tol=` on `px.Curve` and `sk.Curve` for a Python function: how closely the fitted cubic passes the curve, relative to
  its size (default 1e-10 as before); a curve with fine detail that 4096 samples do not fit to 1e-10 takes a looser one
  without the warning (`px.Curve(f, (0, 1), tol=1e-6)`)
- `sk.dependencies`: each redundant constraint with the constraints it follows from
  (`{"Horizontal.1": ["Fix.1", "Fix.2"]}`); a small set that makes it redundant, not always the smallest
- An angle measured from `px.H` or `px.V` (`sk.Constrain(px.H, "arm", px.Angle, 30)`) is drafted in sketch plots,
  drawings and DXF: a thin auxiliary line along the axis from the line's start, and the angle's arc and value between
  it and the line, placed with `dw.Place` as an angle between two lines; before, it kept its value label

### Fixed

- Two sketches in one drawing with equal ids (two `Circle.1`, two `Distance.1`) keep them apart: each circle gets its
  center lines, and a shared id is named by its full id (`dw.Place("Sketch.2.Distance.1", 10)`) or a selected
  element (`dw.Center(sk("Circle.1"), off=True)`); a plain shared id raises naming both ways (before, only the last
  sketch's circle got its center lines and a plain id placed both dimensions)
- A shaded isometric (`px.Drawing(part, px.Isometric)`) shows the nearest face at every point: before, a
  farther face's long triangles could be painted over a nearer face, as small mis-shaded slivers on a gear's tooth
  flanks; drawings with one are also written about four times faster
- A drawing draws a slot's width (`slot.w`) across the slot, and the center mark of an arc dimensioned by its
  diameter; before, it left the width out and marked only arcs dimensioned by their radius
- Angles in drawings and `sk.Plot` stay near their vertex: an angle whose edges start at the vertex is drawn at most
  three dimension-line spacings out (24 mm on paper), and the angles at one vertex stack a spacing apart, the
  smaller inside; before, long edges or a long name drew the arc out to the shorter edge's end, far past the part
- `sk.Animate` gives every frame the grid of the view all frames share; before, each frame took its own sketch's
  step and the tick labels changed between frames and ran together
- Sketch plots draw splines, curves and offset curves within 2e-4 of the sketch's size, not 16 pieces between two of
  their points: a B-spline of one span no longer looks faceted
- `Drawing.Add` of a sketch on another plane (a face of the solid in a view, another sketch on a parallel plane) puts
  its dimensions and center lines in the view, turned with its axes and mirrored when the view sees it from behind;
  before, its dimensions raised an IndexError or a ValueError
- `MinThickness` finds a thin wall between its samples: every sample thinner than its neighbours (within 1.5 times the
  thinnest) is refined along its edge, not only the thinnest sample. On spline faces, sampled evenly along their length,
  a thin neck whose samples fell beside it was missed (docs' auxetic sheet: 0.953 mm instead of 0.805 mm at 4000
  samples; now found already at 1000)
- A pattern's new count, angle or spacing leaves its original where nothing holds it: the copies re-spread round it
  (before, the original moved so that the copies moved less); a constraint on a copy still moves the original as far
  as it needs
- After a dimension change a sketch's free geometry stays where it was drawn: the solver's last phase, which takes
  back what the steps moved along free directions, failed whenever the change was large and the first solution was kept
  (a slot whose width went from 6.6 to 9 turned by 1.4e-4 rad, its area off in the 7th digit; a line after a changed
  arc radius turned slightly)
- `Simplified` (and the cleanup of an export) keeps a periodic cell's identified sides matched: a run of edges shorter
  than `remove_edges_smaller` goes to one point, the mean of its vertices, whichever way the wire walks it, and to the
  end of a side's piece where it reaches one, so a short edge inside the cell no longer pulls a side's vertex along
  (before, each edge went to its midpoint in turn, so the two sides of a cell, walked in opposite directions, ended at
  different points and `px.OCCGeometry` refused the identification)
- `sk.Plot(show_parent=solid)` fills the solid's shadow: a curved face seen side-on (a cylinder on top of a housing)
  has its outline and its fill
- planex's OCC bindings compile with OpenCASCADE 8 (netgen's nightly builds use it)
- A piece of a live offset in a `px.Sketch` can be trimmed, split, removed, filleted or chamfered alone; what is left
  keeps following its source (its ties and points still refuse: they hold the offset together). This includes the offset
  of a spline, whose pieces stay on its exact offset curve (a closed spline's offset also across its seam); before, Trim
  and Split refused it and changing the offset's distance after removing it raised an IndexError
- `Relate` of a face and a face of the same outline with a hole has the hole in the first face's interior and the
  second's exterior; before, the matrix left it out and `Equals`, `Covers` and the other named relations followed it
  (a square "equalled" the same square with a hole)
- `px.measurements.EnclosingCircle` holds an outline with arcs or splines whole and `EnclosingRectangle` is the least
  rectangle around it; before, the circle could leave a spline's outline out by about 1e-5 of its size and the rectangle
  come out larger than the least
- A spline whose approximation has a piece with nearly equal end tangents is built (or, crossing itself, refused with
  the wire's message); before, a closed spline with tangents through points on one line raised "Arc: a value is not
  finite (-nan)"
- `sk.Update` changes a `px.CurvatureRadius` to the other sign or from straight (`math.inf`); before, it found no
  solution
- A coordinate as small as 5e-324 (the least subnormal number) no longer runs a wire's build or an offset out of
  memory (`std::bad_alloc`)

### Changed

- OCC bindings that do not fit leave planex importable: a planex2d-occ without a module for the installed netgen (a
  netgen newer than its last build) or built for another planex2d, and a planex built from source under another netgen
  than it was compiled against, make `import planex` warn once with the pip command that fits (for the build from
  source: `pip install --no-build-isolation --force-reinstall planex2d`), and the OCC calls raise with it; before,
  `import planex` raised an ImportError, and a build from source under a changed netgen failed or crashed
- `px.config.spline_style` is `'quintic'` by default: `px.Spline`, `sk.Spline` and `px.QuickSketch`'s splines without
  `style=` are quintics, so existing code draws a slightly different curve through the same points (degree 5 in
  DXF and STEP); `style="averaged"` gives the cubic of before, `px.config.spline_style = "averaged"` for the whole
  session
- `sk.Plot(show_parent=solid)` draws the solid as `px.Drawing`'s isometric view draws one, seen along the sketch's
  normal: grey shaded faces, wide visible edges, thin smooth edges, no hidden edges (before: every edge projected flat
  over a lightly filled shadow)
- A write call's path whose file name has no extension gets the format's own: `face.WriteStep("face")` writes
  face.step (`WriteStep` .step, `WriteBrep` .brep, `WriteDXF` .dxf, `WriteSVG` .svg, `WriteGeo` .geo, `WriteKiCad`
  .kicad_pcb, a drawing's or sheet's `WriteSVG`, `WritePDF`, `WriteDXF` .svg, .pdf, .dxf); before, the file had no
  extension. A path with any extension is written as given
- The redundancy warning names what the constraint follows from: `Horizontal.1 ("l", px.Horizontal) follows from Fix.1,
  Fix.2; remove one of them` (was `... is redundant (consistent) with ...`, which listed every other redundant
  constraint), also when a new constraint makes older ones redundant (`r.Perpendicular.1, r.Perpendicular.2,
  r.Perpendicular.3 follow from G; remove them to keep the sketch minimal`, was `G makes ... redundant`)
- `sk.Update` without a solution names what contradicts the new values: `Update(dc = 10): no solution, nothing changed;
  dc contradicts da, db`
- `px.BSpline(poles)` takes clamped uniform knots and degree 3 when none are given (each end degree + 1 times, the
  inner knots evenly between; a periodic curve one period evenly)
- `wire.Offset(d)` returns a Wire, as the sketch's `Offset` makes it, instead of the band as a Shape: an open wire's
  band boundary (both sides by default; `both_sides=False` the run on one side, d > 0 left, d < 0 right), a closed
  wire's loop outside (d > 0) or inside (d < 0), both loops with `both_sides=True`. `.Face()` or `px.Shape([...])`
  makes the region: `wire.Offset(0.5).Face()` for the band of before
- `MinThickness` and `MaxThickness` measure walls: a disc counts where the sides it touches face each other (its
  touching points more than 90 degrees apart). At a convex corner or bend the disc grows along the medial axis to the
  wall it leads to, so a rectangle's corners measure its wall and an outline drawn as lines measures the same as its
  curves, at any resolution; only a knife edge sharper than 90 degrees stays thin, with the warning. A NACA 2412's
  `MinThickness(ignore_corners=30)` is now about 4.0 on chord 100 (where its leading edge's sides first face each
  other), not 3.13

## v0.2.4

### Fixed

- `MinThickness` and `MaxThickness` on faces with spline edges: no wall of about 1e-12 in the middle of a smooth side,
  and fast (a NACA profile in about 0.1 s instead of 4 s); the result lies on the exact curve
- Plotly sketch plots show an element's id all along it, not only near its middle

### Changed

- `ignore_corners` decides by geometry which samples a sharp corner holds: a disc whose touching points all lie in the
  corner's wedge (within 2.2 r / tan(angle / 2) of it), so splines and lines give the same result and a plate barely
  longer than it is wide still has its wall. On a tapered edge, such as an airfoil's trailing edge, the whole taper is
  the corner's: `MinThickness(ignore_corners=30)` of a NACA profile is its leading edge

## v0.2.3

### Packaging

- Wheels for Pyodide 0.29 and 314 (Python 3.13 and 3.14, wasm32): planex runs in the browser,
  `micropip.install("planex2d")`; the core without the OCC bindings, on one thread

## v0.2.2

### Packaging

- Wheels for Windows (x86-64), Python 3.11 to 3.14; planex2d-occ follows with Windows wheels for the same netgen
  releases, so that `pip install "planex2d[occ]"` works there as on Linux and macOS
- On Windows planex needs the latest Microsoft Visual C++ Redistributable (x64); the wheels do not bring Microsoft's
  runtime, and `import planex` says so where it is missing
- planex compiles with MSVC: a source build on Windows needs Visual Studio 2022

### Fixed

- `WriteGeo` writes UTF-8 on every platform (on Windows it wrote the system's code page)

## v0.2.1

### Fixed

- `Sketch.Update` is fast again where it moves geometry without changing a drawn shape (a circle moved by its position
  dimensions, any rigid move): it took thousands of smallest steps, about 0.4 s per update, now under a millisecond

### Changed

- `CloseGaps` joins ends at their midpoint, whatever the order of the edges (it kept the end that came first)

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
