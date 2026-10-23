# Geometry

Vertices, edges, wires, faces, shapes, compounds and planes. See [Objects in planex2d](../core/basics/objects.md) for an overview.

## Edges and wires

Line(*start: Point*, *end: Point*, *plane: [Plane](#planex.Plane "planex.Plane") \| None = None*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
A wire of one line from start to end.

Parameters:  
- **start** – the start (h, v) in the plane, or (x, y, z) on it.

- **end** – the end (h, v) in the plane, or (x, y, z) on it.

- **plane** – the plane the wire lies on: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar OCC shape (its plane); default the xy plane.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
- **TypeError** – plane has another form.

- **ValueError** – an (x, y, z) point is off the plane.

Examples

``` python
px.Line((0, 0), (10, 0))
px.Line((0, 0), (0, 5), plane=px.YZ)
```

&nbsp;

Arc(*start: Point*, *end: Point*, *center: Point*, *reverse: bool = False*, *tol: float \| None = None*, *plane: [Plane](#planex.Plane "planex.Plane") \| None = None*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
A wire of one arc from start to end around center, counterclockwise, or clockwise with reverse=True.

Start and end lie equally far from the center within tol; the center is then moved onto their bisector.

Parameters:  
- **start** – the start (h, v) in the plane, or (x, y, z) on it.

- **end** – the end (h, v) in the plane, or (x, y, z) on it.

- **center** – the centre (h, v) in the plane, or (x, y, z) on it.

- **reverse** – True runs clockwise from start to end.

- **tol** – the largest difference of the distances of start and end from the center; None: the rounding.

- **plane** – the plane the wire lies on: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar OCC shape (its plane); default the xy plane.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
- **ValueError** – start and end differ in their distance from the center by more than tol, or tol is negative or not finite; an (x, y, z) point is off the plane.

- **TypeError** – plane has another form.

Examples

``` python
px.Arc((1, 0), (0, 1), (0, 0))
px.Arc((1, 0), (0, 1), (0, 0), reverse=True)
```

&nbsp;

Circle(*center: Point*, *r: float*, *reverse: bool = False*, *plane: [Plane](#planex.Plane "planex.Plane") \| None = None*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
A wire of one circle of radius r around center, counterclockwise, or clockwise with reverse=True.

The circle is one edge with no vertex; where other edges cross or touch it, it is cut into arcs. Its origin, where the edge starts and ends, is the point r from the center along +h.

Parameters:  
- **center** – the centre (h, v) in the plane, or (x, y, z) on it.

- **r** – the radius, \> 0.

- **reverse** – True runs clockwise.

- **plane** – the plane the wire lies on: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar OCC shape (its plane); default the xy plane.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
- **ValueError** – r is not positive and finite; the (x, y, z) center is off the plane.

- **TypeError** – plane has another form.

Examples

``` python
px.Circle((0, 0), 1)
px.Circle((5, 0), 2, reverse=True).Face()
```

&nbsp;

Spline(*points: list\[Point\]*, */*, *periodic: bool = False*, *tol: float = 1e-08*, *tangents: dict\[int, tuple\[float, float\]\] = {}*, *plane: [Plane](#planex.Plane "planex.Plane") \| None = None*, *style: str \| None = None*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
A wire of one spline through the points, two pieces if periodic.

Style ‘px’ is planex’s interpolation, using only the tangents’ directions; periodic, the C2 periodic cubic, the same curve whichever point the list starts with. Style ‘occ’ is OCC’s Geom2dAPI_Interpolate as netgen calls it, its curve exactly (periodic: OCC’s, which depends on the start point and has a kink there).

Parameters:  
- **points** – the points (h, v), or (x, y, z) on the plane, the spline runs through, in order; a periodic spline lists its start once.

- **periodic** – True closes the spline smoothly through the first point.

- **tol** – points closer than this to the next count as the same point.

- **tangents** – {index of a point: its tangent direction (dh, dv)}.

- **plane** – the plane the wire lies on: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar OCC shape (its plane); default the xy plane.

- **style** – ‘px’ or ‘occ’; None takes px.config.spline_style.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
- **ValueError** – the curve crosses itself; fewer than 2 points (3 if periodic); two neighbouring points coincide; a tangent is zero or at no point; a periodic spline repeats its first point at the end; style is not ‘px’ or ‘occ’; a value is not finite.

- **TypeError** – plane has another form.

Examples

``` python
px.Spline([(0, 0), (1, 1), (2, 0)])
px.Spline([(0, 0), (1, 1), (2, 0)], tangents={0: (0, 1)}, style="occ")
```

&nbsp;

BSpline(*poles: list\[Point\]*, *knots: list\[float\]*, *degree: int*, *weights: list\[float\] \| None = None*, *periodic: bool = False*, *plane: [Plane](#planex.Plane "planex.Plane") \| None = None*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
A wire of one spline given as a b-spline, two pieces if it is closed.

Parameters:  
- **poles** – the control points (h, v), or (x, y, z) on the plane.

- **knots** – the flattened knots, each repeated by its multiplicity.

- **degree** – the degree of the curve.

- **weights** – one weight per pole for a rational curve; None: not rational.

- **periodic** – True for a periodic b-spline.

- **plane** – the plane the wire lies on: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar OCC shape (its plane); default the xy plane.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
- **ValueError** – the curve crosses itself.

- **ValueError** – the poles, knots, degree and weights do not form a b-spline.

- **TypeError** – plane has another form.

Examples

``` python
px.BSpline([(0, 0), (1, 2), (3, 2), (4, 0)], [0, 0, 0, 0, 1, 1, 1, 1], 3)
px.BSpline([(1, 0), (1, 1), (0, 1)], [0, 0, 0, 1, 1, 1], 2, weights=[1, 0.7071, 1])
```

&nbsp;

Wires(*edges: [Wire](#planex.Wire "planex.Wire") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView") \| list\[[Wire](#planex.Wire "planex.Wire") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView")\]*, */*, *tol: float \| None = None*, *\**, *id: str \| None = None*) → list\[[Wire](#planex.Wire "planex.Wire")\]  
Loose edges as a list of clean wires: ends within tol joined, the edges cut against each other (crossings, T-junctions, an overlapping stretch kept once), then the chains.

Nothing is merged, a gap above tol stays open. An outline with a rib ending on it gives the outline cut at the rib’s end, still one closed wire, and the rib as its own wire.

Parameters:  
- **edges** – an edge or wire, or a list of them (px.Line, px.Arc, px.Spline, edge views, wires).

- **tol** – the largest gap joined; None: the rounding.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
- **ValueError** – the edges lie on different planes, or tol is negative or not finite.

- **TypeError** – an item is not an edge or wire.

Examples

``` python
px.Wires([px.Line((0, 0), (2, 0)), px.Line((1, -1), (1, 1))])
px.Wires(edges, tol=1e-6)
```

&nbsp;

ClosedOpenWires(*wires: [Wire](#planex.Wire "planex.Wire") \| list\[[Wire](#planex.Wire "planex.Wire")\]*, */*) → list\[[Wire](#planex.Wire "planex.Wire")\]  
Copies of the wires, each open one closed by a straight line from its end back to its start.

Closed wires are copied as they are.

Parameters:  
**wires** – a wire or a list of wires.

Returns:  
a list of wires.

Raises:  
**ValueError** – the closing line would cross or overlap its wire (a single straight edge cannot be closed).

Examples

``` python
px.ClosedOpenWires(u)
px.Face(px.ClosedOpenWires([outline, u]))
```

&nbsp;

*class* Vertex  
A point (h, v) of the plane, the construction vertex; it acts as its coordinates: h, v = p, np.array(p), p == (h, v).

Copy() → [Vertex](#planex.Vertex "planex.Vertex")  
An independent copy; copy.copy and copy.deepcopy give the same.

Examples

``` python
b = a.Copy()
```

*property* h  
float

Where it is along the plane’s first axis.

Type:  
h

*property* v  
float

Where it is along the plane’s second axis.

Type:  
v

*property* x  
float

Its first coordinate in the plane (h), writable.

Type:  
x

*property* y  
float

Its second coordinate in the plane (v), writable.

Type:  
y

&nbsp;

*class* Wire  
One clean chain of edges, open or closed: consecutive edges share an end bit-exactly, no branch, crossing or overlap; the edges are in chain order, each with its own direction.

Add(*edge: [Wire](#planex.Wire "planex.Wire") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView") \| list\[[Wire](#planex.Wire "planex.Wire") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView")\]*, *tol: float \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
Extends the chain in place: the edge or wire is appended at the wire’s end or prepended at its start.

It joins in either direction and keeps its own; touching both ends closes the wire. Its ends within tol of the wire’s are moved onto them; the wire’s edges stay as they are.

Parameters:  
- **edge** – an edge (px.Line, px.Arc, px.Spline, an edge view) or a wire.

- **tol** – the largest gap joined; None: the rounding.

Returns:  
this Wire.

Raises:  
- **ValueError** – the wire is closed, the edge touches neither free end, it would cross, overlap or branch the wire (px.Wires builds several wires), it lies on another plane, or tol is negative or not finite; nothing changes then.

- **TypeError** – edge is not an edge or wire.

Examples

``` python
w = px.Wire().Add(px.Line((0, 0), (1, 0))).Add(px.Line((1, 0), (1, 1)))
w.Add(px.Line((1, 1), (0, 0)), tol=1e-6)
```

Chamfer(*at: [VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str \| list\[[VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str\]*, *d: float*, */*, *d2: float \| None = None*, *angle: float \| None = None*, *reverse: bool = False*) → [Wire](#planex.Wire "planex.Wire")  
Cuts corners with a straight line, in place.

A corner is the end of exactly two edges of one part whose directions differ (a vertex that more edges or faces share, an open end, a point on no vertex and a tangent joint are not corners). The new line is a new edge with the id Chamfer.k (k counts per object, in the order the corners are given) and the properties of the two edges merged, the name of the first edge winning: the one that arrives at the corner going counterclockwise around its loop (along the chain for an open wire). The two edges keep their ids and properties, shortened to the cut points; the corner vertex and its properties are gone. Lines and arcs are cut exactly; on a spline the point is found on its exact curve to rounding and the spline is cut there.

A length on a curved edge is the straight distance from the corner to the cut point.

A size that does not fit (the point on or beyond the far end of an edge, or past a neighbouring cut of the same call) makes that corner not viable: it is skipped with a PlanexWarning that names the largest size that fits. If no corner is viable the call raises.

Parameters:  
- **at** – the corners: a vertex (wire.vertices\[0\]), a point (h, v) on a vertex, a vertex name (every vertex with that name), or a list or selection of these; there is no default.

- **d** – the length on the first edge, a length \> 0; also on the second if d2 and angle are None.

- **d2** – the length on the second edge, a length \> 0; None: not given.

- **angle** – with d, the angle in degrees between the chamfer and the first edge, measured at its cut point towards the corner, between 0 and 180; None: not given. d2 and angle exclude each other.

- **reverse** – True makes the second edge the first.

Returns:  
this Wire, so calls chain.

Raises:  
- **TypeError** – at is none of the forms above.

- **ValueError** – a size is not positive and finite, the angle is not between 0 and 180 or d2 and angle are both given, no corner is given, a name is no vertex’s name, a vertex belongs to another object, or no corner can be cut (the message names each corner and why); nothing changes then.

Examples

``` python
wire.Chamfer((4, 0), 1)
wire.Chamfer([(4, 0), (4, 4)], 1, d2=2)
```

Chamfered(*at: [VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str \| list\[[VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str\]*, *d: float*, */*, *d2: float \| None = None*, *angle: float \| None = None*, *reverse: bool = False*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
A copy with the corners chamfered as Chamfer does; this Wire is not changed.

Parameters:  
- **at** – the corners: a vertex (wire.vertices\[0\]), a point (h, v) on a vertex, a vertex name (every vertex with that name), or a list or selection of these; there is no default.

- **d** – the length on the first edge, a length \> 0; also on the second if d2 and angle are None.

- **d2** – the length on the second edge, a length \> 0; None: not given.

- **angle** – with d, the angle in degrees between the chamfer and the first edge, measured at its cut point towards the corner, between 0 and 180; None: not given. d2 and angle exclude each other.

- **reverse** – True makes the second edge the first.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Returns:  
the copy.

Raises:  
- **TypeError** – at is none of the forms above.

- **ValueError** – a size is not positive and finite, the angle is not between 0 and 180 or d2 and angle are both given, no corner is given, a name is no vertex’s name, a vertex belongs to another object, or no corner can be cut (the message names each corner and why); nothing changes then.

Examples

``` python
wire.Chamfered((4, 0), 1)
wire.Chamfered([(4, 0), (4, 4)], 1, d2=2)
```

CloseGaps(*tol: float*) → [Wire](#planex.Wire "planex.Wire")  
Joins every endpoint without an exact match on another edge with all endpoints of other edges within tol, in place.

It closes the wire if its ends are that close.

Raises:  
**ValueError** – an end would land on the wire’s own inner vertex; nothing changes then.

Parameters:  
**tol** – the largest gap closed, a length.

Returns:  
this Wire.

Examples

``` python
wire.CloseGaps(1e-6)
```

ClosestPoint(*point: Point \| Point3 \| np.ndarray*, */*) → tuple\[float, ...\] \| np.ndarray  
The nearest point to point, in the plane for (h, v), in 3d for (x, y, z).

Parameters:  
**point** – (h, v) or (x, y, z); many points as an (n, 2) or (n, 3) array give an array of that shape.

Raises:  
- **ValueError** – it is empty.

- **TypeError** – point is not a point.

Examples

``` python
wire.ClosestPoint((5, 0))
wire.ClosestPoint([(0, 0), (5, 0)])
```

Copy(*\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
An independent copy; copy.copy and copy.deepcopy give the same.

Parameters:  
**id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Examples

``` python
b = a.Copy()
```

Distance(*target: Point \| Point3 \| [Face](#planex.Face "planex.Face") \| [Shape](#planex.Shape "planex.Shape") \| [Wire](#planex.Wire "planex.Wire") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView") \| [VertexView](selections.md#planex.VertexView "planex.VertexView") \| [FaceView](selections.md#planex.FaceView "planex.FaceView") \| TopoDS_Shape \| np.ndarray*, */*, *project: bool = False*, *components: str \| None = None*, *signed: bool = False*) → float \| tuple\[float, ...\] \| np.ndarray  
The smallest distance to the target; 0 inside a face.

A target in another plane is measured in 3d, or projected into this plane with project.

Parameters:  
- **target** – a point, (h, v) in the plane or (x, y, z) in 3d, a Face, Shape, Wire, edge, vertex, face of a shape or OCC shape; many points as an (n, 2) or (n, 3) array.

- **project** – True projects a target in another plane into this one.

- **components** – letters of ‘hvxyz’: the components of the shortest connection from here to the target instead of its length.

- **signed** – True: the distance of points (h, v) to the boundary of the union of the faces (edges between faces do not count), negative inside, 0 on it.

Returns:  
a float; with components a tuple of them, one letter a float; many points an array (n,), with k components (n, k), one letter (n,).

Raises:  
- **ValueError** – it is empty; components has other letters; signed with components, with points not (h, v), or without faces.

- **TypeError** – the target has another form.

Examples

``` python
wire.Distance((5, 0))
wire.Distance(other, components="hv")
```

Face(*\**, *id: str \| None = None*) → [Face](#planex.Face "planex.Face")  
The region bounded by the edges: overlapping stretches removed (of an open wire one copy stays), crossings cut.

Parameters:  
**id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – the region has several parts; px.Shape(\[wire\]) keeps them all.

Examples

``` python
face = wire.Face()
```

Fillet(*at: [VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str \| list\[[VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str\]*, *r: float*, */*) → [Wire](#planex.Wire "planex.Wire")  
Rounds corners with an arc tangent to both edges, in place.

A corner is the end of exactly two edges of one part whose directions differ (a vertex that more edges or faces share, an open end, a point on no vertex and a tangent joint are not corners). The arc of radius r is a new edge with the id Fillet.k (k counts per object, in the order the corners are given) and the properties of the two edges merged, the name of the first edge winning: the one that arrives at the corner going counterclockwise around its loop (along the chain for an open wire). The two edges keep their ids and properties, shortened to the touch points; the corner vertex and its properties are gone. Lines and arcs are cut exactly; on a spline the point is found on its exact curve to rounding and the spline is cut there.

A size that does not fit (the point on or beyond the far end of an edge, or past a neighbouring cut of the same call) makes that corner not viable: it is skipped with a PlanexWarning that names the largest size that fits. If no corner is viable the call raises.

Parameters:  
- **at** – the corners: a vertex (wire.vertices\[0\]), a point (h, v) on a vertex, a vertex name (every vertex with that name), or a list or selection of these; there is no default.

- **r** – the radius, a length \> 0.

Returns:  
this Wire, so calls chain.

Raises:  
- **TypeError** – at is none of the forms above.

- **ValueError** – a size is not positive and finite, no corner is given, a name is no vertex’s name, a vertex belongs to another object, or no corner can be cut (the message names each corner and why); nothing changes then.

Examples

``` python
wire.Fillet((4, 0), 0.5)
wire.Fillet(wire.vertices[0], 0.5)
```

Filleted(*at: [VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str \| list\[[VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str\]*, *r: float*, */*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
A copy with the corners rounded as Fillet does; this Wire is not changed.

Parameters:  
- **at** – the corners: a vertex (wire.vertices\[0\]), a point (h, v) on a vertex, a vertex name (every vertex with that name), or a list or selection of these; there is no default.

- **r** – the radius, a length \> 0.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Returns:  
the copy.

Raises:  
- **TypeError** – at is none of the forms above.

- **ValueError** – a size is not positive and finite, no corner is given, a name is no vertex’s name, a vertex belongs to another object, or no corner can be cut (the message names each corner and why); nothing changes then.

Examples

``` python
wire.Filleted((4, 0), 0.5)
wire.Filleted(wire.vertices[0], 0.5)
```

FitArcs(*tol: float \| None = None*, *max_edge_length: float \| None = None*, *max_edge_angle: float \| None = None*, *ignore_edges_smaller: float \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
Replaces chains of at least three edges shorter than max_edge_length by arcs of at most 180 degrees (or lines) that stay within tol of the chain, in place.

Parameters:  
- **tol** – the largest distance of the arc from the chain; None takes px.config.fit_arcs\[“tol”\].

- **max_edge_length** – only edges shorter than this form a chain; None takes px.config.fit_arcs\[“max_edge_length”\].

- **max_edge_angle** – the largest turn between neighbouring edges of a chain, in degrees; None takes px.config.fit_arcs\[“max_edge_angle”\].

- **ignore_edges_smaller** – edges shorter than this do not end a chain by their direction; None takes px.config.fit_arcs\[“ignore_edges_smaller”\].

Returns:  
this Wire.

Examples

``` python
wire.FitArcs()
wire.FitArcs(tol=1e-4, max_edge_length=0.5)
```

Hausdorff(*other: Any*, *samples: int = 64*) → float  
The Hausdorff distance to other: the largest distance from a point of one to the other.

Parameters:  
- **other** – an object, element or selection.

- **samples** – the samples per edge before the search for the largest distance refines.

Examples

``` python
wire.Hausdorff(other_wire)
shape.edges("Line.1").Hausdorff(shape.edges("Line.2"), samples=128)
```

Intersections(*other: [Wire](#planex.Wire "planex.Wire")*, */*) → list\[[Vertex](#planex.Vertex "planex.Vertex")\]  
The points where its edges meet the edges of other, each once, in the plane.

Parameters:  
**other** – a Wire on the same plane.

Raises:  
**ValueError** – other lies on a different plane.

Examples

``` python
wire.Intersections(px.Line((0, -5), (0, 5)))
```

IsClosed() → bool  
True if every endpoint has an exact match on another edge (OpenEnds() is empty).

Examples

``` python
wire.IsClosed()
```

Measure(*name: str \| [FaceView](selections.md#planex.FaceView "planex.FaceView") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView") \| None = None*, *to: str \| Point \| Point3 \| [Face](#planex.Face "planex.Face") \| [Shape](#planex.Shape "planex.Shape") \| [Wire](#planex.Wire "planex.Wire") \| None = None*, *project: bool = False*) → dict\
Measure(*f: Callable*, */*, *\*args*, *id: str \| None = None*, *\*\*params*) → object  
The measures of the faces and edges named name, or of the one with that id or selected, as a dict.

Its keys: type, faces, area, edges, length, center (in 3d; of an arc or circle its centre, with radius and angle), inertia, Ih, Iv, Ihv, I1, I2, principal_angle, bounding_box; with to also distance, du, dv, dx, dy, dz, points and the angle between two lines.

Parameters:  
- **name** – a name, an id, or a selected face or edge; None measures all.

- **to** – a name, or anything Distance takes: adds the distance to it.

- **project** – True projects a target of to in another plane into this one.

- **f** – a measurement, a callable f(obj, \*args, \*\*params) (see px.measurements): Measure returns its value.

- **args** – the further arguments of f.

- **id** – stores the measurement f under this id: obj.measures then holds it next to the standard measurements, RemoveMeasure(id) drops it.

- **params** – the keyword arguments of f.

Raises:  
- **ValueError** – no face or edge has that name or id.

- **TypeError** – name has another form.

- **TypeError** – id without a measurement f.

Examples

``` python
wire.Measure()
wire.Measure(px.measurements.MinThickness(), id="wall")
```

MergeCollinearEdges(*tol: float \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
Merges lines that meet alone at their vertices into fewer lines, in place: a vertex goes while every vertex removed between its neighbours stays within tol of the line through them.

The result stays within tol of the edges the call was given; a second call may merge more. The edges of a spline stay.

Parameters:  
**tol** – the largest distance of a removed vertex from the new line; None takes px.config.merge_collinear_closer.

Returns:  
this Wire.

Examples

``` python
wire.MergeCollinearEdges()
wire.MergeCollinearEdges(1e-6)
```

Mirror(*point: Point*, */*, *dir: tuple\[float, float\]*) → [Wire](#planex.Wire "planex.Wire")\
Mirror(*point: Point3*, */*, *normal: tuple\[float, float, float\]*) → [Wire](#planex.Wire "planex.Wire")  
Mirrors it in place at the line through point along dir in its plane, or at the plane through point with normal in 3d.

In 3d its plane is mirrored and keeps a right-handed coordinate system, so its coordinates in the plane are mirrored at the plane’s first axis.

Parameters:  
- **point** – a point of the mirror line, (h, v), or of the mirror plane in 3d, (x, y, z).

- **dir** – the direction of the mirror line in the plane, (dh, dv).

- **normal** – the normal of the mirror plane in 3d, (dx, dy, dz).

Returns:  
this Wire.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
wire.Mirror((0, 0), (0, 1))
wire.Mirror((0, 0, 0), (1, 0, 0))
```

Mirrored(*point: Point*, */*, *dir: tuple\[float, float\]*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")\
Mirrored(*point: Point3*, */*, *normal: tuple\[float, float, float\]*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
A copy mirrored at the line through point along dir in its plane, or at the plane through point with normal in 3d; this Wire is not changed.

In 3d its plane is mirrored and keeps a right-handed coordinate system, so its coordinates in the plane are mirrored at the plane’s first axis.

Parameters:  
- **point** – a point of the mirror line, (h, v), or of the mirror plane in 3d, (x, y, z).

- **dir** – the direction of the mirror line in the plane, (dh, dv).

- **normal** – the normal of the mirror plane in 3d, (dx, dy, dz).

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
wire.Mirrored((0, 0), (0, 1))
wire.Mirrored((0, 0, 0), (1, 0, 0))
```

Normal() → tuple\[float, float, float\]  
The normal of its plane in 3d.

Examples

``` python
wire.Normal()
```

Offset(*d: float*, *\**, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")  
The band within distance d of the edges, the union of the offsets of all edges, as a Shape.

Parameters:  
- **d** – the distance from the edges, a length.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Examples

``` python
band = wire.Offset(0.5)
```

OpenEnds() → list\[tuple\[int, bool\]\]  
The endpoints without an exact match on another edge, as (edge index, True at its end, False at its start).

Examples

``` python
wire.OpenEnds()
```

Plot(*plane: [Plane](#planex.Plane "planex.Plane") \| None = None*, */*, *ax: Any = None*, *line_col: Color = 'black'*, *arc_col: Color = '#ff7f0e'*, *spline_col: Color = '#2ca02c'*, *face_col: Color \| None = '#dde6f0'*, *mark_edges_smaller: float \| None = None*, *mark_free_edges: bool = False*, *highlight: Ids \| Any = None*, *legend: bool = True*, *show_constraints: bool \| None = None*, *show_ends: bool = False*, *show_direction: bool = False*, *show_parent: bool \| None = None*, *backend: str \| None = None*) → Any  
Draws a planex object in its plane with matplotlib or plotly.

A sketch is drawn as sk.Plot draws it (show_constraints, show_ends, show_direction, show_parent); the other options are for the other objects, e.g. px.Plot(sk.Shape(), face_col=”C0”). An OCC shape is cut at the plane (px.Intersect3d(plane, shape)), or without one drawn flat in its own plane as px.FromOCC gives it. Fields: px.PlotField; meshes: px.PlotMesh.

Parameters:  
- **obj** – a Face, Shape, Wire, Compound, sketch, selection, DeformableShape or OCC shape, or an id that names one (“Sketch.1”, “Shape.2.Line.3”).

- **plane** – the plane that cuts an OCC shape; None draws it flat in its own plane.

- **ax** – a matplotlib Axes or a plotly Figure to draw into, which also selects the backend.

- **line_col** – the colour of every edge; “type” draws lines in black, arcs in arc_col and splines in spline_col, with a legend entry each; “property” takes each edge’s own col where it has one.

- **arc_col** – the colour of arcs with line_col=”type”.

- **spline_col** – the colour of splines with line_col=”type”.

- **face_col** – fills the closed loops (faces and shapes; a wire on its own stays an outline unless face_col is given); a fill other than the default gets a legend entry labelled with the object’s id (‘Face.3’), the default fill none; None draws the outlines only.

- **mark_edges_smaller** – marks the edges shorter than this at their midpoints.

- **mark_free_edges** – True marks the edges with an end no other edge shares.

- **highlight** – marks the edges given: ids (“Line.3”, “Shape.1.Line.3”), selected edges or faces (a face: its edges), a selection, or indices or a boolean mask over obj.edges (for a Compound those of its parts in turn).

- **legend** – True shows the legend.

- **show_constraints** – for a sketch, as sk.Plot.

- **show_ends** – for a sketch, as sk.Plot.

- **show_direction** – for a sketch, as sk.Plot.

- **show_parent** – for a sketch, as sk.Plot.

- **backend** – “matplotlib” or “plotly” without ax; None is matplotlib.

Returns:  
the matplotlib Axes or the plotly Figure.

Raises:  
**ValueError** – a sketch with an option for other objects, an option for sketches with another object, a plane with a planex object, obj is a plane, or backend is unknown.

Examples

``` python
px.Plot(face, line_col="type")
px.Plot(solid, px.XY)
```

Plot3d(*plane_size: float \| None = None*, *backend: str = 'webgpu'*, *\*\*kwargs: Any*) → Any  
Draws planex objects, planes and OCC shapes in one 3d view.

Everything is exported to OCC, put into one compound and drawn; a sketch contributes its drawn edges. A plane is drawn as a square of side plane_size with a line along its u axis. backend “webgpu” draws with ngsolve_webgpu.jupyter.Draw: it needs ngsolve_webgpu (pip install ngsolve_webgpu, which brings the full ngsolve) and a browser with WebGPU, with the kernel on the same machine as the browser or its websocket port forwarded. backend “webgui” draws with netgen.webgui.Draw and needs only netgen.

Parameters:  
- **objects** – a Wire, Face, Shape, Compound, sketch, Plane or OCC shape, or a list of them.

- **plane_size** – the side of a plane’s square; None takes the largest extent of everything else, 1 without.

- **backend** – ‘webgpu’ or ‘webgui’.

- **kwargs** – go to the backend’s Draw (width, height; settings, euler_angles only for webgui).

Returns:  
the backend’s scene.

Raises:  
- **ValueError** – backend is unknown, or there is nothing to draw.

- **ImportError** – backend ‘webgpu’ without ngsolve_webgpu installed.

Examples

``` python
px.Plot3d([face, px.XY], plane_size=2)
shape.Plot3d(backend="webgui", height=400)
```

Polylines(*tol: float*) → list\[np.ndarray\]  
The loops and open chains as (n, 2) arrays of plane coordinates, each chord within tol of its edge.

A face gives its outer loop counterclockwise, then its holes clockwise; a shape its faces in turn, then its free wires; a wire’s loop runs the way the wire’s first edge runs; a closed loop ends at its first point.

Parameters:  
**tol** – the largest distance of a chord from its edge, a length \> 0.

Raises:  
**ValueError** – tol is not positive and finite, or below the rounding of an arc’s or a spline’s coordinates.

Examples

``` python
wire.Polylines(0.01)
```

Project(*shape: TopoDS_Shape*, */*) → list\[[Wire](#planex.Wire "planex.Wire")\]  
The edges of the OCC shape projected straight onto this plane, as a list of wires, as px.Project.

Parameters:  
**shape** – an OCC shape.

Examples

``` python
wire.Project(box)
```

RemoveMeasure(*id: str*) → None  
Drops the stored measurement id.

Parameters:  
**id** – the id the measurement was stored under (Measure(f, id=…)).

Raises:  
**ValueError** – no measurement is stored under id.

Examples

``` python
shape.RemoveMeasure("gap")
```

RemoveSmallEdges(*tol: float*) → [Wire](#planex.Wire "planex.Wire")  
Removes the lines and arcs no longer than tol in place; both ends of each merge into its midpoint.

Lengths are measured before any merge; splines and the edges of a spline (fitted at export) stay.

Parameters:  
**tol** – the largest length removed.

Returns:  
this Wire.

Examples

``` python
wire.RemoveSmallEdges(1e-4)
```

Rotate(*center: Point*, *angle: float*) → [Wire](#planex.Wire "planex.Wire")\
Rotate(*point: Point3*, */*, *dir: tuple\[float, float, float\]*, *angle: float*) → [Wire](#planex.Wire "planex.Wire")  
Rotates it in place by angle around center in its plane, or around the axis through point along dir in 3d.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **center** – the centre of the rotation in the plane, (h, v).

- **angle** – the angle in degrees.

- **point** – a point of the axis in 3d, (x, y, z).

- **dir** – the direction of the axis in 3d, (dx, dy, dz).

Returns:  
this Wire.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
wire.Rotate((0, 0), 90)
wire.Rotate((0, 0, 0), (0, 0, 1), 45)
```

Rotated(*center: Point*, *angle: float*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")\
Rotated(*point: Point3*, */*, *dir: tuple\[float, float, float\]*, *angle: float*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
A copy rotated by angle around center in its plane, or around the axis through point along dir in 3d; this Wire is not changed.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **center** – the centre of the rotation in the plane, (h, v).

- **angle** – the angle in degrees.

- **point** – a point of the axis in 3d, (x, y, z).

- **dir** – the direction of the axis in 3d, (dx, dy, dz).

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
wire.Rotated((0, 0), 90)
wire.Rotated((0, 0, 0), (0, 0, 1), 45)
```

Scale(*center: Point*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*) → [Wire](#planex.Wire "planex.Wire")\
Scale(*point: Point3*, */*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*) → [Wire](#planex.Wire "planex.Wire")  
Scales it in place by factor around center in its plane, or around point in 3d.

The mesh sizes maxh and minh stay unless asked for.

Parameters:  
- **center** – the centre of the scaling in the plane, (h, v).

- **point** – the centre of the scaling in 3d, (x, y, z).

- **factor** – the scale factor, \> 0.

- **scale_maxh** – True also scales maxh.

- **scale_minh** – True also scales minh.

Returns:  
this Wire.

Raises:  
**ValueError** – factor is not positive and finite, or a coordinate is not finite.

Examples

``` python
wire.Scale((0, 0), 2)
wire.Scale((0, 0), 0.5, scale_maxh=True)
```

Scaled(*center: Point*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")\
Scaled(*point: Point3*, */*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
A copy scaled by factor around center in its plane, or around point in 3d; this Wire is not changed.

The mesh sizes maxh and minh stay unless asked for.

Parameters:  
- **center** – the centre of the scaling in the plane, (h, v).

- **point** – the centre of the scaling in 3d, (x, y, z).

- **factor** – the scale factor, \> 0.

- **scale_maxh** – True also scales maxh.

- **scale_minh** – True also scales minh.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – factor is not positive and finite, or a coordinate is not finite.

Examples

``` python
wire.Scaled((0, 0), 2)
wire.Scaled((0, 0), 0.5, scale_maxh=True)
```

Simplified(*fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
A copy defeatured as Simplify does it; this wire is not changed.

Parameters:  
- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None: False for a wire. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
wire.Simplified()
wire.Simplified(fit_arcs=True, remove_edges_smaller=1e-6)
```

Simplify(*fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
Defeatures the wire in place as the OCC exports do: gaps closed, collinear lines merged, arcs and splines fitted, small edges removed, as enabled.

Edges that touch no other edge at either end are removed only with remove_free_edges=True.

Parameters:  
- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None: False for a wire. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
wire.Simplify()
wire.Simplify(fit_arcs=True, remove_edges_smaller=1e-6)
```

SmallEdges(*tol: float*) → list\[int\]  
The indices of the edges no longer than tol.

Parameters:  
**tol** – the largest length counted.

Examples

``` python
wire.SmallEdges(1e-3)
```

Split(*cutter: [Wire](#planex.Wire "planex.Wire") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView") \| [Face](#planex.Face "planex.Face") \| [Shape](#planex.Shape "planex.Shape") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch") \| TopoDS_Shape*, */*) → list\[[Shape](#planex.Shape "planex.Shape")\]  
Cuts every face and wire by the cutter into one Shape per piece.

What the cutter does not separate stays as it is; the pieces keep the face’s properties, the new edges the cutter’s.

Parameters:  
**cutter** – the edges that cut, a Wire, edge, Face, Shape, sketch or OCC shape; projected into the plane if it lies elsewhere.

Raises:  
**TypeError** – the cutter has the wrong form.

Examples

``` python
pieces = wire.Split(px.Line((0, -5), (0, 5)))
```

ToHV(*point: Point3*, */*) → tuple\[float, float\]\
ToHV(*points: list\[Point3\]*, */*) → list\[tuple\[float, float\]\]  
The plane coordinates (h, v) of the projection of a 3d point onto its plane, or of a list of points.

Parameters:  
- **point** – (x, y, z).

- **points** – a list of (x, y, z).

Examples

``` python
wire.ToHV((1, 2, 0))
wire.ToHV([(0, 0, 0), (1, 2, 0)])
```

ToOCCFace(*fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → TopoDS_Shape  
The faces of the closed wires of the edges, without removing internal edges: an OCC face, or a compound of several.

Parameters:  
- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None: False for a wire. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
wire.ToOCCFace()
wire.ToOCCFace(fit_arcs=True, remove_edges_smaller=1e-6)
```

ToOCCShape(*fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → TopoDS_Shape  
The wires of the edges as an OCC compound, as ToOCCWires().

Parameters:  
- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None: False for a wire. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
wire.ToOCCShape()
wire.ToOCCShape(fit_arcs=True, remove_edges_smaller=1e-6)
```

ToOCCWire(*approximation: bool = False*, *fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → TopoDS_Shape  
The edges as a single OCC wire.

Parameters:  
- **approximation** – True gives the splines as their arc approximation.

- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None: False for a wire. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
wire.ToOCCWire()
wire.ToOCCWire(fit_arcs=True, remove_edges_smaller=1e-6)
```

ToOCCWires(*approximation: bool = False*, *fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → TopoDS_Shape  
The wires of the edges as an OCC compound.

Parameters:  
- **approximation** – True gives the splines as their arc approximation.

- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None: False for a wire. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
wire.ToOCCWires()
wire.ToOCCWires(fit_arcs=True, remove_edges_smaller=1e-6)
```

ToPolyline(*tol: float*) → np.ndarray  
The wire as one (n, 2) array of plane coordinates, each chord within tol of its edge; a closed wire ends at its first point. The points run the way the wire’s first edge runs, so a reversed wire gives the reversed polyline. px.FromPolyline makes a wire of it again.

Parameters:  
**tol** – the largest distance of a chord from its edge, a length \> 0.

Raises:  
**ValueError** – tol is not positive and finite, or below the rounding of an arc’s or a spline’s coordinates; the wire is several chains.

Examples

``` python
points = wire.ToPolyline(0.01)
px.FromPolyline(points)
```

ToSVG(*size: float = 300*, *\*\*options: Any*) → str \| None  
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

ToShapely(*tol: float = 0.001*) → Any  
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

ToXYZ(*point: Point*, */*) → tuple\[float, float, float\]\
ToXYZ(*points: list\[Point\]*, */*) → list\[tuple\[float, float, float\]\]  
The 3d coordinates of a point (h, v) of its plane, or of a list of points.

Parameters:  
- **point** – (h, v) in the plane.

- **points** – a list of (h, v).

Examples

``` python
wire.ToXYZ((1, 2))
wire.ToXYZ([(0, 0), (1, 2)])
```

Translate(*d: tuple\[float, float\]*) → [Wire](#planex.Wire "planex.Wire")\
Translate(*dh: float*, *dv: float*) → [Wire](#planex.Wire "planex.Wire")\
Translate(*d: tuple\[float, float, float\]*) → [Wire](#planex.Wire "planex.Wire")\
Translate(*dx: float*, *dy: float*, *dz: float*) → [Wire](#planex.Wire "planex.Wire")  
Moves it in place by d: in the plane by (dh, dv), in 3d by (dx, dy, dz).

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **d** – the displacement, (dh, dv) in the plane or (dx, dy, dz) in 3d.

- **dh** – the displacement along the plane’s first axis.

- **dv** – the displacement along the plane’s second axis.

- **dx** – the displacement along x in 3d.

- **dy** – the displacement along y in 3d.

- **dz** – the displacement along z in 3d.

Returns:  
this Wire.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
wire.Translate((10, 0))
wire.Translate(0, 0, 5)
```

Translated(*d: tuple\[float, float\]*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")\
Translated(*dh: float*, *dv: float*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")\
Translated(*d: tuple\[float, float, float\]*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")\
Translated(*dx: float*, *dy: float*, *dz: float*, *\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
A copy moved by d: in the plane by (dh, dv), in 3d by (dx, dy, dz); this Wire is not changed.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **d** – the displacement, (dh, dv) in the plane or (dx, dy, dz) in 3d.

- **dh** – the displacement along the plane’s first axis.

- **dv** – the displacement along the plane’s second axis.

- **dx** – the displacement along x in 3d.

- **dy** – the displacement along y in 3d.

- **dz** – the displacement along z in 3d.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
wire.Translated((10, 0))
wire.Translated(0, 0, 5)
```

Trim(*cutter: [Wire](#planex.Wire "planex.Wire") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView") \| [Face](#planex.Face "planex.Face") \| [Shape](#planex.Shape "planex.Shape") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch") \| TopoDS_Shape*, *\**, *keep_closest_to: Point \| Point3 \| None = None*, *remove_closest_to: Point \| Point3 \| None = None*, *keep_largest: bool = False*, *keep_smallest: bool = False*, *keep_left: bool = False*, *keep_right: bool = False*, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")  
Cuts every face and wire by the cutter and keeps the pieces the one option given chooses.

What the cutter does not separate stays as it is.

Parameters:  
- **cutter** – the edges that cut, a Wire, edge, Face, Shape, sketch or OCC shape; projected into the plane if it lies elsewhere.

- **keep_closest_to** – keeps the piece closest to this point (a face piece by its centre, a wire piece by itself), (h, v) or (x, y, z).

- **remove_closest_to** – removes the piece closest to this point and keeps the others.

- **keep_largest** – keeps the piece of the largest area (faces) or length (wires).

- **keep_smallest** – keeps the piece of the smallest area or length.

- **keep_left** – keeps the face pieces left of the cutter as drawn; a shape’s free wires stay.

- **keep_right** – keeps the face pieces right of the cutter as drawn; a shape’s free wires stay.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Returns:  
a new Shape; this one is not changed.

Raises:  
- **ValueError** – not exactly one option is given; keep_left or keep_right without faces, or no piece lies wholly on that side.

- **TypeError** – the cutter or the point has the wrong form.

Examples

``` python
wire.Trim(px.Line((0, -5), (0, 5)), keep_left=True)
wire.Trim(cutter, keep_closest_to=(2, 1))
```

WriteBrep(*path: str \| os.PathLike*, */*, *\*\*options: Any*) → None  
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

WriteDXF(*path: str \| os.PathLike*, */*, *unit: float = 1.0*) → None  
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

WriteGeo(*path: str \| os.PathLike*, */*) → None  
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

WriteStep(*path: str \| os.PathLike*, */*, *\*\*options: Any*) → None  
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

*property* arcs  
list\[ArcEdge\]

The arcs among the edges, copies.

Type:  
arcs

*property* bounding_box  
tuple\[tuple\[float, float\], tuple\[float, float\]\] \| None

Its bounding box in the plane, ((hmin, vmin), (hmax, vmax)); None if it is empty.

Type:  
bounding_box

*property* center  
tuple\[float, float, float\] \| None

The centre of mass in 3d, (x, y, z): of the area for faces, of the length for wires; None if it is empty.

Type:  
center

*property* circles  
list\[CircleEdge\]

The circles among the edges, copies.

Type:  
circles

*property* col  
tuple\[float, float, float, float\] \| None

The colour (r, g, b, a), set as (r, g, b) or (r, g, b, a) all edges share, None if they differ; set on all edges.

Type:  
col

*property* edges  
EdgesView

The edges in chain order, a view of the wire’s edges: a property set on it is set on all of them.

Type:  
edges

*property* faces  
list\[Face\]

An empty list: a wire has no faces.

Type:  
faces

*property* hpref  
float \| None

Netgen’s hpref all edges share, None if they differ; set on all edges.

Type:  
hpref

*property* id  
str

The object’s id: the kind that made it and a number, given when first asked for (Sketch.1, Shape.3), or set (an ASCII letter, then letters, digits, \_ and single inner spaces); its elements’ ids follow it (Shape.3.Line.2).

Type:  
id

*property* inertia  
tuple\[tuple\[float, float, float\], tuple\[float, float, float\], tuple\[float, float, float\]\] \| None

The inertia tensor about the centre in 3d for unit density (per area for faces, per length for wires), three rows of three; None if it is empty.

Type:  
inertia

*property* layer  
int \| None

Netgen’s layer number all edges share, None if they differ; set on all edges.

Type:  
layer

*property* lines  
list\[LineEdge\]

The lines among the edges, copies.

Type:  
lines

*property* mass  
float

The total length of the edges.

Type:  
mass

*property* maxh  
float \| None

The largest mesh size (netgen’s maxh) all edges share, None if they differ; set on all edges.

Type:  
maxh

*property* measures  
dict

The standard measurements and the stored ones, evaluated now, as a dict.

Type:  
measures

*property* minh  
float \| None

The smallest mesh size (netgen’s minh) all edges share, None if they differ; set on all edges.

Type:  
minh

*property* name  
str \| None

The name all edges share, None if they differ; set on all edges.

Type:  
name

*property* partition  
list\[float\] \| None

The partition (the mesh points along an edge as increasing fractions of its length in (0, 1)) all edges share, None if they differ; set on all edges.

Type:  
partition

*property* splines  
list\[SplineEdge\]

The splines among the edges, copies.

Type:  
splines

*property* vertices  
VerticesView

The vertices, each once, in the order the edges reach them.

Type:  
vertices

*property* wires  
list\[Wire\]

The connected chains of the edges in traversal order, a list of wires.

Type:  
wires

&nbsp;

*class* LineEdge  
The geometry of a line; edges live in wires, made by px.Line.

Copy() → [LineEdge](#planex.LineEdge "planex.LineEdge")  
An independent copy; copy.copy and copy.deepcopy give the same.

Examples

``` python
b = a.Copy()
```

Cut(*other: [LineEdge](#planex.LineEdge "planex.LineEdge") \| [ArcEdge](#planex.ArcEdge "planex.ArcEdge")*, */*) → tuple\[CutResult, list\[[LineEdge](#planex.LineEdge "planex.LineEdge") \| [ArcEdge](#planex.ArcEdge "planex.ArcEdge")\]\]  
Cuts this line and other in place where they cross, touch or overlap: each keeps its first piece.

Parameters:  
**other** – a LineEdge or ArcEdge.

Returns:  
this line’s, then other’s without those lying on this line.

Return type:  
how they meet (a CutResult), and the further pieces

Examples

``` python
result, pieces = line.Cut(other)
```

MidPoint() → [Vertex](#planex.Vertex "planex.Vertex")  
The point halfway along the line.

Examples

``` python
line.MidPoint()
```

Offset(*d: float*) → [Face](#planex.Face "planex.Face")  
The face of the points within d of the line: its outline runs at distance d around it, round at the ends.

Parameters:  
**d** – the distance, \> 0.

Raises:  
**ValueError** – d is not positive and finite.

Examples

``` python
line.Offset(0.5)
```

Tangent(*p: Point*) → [Vertex](#planex.Vertex "planex.Vertex")  
The unit tangent in the line’s direction at its point p; (0, 0) if p is not on the line.

Parameters:  
**p** – a point (h, v) of the line.

Examples

``` python
line.Tangent(line.MidPoint())
```

*property* end  
Vertex

The end, a Vertex.

Type:  
end

*property* from_spline  
bool

True for a piece of a spline’s arc approximation.

Type:  
from_spline

*property* start  
Vertex

The start, a Vertex.

Type:  
start

&nbsp;

*class* ArcEdge  
The geometry of an arc; edges live in wires, made by px.Arc.

Copy() → [ArcEdge](#planex.ArcEdge "planex.ArcEdge")  
An independent copy; copy.copy and copy.deepcopy give the same.

Examples

``` python
b = a.Copy()
```

Cut(*other: [LineEdge](#planex.LineEdge "planex.LineEdge") \| [ArcEdge](#planex.ArcEdge "planex.ArcEdge")*, */*) → tuple\[CutResult, list\[[LineEdge](#planex.LineEdge "planex.LineEdge") \| [ArcEdge](#planex.ArcEdge "planex.ArcEdge")\]\]  
Cuts this arc and other in place where they cross, touch or overlap: each keeps its first piece.

Parameters:  
**other** – a LineEdge or ArcEdge.

Returns:  
this arc’s, then other’s without those lying on this arc.

Return type:  
how they meet (a CutResult), and the further pieces

Examples

``` python
result, pieces = arc.Cut(other)
```

MidPoint() → [Vertex](#planex.Vertex "planex.Vertex")  
The point halfway along the arc.

Examples

``` python
arc.MidPoint()
```

Offset(*d: float*) → [Face](#planex.Face "planex.Face")  
The face of the points within d of the arc: its outline runs at distance d around it, round at the ends.

Parameters:  
**d** – the distance, \> 0.

Raises:  
**ValueError** – d is not positive and finite.

Examples

``` python
arc.Offset(0.5)
```

Tangent(*p: Point*) → [Vertex](#planex.Vertex "planex.Vertex")  
The unit tangent in the arc’s direction at its point p; (0, 0) if p is not on the arc.

Parameters:  
**p** – a point (h, v) of the arc.

Examples

``` python
arc.Tangent(arc.MidPoint())
```

*property* ccw  
bool

True if the arc runs counterclockwise from start to end.

Type:  
ccw

*property* center  
Vertex

The centre, a Vertex.

Type:  
center

*property* end  
Vertex

The end, a Vertex.

Type:  
end

*property* from_spline  
bool

True for a piece of a spline’s arc approximation.

Type:  
from_spline

*property* radius  
float

The radius.

Type:  
radius

*property* start  
Vertex

The start, a Vertex.

Type:  
start

*property* sweep  
float

The angle from start to end in degrees, negative for a clockwise arc.

Type:  
sweep

&nbsp;

*class* CircleEdge  
The geometry of a full circle, one edge with no vertex; edges live in wires, made by px.Circle.

Copy() → [CircleEdge](#planex.CircleEdge "planex.CircleEdge")  
An independent copy; copy.copy and copy.deepcopy give the same.

Examples

``` python
b = a.Copy()
```

MidPoint() → [Vertex](#planex.Vertex "planex.Vertex")  
The point opposite the origin.

Examples

``` python
circle.MidPoint()
```

Offset(*d: float*) → [Face](#planex.Face "planex.Face")  
The face of the points within d of the circle: the ring between the circles of radius r - d and r + d, the disc of radius r + d if d reaches r.

Parameters:  
**d** – the distance, \> 0.

Raises:  
**ValueError** – d is not positive and finite.

Examples

``` python
circle.Offset(0.5)
```

Tangent(*p: Point*) → [Vertex](#planex.Vertex "planex.Vertex")  
The unit tangent in the circle’s direction at its point p; (0, 0) if p is not on the circle.

Parameters:  
**p** – a point (h, v) of the circle.

Examples

``` python
circle.Tangent(circle.MidPoint())
```

*property* approximation  
list\[ArcEdge\]

The two half arcs planex computes with and exports.

Type:  
approximation

*property* ccw  
bool

True if the circle runs counterclockwise.

Type:  
ccw

*property* center  
Vertex

The centre, a Vertex.

Type:  
center

*property* end  
Vertex

The end, the origin, a Vertex.

Type:  
end

*property* origin  
Vertex

The point the circle starts and ends at, a Vertex: it has no vertex there.

Type:  
origin

*property* radius  
float

The radius.

Type:  
radius

*property* start  
Vertex

The start, the origin, a Vertex.

Type:  
start

&nbsp;

*class* SplineEdge  
The geometry of a spline; edges live in wires, made by px.Spline or px.BSpline.

Copy() → [SplineEdge](#planex.SplineEdge "planex.SplineEdge")  
An independent copy; copy.copy and copy.deepcopy give the same.

Examples

``` python
b = a.Copy()
```

MidPoint() → [Vertex](#planex.Vertex "planex.Vertex")  
A point near the middle of the spline: the midpoint of the middle piece of its arc approximation.

Examples

``` python
spline.MidPoint()
```

Offset(*d: float*) → [Face](#planex.Face "planex.Face")  
The face of the points within d of the spline: its outline runs at distance d around it, round at the ends.

Parameters:  
**d** – the distance, \> 0.

Raises:  
**ValueError** – d is not positive and finite.

Examples

``` python
spline.Offset(0.5)
```

Sample(*tol: float*) → np.ndarray  
An (n, 2) array of points along the exact curve from the start to the end, each chord within tol of it.

Parameters:  
**tol** – the largest distance of a chord from the curve, \> 0.

Raises:  
**ValueError** – tol is not positive and finite, or below the rounding of the spline’s coordinates.

Examples

``` python
spline.Sample(1e-3)
```

Tangent(*p: Point*) → [Vertex](#planex.Vertex "planex.Vertex")  
The unit tangent in the spline’s direction at its point p; (0, 0) if p is not on the spline.

Parameters:  
**p** – a point (h, v) of the spline.

Examples

``` python
spline.Tangent(spline.MidPoint())
```

*property* approximation  
list\[LineEdge \| ArcEdge\]

The lines and arcs planex computes with, its approximation.

Type:  
approximation

*property* bspline  
dict

The exact curve of this piece as a dict: poles, flattened knots, degree, weights (None if not rational).

Type:  
bspline

*property* end  
Vertex

The end, a Vertex.

Type:  
end

*property* napprox  
int

The number of lines and arcs of its approximation.

Type:  
napprox

*property* points  
list\[Vertex\]

The points it runs through, from the start to the end.

Type:  
points

*property* start  
Vertex

The start, a Vertex.

Type:  
start

&nbsp;

*class* Edge  
Any edge, for isinstance: LineEdge, ArcEdge, CircleEdge, SplineEdge, or an EdgeView of a wire, face or shape.

Examples

``` python
isinstance(wire.edges[0], px.Edge)
```

## Faces, shapes and compounds

*class* Face  
One region: an outer loop and its holes, edges meeting only at shared endpoints.

Chamfer(*at: [VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str \| list\[[VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str\]*, *d: float*, */*, *d2: float \| None = None*, *angle: float \| None = None*, *reverse: bool = False*) → [Face](#planex.Face "planex.Face")  
Cuts corners with a straight line, in place.

A corner is the end of exactly two edges of one part whose directions differ (a vertex that more edges or faces share, an open end, a point on no vertex and a tangent joint are not corners). The new line is a new edge with the id Chamfer.k (k counts per object, in the order the corners are given) and the properties of the two edges merged, the name of the first edge winning: the one that arrives at the corner going counterclockwise around its loop (along the chain for an open wire). The two edges keep their ids and properties, shortened to the cut points; the corner vertex and its properties are gone. Lines and arcs are cut exactly; on a spline the point is found on its exact curve to rounding and the spline is cut there.

A length on a curved edge is the straight distance from the corner to the cut point.

A size that does not fit (the point on or beyond the far end of an edge, or past a neighbouring cut of the same call) makes that corner not viable: it is skipped with a PlanexWarning that names the largest size that fits. If no corner is viable the call raises.

Parameters:  
- **at** – the corners: a vertex (face.vertices\[0\]), a point (h, v) on a vertex, a vertex name (every vertex with that name), or a list or selection of these; there is no default.

- **d** – the length on the first edge, a length \> 0; also on the second if d2 and angle are None.

- **d2** – the length on the second edge, a length \> 0; None: not given.

- **angle** – with d, the angle in degrees between the chamfer and the first edge, measured at its cut point towards the corner, between 0 and 180; None: not given. d2 and angle exclude each other.

- **reverse** – True makes the second edge the first.

Returns:  
this Face, so calls chain.

Raises:  
- **TypeError** – at is none of the forms above.

- **ValueError** – a size is not positive and finite, the angle is not between 0 and 180 or d2 and angle are both given, no corner is given, a name is no vertex’s name, a vertex belongs to another object, or no corner can be cut (the message names each corner and why); nothing changes then.

Examples

``` python
face.Chamfer((4, 0), 1)
face.Chamfer([(4, 0), (4, 4)], 1, d2=2)
```

Chamfered(*at: [VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str \| list\[[VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str\]*, *d: float*, */*, *d2: float \| None = None*, *angle: float \| None = None*, *reverse: bool = False*, *\**, *id: str \| None = None*) → [Face](#planex.Face "planex.Face")  
A copy with the corners chamfered as Chamfer does; this Face is not changed.

Parameters:  
- **at** – the corners: a vertex (face.vertices\[0\]), a point (h, v) on a vertex, a vertex name (every vertex with that name), or a list or selection of these; there is no default.

- **d** – the length on the first edge, a length \> 0; also on the second if d2 and angle are None.

- **d2** – the length on the second edge, a length \> 0; None: not given.

- **angle** – with d, the angle in degrees between the chamfer and the first edge, measured at its cut point towards the corner, between 0 and 180; None: not given. d2 and angle exclude each other.

- **reverse** – True makes the second edge the first.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Returns:  
the copy.

Raises:  
- **TypeError** – at is none of the forms above.

- **ValueError** – a size is not positive and finite, the angle is not between 0 and 180 or d2 and angle are both given, no corner is given, a name is no vertex’s name, a vertex belongs to another object, or no corner can be cut (the message names each corner and why); nothing changes then.

Examples

``` python
face.Chamfered((4, 0), 1)
face.Chamfered([(4, 0), (4, 4)], 1, d2=2)
```

CloseGaps(*tol: float*) → [Face](#planex.Face "planex.Face")  
Joins every endpoint without an exact match on another edge with all endpoints of other edges within tol, in place.

Parameters:  
**tol** – the largest gap closed, a length.

Returns:  
this Face.

Examples

``` python
face.CloseGaps(1e-6)
```

ClosestPoint(*point: Point \| Point3 \| np.ndarray*, */*) → tuple\[float, ...\] \| np.ndarray  
The nearest point to point, in the plane for (h, v), in 3d for (x, y, z); inside a face the point itself.

Parameters:  
**point** – (h, v) or (x, y, z); many points as an (n, 2) or (n, 3) array give an array of that shape.

Raises:  
- **ValueError** – it is empty.

- **TypeError** – point is not a point.

Examples

``` python
face.ClosestPoint((5, 0))
face.ClosestPoint([(0, 0), (5, 0)])
```

Contains(*point: Point \| Point3 \| np.ndarray*, */*, *boundary: bool = True*) → bool \| np.ndarray  
True if the point lies inside the face.

A point (x, y, z) off the plane is not inside.

Parameters:  
- **point** – (h, v) in the plane or (x, y, z); many points as an (n, 2) or (n, 3) array give a bool array (n,).

- **boundary** – the answer for a point on the boundary.

Raises:  
**TypeError** – point is not a point.

Examples

``` python
face.Contains((1, 2))
face.Contains([(0, 0), (1, 2)], boundary=False)
```

Copy(*\**, *id: str \| None = None*) → [Face](#planex.Face "planex.Face")  
An independent copy; copy.copy and copy.deepcopy give the same.

Parameters:  
**id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Examples

``` python
b = a.Copy()
```

Distance(*target: Point \| Point3 \| [Face](#planex.Face "planex.Face") \| [Shape](#planex.Shape "planex.Shape") \| [Wire](#planex.Wire "planex.Wire") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView") \| [VertexView](selections.md#planex.VertexView "planex.VertexView") \| [FaceView](selections.md#planex.FaceView "planex.FaceView") \| TopoDS_Shape \| np.ndarray*, */*, *project: bool = False*, *components: str \| None = None*, *signed: bool = False*) → float \| tuple\[float, ...\] \| np.ndarray  
The smallest distance to the target; 0 inside a face.

A target in another plane is measured in 3d, or projected into this plane with project.

Parameters:  
- **target** – a point, (h, v) in the plane or (x, y, z) in 3d, a Face, Shape, Wire, edge, vertex, face of a shape or OCC shape; many points as an (n, 2) or (n, 3) array.

- **project** – True projects a target in another plane into this one.

- **components** – letters of ‘hvxyz’: the components of the shortest connection from here to the target instead of its length.

- **signed** – True: the distance of points (h, v) to the boundary of the union of the faces (edges between faces do not count), negative inside, 0 on it.

Returns:  
a float; with components a tuple of them, one letter a float; many points an array (n,), with k components (n, k), one letter (n,).

Raises:  
- **ValueError** – it is empty; components has other letters; signed with components, with points not (h, v), or without faces.

- **TypeError** – the target has another form.

Examples

``` python
face.Distance((5, 0))
face.Distance(other, components="hv")
```

Fillet(*at: [VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str \| list\[[VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str\]*, *r: float*, */*) → [Face](#planex.Face "planex.Face")  
Rounds corners with an arc tangent to both edges, in place.

A corner is the end of exactly two edges of one part whose directions differ (a vertex that more edges or faces share, an open end, a point on no vertex and a tangent joint are not corners). The arc of radius r is a new edge with the id Fillet.k (k counts per object, in the order the corners are given) and the properties of the two edges merged, the name of the first edge winning: the one that arrives at the corner going counterclockwise around its loop (along the chain for an open wire). The two edges keep their ids and properties, shortened to the touch points; the corner vertex and its properties are gone. Lines and arcs are cut exactly; on a spline the point is found on its exact curve to rounding and the spline is cut there.

A size that does not fit (the point on or beyond the far end of an edge, or past a neighbouring cut of the same call) makes that corner not viable: it is skipped with a PlanexWarning that names the largest size that fits. If no corner is viable the call raises.

Parameters:  
- **at** – the corners: a vertex (face.vertices\[0\]), a point (h, v) on a vertex, a vertex name (every vertex with that name), or a list or selection of these; there is no default.

- **r** – the radius, a length \> 0.

Returns:  
this Face, so calls chain.

Raises:  
- **TypeError** – at is none of the forms above.

- **ValueError** – a size is not positive and finite, no corner is given, a name is no vertex’s name, a vertex belongs to another object, or no corner can be cut (the message names each corner and why); nothing changes then.

Examples

``` python
face.Fillet((4, 0), 0.5)
face.Fillet(face.vertices[0], 0.5)
```

Filleted(*at: [VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str \| list\[[VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str\]*, *r: float*, */*, *\**, *id: str \| None = None*) → [Face](#planex.Face "planex.Face")  
A copy with the corners rounded as Fillet does; this Face is not changed.

Parameters:  
- **at** – the corners: a vertex (face.vertices\[0\]), a point (h, v) on a vertex, a vertex name (every vertex with that name), or a list or selection of these; there is no default.

- **r** – the radius, a length \> 0.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Returns:  
the copy.

Raises:  
- **TypeError** – at is none of the forms above.

- **ValueError** – a size is not positive and finite, no corner is given, a name is no vertex’s name, a vertex belongs to another object, or no corner can be cut (the message names each corner and why); nothing changes then.

Examples

``` python
face.Filleted((4, 0), 0.5)
face.Filleted(face.vertices[0], 0.5)
```

FitArcs(*tol: float \| None = None*, *max_edge_length: float \| None = None*, *max_edge_angle: float \| None = None*, *ignore_edges_smaller: float \| None = None*) → [Face](#planex.Face "planex.Face")  
Replaces chains of at least three edges shorter than max_edge_length by arcs of at most 180 degrees (or lines) that stay within tol of the chain, in place.

Parameters:  
- **tol** – the largest distance of the arc from the chain; None takes px.config.fit_arcs\[“tol”\].

- **max_edge_length** – only edges shorter than this form a chain; None takes px.config.fit_arcs\[“max_edge_length”\].

- **max_edge_angle** – the largest turn between neighbouring edges of a chain, in degrees; None takes px.config.fit_arcs\[“max_edge_angle”\].

- **ignore_edges_smaller** – edges shorter than this do not end a chain by their direction; None takes px.config.fit_arcs\[“ignore_edges_smaller”\].

Returns:  
this Face.

Examples

``` python
face.FitArcs()
face.FitArcs(tol=1e-4, max_edge_length=0.5)
```

Hausdorff(*other: Any*, *samples: int = 64*) → float  
The Hausdorff distance to other: the largest distance from a point of one to the other.

Parameters:  
- **other** – an object, element or selection.

- **samples** – the samples per edge before the search for the largest distance refines.

Examples

``` python
wire.Hausdorff(other_wire)
shape.edges("Line.1").Hausdorff(shape.edges("Line.2"), samples=128)
```

Identify(*a: str*, *b: str*, */*, *trafo: Transformation \| tuple \| None = None*, *\**, *name: str \| None = None*, *type: IdentificationType \| None = None*) → [Face](#planex.Face "planex.Face")  
Identifies the edges named a with those named b for the mesh: b’s mesh is copied from a’s (netgen’s Identify).

The identification is stored by name and applied at every OCC export, which raises unless every exported piece of one side pairs with one of the other. Booleans, Glue, Split, Trim and copies keep it (the first operand wins where names clash), motions move it.

Parameters:  
- **a** – the name of the edges mapped onto b.

- **b** – the name of the edges that get a’s mesh.

- **trafo** – the map of a onto b in the plane’s coordinates: a Transformation (px.Translation, px.Rotation) or (matrix 2x2, vector 2); None finds it from the geometry at every export (px.GetTransformation), so it follows later changes.

- **name** – the identification’s name; default ‘a-b’; an identification of the same name is replaced.

- **type** – a netgen.meshing.IdentificationType; None: PERIODIC.

Returns:  
this Face.

Raises:  
- **ValueError** – a and b are the same; no edge has the name a or b; type is not an IdentificationType; without trafo, no map takes a onto b (as px.GetTransformation).

- **TypeError** – trafo has another form.

Examples

``` python
face.Identify("left", "right", px.Translation(10, 0))
face.Identify("inner", "outer", name="ring")
```

Intersections(*other: [Wire](#planex.Wire "planex.Wire")*, */*) → list\[[Vertex](#planex.Vertex "planex.Vertex")\]  
The points where its edges meet the edges of other, each once, in the plane.

Parameters:  
**other** – a Wire on the same plane.

Raises:  
**ValueError** – other lies on a different plane.

Examples

``` python
face.Intersections(px.Line((0, -5), (0, 5)))
```

IsClosed() → bool  
True if every endpoint has an exact match on another edge (OpenEnds() is empty).

Examples

``` python
face.IsClosed()
```

Measure(*name: str \| [FaceView](selections.md#planex.FaceView "planex.FaceView") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView") \| None = None*, *to: str \| Point \| Point3 \| [Face](#planex.Face "planex.Face") \| [Shape](#planex.Shape "planex.Shape") \| [Wire](#planex.Wire "planex.Wire") \| None = None*, *project: bool = False*) → dict\
Measure(*f: Callable*, */*, *\*args*, *id: str \| None = None*, *\*\*params*) → object  
The measures of the faces and edges named name, or of the one with that id or selected, as a dict.

Its keys: type, faces, area, edges, length, center (in 3d; of an arc or circle its centre, with radius and angle), inertia, Ih, Iv, Ihv, I1, I2, principal_angle, bounding_box; with to also distance, du, dv, dx, dy, dz, points and the angle between two lines.

Parameters:  
- **name** – a name, an id, or a selected face or edge; None measures all.

- **to** – a name, or anything Distance takes: adds the distance to it.

- **project** – True projects a target of to in another plane into this one.

- **f** – a measurement, a callable f(obj, \*args, \*\*params) (see px.measurements): Measure returns its value.

- **args** – the further arguments of f.

- **id** – stores the measurement f under this id: obj.measures then holds it next to the standard measurements, RemoveMeasure(id) drops it.

- **params** – the keyword arguments of f.

Raises:  
- **ValueError** – no face or edge has that name or id.

- **TypeError** – name has another form.

- **TypeError** – id without a measurement f.

Examples

``` python
face.Measure()
face.Measure(px.measurements.MinThickness(), id="wall")
```

MergeCollinearEdges(*tol: float \| None = None*) → [Face](#planex.Face "planex.Face")  
Merges lines that meet alone at their vertices into fewer lines, in place: a vertex goes while every vertex removed between its neighbours stays within tol of the line through them.

The result stays within tol of the edges the call was given; a second call may merge more. The edges of a spline stay.

Parameters:  
**tol** – the largest distance of a removed vertex from the new line; None takes px.config.merge_collinear_closer.

Returns:  
this Face.

Examples

``` python
face.MergeCollinearEdges()
face.MergeCollinearEdges(1e-6)
```

Mirror(*point: Point*, */*, *dir: tuple\[float, float\]*) → [Face](#planex.Face "planex.Face")\
Mirror(*point: Point3*, */*, *normal: tuple\[float, float, float\]*) → [Face](#planex.Face "planex.Face")  
Mirrors it in place at the line through point along dir in its plane, or at the plane through point with normal in 3d.

In 3d its plane is mirrored and keeps a right-handed coordinate system, so its coordinates in the plane are mirrored at the plane’s first axis.

Parameters:  
- **point** – a point of the mirror line, (h, v), or of the mirror plane in 3d, (x, y, z).

- **dir** – the direction of the mirror line in the plane, (dh, dv).

- **normal** – the normal of the mirror plane in 3d, (dx, dy, dz).

Returns:  
this Face.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
face.Mirror((0, 0), (0, 1))
face.Mirror((0, 0, 0), (1, 0, 0))
```

Mirrored(*point: Point*, */*, *dir: tuple\[float, float\]*, *\**, *id: str \| None = None*) → [Face](#planex.Face "planex.Face")\
Mirrored(*point: Point3*, */*, *normal: tuple\[float, float, float\]*, *\**, *id: str \| None = None*) → [Face](#planex.Face "planex.Face")  
A copy mirrored at the line through point along dir in its plane, or at the plane through point with normal in 3d; this Face is not changed.

In 3d its plane is mirrored and keeps a right-handed coordinate system, so its coordinates in the plane are mirrored at the plane’s first axis.

Parameters:  
- **point** – a point of the mirror line, (h, v), or of the mirror plane in 3d, (x, y, z).

- **dir** – the direction of the mirror line in the plane, (dh, dv).

- **normal** – the normal of the mirror plane in 3d, (dx, dy, dz).

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
face.Mirrored((0, 0), (0, 1))
face.Mirrored((0, 0, 0), (1, 0, 0))
```

Normal() → tuple\[float, float, float\]  
The normal of its plane in 3d.

Examples

``` python
face.Normal()
```

Offset(*d: float*, *inplace: bool = True*, *\**, *id: str \| None = None*) → [Face](#planex.Face "planex.Face") \| [Shape](#planex.Shape "planex.Shape")  
Grows the faces by d: the band of width [\|d\|](#id1) along their boundaries added for d \> 0, taken away for d \< 0.

The band is Wire.Offset of their edges, round around convex corners; the faces keep their properties, the new edges those of the edges they come from.

Parameters:  
- **d** – the width of the band, a length; d \> 0 grows, d \< 0 shrinks, 0 changes nothing.

- **inplace** – True changes this Face and returns it; False returns the offset as a new Shape and leaves this one.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – in place, the offset is several faces; inplace=False returns them as a Shape.

Examples

``` python
face.Offset(0.5)
band = face.Offset(0.5, inplace=False)
```

OpenEnds() → list\[tuple\[int, bool\]\]  
The endpoints without an exact match on another edge, as (edge index, True at its end, False at its start).

Examples

``` python
face.OpenEnds()
```

Plot(*plane: [Plane](#planex.Plane "planex.Plane") \| None = None*, */*, *ax: Any = None*, *line_col: Color = 'black'*, *arc_col: Color = '#ff7f0e'*, *spline_col: Color = '#2ca02c'*, *face_col: Color \| None = '#dde6f0'*, *mark_edges_smaller: float \| None = None*, *mark_free_edges: bool = False*, *highlight: Ids \| Any = None*, *legend: bool = True*, *show_constraints: bool \| None = None*, *show_ends: bool = False*, *show_direction: bool = False*, *show_parent: bool \| None = None*, *backend: str \| None = None*) → Any  
Draws a planex object in its plane with matplotlib or plotly.

A sketch is drawn as sk.Plot draws it (show_constraints, show_ends, show_direction, show_parent); the other options are for the other objects, e.g. px.Plot(sk.Shape(), face_col=”C0”). An OCC shape is cut at the plane (px.Intersect3d(plane, shape)), or without one drawn flat in its own plane as px.FromOCC gives it. Fields: px.PlotField; meshes: px.PlotMesh.

Parameters:  
- **obj** – a Face, Shape, Wire, Compound, sketch, selection, DeformableShape or OCC shape, or an id that names one (“Sketch.1”, “Shape.2.Line.3”).

- **plane** – the plane that cuts an OCC shape; None draws it flat in its own plane.

- **ax** – a matplotlib Axes or a plotly Figure to draw into, which also selects the backend.

- **line_col** – the colour of every edge; “type” draws lines in black, arcs in arc_col and splines in spline_col, with a legend entry each; “property” takes each edge’s own col where it has one.

- **arc_col** – the colour of arcs with line_col=”type”.

- **spline_col** – the colour of splines with line_col=”type”.

- **face_col** – fills the closed loops (faces and shapes; a wire on its own stays an outline unless face_col is given); a fill other than the default gets a legend entry labelled with the object’s id (‘Face.3’), the default fill none; None draws the outlines only.

- **mark_edges_smaller** – marks the edges shorter than this at their midpoints.

- **mark_free_edges** – True marks the edges with an end no other edge shares.

- **highlight** – marks the edges given: ids (“Line.3”, “Shape.1.Line.3”), selected edges or faces (a face: its edges), a selection, or indices or a boolean mask over obj.edges (for a Compound those of its parts in turn).

- **legend** – True shows the legend.

- **show_constraints** – for a sketch, as sk.Plot.

- **show_ends** – for a sketch, as sk.Plot.

- **show_direction** – for a sketch, as sk.Plot.

- **show_parent** – for a sketch, as sk.Plot.

- **backend** – “matplotlib” or “plotly” without ax; None is matplotlib.

Returns:  
the matplotlib Axes or the plotly Figure.

Raises:  
**ValueError** – a sketch with an option for other objects, an option for sketches with another object, a plane with a planex object, obj is a plane, or backend is unknown.

Examples

``` python
px.Plot(face, line_col="type")
px.Plot(solid, px.XY)
```

Plot3d(*plane_size: float \| None = None*, *backend: str = 'webgpu'*, *\*\*kwargs: Any*) → Any  
Draws planex objects, planes and OCC shapes in one 3d view.

Everything is exported to OCC, put into one compound and drawn; a sketch contributes its drawn edges. A plane is drawn as a square of side plane_size with a line along its u axis. backend “webgpu” draws with ngsolve_webgpu.jupyter.Draw: it needs ngsolve_webgpu (pip install ngsolve_webgpu, which brings the full ngsolve) and a browser with WebGPU, with the kernel on the same machine as the browser or its websocket port forwarded. backend “webgui” draws with netgen.webgui.Draw and needs only netgen.

Parameters:  
- **objects** – a Wire, Face, Shape, Compound, sketch, Plane or OCC shape, or a list of them.

- **plane_size** – the side of a plane’s square; None takes the largest extent of everything else, 1 without.

- **backend** – ‘webgpu’ or ‘webgui’.

- **kwargs** – go to the backend’s Draw (width, height; settings, euler_angles only for webgui).

Returns:  
the backend’s scene.

Raises:  
- **ValueError** – backend is unknown, or there is nothing to draw.

- **ImportError** – backend ‘webgpu’ without ngsolve_webgpu installed.

Examples

``` python
px.Plot3d([face, px.XY], plane_size=2)
shape.Plot3d(backend="webgui", height=400)
```

Polylines(*tol: float*) → list\[np.ndarray\]  
The loops and open chains as (n, 2) arrays of plane coordinates, each chord within tol of its edge.

A face gives its outer loop counterclockwise, then its holes clockwise; a shape its faces in turn, then its free wires; a wire’s loop runs the way the wire’s first edge runs; a closed loop ends at its first point.

Parameters:  
**tol** – the largest distance of a chord from its edge, a length \> 0.

Raises:  
**ValueError** – tol is not positive and finite, or below the rounding of an arc’s or a spline’s coordinates.

Examples

``` python
face.Polylines(0.01)
```

Project(*shape: TopoDS_Shape*, */*) → list\[[Wire](#planex.Wire "planex.Wire")\]  
The edges of the OCC shape projected straight onto this plane, as a list of wires, as px.Project.

Parameters:  
**shape** – an OCC shape.

Examples

``` python
face.Project(box)
```

RemoveIdentification(*name: str*) → [Face](#planex.Face "planex.Face")  
Removes the identification called name.

Parameters:  
**name** – the identification’s name, a key of identifications.

Returns:  
this Face.

Raises:  
**KeyError** – no identification has that name.

Examples

``` python
face.RemoveIdentification("left-right")
```

RemoveMeasure(*id: str*) → None  
Drops the stored measurement id.

Parameters:  
**id** – the id the measurement was stored under (Measure(f, id=…)).

Raises:  
**ValueError** – no measurement is stored under id.

Examples

``` python
shape.RemoveMeasure("gap")
```

RemoveSmallEdges(*tol: float*) → [Face](#planex.Face "planex.Face")  
Removes the lines and arcs no longer than tol in place; both ends of each merge into its midpoint.

Lengths are measured before any merge; splines and the edges of a spline (fitted at export) stay.

Parameters:  
**tol** – the largest length removed.

Returns:  
this Face.

Examples

``` python
face.RemoveSmallEdges(1e-4)
```

Rotate(*center: Point*, *angle: float*) → [Face](#planex.Face "planex.Face")\
Rotate(*point: Point3*, */*, *dir: tuple\[float, float, float\]*, *angle: float*) → [Face](#planex.Face "planex.Face")  
Rotates it in place by angle around center in its plane, or around the axis through point along dir in 3d.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **center** – the centre of the rotation in the plane, (h, v).

- **angle** – the angle in degrees.

- **point** – a point of the axis in 3d, (x, y, z).

- **dir** – the direction of the axis in 3d, (dx, dy, dz).

Returns:  
this Face.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
face.Rotate((0, 0), 90)
face.Rotate((0, 0, 0), (0, 0, 1), 45)
```

Rotated(*center: Point*, *angle: float*, *\**, *id: str \| None = None*) → [Face](#planex.Face "planex.Face")\
Rotated(*point: Point3*, */*, *dir: tuple\[float, float, float\]*, *angle: float*, *\**, *id: str \| None = None*) → [Face](#planex.Face "planex.Face")  
A copy rotated by angle around center in its plane, or around the axis through point along dir in 3d; this Face is not changed.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **center** – the centre of the rotation in the plane, (h, v).

- **angle** – the angle in degrees.

- **point** – a point of the axis in 3d, (x, y, z).

- **dir** – the direction of the axis in 3d, (dx, dy, dz).

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
face.Rotated((0, 0), 90)
face.Rotated((0, 0, 0), (0, 0, 1), 45)
```

Scale(*center: Point*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*) → [Face](#planex.Face "planex.Face")\
Scale(*point: Point3*, */*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*) → [Face](#planex.Face "planex.Face")  
Scales it in place by factor around center in its plane, or around point in 3d.

The mesh sizes maxh and minh stay unless asked for.

Parameters:  
- **center** – the centre of the scaling in the plane, (h, v).

- **point** – the centre of the scaling in 3d, (x, y, z).

- **factor** – the scale factor, \> 0.

- **scale_maxh** – True also scales maxh.

- **scale_minh** – True also scales minh.

Returns:  
this Face.

Raises:  
**ValueError** – factor is not positive and finite, or a coordinate is not finite.

Examples

``` python
face.Scale((0, 0), 2)
face.Scale((0, 0), 0.5, scale_maxh=True)
```

Scaled(*center: Point*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*, *\**, *id: str \| None = None*) → [Face](#planex.Face "planex.Face")\
Scaled(*point: Point3*, */*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*, *\**, *id: str \| None = None*) → [Face](#planex.Face "planex.Face")  
A copy scaled by factor around center in its plane, or around point in 3d; this Face is not changed.

The mesh sizes maxh and minh stay unless asked for.

Parameters:  
- **center** – the centre of the scaling in the plane, (h, v).

- **point** – the centre of the scaling in 3d, (x, y, z).

- **factor** – the scale factor, \> 0.

- **scale_maxh** – True also scales maxh.

- **scale_minh** – True also scales minh.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – factor is not positive and finite, or a coordinate is not finite.

Examples

``` python
face.Scaled((0, 0), 2)
face.Scaled((0, 0), 0.5, scale_maxh=True)
```

Simplified(*fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*, *\**, *id: str \| None = None*) → [Face](#planex.Face "planex.Face")  
A copy defeatured as Simplify does it; this face is not changed.

Parameters:  
- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
face.Simplified()
face.Simplified(fit_arcs=True, remove_edges_smaller=1e-6)
```

Simplify(*fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → [Face](#planex.Face "planex.Face")  
Defeatures the face in place as the OCC exports do: gaps closed, collinear lines merged, arcs and splines fitted, small edges and edges that touch no other edge at either end removed, as enabled.

Parameters:  
- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
face.Simplify()
face.Simplify(fit_arcs=True, remove_edges_smaller=1e-6)
```

SmallEdges(*tol: float*) → list\[int\]  
The indices of the edges no longer than tol.

Parameters:  
**tol** – the largest length counted.

Examples

``` python
face.SmallEdges(1e-3)
```

Split(*cutter: [Wire](#planex.Wire "planex.Wire") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView") \| [Face](#planex.Face "planex.Face") \| [Shape](#planex.Shape "planex.Shape") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch") \| TopoDS_Shape*, */*, *\**, *id: str \| None = None*) → list\[[Shape](#planex.Shape "planex.Shape")\]  
Cuts every face and wire by the cutter into one Shape per piece.

What the cutter does not separate stays as it is; the pieces keep the face’s properties, the new edges the cutter’s.

Parameters:  
- **cutter** – the edges that cut, a Wire, edge, Face, Shape, sketch or OCC shape; projected into the plane if it lies elsewhere.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**TypeError** – the cutter has the wrong form.

Examples

``` python
pieces = face.Split(px.Line((0, -5), (0, 5)))
```

ToHV(*point: Point3*, */*) → tuple\[float, float\]\
ToHV(*points: list\[Point3\]*, */*) → list\[tuple\[float, float\]\]  
The plane coordinates (h, v) of the projection of a 3d point onto its plane, or of a list of points.

Parameters:  
- **point** – (x, y, z).

- **points** – a list of (x, y, z).

Examples

``` python
face.ToHV((1, 2, 0))
face.ToHV([(0, 0, 0), (1, 2, 0)])
```

ToOCCFace(*fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → TopoDS_Shape  
The face as an OCC face, or a compound if the export gives several.

Parameters:  
- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
face.ToOCCFace()
face.ToOCCFace(fit_arcs=True, remove_edges_smaller=1e-6)
```

ToOCCShape(*fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → TopoDS_Shape  
The face as an OCC face, or a compound if the export gives several, as ToOCCFace().

Parameters:  
- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
face.ToOCCShape()
face.ToOCCShape(fit_arcs=True, remove_edges_smaller=1e-6)
```

ToOCCWire(*approximation: bool = False*, *fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → TopoDS_Shape  
The edges as a single OCC wire.

Parameters:  
- **approximation** – True gives the splines as their arc approximation.

- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
face.ToOCCWire()
face.ToOCCWire(fit_arcs=True, remove_edges_smaller=1e-6)
```

ToOCCWires(*approximation: bool = False*, *fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → TopoDS_Shape  
The loops as an OCC compound of wires.

Parameters:  
- **approximation** – True gives the splines as their arc approximation.

- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
face.ToOCCWires()
face.ToOCCWires(fit_arcs=True, remove_edges_smaller=1e-6)
```

ToSVG(*size: float = 300*, *\*\*options: Any*) → str \| None  
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

ToShapely(*tol: float = 0.001*) → Any  
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

ToXYZ(*point: Point*, */*) → tuple\[float, float, float\]\
ToXYZ(*points: list\[Point\]*, */*) → list\[tuple\[float, float, float\]\]  
The 3d coordinates of a point (h, v) of its plane, or of a list of points.

Parameters:  
- **point** – (h, v) in the plane.

- **points** – a list of (h, v).

Examples

``` python
face.ToXYZ((1, 2))
face.ToXYZ([(0, 0), (1, 2)])
```

Translate(*d: tuple\[float, float\]*) → [Face](#planex.Face "planex.Face")\
Translate(*dh: float*, *dv: float*) → [Face](#planex.Face "planex.Face")\
Translate(*d: tuple\[float, float, float\]*) → [Face](#planex.Face "planex.Face")\
Translate(*dx: float*, *dy: float*, *dz: float*) → [Face](#planex.Face "planex.Face")  
Moves it in place by d: in the plane by (dh, dv), in 3d by (dx, dy, dz).

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **d** – the displacement, (dh, dv) in the plane or (dx, dy, dz) in 3d.

- **dh** – the displacement along the plane’s first axis.

- **dv** – the displacement along the plane’s second axis.

- **dx** – the displacement along x in 3d.

- **dy** – the displacement along y in 3d.

- **dz** – the displacement along z in 3d.

Returns:  
this Face.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
face.Translate((10, 0))
face.Translate(0, 0, 5)
```

Translated(*d: tuple\[float, float\]*, *\**, *id: str \| None = None*) → [Face](#planex.Face "planex.Face")\
Translated(*dh: float*, *dv: float*, *\**, *id: str \| None = None*) → [Face](#planex.Face "planex.Face")\
Translated(*d: tuple\[float, float, float\]*, *\**, *id: str \| None = None*) → [Face](#planex.Face "planex.Face")\
Translated(*dx: float*, *dy: float*, *dz: float*, *\**, *id: str \| None = None*) → [Face](#planex.Face "planex.Face")  
A copy moved by d: in the plane by (dh, dv), in 3d by (dx, dy, dz); this Face is not changed.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **d** – the displacement, (dh, dv) in the plane or (dx, dy, dz) in 3d.

- **dh** – the displacement along the plane’s first axis.

- **dv** – the displacement along the plane’s second axis.

- **dx** – the displacement along x in 3d.

- **dy** – the displacement along y in 3d.

- **dz** – the displacement along z in 3d.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
face.Translated((10, 0))
face.Translated(0, 0, 5)
```

Trim(*cutter: [Wire](#planex.Wire "planex.Wire") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView") \| [Face](#planex.Face "planex.Face") \| [Shape](#planex.Shape "planex.Shape") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch") \| TopoDS_Shape*, *\**, *keep_closest_to: Point \| Point3 \| None = None*, *remove_closest_to: Point \| Point3 \| None = None*, *keep_largest: bool = False*, *keep_smallest: bool = False*, *keep_left: bool = False*, *keep_right: bool = False*, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")  
Cuts every face and wire by the cutter and keeps the pieces the one option given chooses.

What the cutter does not separate stays as it is.

Parameters:  
- **cutter** – the edges that cut, a Wire, edge, Face, Shape, sketch or OCC shape; projected into the plane if it lies elsewhere.

- **keep_closest_to** – keeps the piece closest to this point (a face piece by its centre, a wire piece by itself), (h, v) or (x, y, z).

- **remove_closest_to** – removes the piece closest to this point and keeps the others.

- **keep_largest** – keeps the piece of the largest area (faces) or length (wires).

- **keep_smallest** – keeps the piece of the smallest area or length.

- **keep_left** – keeps the face pieces left of the cutter as drawn; a shape’s free wires stay.

- **keep_right** – keeps the face pieces right of the cutter as drawn; a shape’s free wires stay.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Returns:  
a new Shape; this one is not changed.

Raises:  
- **ValueError** – not exactly one option is given; keep_left or keep_right without faces, or no piece lies wholly on that side.

- **TypeError** – the cutter or the point has the wrong form.

Examples

``` python
face.Trim(px.Line((0, -5), (0, 5)), keep_left=True)
face.Trim(cutter, keep_closest_to=(2, 1))
```

Wire() → [Wire](#planex.Wire "planex.Wire")  
The single loop of a face without holes.

Raises:  
**ValueError** – the face has holes; .wires gives the loops, .outer the outline.

Examples

``` python
face.Wire()
```

WriteBrep(*path: str \| os.PathLike*, */*, *\*\*options: Any*) → None  
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

WriteDXF(*path: str \| os.PathLike*, */*, *unit: float = 1.0*) → None  
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

WriteGeo(*path: str \| os.PathLike*, */*) → None  
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

WriteStep(*path: str \| os.PathLike*, */*, *\*\*options: Any*) → None  
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

*property* arcs  
list\[ArcEdge\]

The arcs among the edges, copies.

Type:  
arcs

*property* bounding_box  
tuple\[tuple\[float, float\], tuple\[float, float\]\] \| None

Its bounding box in the plane, ((hmin, vmin), (hmax, vmax)); None if it is empty.

Type:  
bounding_box

*property* center  
tuple\[float, float, float\] \| None

The centre of mass in 3d, (x, y, z): of the area for faces, of the length for wires; None if it is empty.

Type:  
center

*property* circles  
list\[CircleEdge\]

The circles among the edges, copies.

Type:  
circles

*property* col  
tuple\[float, float, float, float\] \| None

The colour (r, g, b, a), set as (r, g, b) or (r, g, b, a) of the face; None if not set.

Type:  
col

*property* edges  
EdgesView

The edges, a view of the face’s edges: a property set on it is set on all of them.

Type:  
edges

*property* faces  
list\[Face\]

A list of this face alone.

Type:  
faces

*property* hpref  
float \| None

Netgen’s hpref of the face; None if not set.

Type:  
hpref

*property* id  
str

The object’s id: the kind that made it and a number, given when first asked for (Sketch.1, Shape.3), or set (an ASCII letter, then letters, digits, \_ and single inner spaces); its elements’ ids follow it (Shape.3.Line.2).

Type:  
id

*property* identifications  
dict

The identifications as a dict {name: (a, b, trafo or None, type)}.

Type:  
identifications

*property* inertia  
tuple\[tuple\[float, float, float\], tuple\[float, float, float\], tuple\[float, float, float\]\] \| None

The inertia tensor about the centre in 3d for unit density (per area for faces, per length for wires), three rows of three; None if it is empty.

Type:  
inertia

*property* inner  
list\[Wire\]

The inner loops (the holes), a list of wires, their edges in clockwise order.

Type:  
inner

*property* layer  
int \| None

Netgen’s layer number of the face; None if not set.

Type:  
layer

*property* lines  
list\[LineEdge\]

The lines among the edges, copies.

Type:  
lines

*property* mass  
float

The area, holes subtracted; splines by their approximation.

Type:  
mass

*property* maxh  
float \| None

The largest mesh size (netgen’s maxh) of the face; None if not set.

Type:  
maxh

*property* measures  
dict

The standard measurements and the stored ones, evaluated now, as a dict.

Type:  
measures

*property* minh  
float \| None

The smallest mesh size (netgen’s minh) of the face; None if not set.

Type:  
minh

*property* name  
str \| None

The name of the face; None if not set.

Type:  
name

*property* outer  
Wire

The outer loop, a Wire, its edges in counterclockwise order.

Type:  
outer

*property* quad_dominated  
bool \| None

The quad_dominated flag (True: netgen meshes with mostly quadrilaterals) of the face; None if not set.

Type:  
quad_dominated

*property* splines  
list\[SplineEdge\]

The splines among the edges, copies.

Type:  
splines

*property* vertices  
VerticesView

The vertices, each once, in the order the edges reach them.

Type:  
vertices

*property* wires  
list\[Wire\]

The loops, the outline and the holes, a list of wires.

Type:  
wires

&nbsp;

*class* Shape  
Faces that do not overlap and wires on one plane, cut against each other, every edge stored once; the result of every boolean.

Chamfer(*at: [VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str \| list\[[VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str\]*, *d: float*, */*, *d2: float \| None = None*, *angle: float \| None = None*, *reverse: bool = False*) → [Shape](#planex.Shape "planex.Shape")  
Cuts corners with a straight line, in place.

A corner is the end of exactly two edges of one part whose directions differ (a vertex that more edges or faces share, an open end, a point on no vertex and a tangent joint are not corners). The new line is a new edge with the id Chamfer.k (k counts per object, in the order the corners are given) and the properties of the two edges merged, the name of the first edge winning: the one that arrives at the corner going counterclockwise around its loop (along the chain for an open wire). The two edges keep their ids and properties, shortened to the cut points; the corner vertex and its properties are gone. Lines and arcs are cut exactly; on a spline the point is found on its exact curve to rounding and the spline is cut there.

A length on a curved edge is the straight distance from the corner to the cut point.

A size that does not fit (the point on or beyond the far end of an edge, or past a neighbouring cut of the same call) makes that corner not viable: it is skipped with a PlanexWarning that names the largest size that fits. If no corner is viable the call raises.

Parameters:  
- **at** – the corners: a vertex (shape.vertices\[0\]), a point (h, v) on a vertex, a vertex name (every vertex with that name), or a list or selection of these; there is no default.

- **d** – the length on the first edge, a length \> 0; also on the second if d2 and angle are None.

- **d2** – the length on the second edge, a length \> 0; None: not given.

- **angle** – with d, the angle in degrees between the chamfer and the first edge, measured at its cut point towards the corner, between 0 and 180; None: not given. d2 and angle exclude each other.

- **reverse** – True makes the second edge the first.

Returns:  
this Shape, so calls chain.

Raises:  
- **TypeError** – at is none of the forms above.

- **ValueError** – a size is not positive and finite, the angle is not between 0 and 180 or d2 and angle are both given, no corner is given, a name is no vertex’s name, a vertex belongs to another object, or no corner can be cut (the message names each corner and why); nothing changes then.

Examples

``` python
shape.Chamfer((4, 0), 1)
shape.Chamfer([(4, 0), (4, 4)], 1, d2=2)
```

Chamfered(*at: [VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str \| list\[[VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str\]*, *d: float*, */*, *d2: float \| None = None*, *angle: float \| None = None*, *reverse: bool = False*, *\**, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")  
A copy with the corners chamfered as Chamfer does; this Shape is not changed.

Parameters:  
- **at** – the corners: a vertex (shape.vertices\[0\]), a point (h, v) on a vertex, a vertex name (every vertex with that name), or a list or selection of these; there is no default.

- **d** – the length on the first edge, a length \> 0; also on the second if d2 and angle are None.

- **d2** – the length on the second edge, a length \> 0; None: not given.

- **angle** – with d, the angle in degrees between the chamfer and the first edge, measured at its cut point towards the corner, between 0 and 180; None: not given. d2 and angle exclude each other.

- **reverse** – True makes the second edge the first.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Returns:  
the copy.

Raises:  
- **TypeError** – at is none of the forms above.

- **ValueError** – a size is not positive and finite, the angle is not between 0 and 180 or d2 and angle are both given, no corner is given, a name is no vertex’s name, a vertex belongs to another object, or no corner can be cut (the message names each corner and why); nothing changes then.

Examples

``` python
shape.Chamfered((4, 0), 1)
shape.Chamfered([(4, 0), (4, 4)], 1, d2=2)
```

CloseGaps(*tol: float*) → [Shape](#planex.Shape "planex.Shape")  
Joins every endpoint without an exact match on another edge with all endpoints of other edges within tol, in place.

Parameters:  
**tol** – the largest gap closed, a length.

Returns:  
this Shape.

Examples

``` python
shape.CloseGaps(1e-6)
```

ClosestPoint(*point: Point \| Point3 \| np.ndarray*, */*) → tuple\[float, ...\] \| np.ndarray  
The nearest point to point, in the plane for (h, v), in 3d for (x, y, z); inside a face the point itself.

Parameters:  
**point** – (h, v) or (x, y, z); many points as an (n, 2) or (n, 3) array give an array of that shape.

Raises:  
- **ValueError** – it is empty.

- **TypeError** – point is not a point.

Examples

``` python
shape.ClosestPoint((5, 0))
shape.ClosestPoint([(0, 0), (5, 0)])
```

Contains(*point: Point \| Point3 \| np.ndarray*, */*, *boundary: bool = True*) → bool \| np.ndarray  
True if the point lies inside a face.

A point (x, y, z) off the plane is not inside.

Parameters:  
- **point** – (h, v) in the plane or (x, y, z); many points as an (n, 2) or (n, 3) array give a bool array (n,).

- **boundary** – the answer for a point on the boundary.

Raises:  
**TypeError** – point is not a point.

Examples

``` python
shape.Contains((1, 2))
shape.Contains([(0, 0), (1, 2)], boundary=False)
```

Copy(*\**, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")  
An independent copy; copy.copy and copy.deepcopy give the same.

Parameters:  
**id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Examples

``` python
b = a.Copy()
```

Distance(*target: Point \| Point3 \| [Face](#planex.Face "planex.Face") \| [Shape](#planex.Shape "planex.Shape") \| [Wire](#planex.Wire "planex.Wire") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView") \| [VertexView](selections.md#planex.VertexView "planex.VertexView") \| [FaceView](selections.md#planex.FaceView "planex.FaceView") \| TopoDS_Shape \| np.ndarray*, */*, *project: bool = False*, *components: str \| None = None*, *signed: bool = False*) → float \| tuple\[float, ...\] \| np.ndarray  
The smallest distance to the target; 0 inside a face.

A target in another plane is measured in 3d, or projected into this plane with project.

Parameters:  
- **target** – a point, (h, v) in the plane or (x, y, z) in 3d, a Face, Shape, Wire, edge, vertex, face of a shape or OCC shape; many points as an (n, 2) or (n, 3) array.

- **project** – True projects a target in another plane into this one.

- **components** – letters of ‘hvxyz’: the components of the shortest connection from here to the target instead of its length.

- **signed** – True: the distance of points (h, v) to the boundary of the union of the faces (edges between faces do not count), negative inside, 0 on it.

Returns:  
a float; with components a tuple of them, one letter a float; many points an array (n,), with k components (n, k), one letter (n,).

Raises:  
- **ValueError** – it is empty; components has other letters; signed with components, with points not (h, v), or without faces.

- **TypeError** – the target has another form.

Examples

``` python
shape.Distance((5, 0))
shape.Distance(other, components="hv")
```

Face(*\**, *id: str \| None = None*) → [Face](#planex.Face "planex.Face")  
The single face of a shape of one face and no wires.

Parameters:  
**id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – the shape has several faces or wires; .faces gives the faces, .wires the wires.

Examples

``` python
shape.Face()
```

Fillet(*at: [VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str \| list\[[VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str\]*, *r: float*, */*) → [Shape](#planex.Shape "planex.Shape")  
Rounds corners with an arc tangent to both edges, in place.

A corner is the end of exactly two edges of one part whose directions differ (a vertex that more edges or faces share, an open end, a point on no vertex and a tangent joint are not corners). The arc of radius r is a new edge with the id Fillet.k (k counts per object, in the order the corners are given) and the properties of the two edges merged, the name of the first edge winning: the one that arrives at the corner going counterclockwise around its loop (along the chain for an open wire). The two edges keep their ids and properties, shortened to the touch points; the corner vertex and its properties are gone. Lines and arcs are cut exactly; on a spline the point is found on its exact curve to rounding and the spline is cut there.

A size that does not fit (the point on or beyond the far end of an edge, or past a neighbouring cut of the same call) makes that corner not viable: it is skipped with a PlanexWarning that names the largest size that fits. If no corner is viable the call raises.

Parameters:  
- **at** – the corners: a vertex (shape.vertices\[0\]), a point (h, v) on a vertex, a vertex name (every vertex with that name), or a list or selection of these; there is no default.

- **r** – the radius, a length \> 0.

Returns:  
this Shape, so calls chain.

Raises:  
- **TypeError** – at is none of the forms above.

- **ValueError** – a size is not positive and finite, no corner is given, a name is no vertex’s name, a vertex belongs to another object, or no corner can be cut (the message names each corner and why); nothing changes then.

Examples

``` python
shape.Fillet((4, 0), 0.5)
shape.Fillet(shape.vertices[0], 0.5)
```

Filleted(*at: [VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str \| list\[[VertexView](selections.md#planex.VertexView "planex.VertexView") \| Point \| str\]*, *r: float*, */*, *\**, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")  
A copy with the corners rounded as Fillet does; this Shape is not changed.

Parameters:  
- **at** – the corners: a vertex (shape.vertices\[0\]), a point (h, v) on a vertex, a vertex name (every vertex with that name), or a list or selection of these; there is no default.

- **r** – the radius, a length \> 0.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Returns:  
the copy.

Raises:  
- **TypeError** – at is none of the forms above.

- **ValueError** – a size is not positive and finite, no corner is given, a name is no vertex’s name, a vertex belongs to another object, or no corner can be cut (the message names each corner and why); nothing changes then.

Examples

``` python
shape.Filleted((4, 0), 0.5)
shape.Filleted(shape.vertices[0], 0.5)
```

FitArcs(*tol: float \| None = None*, *max_edge_length: float \| None = None*, *max_edge_angle: float \| None = None*, *ignore_edges_smaller: float \| None = None*) → [Shape](#planex.Shape "planex.Shape")  
Replaces chains of at least three edges of one part shorter than max_edge_length by arcs of at most 180 degrees (or lines) that stay within tol of the chain, in place.

Parameters:  
- **tol** – the largest distance of the arc from the chain; None takes px.config.fit_arcs\[“tol”\].

- **max_edge_length** – only edges shorter than this form a chain; None takes px.config.fit_arcs\[“max_edge_length”\].

- **max_edge_angle** – the largest turn between neighbouring edges of a chain, in degrees; None takes px.config.fit_arcs\[“max_edge_angle”\].

- **ignore_edges_smaller** – edges shorter than this do not end a chain by their direction; None takes px.config.fit_arcs\[“ignore_edges_smaller”\].

Returns:  
this Shape.

Examples

``` python
shape.FitArcs()
shape.FitArcs(tol=1e-4, max_edge_length=0.5)
```

Hausdorff(*other: Any*, *samples: int = 64*) → float  
The Hausdorff distance to other: the largest distance from a point of one to the other.

Parameters:  
- **other** – an object, element or selection.

- **samples** – the samples per edge before the search for the largest distance refines.

Examples

``` python
wire.Hausdorff(other_wire)
shape.edges("Line.1").Hausdorff(shape.edges("Line.2"), samples=128)
```

Identify(*a: str*, *b: str*, */*, *trafo: Transformation \| tuple \| None = None*, *\**, *name: str \| None = None*, *type: IdentificationType \| None = None*) → [Shape](#planex.Shape "planex.Shape")  
Identifies the edges named a with those named b for the mesh: b’s mesh is copied from a’s (netgen’s Identify).

The identification is stored by name and applied at every OCC export, which raises unless every exported piece of one side pairs with one of the other. Booleans, Glue, Split, Trim and copies keep it (the first operand wins where names clash), motions move it.

Parameters:  
- **a** – the name of the edges mapped onto b.

- **b** – the name of the edges that get a’s mesh.

- **trafo** – the map of a onto b in the plane’s coordinates: a Transformation (px.Translation, px.Rotation) or (matrix 2x2, vector 2); None finds it from the geometry at every export (px.GetTransformation), so it follows later changes.

- **name** – the identification’s name; default ‘a-b’; an identification of the same name is replaced.

- **type** – a netgen.meshing.IdentificationType; None: PERIODIC.

Returns:  
this Shape.

Raises:  
- **ValueError** – a and b are the same; no edge has the name a or b; type is not an IdentificationType; without trafo, no map takes a onto b (as px.GetTransformation).

- **TypeError** – trafo has another form.

Examples

``` python
shape.Identify("left", "right", px.Translation(10, 0))
shape.Identify("inner", "outer", name="ring")
```

Measure(*name: str \| [FaceView](selections.md#planex.FaceView "planex.FaceView") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView") \| None = None*, *to: str \| Point \| Point3 \| [Face](#planex.Face "planex.Face") \| [Shape](#planex.Shape "planex.Shape") \| [Wire](#planex.Wire "planex.Wire") \| None = None*, *project: bool = False*) → dict\
Measure(*f: Callable*, */*, *\*args*, *id: str \| None = None*, *\*\*params*) → object  
The measures of the faces and edges named name, or of the one with that id or selected, as a dict.

Its keys: type, faces, area, edges, length, center (in 3d; of an arc or circle its centre, with radius and angle), inertia, Ih, Iv, Ihv, I1, I2, principal_angle, bounding_box; with to also distance, du, dv, dx, dy, dz, points and the angle between two lines.

Parameters:  
- **name** – a name, an id, or a selected face or edge; None measures all.

- **to** – a name, or anything Distance takes: adds the distance to it.

- **project** – True projects a target of to in another plane into this one.

- **f** – a measurement, a callable f(obj, \*args, \*\*params) (see px.measurements): Measure returns its value.

- **args** – the further arguments of f.

- **id** – stores the measurement f under this id: obj.measures then holds it next to the standard measurements, RemoveMeasure(id) drops it.

- **params** – the keyword arguments of f.

Raises:  
- **ValueError** – no face or edge has that name or id.

- **TypeError** – name has another form.

- **TypeError** – id without a measurement f.

Examples

``` python
shape.Measure()
shape.Measure(px.measurements.MinThickness(), id="wall")
```

MergeCollinearEdges(*tol: float \| None = None*) → [Shape](#planex.Shape "planex.Shape")  
Merges lines of one part that meet alone at their vertices into fewer lines, in place: a vertex goes while every vertex removed between its neighbours stays within tol of the line through them.

The result stays within tol of the edges the call was given; a second call may merge more. The edges of a spline stay.

Parameters:  
**tol** – the largest distance of a removed vertex from the new line; None takes px.config.merge_collinear_closer.

Returns:  
this Shape.

Examples

``` python
shape.MergeCollinearEdges()
shape.MergeCollinearEdges(1e-6)
```

Mirror(*point: Point*, */*, *dir: tuple\[float, float\]*) → [Shape](#planex.Shape "planex.Shape")\
Mirror(*point: Point3*, */*, *normal: tuple\[float, float, float\]*) → [Shape](#planex.Shape "planex.Shape")  
Mirrors it in place at the line through point along dir in its plane, or at the plane through point with normal in 3d.

In 3d its plane is mirrored and keeps a right-handed coordinate system, so its coordinates in the plane are mirrored at the plane’s first axis.

Parameters:  
- **point** – a point of the mirror line, (h, v), or of the mirror plane in 3d, (x, y, z).

- **dir** – the direction of the mirror line in the plane, (dh, dv).

- **normal** – the normal of the mirror plane in 3d, (dx, dy, dz).

Returns:  
this Shape.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
shape.Mirror((0, 0), (0, 1))
shape.Mirror((0, 0, 0), (1, 0, 0))
```

Mirrored(*point: Point*, */*, *dir: tuple\[float, float\]*, *\**, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")\
Mirrored(*point: Point3*, */*, *normal: tuple\[float, float, float\]*, *\**, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")  
A copy mirrored at the line through point along dir in its plane, or at the plane through point with normal in 3d; this Shape is not changed.

In 3d its plane is mirrored and keeps a right-handed coordinate system, so its coordinates in the plane are mirrored at the plane’s first axis.

Parameters:  
- **point** – a point of the mirror line, (h, v), or of the mirror plane in 3d, (x, y, z).

- **dir** – the direction of the mirror line in the plane, (dh, dv).

- **normal** – the normal of the mirror plane in 3d, (dx, dy, dz).

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
shape.Mirrored((0, 0), (0, 1))
shape.Mirrored((0, 0, 0), (1, 0, 0))
```

Normal() → tuple\[float, float, float\]  
The normal of its plane in 3d.

Examples

``` python
shape.Normal()
```

Offset(*d: float*, *inplace: bool = True*, *\**, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")  
Grows the faces by d: the band of width [\|d\|](#id3) along their boundaries added for d \> 0, taken away for d \< 0.

The band is Wire.Offset of their edges, round around convex corners; the faces keep their properties, the new edges those of the edges they come from.

Parameters:  
- **d** – the width of the band, a length; d \> 0 grows, d \< 0 shrinks, 0 changes nothing.

- **inplace** – True changes this Shape and returns it; False returns the offset as a new Shape and leaves this one.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – the faces share edges (glued); px.Unify merges them first.

Examples

``` python
shape.Offset(0.5)
band = shape.Offset(0.5, inplace=False)
```

Plot(*plane: [Plane](#planex.Plane "planex.Plane") \| None = None*, */*, *ax: Any = None*, *line_col: Color = 'black'*, *arc_col: Color = '#ff7f0e'*, *spline_col: Color = '#2ca02c'*, *face_col: Color \| None = '#dde6f0'*, *mark_edges_smaller: float \| None = None*, *mark_free_edges: bool = False*, *highlight: Ids \| Any = None*, *legend: bool = True*, *show_constraints: bool \| None = None*, *show_ends: bool = False*, *show_direction: bool = False*, *show_parent: bool \| None = None*, *backend: str \| None = None*) → Any  
Draws a planex object in its plane with matplotlib or plotly.

A sketch is drawn as sk.Plot draws it (show_constraints, show_ends, show_direction, show_parent); the other options are for the other objects, e.g. px.Plot(sk.Shape(), face_col=”C0”). An OCC shape is cut at the plane (px.Intersect3d(plane, shape)), or without one drawn flat in its own plane as px.FromOCC gives it. Fields: px.PlotField; meshes: px.PlotMesh.

Parameters:  
- **obj** – a Face, Shape, Wire, Compound, sketch, selection, DeformableShape or OCC shape, or an id that names one (“Sketch.1”, “Shape.2.Line.3”).

- **plane** – the plane that cuts an OCC shape; None draws it flat in its own plane.

- **ax** – a matplotlib Axes or a plotly Figure to draw into, which also selects the backend.

- **line_col** – the colour of every edge; “type” draws lines in black, arcs in arc_col and splines in spline_col, with a legend entry each; “property” takes each edge’s own col where it has one.

- **arc_col** – the colour of arcs with line_col=”type”.

- **spline_col** – the colour of splines with line_col=”type”.

- **face_col** – fills the closed loops (faces and shapes; a wire on its own stays an outline unless face_col is given); a fill other than the default gets a legend entry labelled with the object’s id (‘Face.3’), the default fill none; None draws the outlines only.

- **mark_edges_smaller** – marks the edges shorter than this at their midpoints.

- **mark_free_edges** – True marks the edges with an end no other edge shares.

- **highlight** – marks the edges given: ids (“Line.3”, “Shape.1.Line.3”), selected edges or faces (a face: its edges), a selection, or indices or a boolean mask over obj.edges (for a Compound those of its parts in turn).

- **legend** – True shows the legend.

- **show_constraints** – for a sketch, as sk.Plot.

- **show_ends** – for a sketch, as sk.Plot.

- **show_direction** – for a sketch, as sk.Plot.

- **show_parent** – for a sketch, as sk.Plot.

- **backend** – “matplotlib” or “plotly” without ax; None is matplotlib.

Returns:  
the matplotlib Axes or the plotly Figure.

Raises:  
**ValueError** – a sketch with an option for other objects, an option for sketches with another object, a plane with a planex object, obj is a plane, or backend is unknown.

Examples

``` python
px.Plot(face, line_col="type")
px.Plot(solid, px.XY)
```

Plot3d(*plane_size: float \| None = None*, *backend: str = 'webgpu'*, *\*\*kwargs: Any*) → Any  
Draws planex objects, planes and OCC shapes in one 3d view.

Everything is exported to OCC, put into one compound and drawn; a sketch contributes its drawn edges. A plane is drawn as a square of side plane_size with a line along its u axis. backend “webgpu” draws with ngsolve_webgpu.jupyter.Draw: it needs ngsolve_webgpu (pip install ngsolve_webgpu, which brings the full ngsolve) and a browser with WebGPU, with the kernel on the same machine as the browser or its websocket port forwarded. backend “webgui” draws with netgen.webgui.Draw and needs only netgen.

Parameters:  
- **objects** – a Wire, Face, Shape, Compound, sketch, Plane or OCC shape, or a list of them.

- **plane_size** – the side of a plane’s square; None takes the largest extent of everything else, 1 without.

- **backend** – ‘webgpu’ or ‘webgui’.

- **kwargs** – go to the backend’s Draw (width, height; settings, euler_angles only for webgui).

Returns:  
the backend’s scene.

Raises:  
- **ValueError** – backend is unknown, or there is nothing to draw.

- **ImportError** – backend ‘webgpu’ without ngsolve_webgpu installed.

Examples

``` python
px.Plot3d([face, px.XY], plane_size=2)
shape.Plot3d(backend="webgui", height=400)
```

Polylines(*tol: float*) → list\[np.ndarray\]  
The loops and open chains as (n, 2) arrays of plane coordinates, each chord within tol of its edge.

A face gives its outer loop counterclockwise, then its holes clockwise; a shape its faces in turn, then its free wires; a wire’s loop runs the way the wire’s first edge runs; a closed loop ends at its first point.

Parameters:  
**tol** – the largest distance of a chord from its edge, a length \> 0.

Raises:  
**ValueError** – tol is not positive and finite, or below the rounding of an arc’s or a spline’s coordinates.

Examples

``` python
shape.Polylines(0.01)
```

Project(*shape: TopoDS_Shape*, */*) → list\[[Wire](#planex.Wire "planex.Wire")\]  
The edges of the OCC shape projected straight onto this plane, as a list of wires, as px.Project.

Parameters:  
**shape** – an OCC shape.

Examples

``` python
shape.Project(box)
```

RemoveIdentification(*name: str*) → [Shape](#planex.Shape "planex.Shape")  
Removes the identification called name.

Parameters:  
**name** – the identification’s name, a key of identifications.

Returns:  
this Shape.

Raises:  
**KeyError** – no identification has that name.

Examples

``` python
shape.RemoveIdentification("left-right")
```

RemoveMeasure(*id: str*) → None  
Drops the stored measurement id.

Parameters:  
**id** – the id the measurement was stored under (Measure(f, id=…)).

Raises:  
**ValueError** – no measurement is stored under id.

Examples

``` python
shape.RemoveMeasure("gap")
```

RemoveSmallEdges(*tol: float*) → [Shape](#planex.Shape "planex.Shape")  
Removes the lines and arcs no longer than tol in place; both ends of each merge into its midpoint.

Lengths are measured before any merge; splines and the edges of a spline (fitted at export) stay.

Parameters:  
**tol** – the largest length removed.

Returns:  
this Shape.

Examples

``` python
shape.RemoveSmallEdges(1e-4)
```

Rotate(*center: Point*, *angle: float*) → [Shape](#planex.Shape "planex.Shape")\
Rotate(*point: Point3*, */*, *dir: tuple\[float, float, float\]*, *angle: float*) → [Shape](#planex.Shape "planex.Shape")  
Rotates it in place by angle around center in its plane, or around the axis through point along dir in 3d.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **center** – the centre of the rotation in the plane, (h, v).

- **angle** – the angle in degrees.

- **point** – a point of the axis in 3d, (x, y, z).

- **dir** – the direction of the axis in 3d, (dx, dy, dz).

Returns:  
this Shape.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
shape.Rotate((0, 0), 90)
shape.Rotate((0, 0, 0), (0, 0, 1), 45)
```

Rotated(*center: Point*, *angle: float*, *\**, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")\
Rotated(*point: Point3*, */*, *dir: tuple\[float, float, float\]*, *angle: float*, *\**, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")  
A copy rotated by angle around center in its plane, or around the axis through point along dir in 3d; this Shape is not changed.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **center** – the centre of the rotation in the plane, (h, v).

- **angle** – the angle in degrees.

- **point** – a point of the axis in 3d, (x, y, z).

- **dir** – the direction of the axis in 3d, (dx, dy, dz).

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
shape.Rotated((0, 0), 90)
shape.Rotated((0, 0, 0), (0, 0, 1), 45)
```

Scale(*center: Point*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*) → [Shape](#planex.Shape "planex.Shape")\
Scale(*point: Point3*, */*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*) → [Shape](#planex.Shape "planex.Shape")  
Scales it in place by factor around center in its plane, or around point in 3d.

The mesh sizes maxh and minh stay unless asked for.

Parameters:  
- **center** – the centre of the scaling in the plane, (h, v).

- **point** – the centre of the scaling in 3d, (x, y, z).

- **factor** – the scale factor, \> 0.

- **scale_maxh** – True also scales maxh.

- **scale_minh** – True also scales minh.

Returns:  
this Shape.

Raises:  
**ValueError** – factor is not positive and finite, or a coordinate is not finite.

Examples

``` python
shape.Scale((0, 0), 2)
shape.Scale((0, 0), 0.5, scale_maxh=True)
```

Scaled(*center: Point*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*, *\**, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")\
Scaled(*point: Point3*, */*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*, *\**, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")  
A copy scaled by factor around center in its plane, or around point in 3d; this Shape is not changed.

The mesh sizes maxh and minh stay unless asked for.

Parameters:  
- **center** – the centre of the scaling in the plane, (h, v).

- **point** – the centre of the scaling in 3d, (x, y, z).

- **factor** – the scale factor, \> 0.

- **scale_maxh** – True also scales maxh.

- **scale_minh** – True also scales minh.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – factor is not positive and finite, or a coordinate is not finite.

Examples

``` python
shape.Scaled((0, 0), 2)
shape.Scaled((0, 0), 0.5, scale_maxh=True)
```

Simplified(*fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*, *\**, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")  
A copy defeatured as Simplify does it; this shape is not changed.

Parameters:  
- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
shape.Simplified()
shape.Simplified(fit_arcs=True, remove_edges_smaller=1e-6)
```

Simplify(*fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → [Shape](#planex.Shape "planex.Shape")  
Defeatures all edges at once in place as the OCC exports do: gaps closed, collinear lines merged, arcs and splines fitted, small edges removed, as enabled.

Free edges are removed from the faces only.

Parameters:  
- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
shape.Simplify()
shape.Simplify(fit_arcs=True, remove_edges_smaller=1e-6)
```

Split(*cutter: [Wire](#planex.Wire "planex.Wire") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView") \| [Face](#planex.Face "planex.Face") \| [Shape](#planex.Shape "planex.Shape") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch") \| TopoDS_Shape*, */*, *\**, *id: str \| None = None*) → list\[[Shape](#planex.Shape "planex.Shape")\]  
Cuts every face and wire by the cutter into one Shape per piece.

What the cutter does not separate stays as it is; the pieces keep the face’s properties, the new edges the cutter’s.

Parameters:  
- **cutter** – the edges that cut, a Wire, edge, Face, Shape, sketch or OCC shape; projected into the plane if it lies elsewhere.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**TypeError** – the cutter has the wrong form.

Examples

``` python
pieces = shape.Split(px.Line((0, -5), (0, 5)))
```

ToHV(*point: Point3*, */*) → tuple\[float, float\]\
ToHV(*points: list\[Point3\]*, */*) → list\[tuple\[float, float\]\]  
The plane coordinates (h, v) of the projection of a 3d point onto its plane, or of a list of points.

Parameters:  
- **point** – (x, y, z).

- **points** – a list of (x, y, z).

Examples

``` python
shape.ToHV((1, 2, 0))
shape.ToHV([(0, 0, 0), (1, 2, 0)])
```

ToOCCFace(*fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → TopoDS_Shape  
The faces as OCC faces: a single face as itself, else a compound.

Parameters:  
- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
shape.ToOCCFace()
shape.ToOCCFace(fit_arcs=True, remove_edges_smaller=1e-6)
```

ToOCCShape(*fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → TopoDS_Shape  
The faces and wires as OCC shapes sharing their vertices: a single face as itself, else a compound.

Parameters:  
- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
shape.ToOCCShape()
shape.ToOCCShape(fit_arcs=True, remove_edges_smaller=1e-6)
```

ToOCCWires(*approximation: bool = False*, *fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → TopoDS_Shape  
The connected wires of all edges as an OCC compound.

Parameters:  
- **approximation** – True gives the splines as their arc approximation.

- **fit_splines** – fits splines to chains of short edges: True, False or a dict of the fit’s options (tol, max_edge_length, max_edge_angle, ignore_edges_smaller); None takes px.config.fit_splines.

- **remove_edges_smaller** – removes lines and arcs no longer than this; None takes px.config.remove_edges_smaller.

- **close_gaps_smaller** – closes gaps between ends up to this; None takes px.config.close_gaps_smaller.

- **remove_free_edges** – removes the edges that touch no other edge at either end; None takes px.config.remove_free_edges. In a Shape only the edges of its faces go, its wires stay (a wire of one isolated edge too).

- **merge_collinear_closer** – merges two lines whose common vertex is this close to the line through their far ends; None takes px.config.merge_collinear_closer.

- **remove_faces_smaller** – removes the faces of less area than this (0: none); None takes px.config.remove_faces_smaller.

- **merge_small_faces_instead** – True unites such a face with the face it shares the longest boundary with instead; None takes px.config.merge_small_faces_instead.

- **fit_arcs** – fits arcs to chains of short edges: True, False or a dict of FitArcs’ options; None takes px.config.fit_arcs.

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
shape.ToOCCWires()
shape.ToOCCWires(fit_arcs=True, remove_edges_smaller=1e-6)
```

ToSVG(*size: float = 300*, *\*\*options: Any*) → str \| None  
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

ToShapely(*tol: float = 0.001*) → Any  
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

ToXYZ(*point: Point*, */*) → tuple\[float, float, float\]\
ToXYZ(*points: list\[Point\]*, */*) → list\[tuple\[float, float, float\]\]  
The 3d coordinates of a point (h, v) of its plane, or of a list of points.

Parameters:  
- **point** – (h, v) in the plane.

- **points** – a list of (h, v).

Examples

``` python
shape.ToXYZ((1, 2))
shape.ToXYZ([(0, 0), (1, 2)])
```

Translate(*d: tuple\[float, float\]*) → [Shape](#planex.Shape "planex.Shape")\
Translate(*dh: float*, *dv: float*) → [Shape](#planex.Shape "planex.Shape")\
Translate(*d: tuple\[float, float, float\]*) → [Shape](#planex.Shape "planex.Shape")\
Translate(*dx: float*, *dy: float*, *dz: float*) → [Shape](#planex.Shape "planex.Shape")  
Moves it in place by d: in the plane by (dh, dv), in 3d by (dx, dy, dz).

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **d** – the displacement, (dh, dv) in the plane or (dx, dy, dz) in 3d.

- **dh** – the displacement along the plane’s first axis.

- **dv** – the displacement along the plane’s second axis.

- **dx** – the displacement along x in 3d.

- **dy** – the displacement along y in 3d.

- **dz** – the displacement along z in 3d.

Returns:  
this Shape.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
shape.Translate((10, 0))
shape.Translate(0, 0, 5)
```

Translated(*d: tuple\[float, float\]*, *\**, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")\
Translated(*dh: float*, *dv: float*, *\**, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")\
Translated(*d: tuple\[float, float, float\]*, *\**, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")\
Translated(*dx: float*, *dy: float*, *dz: float*, *\**, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")  
A copy moved by d: in the plane by (dh, dv), in 3d by (dx, dy, dz); this Shape is not changed.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **d** – the displacement, (dh, dv) in the plane or (dx, dy, dz) in 3d.

- **dh** – the displacement along the plane’s first axis.

- **dv** – the displacement along the plane’s second axis.

- **dx** – the displacement along x in 3d.

- **dy** – the displacement along y in 3d.

- **dz** – the displacement along z in 3d.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
shape.Translated((10, 0))
shape.Translated(0, 0, 5)
```

Trim(*cutter: [Wire](#planex.Wire "planex.Wire") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView") \| [Face](#planex.Face "planex.Face") \| [Shape](#planex.Shape "planex.Shape") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch") \| TopoDS_Shape*, *\**, *keep_closest_to: Point \| Point3 \| None = None*, *remove_closest_to: Point \| Point3 \| None = None*, *keep_largest: bool = False*, *keep_smallest: bool = False*, *keep_left: bool = False*, *keep_right: bool = False*, *id: str \| None = None*) → [Shape](#planex.Shape "planex.Shape")  
Cuts every face and wire by the cutter and keeps the pieces the one option given chooses.

What the cutter does not separate stays as it is.

Parameters:  
- **cutter** – the edges that cut, a Wire, edge, Face, Shape, sketch or OCC shape; projected into the plane if it lies elsewhere.

- **keep_closest_to** – keeps the piece closest to this point (a face piece by its centre, a wire piece by itself), (h, v) or (x, y, z).

- **remove_closest_to** – removes the piece closest to this point and keeps the others.

- **keep_largest** – keeps the piece of the largest area (faces) or length (wires).

- **keep_smallest** – keeps the piece of the smallest area or length.

- **keep_left** – keeps the face pieces left of the cutter as drawn; a shape’s free wires stay.

- **keep_right** – keeps the face pieces right of the cutter as drawn; a shape’s free wires stay.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Returns:  
a new Shape; this one is not changed.

Raises:  
- **ValueError** – not exactly one option is given; keep_left or keep_right without faces, or no piece lies wholly on that side.

- **TypeError** – the cutter or the point has the wrong form.

Examples

``` python
shape.Trim(px.Line((0, -5), (0, 5)), keep_left=True)
shape.Trim(cutter, keep_closest_to=(2, 1))
```

Wire(*\**, *id: str \| None = None*) → [Wire](#planex.Wire "planex.Wire")  
The single connected wire of all edges.

Parameters:  
**id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – the edges form several wires; .wires gives them.

Examples

``` python
shape.Wire()
```

WriteBrep(*path: str \| os.PathLike*, */*, *\*\*options: Any*) → None  
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

WriteDXF(*path: str \| os.PathLike*, */*, *unit: float = 1.0*) → None  
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

WriteGeo(*path: str \| os.PathLike*, */*) → None  
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

WriteStep(*path: str \| os.PathLike*, */*, *\*\*options: Any*) → None  
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

*property* arcs  
list\[ArcEdge\]

The arcs among the edges, copies.

Type:  
arcs

*property* bounding_box  
tuple\[tuple\[float, float\], tuple\[float, float\]\] \| None

Its bounding box in the plane, ((hmin, vmin), (hmax, vmax)); None if it is empty.

Type:  
bounding_box

*property* center  
tuple\[float, float, float\] \| None

The centre of mass in 3d, (x, y, z): of the area for faces, of the length for wires; None if it is empty.

Type:  
center

*property* circles  
list\[CircleEdge\]

The circles among the edges, copies.

Type:  
circles

*property* col  
tuple\[float, float, float, float\] \| None

The colour (r, g, b, a), set as (r, g, b) or (r, g, b, a) all faces share, None if they differ; set on all faces.

Type:  
col

*property* edges  
EdgesView

All edges, each once, a view: a property set on it is set on all of them.

Type:  
edges

*property* faces  
FacesView

The faces, views whose properties, edges and vertices are the shape’s.

Type:  
faces

*property* hpref  
float \| None

Netgen’s hpref all faces share, None if they differ; set on all faces.

Type:  
hpref

*property* id  
str

The object’s id: the kind that made it and a number, given when first asked for (Sketch.1, Shape.3), or set (an ASCII letter, then letters, digits, \_ and single inner spaces); its elements’ ids follow it (Shape.3.Line.2).

Type:  
id

*property* identifications  
dict

The identifications as a dict {name: (a, b, trafo or None, type)}.

Type:  
identifications

*property* inertia  
tuple\[tuple\[float, float, float\], tuple\[float, float, float\], tuple\[float, float, float\]\] \| None

The inertia tensor about the centre in 3d for unit density (per area for faces, per length for wires), three rows of three; None if it is empty.

Type:  
inertia

*property* layer  
int \| None

Netgen’s layer number all faces share, None if they differ; set on all faces.

Type:  
layer

*property* lines  
list\[LineEdge\]

The lines among the edges, copies.

Type:  
lines

*property* mass  
float

The area of the faces.

Type:  
mass

*property* maxh  
float \| None

The largest mesh size (netgen’s maxh) all faces share, None if they differ; set on all faces.

Type:  
maxh

*property* measures  
dict

The standard measurements and the stored ones, evaluated now, as a dict.

Type:  
measures

*property* minh  
float \| None

The smallest mesh size (netgen’s minh) all faces share, None if they differ; set on all faces.

Type:  
minh

*property* name  
str \| None

The name all faces share, None if they differ; set on all faces.

Type:  
name

*property* quad_dominated  
bool \| None

The quad_dominated flag (True: netgen meshes with mostly quadrilaterals) all faces share, None if they differ; set on all faces.

Type:  
quad_dominated

*property* splines  
list\[SplineEdge\]

The splines among the edges, copies.

Type:  
splines

*property* vertices  
VerticesView

The vertices, each once, in the order the edges reach them; one vertex where parts meet.

Type:  
vertices

*property* wires  
list\[Wire\]

The loops of the faces and the connected chains of the wires, a list of wires.

Type:  
wires

&nbsp;

*class* Compound(*parts: Iterable\[[Face](#planex.Face "planex.Face") \| [Shape](#planex.Shape "planex.Shape") \| [Wire](#planex.Wire "planex.Wire") \| [Compound](#planex.Compound "planex.Compound")\] = ()*, */*, *id: str \| None = None*)  
A plain list of faces, shapes, wires and compounds, also on different planes.

Nothing is cut or merged and there are no booleans (+, -, \* raise TypeError); px.Unify(compound) merges its parts. It is exported as it is. The parts are copies that keep their ids, so moving the compound moves each part once and leaves the objects it was made of where they are. len(), iteration and indexing go over its parts.

Parameters:  
- **parts** – the faces, shapes, wires and compounds.

- **id** – the compound’s id; None gives Compound.N when first asked for.

Raises:  
**ValueError** – id does not follow the rule of a user-given id.

Examples

``` python
px.Compound([plate, rib.Rotated((0, 0, 0), (1, 0, 0), 90)])
px.Compound([outline, holes], id="board")
```

ClosestPoint(*point: Point \| Point3 \| np.ndarray*, */*) → tuple\[float, ...\] \| np.ndarray  
The closest point of the nearest part, as Shape.ClosestPoint.

Parameters:  
**point** – (h, v) if all parts lie on one plane, else (x, y, z); many points as an (n, 2) or (n, 3) array give an array of that shape.

Raises:  
**ValueError** – the compound is empty, or point is (h, v) and the parts lie on different planes.

Examples

``` python
compound.ClosestPoint((5, 0))
compound.ClosestPoint([(0, 0, 0), (5, 0, 0)])
```

Contains(*points: Point \| Point3 \| np.ndarray*, */*, *boundary: bool = True*) → bool \| np.ndarray  
True if a part contains the point, as Shape.Contains.

Parameters:  
- **points** – (h, v) if all parts lie on one plane, else (x, y, z); many points as an (n, 2) or (n, 3) array give a bool array (n,).

- **boundary** – the answer for a point on a boundary.

Raises:  
**ValueError** – points are (h, v) and the parts lie on different planes.

Examples

``` python
compound.Contains((1, 2))
compound.Contains([(0, 0, 0), (1, 2, 0)], boundary=False)
```

Copy() → [Compound](#planex.Compound "planex._compound.Compound")  
An independent copy of the compound and of its parts.

Examples

``` python
compound.Copy()
```

Distance(*target: Point \| Point3 \| [Face](#planex.Face "planex.Face") \| [Shape](#planex.Shape "planex.Shape") \| [Wire](#planex.Wire "planex.Wire") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView") \| [VertexView](selections.md#planex.VertexView "planex.VertexView") \| [FaceView](selections.md#planex.FaceView "planex.FaceView") \| np.ndarray*, */*, *project: bool = False*, *components: str \| None = None*, *signed: bool = False*) → float \| tuple\[float, ...\] \| np.ndarray  
The smallest distance of the parts to the target, as Shape.Distance.

Parameters:  
- **target** – what Shape.Distance takes; a point is (h, v) if all parts lie on one plane, else (x, y, z); many points as an (n, 2) or (n, 3) array.

- **project** – True projects a target in another plane into the parts’ planes.

- **components** – letters of ‘hvxyz’: the components of the shortest connection to the target from the nearest part (the first of equally near ones) instead of its length.

- **signed** – True measures as Shape.Distance(signed=True) of the union of the faces of all parts.

Returns:  
a float; with components a tuple of them, one letter a float; many points an array (n,), with k components (n, k), one letter (n,).

Raises:  
**ValueError** – the compound is empty; points are (h, v) and the parts lie on different planes; signed and the compound has no faces or its faces lie on different planes.

Examples

``` python
compound.Distance((5, 0))
compound.Distance((5, 0, 1), components="xyz")
```

Mirror(*\*args: Any*, *\*\*kwargs: Any*) → [Compound](#planex.Compound "planex._compound.Compound")  
Mirrors every part in place, as Shape.Mirror.

Parameters:  
- **args** – the arguments of Shape.Mirror: point, dir in the plane, or point, normal in 3d.

- **kwargs** – its keyword arguments.

Returns:  
this compound.

Examples

``` python
compound.Mirror((0, 0), (0, 1))
compound.Mirror((0, 0, 0), (1, 0, 0))
```

Mirrored(*\*args: Any*, *\*\*kwargs: Any*) → [Compound](#planex.Compound "planex._compound.Compound")  
A copy with every part mirrored, as Shape.Mirrored; this compound is not changed.

Parameters:  
- **args** – the arguments of Shape.Mirrored: point, dir in the plane, or point, normal in 3d.

- **kwargs** – its keyword arguments.

Examples

``` python
compound.Mirrored((0, 0), (0, 1))
compound.Mirrored((0, 0, 0), (1, 0, 0))
```

Plot(*plane: [Plane](#planex.Plane "planex.Plane") \| None = None*, */*, *ax: Any = None*, *line_col: Color = 'black'*, *arc_col: Color = '#ff7f0e'*, *spline_col: Color = '#2ca02c'*, *face_col: Color \| None = '#dde6f0'*, *mark_edges_smaller: float \| None = None*, *mark_free_edges: bool = False*, *highlight: Ids \| Any = None*, *legend: bool = True*, *show_constraints: bool \| None = None*, *show_ends: bool = False*, *show_direction: bool = False*, *show_parent: bool \| None = None*, *backend: str \| None = None*) → Any  
Draws a planex object in its plane with matplotlib or plotly.

A sketch is drawn as sk.Plot draws it (show_constraints, show_ends, show_direction, show_parent); the other options are for the other objects, e.g. px.Plot(sk.Shape(), face_col=”C0”). An OCC shape is cut at the plane (px.Intersect3d(plane, shape)), or without one drawn flat in its own plane as px.FromOCC gives it. Fields: px.PlotField; meshes: px.PlotMesh.

Parameters:  
- **obj** – a Face, Shape, Wire, Compound, sketch, selection, DeformableShape or OCC shape, or an id that names one (“Sketch.1”, “Shape.2.Line.3”).

- **plane** – the plane that cuts an OCC shape; None draws it flat in its own plane.

- **ax** – a matplotlib Axes or a plotly Figure to draw into, which also selects the backend.

- **line_col** – the colour of every edge; “type” draws lines in black, arcs in arc_col and splines in spline_col, with a legend entry each; “property” takes each edge’s own col where it has one.

- **arc_col** – the colour of arcs with line_col=”type”.

- **spline_col** – the colour of splines with line_col=”type”.

- **face_col** – fills the closed loops (faces and shapes; a wire on its own stays an outline unless face_col is given); a fill other than the default gets a legend entry labelled with the object’s id (‘Face.3’), the default fill none; None draws the outlines only.

- **mark_edges_smaller** – marks the edges shorter than this at their midpoints.

- **mark_free_edges** – True marks the edges with an end no other edge shares.

- **highlight** – marks the edges given: ids (“Line.3”, “Shape.1.Line.3”), selected edges or faces (a face: its edges), a selection, or indices or a boolean mask over obj.edges (for a Compound those of its parts in turn).

- **legend** – True shows the legend.

- **show_constraints** – for a sketch, as sk.Plot.

- **show_ends** – for a sketch, as sk.Plot.

- **show_direction** – for a sketch, as sk.Plot.

- **show_parent** – for a sketch, as sk.Plot.

- **backend** – “matplotlib” or “plotly” without ax; None is matplotlib.

Returns:  
the matplotlib Axes or the plotly Figure.

Raises:  
**ValueError** – a sketch with an option for other objects, an option for sketches with another object, a plane with a planex object, obj is a plane, or backend is unknown.

Examples

``` python
px.Plot(face, line_col="type")
px.Plot(solid, px.XY)
```

Plot3d(*plane_size: float \| None = None*, *backend: str = 'webgpu'*, *\*\*kwargs: Any*) → Any  
Draws planex objects, planes and OCC shapes in one 3d view.

Everything is exported to OCC, put into one compound and drawn; a sketch contributes its drawn edges. A plane is drawn as a square of side plane_size with a line along its u axis. backend “webgpu” draws with ngsolve_webgpu.jupyter.Draw: it needs ngsolve_webgpu (pip install ngsolve_webgpu, which brings the full ngsolve) and a browser with WebGPU, with the kernel on the same machine as the browser or its websocket port forwarded. backend “webgui” draws with netgen.webgui.Draw and needs only netgen.

Parameters:  
- **objects** – a Wire, Face, Shape, Compound, sketch, Plane or OCC shape, or a list of them.

- **plane_size** – the side of a plane’s square; None takes the largest extent of everything else, 1 without.

- **backend** – ‘webgpu’ or ‘webgui’.

- **kwargs** – go to the backend’s Draw (width, height; settings, euler_angles only for webgui).

Returns:  
the backend’s scene.

Raises:  
- **ValueError** – backend is unknown, or there is nothing to draw.

- **ImportError** – backend ‘webgpu’ without ngsolve_webgpu installed.

Examples

``` python
px.Plot3d([face, px.XY], plane_size=2)
shape.Plot3d(backend="webgui", height=400)
```

Polylines(*tol: float*) → list\[ndarray\]  
Every part’s Polylines(tol) in turn, (n, 2) arrays of plane coordinates.

Parameters:  
**tol** – the largest distance of a chord from its edge, a length \> 0.

Raises:  
**ValueError** – the parts lie on different planes, or tol is not positive.

Examples

``` python
compound.Polylines(0.01)
```

Rotate(*\*args: Any*, *\*\*kwargs: Any*) → [Compound](#planex.Compound "planex._compound.Compound")  
Rotates every part in place, as Shape.Rotate.

Parameters:  
- **args** – the arguments of Shape.Rotate: center, angle in the plane, or point, dir, angle in 3d.

- **kwargs** – its keyword arguments.

Returns:  
this compound.

Examples

``` python
compound.Rotate((0, 0), 90)
compound.Rotate((0, 0, 0), (0, 0, 1), 45)
```

Rotated(*\*args: Any*, *\*\*kwargs: Any*) → [Compound](#planex.Compound "planex._compound.Compound")  
A copy with every part rotated, as Shape.Rotated; this compound is not changed.

Parameters:  
- **args** – the arguments of Shape.Rotated: center, angle in the plane, or point, dir, angle in 3d.

- **kwargs** – its keyword arguments.

Examples

``` python
compound.Rotated((0, 0), 90)
compound.Rotated((0, 0, 0), (0, 0, 1), 45)
```

Scale(*\*args: Any*, *\*\*kwargs: Any*) → [Compound](#planex.Compound "planex._compound.Compound")  
Scales every part in place, as Shape.Scale.

Parameters:  
- **args** – the arguments of Shape.Scale: center, factor in the plane, or point, factor in 3d.

- **kwargs** – its keyword arguments.

Returns:  
this compound.

Examples

``` python
compound.Scale((0, 0), 2)
compound.Scale((0, 0), 0.5, scale_maxh=True)
```

Scaled(*\*args: Any*, *\*\*kwargs: Any*) → [Compound](#planex.Compound "planex._compound.Compound")  
A copy with every part scaled, as Shape.Scaled; this compound is not changed.

Parameters:  
- **args** – the arguments of Shape.Scaled: center, factor in the plane, or point, factor in 3d.

- **kwargs** – its keyword arguments.

Examples

``` python
compound.Scaled((0, 0), 2)
compound.Scaled((0, 0), 0.5, scale_maxh=True)
```

ToOCCShape(*\*\*options: Any*) → Any  
The OCC compound of every part’s ToOCCShape(\*\*options).

Parameters:  
**options** – the options of the parts’ ToOCCShape (fit_splines, remove_edges_smaller, …).

Returns:  
a netgen.occ Compound.

Examples

``` python
compound.ToOCCShape()
compound.ToOCCShape(fit_arcs=True)
```

ToSVG(*size: float = 300*, *\*\*options: Any*) → str \| None  
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

ToShapely(*tol: float = 0.001*) → Any  
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

Translate(*\*args: Any*, *\*\*kwargs: Any*) → [Compound](#planex.Compound "planex._compound.Compound")  
Moves every part in place, as Shape.Translate.

Parameters:  
- **args** – the arguments of Shape.Translate: d or dh, dv in the plane, or d or dx, dy, dz in 3d.

- **kwargs** – its keyword arguments.

Returns:  
this compound.

Examples

``` python
compound.Translate((10, 0))
compound.Translate(0, 0, 5)
```

Translated(*\*args: Any*, *\*\*kwargs: Any*) → [Compound](#planex.Compound "planex._compound.Compound")  
A copy with every part moved, as Shape.Translated; this compound is not changed.

Parameters:  
- **args** – the arguments of Shape.Translated: d or dh, dv in the plane, or d or dx, dy, dz in 3d.

- **kwargs** – its keyword arguments.

Examples

``` python
compound.Translated((10, 0))
compound.Translated(0, 0, 5)
```

WriteBrep(*path: str \| os.PathLike*, */*, *\*\*options: Any*) → None  
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

WriteDXF(*path: str \| os.PathLike*, */*, *unit: float = 1.0*) → None  
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

WriteGeo(*path: str \| os.PathLike*, */*) → None  
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

WriteStep(*path: str \| os.PathLike*, */*, *\*\*options: Any*) → None  
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

*property* bounding_box*: tuple\[tuple\[float, float\], tuple\[float, float\]\] \| None*  
The box ((hmin, vmin), (hmax, vmax)) over the parts, each in its own plane coordinates; None if it is empty.

*property* id*: str*  
Compound.N, given when first asked for, or the one set.

Type:  
The compound’s id

## Planes

*class* Plane  
A plane in 3d for construction: an origin, the h and v directions and the normal, always right-handed (v = normal x h); its operations give new planes.

Copy() → [Plane](#planex.Plane "planex.Plane")  
An independent copy; copy.copy and copy.deepcopy give the same.

Examples

``` python
b = a.Copy()
```

Mirrored(*point: Point3*, */*, *normal: tuple\[float, float, float\]*) → [Plane](#planex.Plane "planex.Plane")  
A copy mirrored at the plane through point with that normal; v is reversed, so it stays right-handed.

Parameters:  
- **point** – a point of the mirror plane, (x, y, z).

- **normal** – the normal of the mirror plane, (dx, dy, dz).

Raises:  
**ValueError** – the normal is zero, or a value is not finite.

Examples

``` python
plane.Mirrored((0, 0, 0), (1, 0, 0))
```

Offset(*d: float*) → [Plane](#planex.Plane "planex.Plane")  
The parallel plane at distance d along the normal.

Parameters:  
**d** – the distance along the normal, negative against it.

Examples

``` python
plane.Offset(5)
plane.Offset(-0.5)
```

Rotated(*point: Point3*, */*, *dir: tuple\[float, float, float\]*, *angle: float*) → [Plane](#planex.Plane "planex.Plane")  
A copy rotated by angle around the axis through point along dir; this Plane is not changed.

Parameters:  
- **point** – a point of the axis, (x, y, z).

- **dir** – the direction of the axis, (dx, dy, dz).

- **angle** – the angle in degrees.

Examples

``` python
plane.Rotated((0, 0, 0), (1, 0, 0), 90)
```

ToHV(*point: Point3*, */*) → tuple\[float, float\]\
ToHV(*points: list\[Point3\]*, */*) → list\[tuple\[float, float\]\]  
The plane coordinates (h, v) of the projection of a 3d point onto the plane, or of a list of points.

Parameters:  
- **point** – (x, y, z).

- **points** – a list of (x, y, z).

Examples

``` python
plane.ToHV((1, 2, 0))
plane.ToHV([(0, 0, 0), (1, 2, 0)])
```

ToOCCAxes() → occ.Axes  
The plane as netgen Axes, e.g. for occ.WorkPlane.

Examples

``` python
plane.ToOCCAxes()
```

ToXYZ(*point: Point*, */*) → tuple\[float, float, float\]  
The 3d coordinates of the point (h, v) of the plane.

Parameters:  
**point** – (h, v) in the plane.

Examples

``` python
plane.ToXYZ((1, 2))
```

Translated(*d: tuple\[float, float, float\]*) → [Plane](#planex.Plane "planex.Plane")\
Translated(*dx: float*, *dy: float*, *dz: float*) → [Plane](#planex.Plane "planex.Plane")  
A copy moved by d in 3d; this Plane is not changed.

Parameters:  
- **d** – the displacement, (dx, dy, dz).

- **dx** – the displacement along x.

- **dy** – the displacement along y.

- **dz** – the displacement along z.

Examples

``` python
plane.Translated((0, 0, 5))
plane.Translated(0, 0, 5)
```

*property* h  
tuple\[float, float, float\]

The unit direction of h in 3d.

Type:  
h

*property* normal  
tuple\[float, float, float\]

The unit normal in 3d.

Type:  
normal

*property* origin  
tuple\[float, float, float\]

The origin in 3d, (x, y, z).

Type:  
origin

*property* v  
tuple\[float, float, float\]

The unit direction of v in 3d.

Type:  
v

&nbsp;

XY\
YZ\
ZX  
The planes through the origin spanned by two of the global axes; `XY` is the default plane.
