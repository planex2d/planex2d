# Files and other libraries

Reading and writing files, conversions to and from other Python libraries, and wires from arrays of points. See [Files](../core/files.md) for the guide.

FromPolyline(*points: np.ndarray \| list\[Point\]*, */*, *closed: bool = False*, *\**, *plane: [Plane](geometry.md#planex.Plane "planex.Plane") \| None = None*, *name: str \| None = None*, *maxh: float \| None = None*, *col: tuple\[float, ...\] \| None = None*, *minh: float \| None = None*, *hpref: float \| None = None*, *layer: int \| None = None*, *partition: list\[float\] \| None = None*, *id: str \| None = None*) → [Wire](geometry.md#planex.Wire "planex.Wire")  
A wire of lines through the points, the properties on every line.

Parameters:  
- **points** – an (n, 2) or (n, 3) array or a sequence of (h, v) or (x, y, z) on the plane; a point equal to the one before is skipped.

- **closed** – True joins the last point to the first; a last point equal to the first adds no line.

- **plane** – the plane the wire lies on: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar OCC shape (its plane); default the xy plane.

- **name** – the name of the lines.

- **maxh** – the largest mesh size on the lines (netgen’s maxh).

- **col** – the colour (r, g, b) or (r, g, b, a), each in 0..1.

- **minh** – the smallest mesh size on the lines (netgen’s minh).

- **hpref** – netgen’s hpref.

- **layer** – netgen’s layer number.

- **partition** – the mesh points along each line as increasing fractions of its length in (0, 1).

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
- **ValueError** – fewer than two different points; the lines cross, overlap or revisit a point (px.Wires of the lines cuts them into clean wires); a mesh size is not positive and finite.

- **TypeError** – plane has another form.

Examples

``` python
px.FromPolyline([(0, 0), (10, 0), (10, 5)])
px.FromPolyline([(0, 0), (10, 0), (10, 5)], closed=True, name="outline", maxh=0.5)
```

&nbsp;

FromDXF(*path: str \| os.PathLike*, */*, *tol: float = 1e-07*, *layers: list\[str\] \| None = None*, *unit: float \| None = None*, *plane: [Plane](geometry.md#planex.Plane "planex.Plane") \| None = None*, *\**, *id: str \| None = None*) → list\[[Wire](geometry.md#planex.Wire "planex.Wire")\]  
Reads the lines, arcs, circles, polylines, splines and ellipses of a DXF file’s model space as clean wires.

The coordinates are scaled to metres; the wires are built as by px.Wires: ends within tol (after scaling) joined, the edges cut against each other (crossings, T-junctions, an overlapping stretch kept once), then the chains. Blocks (INSERT) are read as their entities. The layer becomes the edges’ name, the colour their col; an ellipse becomes a spline through sampled points. px.Glue(wires) makes them one Shape.

Parameters:  
- **path** – the DXF file.

- **tol** – ends closer than this (after scaling) are joined, a length.

- **layers** – the names of the layers to read; None reads all.

- **unit** – the length the file counts in (px.mm, px.inch, …); None takes the unit its header (\$INSUNITS) names, metres with a warning if it names none.

- **plane** – the plane of the wires; None is the xy plane.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Returns:  
a list of wires; \[\] for an empty drawing.

Raises:  
**ValueError** – unit is not a positive length.

Examples

``` python
px.FromDXF("part.dxf", unit=px.mm)
px.Glue(px.FromDXF("part.dxf", layers=["outline"]))
```

&nbsp;

WriteDXF(*obj: [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch") \| [Compound](geometry.md#planex.Compound "planex.Compound")*, *path: str \| os.PathLike*, */*, *unit: float = 1.0*) → None  
Writes the edges as LINE, ARC and SPLINE entities of a DXF file, each edge once.

An edge’s name becomes its layer (the characters a layer name cannot have, such as / and :, become \_, with a warning), its col the true colour. The coordinates are divided by unit and \$INSUNITS names it.

Parameters:  
- **obj** – the Face, Shape, Wire, sketch or Compound to write.

- **path** – the file to write.

- **unit** – the length the file counts in (px.m, px.mm, px.inch, …).

Raises:  
- **TypeError** – unit is a name, not a length.

- **ValueError** – unit is not a positive finite length.

Examples

``` python
face.WriteDXF("part.dxf", unit=px.mm)
px.WriteDXF(shape, "part.dxf")
```

&nbsp;

FromOCC(*source: Any*, */*, *\**, *id: str \| None = None*) → [Shape](geometry.md#planex.Shape "planex.Shape")  
Reads a planar OCC shape, or a STEP, BREP or IGES file of one, as a Shape on its plane.

Files are read by netgen, which keeps the names. The faces come with their properties, glued as by px.Glue (an overlap becomes a face of its own); edges on no face become wires; every edge keeps its properties. px.Intersect3d(plane, shape) gives a section of a shape that is not planar, px.Project(shape, plane) a projection.

Parameters:  
- **source** – an OCC shape, or the path of a STEP, BREP or IGES file.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – the shape does not lie in one plane, or it has no edges.

Examples

``` python
px.FromOCC("bracket.step")
px.FromOCC(occ_face)
```

&nbsp;

WriteStep(*obj: [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch") \| [Compound](geometry.md#planex.Compound "planex.Compound")*, *path: str \| os.PathLike*, */*, *\*\*options: Any*) → None  
Writes the OCC export (obj.ToOCCShape) as a STEP file.

Parameters:  
- **obj** – the Face, Shape, Wire, sketch or Compound to write.

- **path** – the file to write.

- **options** – the options of obj.ToOCCShape (fit_splines, fit_arcs, remove_edges_smaller, …).

Examples

``` python
face.WriteStep("part.step")
px.WriteStep(shape, "part.step", fit_arcs=True)
```

&nbsp;

WriteBrep(*obj: [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch") \| [Compound](geometry.md#planex.Compound "planex.Compound")*, *path: str \| os.PathLike*, */*, *\*\*options: Any*) → None  
Writes the OCC export (obj.ToOCCShape) as a BREP file.

Parameters:  
- **obj** – the Face, Shape, Wire, sketch or Compound to write.

- **path** – the file to write.

- **options** – the options of obj.ToOCCShape (fit_splines, fit_arcs, remove_edges_smaller, …).

Examples

``` python
face.WriteBrep("part.brep")
px.WriteBrep(shape, "part.brep", fit_arcs=True)
```

&nbsp;

WriteGeo(*obj: [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch") \| [Compound](geometry.md#planex.Compound "planex.Compound")*, *path: str \| os.PathLike*, */*) → None  
Writes the edges and faces as a gmsh .geo file.

Points carry the maxh of their edges and faces as the characteristic length; lines, circle arcs (split at half a circle) and splines as B-splines through planex’s approximation (not exact: .geo has no NURBS), every curve once; plane surfaces with their holes. Physical groups: a curve group per edge name, a surface group per face name, the unnamed edges and faces in a curve and a surface group named “unnamed” (gmsh writes only elements in a group once there is one, and netgen’s ReadGmsh needs every group named and the boundary segments). Periodic identifications become Periodic Curve.

Parameters:  
- **obj** – the Face, Shape, Wire, sketch or Compound to write.

- **path** – the file to write.

Examples

``` python
face.WriteGeo("part.geo")
px.WriteGeo(shape, "part.geo")
```

&nbsp;

FromKiCad(*path: str \| os.PathLike*, */*, *layer: str \| list\[float\] \| dict \| Any \| None = None*, *remove_faces_smaller: float \| None = 5e-08*, *merge_small_faces_instead: bool \| None = True*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *\**, *id: str \| None = None*) → dict\[str, [Shape](geometry.md#planex.Shape "planex.Shape")\] \| [Shape](geometry.md#planex.Shape "planex.Shape") \| Any  
Reads a KiCad board (.kicad_pcb, KiCad 9 and later) in metres, y up: its layers as Shapes, or the board in 3d.

A board of another program (Altium, Eagle, PADS, CADSTAR, Fabmaster, P-CAD, SolidWorks PCB: a file that is no .kicad_pcb) is converted first with KiCad 10’s kicad-cli (pcb import), found as \$PLANEX_KICAD_CLI, on the PATH or where KiCad installs it.

A copper layer is every track, arc, via, pad, filled zone and copper drawing on it, united per net with the faces named by the net; the drill holes through it are cut out, the edges of plated holes (vias and plated pads) named “pth”, of unplated ones “npth”. The outline face is named “profile”, the holes through the whole board are cut out of it. Every other layer is what KiCad prints there: lines, arcs and Bezier curves (as exact splines) as wide as drawn (dashed ones solid), filled shapes and zones as regions, the drawings of footprints too. The solder mask and paste layers are their openings: the pads on them grown by their solder mask or paste margin (the pad’s, else the footprint’s, else the board’s setup) as the file’s KiCad plots them (a round rectangle keeps its corner ratio in a KiCad 9 file, grows exactly in a KiCad 10 one), what is drawn there, the vias the board’s (or the via’s own) tenting leaves open and the tracks and copper drawings marked for an opening; with the setup’s solder mask minimum width, openings closer than it are joined as KiCad joins them. Text, dimensions, tables, barcodes and hatched fills are left out, with one warning giving their counts; zones need to be filled in KiCad (a warning says when they are not). Copper and drill holes are coloured gold (col), the board solder mask green, in 2d and in 3d. Files newer than the newest tested (KiCad 10, version 20260206) warn.

In 3d every dielectric is the outline extruded between its two copper layers (solids named dielectric_1, … from the top), drilled where a hole joins both, so the hole walls are faces named “pth” or “npth”; the copper layers are faces at their heights (B.Cu at z = 0), named by net.

The cleanup options are those of the OCC exports (ToOCCShape): the copper layers and the outline are cleaned with them (Simplify), the other layers keep what KiCad prints, the 3d board is exported with them.

Parameters:  
- **path** – the board file.

- **layer** – None: a dict {KiCad layer name: Shape}: the copper layers from the top (F.Cu, In1.Cu, …, B.Cu), each even when empty, then the board outline “Edge.Cuts”, then every other layer of the board’s layer table that holds something, in the table’s order. A layer name (“F.Cu”, “F.SilkS”, …): that layer’s Shape alone. A stackup: the board in 3d as one glued OCC shape: a list of dielectric thicknesses in millimetres from the top, as the manufacturer gives it (e.g. \[0.2, 1.3, 0.2\] for four copper layers), a stackup of px.stackups (a dict {number of copper layers: such a list}, e.g. px.stackups.jlc7628), or px.stackups.kicad (the stackup in the board file).

- **remove_faces_smaller** – faces of less area than this (m^2) are united with the neighbour they share the longest edge with, such as the slivers a zone fill’s polygon leaves next to an exact drill circle (merged, the hole keeps its exact circle); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – False throws such faces away instead; None takes px.config.merge_small_faces_instead.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges.

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Returns:  
a dict of Shapes; a layer name: a Shape; a stackup: a netgen.occ shape.

Return type:  
layer None

Raises:  
- **ValueError** – a legacy board (before KiCad 6) or a file older than KiCad 9 (version 20241229): open and save it in KiCad 9 or later; kicad-cli could not convert the board; a layer name the board’s layer table does not have; a stackup without an entry for the board’s number of copper layers, with another number of thicknesses than copper layers - 1, or with a thickness not positive; px.stackups.kicad and the file has no stackup or one that does not fit; the board has no outline on Edge.Cuts where one is needed.

- **RuntimeError** – a board of another program and no kicad-cli of KiCad 10 or later can be run.

Examples

``` python
px.FromKiCad("board.kicad_pcb", layer="F.Cu")
px.FromKiCad("board.kicad_pcb", layer=px.stackups.jlc7628)
```

&nbsp;

WriteKiCad(*path: str \| os.PathLike*, *layers: dict\[str, [Shape](geometry.md#planex.Shape "planex.Shape") \| [Face](geometry.md#planex.Face "planex.Face") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| [Compound](geometry.md#planex.Compound "planex.Compound")\] \| list\[[Shape](geometry.md#planex.Shape "planex.Shape") \| [Face](geometry.md#planex.Face "planex.Face") \| [Compound](geometry.md#planex.Compound "planex.Compound")\]*, */*, *outline: [Shape](geometry.md#planex.Shape "planex.Shape") \| [Face](geometry.md#planex.Face "planex.Face") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| [Compound](geometry.md#planex.Compound "planex.Compound") \| None = None*, *unit: float = 1.0*, *pth: [Shape](geometry.md#planex.Shape "planex.Shape") \| [Face](geometry.md#planex.Face "planex.Face") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| [Compound](geometry.md#planex.Compound "planex.Compound") \| None = None*, *npth: [Shape](geometry.md#planex.Shape "planex.Shape") \| [Face](geometry.md#planex.Face "planex.Face") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| [Compound](geometry.md#planex.Compound "planex.Compound") \| None = None*, *tol: float = 1e-06*, *thickness: float = 0.0016*, *line_width: float = 0.0001*) → None  
Writes the layers as a KiCad 9 board (.kicad_pcb, millimetres, y down).

The board has as many copper layers as the highest one given needs, those not given empty. Everything lies in one plane, the xy plane or the plane of the first shape. Every face becomes one filled polygon (gr_poly): lines and arcs exactly, splines as lines within tol, a face with holes as one outline with every hole joined to it by a bridge there and back, as KiCad fractures a zone. Edges on no face become lines of line_width (gr_line, gr_arc), except on copper, which needs an area: they are left out with a warning. The outline’s loops become lines, arcs and circles on Edge.Cuts.

Drill holes are the faces of pth and npth (or of layers\[“pth”\] and layers\[“npth”\]) and the holes of the layers’ faces whose edges are all named “pth” or “npth” (as FromKiCad names them). Every hole is cut out of every copper layer and the outline. A round plated hole becomes a via through all copper layers, its pad as wide as the copper around the hole on the layer with the least (it adds no copper). A round unplated hole becomes a footprint “MountingHole” with one unplated pad. Other holes become cutouts of the outline.

Nets: faces on one layer with one name, and faces a plated hole goes through, are one net, named after the first named face from the top layer down (faces with two names warn), else net1, net2, … from the top. Numbers are written in millimetres with 6 decimals; the uuids are made from each element’s text, so the same layers give the same file.

Parameters:  
- **path** – the file to write.

- **layers** – a dict {KiCad layer name: Shape, Face, Wire or Compound} (copper “F.Cu”, “In1.Cu”, …, “B.Cu”, the board outline “Edge.Cuts”, the other KiCad layers such as “F.SilkS” or “F.Mask”, and “pth”, “npth”), or a list of the copper layers from the top.

- **outline** – the board outline, instead of “Edge.Cuts” in the dict.

- **unit** – the length the geometry counts in (px.m, px.mm, …); tol, thickness and line_width count in it too.

- **pth** – the plated holes, as faces or closed wires.

- **npth** – the unplated holes, as faces or closed wires.

- **tol** – the largest distance of the lines from a spline.

- **thickness** – the board’s thickness.

- **line_width** – the width of the lines of edges on no face.

Raises:  
**ValueError** – unit, tol, thickness or line_width is not a positive length; the outline or a kind of holes is given twice; a name is no KiCad layer; more than 32 copper layers; the layers lie in different planes; an open wire as a hole; a plated hole without copper around it on a copper layer.

Examples

``` python
px.WriteKiCad("board.kicad_pcb", {"F.Cu": top, "B.Cu": bottom}, outline=board, unit=px.mm)
px.WriteKiCad("board.kicad_pcb", [top, bottom], pth=vias)
```

&nbsp;

FromShapely(*geom: Any*, */*, *plane: [Plane](geometry.md#planex.Plane "planex.Plane") \| None = None*, *tol: float = 1e-07*, *\**, *id: str \| None = None*) → [Shape](geometry.md#planex.Shape "planex.Shape") \| list\[[Wire](geometry.md#planex.Wire "planex.Wire")\] \| [Compound](geometry.md#planex.Compound "planex.Compound")  
Converts a shapely geometry: polygons become faces with their holes subtracted, line strings clean wires.

The wires are built as by px.Wires: ends within tol joined, crossings cut. A GeometryCollection becomes a Compound of its parts as shapely stores them: parts sharing a side or overlapping stay so (px.Glue them for a conforming mesh).

Parameters:  
- **geom** – the shapely geometry.

- **plane** – the plane of the result; None is the xy plane.

- **tol** – ends closer than this are joined, a length.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Returns:  
polygons alone a Shape of the united faces, lines alone a list of wires, both a Compound of the Shape and the wires; a GeometryCollection a Compound.

Examples

``` python
px.FromShapely(polygon)
px.FromShapely(line_string, tol=1e-9, plane=px.YZ)
```

&nbsp;

ToShapely(*obj: [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch") \| [Compound](geometry.md#planex.Compound "planex.Compound")*, */*, *tol: float = 0.001*) → Any  
Converts to a shapely geometry in plane coordinates, arcs and splines sampled within tol.

Faces that share an edge (a glued Shape) give a GeometryCollection of their polygons, since a MultiPolygon must not have parts sharing an edge.

Parameters:  
- **obj** – the Face, Shape, Wire, sketch (its Shape()) or Compound.

- **tol** – the largest distance of a chord from its arc or spline, a length. Where a face is thinner than tol its polygon is sampled again finer (to tol/1000), with a warning; still invalid, it is made valid (make_valid).

Returns:  
a Polygon or MultiPolygon for a Face, a Shape of faces or a sketch; a LineString or MultiLineString for a Wire or a Shape of wires; a GeometryCollection for a Compound or a Shape of both.

Examples

``` python
face.ToShapely()
px.ToShapely(shape, tol=1e-4)
```

&nbsp;

ToSVG(*obj: [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| [Compound](geometry.md#planex.Compound "planex.Compound") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch") \| Any*, */*, *size: float = 300*, *\*\*options: Any*) → str \| None  
The object as the text of an SVG picture in its plane coordinates, its faces filled.

A sketch is drawn in its own look: every element in the colour of its state (green fully constrained, black free, pink over-constrained) with its id when hovered, construction dashed, in a frame with numbered ticks on a grid.

Parameters:  
- **obj** – a Face, Shape, Wire, Compound, sketch or DeformableShape.

- **size** – the larger side of the picture in pixels.

- **options** – for a sketch: show_constraints=True adds a sign per constraint, show_ends=True a blue start and an orange end piece on every line and arc, show_direction=True an arrow from start to end; other objects ignore them.

Returns:  
the SVG text; None if the object has no extent.

Examples

``` python
face.ToSVG()
sk.ToSVG(size=500, show_constraints=True)
```

&nbsp;

FromNACA(*code: str*, */*, *chord: float = 1.0*, *samples: int = 100*, *trailing_edge: str = 'sharp'*, *angle: float = 0.0*, *pivot: float = 0.25*, *plane: [Plane](geometry.md#planex.Plane "planex.Plane") \| None = None*, *\**, *id: str \| None = None*) → [Face](geometry.md#planex.Face "planex.Face")  
A NACA 4-digit or 5-digit airfoil as a Face, the leading edge at the origin and the chord along the plane’s u.

Each side is one spline through cosine spaced stations of the exact contour, both with the same tangent at the leading edge.

Parameters:  
- **code** – the airfoil, 4 digits (‘2412’) or 5 digits (‘23012’, reflex ‘23112’).

- **chord** – the chord length, \> 0.

- **samples** – the number of stations per side, an integer \>= 3.

- **trailing_edge** – ‘sharp’ closes the contour at the trailing edge, ‘blunt’ keeps the classic formula’s gap and closes it with a line.

- **angle** – the angle of attack in degrees, positive nose up.

- **pivot** – the point the angle turns around, on the chord line as a fraction of the chord.

- **plane** – the plane of the face; None is the xy plane.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – code is not a 4- or 5-digit code planex knows, trailing_edge is neither ‘sharp’ nor ‘blunt’, samples is not an integer \>= 3, or chord is not positive.

Examples

``` python
px.FromNACA("2412")
px.FromNACA("23012", chord=0.3, angle=5, trailing_edge="blunt")
```
