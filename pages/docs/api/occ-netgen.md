# OpenCASCADE and netgen

The export to OpenCASCADE and netgen, solids from profiles, projections and sections of 3D shapes, level sets, and netgen’s WorkPlane drawing calls. See [OpenCASCADE](../integrations/occ/index.md) and [netgen](../integrations/netgen/index.md) for the guides.

OCCGeometry(*face: [Face](geometry.md#planex.Face "planex.Face")*, *fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → occ.OCCGeometry\
OCCGeometry(*wire: [Wire](geometry.md#planex.Wire "planex.Wire")*, *fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → occ.OCCGeometry\
OCCGeometry(*shape: [Shape](geometry.md#planex.Shape "planex.Shape")*, */*, *fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → occ.OCCGeometry\
OCCGeometry(*sketch: [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch")*, *fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → occ.OCCGeometry  
The netgen OCCGeometry of a face, wire, shape or sketch, exported with the given cleanup and fits.

A face: dim=2 in the xy plane (z = 0), else dim=3 (a surface mesh). A wire: dim=1, a mesh of segments along the edges, also of a closed wire; px.OCCGeometry(px.Face(\[wire\])) meshes the region inside it. A shape: its faces and wires, dim=2 in the xy plane (z = 0), else dim=3 (a surface mesh); dim=1 without faces. A sketch: netgen OCCGeometry of the region: dim=2 in the xy plane (z = 0), else dim=3 (a surface mesh); dim=1 without faces.

Parameters:  
- **face** – a Face.

- **wire** – a Wire.

- **shape** – a Shape.

- **sketch** – a Sketch or QuickSketch.

- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges, False for a wire. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
- **ValueError** – a sketch in mode=’strict’ while it can still move.

- **TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
px.OCCGeometry(face)
px.OCCGeometry(shape, fit_arcs=True, remove_edges_smaller=1e-6)
```

&nbsp;

Pad(*profile: [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch")*, *length: float \| None = None*, *length2: float = 0.0*, *dir: Point3 \| None = None*, *reverse: bool = False*, *until: [Plane](geometry.md#planex.Plane "planex.Plane") \| [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch") \| TopoDS_Shape \| None = None*, *offset: float = 0.0*) → TopoDS_Shape  
The profile extruded into an OCC solid, as CATIA’s Pad.

The solid has the name, maxh and colour of the profile’s face; each side face the properties of its profile edge; both end faces the colour and maxh of the face, no name; the edges along dir the properties of the profile vertex they start at.

Parameters:  
- **profile** – a closed planar profile: a Face, a Shape with faces, a Sketch or QuickSketch (the faces its ToOCCShape() gives, on its plane).

- **length** – the extent along dir from the profile’s plane; None with until=.

- **length2** – the extent against dir, so the solid reaches across the plane; default 0.

- **dir** – the direction of the extrusion, e.g. (0, 0, 1), any direction that leaves the profile’s plane (it may be tilted); default the plane’s normal, for a Pocket its negative.

- **reverse** – True turns dir around.

- **until** – instead of length: a px.Plane, a flat OCC face, or a planex Face, Shape or Sketch (up to its plane, the end of the prism lies inside it); a Pocket also px.first or px.last (the first or last flat face of the target the prism crosses along dir). Solids and curved faces are not allowed.

- **offset** – with until=: the end this far from the plane along its normal, past it if positive, short of it if negative; the end face stays parallel to the plane.

Raises:  
- **TypeError** – profile or until has another form.

- **ValueError** – neither length nor until, or both; a negative or infinite length; dir zero or parallel to the profile’s plane; until=px.first or px.last (a Pad has no target); until a solid, a curved face, or a plane that does not lie ahead of the whole profile, or a face that does not hold the end of the prism; offset without until, or an offset that puts the end behind the profile.

Examples

``` python
px.Pad(face, 3)
px.Pad(sketch, 2, length2=1, dir=(0, 0, 1), reverse=True)
px.Pad(face, until=px.Plane(origin=(0, 0, 5)), offset=-0.5)
```

&nbsp;

Pocket(*target: TopoDS_Shape*, *profile: [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch")*, *length: float \| None = None*, *length2: float = 0.0*, *dir: Point3 \| None = None*, *reverse: bool = False*, *until: SketchUntil \| [Plane](geometry.md#planex.Plane "planex.Plane") \| [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch") \| TopoDS_Shape \| None = None*, *offset: float = 0.0*) → TopoDS_Shape  
The target solid minus the profile’s prism, as CATIA’s Pocket.

The prism is the one px.Pad makes, by default against the profile’s normal, into the material. The target’s faces keep their properties; the prism’s walls, floor and edges bring those of the profile as in px.Pad.

Parameters:  
- **target** – an OCC solid.

- **profile** – a closed planar profile: a Face, a Shape with faces, a Sketch or QuickSketch (the faces its ToOCCShape() gives, on its plane).

- **length** – the extent along dir from the profile’s plane; None with until=.

- **length2** – the extent against dir, so the solid reaches across the plane; default 0.

- **dir** – the direction of the extrusion, e.g. (0, 0, 1), any direction that leaves the profile’s plane (it may be tilted); default the plane’s normal, for a Pocket its negative.

- **reverse** – True turns dir around.

- **until** – instead of length: a px.Plane, a flat OCC face, or a planex Face, Shape or Sketch (up to its plane, the end of the prism lies inside it); a Pocket also px.first or px.last (the first or last flat face of the target the prism crosses along dir). Solids and curved faces are not allowed.

- **offset** – with until=: the end this far from the plane along its normal, past it if positive, short of it if negative; the end face stays parallel to the plane.

Raises:  
- **TypeError** – profile or until has another form.

- **ValueError** – the target has no solid; neither length nor until, or both; a negative or infinite length; dir zero or parallel to the profile’s plane; until a solid, a curved face, a plane that does not lie ahead of the whole profile, or a face that does not hold the end of the prism; px.first or px.last reaching no face or a curved one; a prism that removes nothing or everything; offset without until, or an offset that puts the end behind the profile.

Examples

``` python
px.Pocket(box, circle_face, 2)
px.Pocket(box, circle_face, until=px.last, offset=-1)
```

&nbsp;

Project(*shape: TopoDS_Shape*, */*, *plane: [Plane](geometry.md#planex.Plane "planex.Plane") \| None = None*, *\**, *id: str \| None = None*) → list\[[Wire](geometry.md#planex.Wire "planex.Wire")\]  
The edges of the OCC shape projected straight onto the plane, as a list of clean wires.

Lines stay lines, circles parallel to the plane stay arcs, other curves become exact splines; points (edges seen end-on, lone vertices) are left out with a warning, Sketch.Project keeps them.

Parameters:  
- **shape** – an OCC shape.

- **plane** – the plane projected onto: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar OCC shape; default the xy plane.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**TypeError** – plane has another form.

Examples

``` python
px.Project(occ_shape)
px.Project(occ_shape, plane=px.YZ)
```

&nbsp;

Intersect3d(*plane: [Plane](geometry.md#planex.Plane "planex.Plane") \| None*, *shape: Any*, */*, *\**, *id: str \| None = None*) → [Shape](geometry.md#planex.Shape "planex.Shape")  
Where the plane meets an OCC shape or a 3d mesh, as a Shape in the plane’s coordinates.

An OCC shape gives the cross-sections of its solids as faces with the solids’ names and properties (several solids glued as by px.Glue, an overlap a face of its own; a face of a solid lying in the plane belongs to its cross-section), its other faces as faces where they lie in the plane, else as the wires where they meet it, and its edges on no face as the stretches lying in the plane; every edge is named after the face or edge it lies on. The points where such edges cross or touch the plane are left out with a warning; a sketch’s Intersect3d keeps them.

A mesh is cut at its boundary facets as they are, straight (a curved boundary gives a polygon): one face per material (NGSolve’s materials, the cell tags’ physical groups; a bare dolfinx mesh is one region) named after it, glued, a face of it lying in the plane included; every edge is named after the boundary of its facet (NGSolve’s boundary names, the facet tags’ physical groups). NGSolve’s name “default” counts as no name. A plane that misses the shape gives an empty Shape and a warning.

Parameters:  
- **plane** – the cutting plane: px.Plane, occ.Axes, a sketch, a planar face or shape, a compound on one plane; None is the xy plane.

- **shape** – an OCC shape, an ngsolve.Mesh, a serial dolfinx.mesh.Mesh of tetrahedra or hexahedra, or the MeshData of dolfinx.io.gmsh (read_from_msh, model_to_mesh).

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – the boundary of a region of the mesh is not watertight (an edge belongs to one boundary facet only), the mesh is not 3d or has no volume elements, a dolfinx mesh is distributed over several MPI processes or has other cells.

Examples

``` python
px.Intersect3d(px.XY, occ_shape)
px.Intersect3d(px.XY.Offset(0.5), mesh)
```

&nbsp;

FromLevelSet(*field: Any*, */*, *edge_length: float \| None*, *inside: str = 'positive'*, *name: str = 'interface'*, *project: bool = True*, *subdivide: int \| None = None*, *plane: [Plane](geometry.md#planex.Plane "planex.Plane") \| None = None*, *periodic: bool = True*, *\**, *id: str \| None = None*) → [Shape](geometry.md#planex.Shape "planex.Shape")  
The region where an NGSolve level set function is positive (or negative) as a Shape of lines.

The level set is cut on a lattice of subdivide steps per element; the interface is then resampled to lines about edge_length long. Where the region reaches the mesh boundary, the boundary is part of the polygon with its boundary names.

Parameters:  
- **field** – the level set function, a scalar GridFunction on a 2d triangular mesh.

- **edge_length** – the length of the interface lines; None keeps the lattice resolution.

- **inside** – ‘positive’ or ‘negative’: the sign of the field inside the region.

- **name** – the name of the interface edges.

- **project** – True moves the interface points onto the exact zero level set by Newton steps.

- **subdivide** – the lattice steps per element; None takes the polynomial order of the field’s space.

- **plane** – the plane of the shape; None is the xy plane.

- **periodic** – True takes the periodic identifications of the mesh (field.space.mesh.ngmesh): a crossing on an identified boundary edge and its partner on the partner edge get the mean of their parameters along the edges, so the two sides match; a crossing without a partner warns (PlanexWarning) and stays. The shape carries the identifications between the boundary names (‘periodic.1’, …) as translations. Sides whose names do not tell them apart (one name on several sides) are named name.periodic.ka and name.periodic.kb for the k-th identification, with a PlanexWarning. False ignores them.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
- **ValueError** – inside is neither ‘positive’ nor ‘negative’, or the mesh is not 2d.

- **NotImplementedError** – the mesh is not triangular.

Examples

``` python
px.FromLevelSet(gf, 0.01)
px.FromLevelSet(gf, None, inside="negative", name="bubble", subdivide=4)
px.FromLevelSet(gf, 0.01, periodic=False)
```

&nbsp;

*class* WorkPlane(*axes: Any = None*, *pos: Any = None*, *\**, *copy: bool = False*)  
Drop-in for netgen’s occ.WorkPlane: the same calls, arguments, defaults and cursor, drawn on a px.QuickSketch.

The results are planex objects: Face() a px.Face, Wire() and Last() a px.Wire, Wires() a list of px.Wire; the drawing calls return the WorkPlane. Names (name=, NameVertex) are the planex edge and vertex names and reach OCC through ToOCCFace, ToOCCWire, ToOCCWires, ToOCCShape (the export of what Face(), Wire(), Wires() return). Face() puts the finished wires into one face as netgen does, the first the outer wire (counterclockwise), the others holes (clockwise, so Reverse() after drawing them); planex finds holes by nesting, so Face() takes a wire inside another as a hole and a clockwise outer wire as the outer wire, each with a PlanexWarning. Ellipse is a closed spline through points of the ellipse.

Wires are grouped as in netgen: MoveTo and Move only move the cursor, a wire stays open until Close() or Finish(), and a later edge joins it where it meets one of its vertices. An edge that meets none, and Circle or Ellipse while a wire is open, raise ValueError. Wires() gives each such wire as clean px.Wire chains (a branch gives several), Wire() and Last() the last one when it is a single chain; ToOCCWire() and ToOCCWires() export the grouping 1:1.

Parameters:  
- **axes** – what to draw on, as px.QuickSketch’s first argument: netgen’s occ.Axes, a px.Plane, a planex or planar OCC object or a list of them on one plane (their axes); a point (h, v): the XY plane, the cursor there; default the XY plane.

- **pos** – netgen’s gp_Ax2d: the start position and direction of the cursor; default (0, 0), direction (1, 0).

- **copy** – True copies the geometry of axes in (OCC: projected); each copied wire is a finished wire of the WorkPlane, so Face() and Wires() take it.

cur_loc  
the cursor’s position, a px.Vertex.

cur_dir  
the cursor’s direction, a px.Vertex.

start_pnt  
where the wire started, the point Close() goes back to, a px.Vertex.

sketch  
the px.QuickSketch drawn on.

Raises:  
**TypeError** – wrong arguments in a call (the message names the call).

Examples

``` python
wp = px.WorkPlane().Rectangle(4, 2).MoveTo(2, 1).Circle(0.5).Reverse()
face = wp.Face()
px.WorkPlane().Line(2, name="bottom").Rotate(90).Line(1).Close().ToOCCFace()
px.WorkPlane(solid.faces.Max(occ.Z), copy=True).MoveTo(2, 1).Circle(0.5).Face()
```

Arc(*r: float*, *ang: float*, *name: str \| None = None*, *maxh: float \| None = None*) → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Draws an arc tangent to the cursor’s direction, to the left for a positive angle, else to the right.

Parameters:  
- **r** – the radius.

- **ang** – the angle in degrees, not a multiple of 360.

- **name** – the arc’s name.

- **maxh** – the arc’s mesh size.

Raises:  
**ValueError** – ang is a multiple of 360.

Examples

``` python
wp.Arc(1, 90)
wp.Arc(0.5, -180, name="notch")
```

ArcTo(*h: float*, *v: float*, *t: Any*, *name: str \| None = None*, *maxh: float \| None = None*) → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Draws an arc to (h, v) that leaves the cursor’s position along t; the direction turns by the arc’s turn.

Parameters:  
- **h** – the end along the plane’s first axis.

- **v** – the end along the plane’s second axis.

- **t** – the start tangent, (dh, dv), a px.Vertex or netgen’s gp_Vec2d.

- **name** – the arc’s name.

- **maxh** – the arc’s mesh size.

Raises:  
**ValueError** – the end is the cursor’s position, or t runs along the chord; the arc meets no vertex of the open wire (call Finish() first).

Examples

``` python
wp.ArcTo(2, 2, (1, 0))
```

Circle(*\*args: float*) → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Draws a circle as a wire of its own: Circle(h, v, r) around (h, v), Circle(r) around the cursor’s position.

Parameters:  
**args** – h, v, r, or r.

Raises:  
- **TypeError** – not one or three numbers.

- **ValueError** – a wire is open; call Finish() first.

Examples

``` python
wp.Circle(0, 0, 1)
wp.Circle(0.5)
```

Close(*name: str \| None = None*) → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Draws a line back to the wire’s start point and finishes the wire.

Parameters:  
**name** – the line’s name.

Examples

``` python
wp.Close()
wp.Close(name="bottom")
```

Direction(*dirh: float*, *dirv: float*) → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Sets the cursor’s direction to (dirh, dirv).

Parameters:  
- **dirh** – the direction’s component along the plane’s first axis.

- **dirv** – the direction’s component along the plane’s second axis.

Raises:  
**ValueError** – the direction has zero length.

Examples

``` python
wp.Direction(0, 1)
```

Ellipse(*major: float*, *minor: float*) → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Draws an ellipse around the cursor’s position, major along the direction, as a wire of its own.

It is a closed spline through points of the ellipse; the cursor stays.

Parameters:  
- **major** – the semi-axis along the direction.

- **minor** – the semi-axis across it.

Raises:  
**ValueError** – a wire is open; call Finish() first.

Examples

``` python
wp.Ellipse(2, 1)
```

Face() → [Face](geometry.md#planex.Face "planex._planex.Face") \| [Shape](geometry.md#planex.Shape "planex._planex.Shape")  
The face of all finished wires, which are then forgotten; a px.Shape when they make several regions.

Regions follow planex’s rule for drawn edges (even-odd), as netgen’s one face holds them: a wire inside one other wire is a hole, a wire inside a hole an island (a face of its own), a wire outside the others another region, and where two wires overlap is a hole. A hole drawn without Reverse(), a clockwise outer wire and crossing wires give a PlanexWarning.

Raises:  
**ValueError** – there is no finished wire; a wire is not closed or not one chain.

Examples

``` python
wp.Rectangle(4, 2).MoveTo(2, 1).Circle(0.5).Reverse().Face()
```

Finish() → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Finishes the wire being drawn without closing it.

Examples

``` python
wp.Finish()
```

Last() → [Wire](geometry.md#planex.Wire "planex._planex.Wire") \| None  
The last finished wire, None if there is none; also called Last.

Raises:  
**ValueError** – the wire is not one chain (it branches or crosses itself); Wires() gives its chains.

Examples

``` python
wp.Wire()
```

Line(*\*args: Any*, *\*\*kwargs: Any*) → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Draws a line l along the direction, or by (dx, dy); the direction stays.

Parameters:  
- **args** – l and name, or dx, dy and name: netgen’s Line(l, name=None) and Line(dx, dy, name=None).

- **kwargs** – l, dx, dy, name.

Raises:  
**TypeError** – too many arguments, a missing or doubled one, or another keyword.

Examples

``` python
wp.Line(2)
wp.Line(1, 0.5, name="slope")
```

LineTo(*h: float*, *v: float*, *name: str \| None = None*) → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Draws a line to (h, v); the direction stays.

Parameters:  
- **h** – the end along the plane’s first axis.

- **v** – the end along the plane’s second axis.

- **name** – the line’s name.

Raises:  
**ValueError** – the line meets no vertex of the open wire; call Finish() first.

Examples

``` python
wp.LineTo(3, 0)
wp.LineTo(3, 2, name="side")
```

Move(*l: float*) → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Moves the cursor l along its direction, as MoveTo.

Parameters:  
**l** – the distance, negative backwards.

Examples

``` python
wp.Move(2)
```

MoveTo(*h: float*, *v: float*) → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Moves the cursor to (h, v); the next edge starts there, and joins the open wire if it meets it.

Parameters:  
- **h** – the position along the plane’s first axis.

- **v** – the position along the plane’s second axis.

Examples

``` python
wp.MoveTo(1, 2)
```

NameVertex(*name: str*) → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Names the vertex where the last line, arc or spline ended.

Parameters:  
**name** – the vertex’s name.

Examples

``` python
wp.Line(2).NameVertex("corner")
```

Offset(*d: float*) → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Replaces the last wire by its offset at distance d, running the way the wire did; a wire being drawn is finished first.

A positive d moves the wire to its left as netgen does: a counterclockwise wire grows, a reversed one shrinks.

Parameters:  
**d** – the offset distance, as Face.Offset for a closed wire and Wire.Offset for an open one.

Raises:  
**ValueError** – there is no finished wire, or the offset has more than one loop.

Examples

``` python
wp.Rectangle(2, 1).Offset(0.25)
```

Plot(*\*args: Any*, *\*\*kwargs: Any*) → Any  
Draws the sketch drawn on, as its Plot.

Parameters:  
- **args** – the arguments of the sketch’s Plot.

- **kwargs** – its keyword arguments.

Examples

``` python
wp.Plot()
```

Rectangle(*l: float*, *w: float*, *name: str \| None = None*) → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Draws a rectangle with the cursor’s position as a corner, l along the direction and w to its left.

Parameters:  
- **l** – the length along the direction.

- **w** – the width.

- **name** – the sides’ name.

Examples

``` python
wp.Rectangle(3, 2)
```

RectangleC(*l: float*, *w: float*, *name: str \| None = None*) → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Draws a rectangle centred on the cursor’s position, l along the direction.

Parameters:  
- **l** – the length along the direction.

- **w** – the width.

- **name** – the sides’ name.

Examples

``` python
wp.RectangleC(3, 2)
```

Reverse() → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Reverses the last finished wire: its edges and their order; a hole of a face is a clockwise wire.

Raises:  
**ValueError** – there is no finished wire.

Examples

``` python
wp.Circle(0.5).Reverse()
```

Rotate(*ang: float*) → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Turns the cursor’s direction by ang degrees, counterclockwise.

Parameters:  
**ang** – the angle in degrees.

Examples

``` python
wp.Rotate(90)
```

Spline(*points: list*, *periodic: bool = False*, *tol: float = 1e-08*, *tangents: dict \| None = None*, *start_from_localpos: bool = True*, *name: str \| None = None*) → [WorkPlane](#planex.WorkPlane "planex._workplane.WorkPlane")  
Draws a spline through the points (OCC’s interpolation, as netgen); the direction becomes its end tangent’s.

Parameters:  
- **points** – the points (h, v) to pass through.

- **periodic** – True closes the spline smoothly.

- **tol** – the first point may not lie this close to the cursor’s position; points this close count as one.

- **tangents** – {index: (dh, dv)}: the tangent at a point; 0 is the start.

- **start_from_localpos** – True starts at the cursor’s position, added before the points.

- **name** – the spline’s name.

Raises:  
**ValueError** – the first point lies within tol of the cursor’s position; the spline meets no vertex of the open wire (call Finish() first).

Examples

``` python
wp.Spline([(1, 1), (2, 0)])
wp.Spline([(1, 1), (2, 0)], tangents={0: (1, 0)})
```

ToOCCFace(*\*args: Any*, *\*\*kwargs: Any*) → Any  
The OCC export of Face()’s px.Face, an occ.Face; the wires are not forgotten.

Parameters:  
- **args** – the arguments of px.Face.ToOCCFace.

- **kwargs** – its keyword arguments.

Raises:  
**ValueError** – as Face().

Examples

``` python
wp.Rectangle(4, 2).ToOCCFace()
```

ToOCCShape(*\*args: Any*, *\*\*kwargs: Any*) → Any  
The OCC export of Face()’s px.Face as px.Face.ToOCCShape; the wires are not forgotten.

Parameters:  
- **args** – the arguments of px.Face.ToOCCShape.

- **kwargs** – its keyword arguments.

Raises:  
**ValueError** – as Face().

Examples

``` python
wp.Rectangle(4, 2).ToOCCShape()
```

ToOCCWire(*\*args: Any*, *\*\*kwargs: Any*) → Any  
The OCC export of Wire(), an occ.Wire.

Parameters:  
- **args** – the arguments of px.Wire.ToOCCWire.

- **kwargs** – its keyword arguments.

Raises:  
**ValueError** – there is no finished wire.

Examples

``` python
wp.Line(2).Rotate(90).Line(1).Finish().ToOCCWire()
```

ToOCCWires(*\*args: Any*, *\*\*kwargs: Any*) → Any  
The OCC export of the finished wires as netgen groups them, a compound of occ.Wire, one per wire.

Parameters:  
- **args** – the arguments of px.Wire.ToOCCWires.

- **kwargs** – its keyword arguments.

Raises:  
**ValueError** – there is no finished wire.

Examples

``` python
wp.Rectangle(4, 2).MoveTo(2, 1).Circle(0.5).ToOCCWires()
```

Wire() → [Wire](geometry.md#planex.Wire "planex._planex.Wire") \| None  
The last finished wire, None if there is none; also called Last.

Raises:  
**ValueError** – the wire is not one chain (it branches or crosses itself); Wires() gives its chains.

Examples

``` python
wp.Wire()
```

Wires() → list\[[Wire](geometry.md#planex.Wire "planex._planex.Wire")\]  
All finished wires, each as clean chains: a wire that branches or crosses itself gives several.

Examples

``` python
wp.Wires()
```

*property* cur_dir*: [Vertex](geometry.md#planex.Vertex "planex._planex.Vertex")*  
The cursor’s direction, a unit vector.

*property* cur_loc*: [Vertex](geometry.md#planex.Vertex "planex._planex.Vertex")*  
The cursor’s position.

*property* sketch*: [QuickSketch](sketches.md#planex.QuickSketch "planex._planex.QuickSketch")*  
The px.QuickSketch drawn on.

*property* start_pnt*: [Vertex](geometry.md#planex.Vertex "planex._planex.Vertex")*  
Where the wire started, the point Close() goes back to.
