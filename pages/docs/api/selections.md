# Selections and views

The edges, faces and vertices of an object are views: a property set on a view changes the object. See [Measuring](../core/measuring/index.md) for queries and selections in use.

Find(*id: str*) → Any  
The object with this id, or the elements the rest of the id names in it.

Only ids already given are found: an auto id is given when it is first asked for, a sketch’s when it is made. A part of a compound is named by the compound’s id, then the part’s (“both.plate”, “both.plate.Line.2”); a plain id names the object outside any compound, else the one part that has it.

Parameters:  
**id** – the id: an object’s (“Sketch.1”), or an object’s and an element’s in it (“Sketch.1.Line.5”).

Returns:  
the object, or a Selection of the elements.

Raises:  
**ValueError** – no object has the id, several objects have it, or the rest names no element.

Examples

``` python
px.Find("Sketch.1")
px.Find("both.plate.Line.2")
```

&nbsp;

*class* Selection(*items: Any*, */*)  
Elements of an object, chosen: edges, faces, vertices, sketch elements and points.

obj(…), obj.edges(…), obj.faces(…), obj.vertices(…) and sk(…) make one; calling it chooses further (see \_\_call\_\_); \[\] takes an index, a slice, a list of indices or a condition (px.x \> 5, combined with & \| ~). Setting a property (name, maxh, col, …) sets it on every element; reading it gives the value they share, None if they differ.

Parameters:  
**items** – the elements.

Examples

``` python
shape.edges(name="web")(kind="arc")
shape.edges[(px.x > 5) & (px.h < 2)].Min("length")
```

Copy() → [Compound](geometry.md#planex.Compound "planex.Compound")  
Copies of everything selected as one Compound; the object they were selected from stays as it is.

Faces become Faces, edges Wires with their properties, sketch elements their Wires.

Raises:  
**TypeError** – a vertex or point is selected (\[v.Copy() for v in selection\] copies vertices).

Examples

``` python
shape.edges(name="slot").Copy()
```

Distance(*\*args: Any*, *\*\*kwargs: Any*) → Any  
The distance from the nearest selected element, as their Distance.

Parameters:  
- **args** – the arguments of the elements’ Distance.

- **kwargs** – their keyword arguments.

Raises:  
**ValueError** – the selection is empty.

Examples

``` python
shape.edges("Line.3").Distance((5, 0))
face.edges(name="channel").Distance((30, 10))
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

Max(*key: str \| Coordinate \| Any*) → [Selection](#planex.Selection "planex._select.Selection")  
The element with the largest value of key, as a selection of one (none if it is empty).

Parameters:  
**key** – as Sorted: a measure, a coordinate, an axis or a direction.

Examples

``` python
selection.Max("area")
selection.Max((1, 1))
```

Measure(*\*args: Any*, *\*\*kwargs: Any*) → dict  
The measures of the one selected element, as its Measure.

Parameters:  
- **args** – the arguments of the element’s Measure.

- **kwargs** – its keyword arguments.

Raises:  
**ValueError** – the selection does not hold exactly one element.

Examples

``` python
shape.edges("Line.3").Measure()
```

Min(*key: str \| Coordinate \| Any*) → [Selection](#planex.Selection "planex._select.Selection")  
The element with the smallest value of key, as a selection of one (none if it is empty).

Parameters:  
**key** – as Sorted: a measure, a coordinate, an axis or a direction.

Examples

``` python
selection.Min("length")
selection.Min(px.x)
```

Sorted(*key: str \| Coordinate \| Any*, *reverse: bool = False*) → [Selection](#planex.Selection "planex._select.Selection")  
The elements sorted by key, smallest first.

Parameters:  
- **key** – a measure (“length”, “area”, “radius”, or another key of Measure()), a coordinate of the centre (“h”, “v”, “x”, “y”, “z”, px.x, px.h, …), an axis (px.X, the sketch’s H or V, netgen’s X), or a direction: the centre’s component along (dh, dv) in the plane or (dx, dy, dz) in 3d.

- **reverse** – True sorts largest first.

Raises:  
**ValueError** – an element has no such measure.

Examples

``` python
selection.Sorted("length")
selection.Sorted(px.Y, reverse=True)
```

Wire() → [Wire](geometry.md#planex.Wire "planex.Wire")  
The selected edges as one Wire, joined as px.Wire(edges); other elements give their edges.

Raises:  
- **ValueError** – the edges do not form one chain (px.Wires of the edges builds several).

- **TypeError** – a point is selected.

Examples

``` python
shape.edges(name="slot").Wire()
```

*property* center*: tuple\[float, ...\]*  
The centre (x, y, z) of the one selected element.

*property* col  
tuple\[float, float, float, float\] \| None

The selected elements’ col: set on all, read as the value they share (None if they differ).

Type:  
col

*property* construction  
bool \| None

The selected elements’ construction: set on all, read as the value they share (None if they differ).

Type:  
construction

*property* edges*: [Selection](#planex.Selection "planex._select.Selection")*  
The edges of the selected faces and the selected edges themselves, each once.

*property* faces*: [Selection](#planex.Selection "planex._select.Selection")*  
The faces the selected edges bound and the selected faces themselves, each once.

*property* hpref  
float \| None

The selected elements’ hpref: set on all, read as the value they share (None if they differ).

Type:  
hpref

*property* ids*: list\[str\]*  
The ids of the selected elements, in order.

*property* layer  
int \| None

The selected elements’ layer: set on all, read as the value they share (None if they differ).

Type:  
layer

*property* maxh  
float \| None

The selected elements’ maxh: set on all, read as the value they share (None if they differ).

Type:  
maxh

*property* minh  
float \| None

The selected elements’ minh: set on all, read as the value they share (None if they differ).

Type:  
minh

*property* name  
str \| None

The selected elements’ name: set on all, read as the value they share (None if they differ).

Type:  
name

*property* outline*: [Selection](#planex.Selection "planex._select.Selection")*  
The edges on the outer loops of the selected faces and of the faces the selected edges bound.

*property* partition  
list\[float\] \| None

The selected elements’ partition: set on all, read as the value they share (None if they differ).

Type:  
partition

*property* quad_dominated  
bool \| None

The selected elements’ quad_dominated: set on all, read as the value they share (None if they differ).

Type:  
quad_dominated

&nbsp;

*class* EdgeView  
An edge of a wire, face or shape: its geometry, and properties set on it change the edge there.

ClosestPoint(*point: Point \| Point3 \| np.ndarray*, */*) → tuple\[float, ...\] \| np.ndarray  
The nearest point to point, in the plane for (h, v), in 3d for (x, y, z).

Parameters:  
**point** – (h, v) or (x, y, z); many points as an (n, 2) or (n, 3) array give an array of that shape.

Raises:  
- **ValueError** – it is empty.

- **TypeError** – point is not a point.

Examples

``` python
edge.ClosestPoint((5, 0))
edge.ClosestPoint([(0, 0), (5, 0)])
```

Copy() → [Wire](geometry.md#planex.Wire "planex.Wire")  
The edge as a Wire of its own, with its properties; copy.copy and copy.deepcopy give the same.

Examples

``` python
w = edge.Copy()
```

Distance(*target: Point \| Point3 \| [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| [EdgeView](#planex.EdgeView "planex.EdgeView") \| [VertexView](#planex.VertexView "planex.VertexView") \| [FaceView](#planex.FaceView "planex.FaceView") \| TopoDS_Shape \| np.ndarray*, */*, *project: bool = False*, *components: str \| None = None*, *on_carrier: bool = False*, *signed: bool = False*) → float \| tuple\[float, ...\] \| np.ndarray  
The smallest distance from the edge to the target, or from the whole line or circle it lies on.

A target in another plane is measured in 3d, or projected into this plane with project.

Parameters:  
- **target** – a point, (h, v) in the plane or (x, y, z) in 3d, a Face, Shape, Wire, edge, vertex, face of a shape or OCC shape; many points as an (n, 2) or (n, 3) array.

- **project** – True projects a target in another plane into this one.

- **components** – letters of ‘hvxyz’: the components of the shortest connection from here to the target instead of its length.

- **on_carrier** – True measures to the whole line or circle the edge lies on.

- **signed** – only False: an edge has no inside.

Returns:  
a float; with components a tuple of them, one letter a float; many points an array (n,), with k components (n, k), one letter (n,).

Raises:  
- **ValueError** – signed=True; components has other letters; on_carrier for a spline, or with a target off the plane without project.

- **TypeError** – the target has another form.

Examples

``` python
edge.Distance((5, 0))
edge.Distance((5, 3), on_carrier=True)
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

Measure(*to: Point \| Point3 \| [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| [EdgeView](#planex.EdgeView "planex.EdgeView") \| None = None*, *project: bool = False*) → dict\
Measure(*f: Callable*, */*, *\*args*, *id: str \| None = None*, *\*\*params*) → object  
The measures of the edge as a dict, with the keys of Wire.Measure.

Parameters:  
- **to** – anything Distance takes: adds the distance to it.

- **project** – True projects a target of to in another plane into this one.

- **f** – a measurement, a callable f(obj, \*args, \*\*params) (see px.measurements): Measure returns its value.

- **args** – the further arguments of f.

- **id** – stores the measurement f under this id: obj.measures then holds it next to the standard measurements, RemoveMeasure(id) drops it.

- **params** – the keyword arguments of f.

Raises:  
- **ValueError** – to is a name: an edge has no names to look up.

- **TypeError** – id without a measurement f.

Examples

``` python
edge.Measure()
edge.Measure(px.measurements.MinThickness(), id="wall")
```

Mirrored(*point: Point*, */*, *dir: tuple\[float, float\]*, *\**, *id: str \| None = None*) → [Wire](geometry.md#planex.Wire "planex.Wire")\
Mirrored(*point: Point3*, */*, *normal: tuple\[float, float, float\]*, *\**, *id: str \| None = None*) → [Wire](geometry.md#planex.Wire "planex.Wire")  
The edge as a Wire of its own, mirrored at the line through point along dir in its plane, or at the plane through point with normal in 3d; the edge is not changed.

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
edge.Mirrored((0, 0), (0, 1))
edge.Mirrored((0, 0, 0), (1, 0, 0))
```

PointAt(*t: float*) → [Vertex](geometry.md#planex.Vertex "planex.Vertex")  
The point at the fraction t of the edge’s length.

Parameters:  
**t** – the fraction of the length in \[0, 1\]: 0 the start, 0.5 the midpoint, 1 the end.

Raises:  
**ValueError** – t is outside \[0, 1\].

Examples

``` python
edge.PointAt(0.5)
edge.PointAt(t=0.25)
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

Rotated(*center: Point*, *angle: float*, *\**, *id: str \| None = None*) → [Wire](geometry.md#planex.Wire "planex.Wire")\
Rotated(*point: Point3*, */*, *dir: tuple\[float, float, float\]*, *angle: float*, *\**, *id: str \| None = None*) → [Wire](geometry.md#planex.Wire "planex.Wire")  
The edge as a Wire of its own, rotated by angle around center in its plane, or around the axis through point along dir in 3d; the edge is not changed.

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
edge.Rotated((0, 0), 90)
edge.Rotated((0, 0, 0), (0, 0, 1), 45)
```

Sample(*tol: float*) → np.ndarray  
An (n, 2) array of points from the start to the end, each chord within tol of the edge.

A line gives its ends, an arc points by its sagitta, a spline points along its exact curve.

Parameters:  
**tol** – the largest distance of a chord from the edge, \> 0.

Raises:  
**ValueError** – tol is not positive and finite, or below the rounding of an arc’s or a spline’s coordinates.

Examples

``` python
edge.Sample(1e-3)
edge.Sample(tol=0.01)
```

Scaled(*center: Point*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*, *\**, *id: str \| None = None*) → [Wire](geometry.md#planex.Wire "planex.Wire")\
Scaled(*point: Point3*, */*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*, *\**, *id: str \| None = None*) → [Wire](geometry.md#planex.Wire "planex.Wire")  
The edge as a Wire of its own, scaled by factor around center in its plane, or around point in 3d; the edge is not changed.

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
edge.Scaled((0, 0), 2)
edge.Scaled((0, 0), 0.5, scale_maxh=True)
```

TangentAt(*t: float*) → [Vertex](geometry.md#planex.Vertex "planex.Vertex")  
The unit tangent in the edge’s direction at the fraction t of its length.

Parameters:  
**t** – the fraction of the length in \[0, 1\]: 0 the start, 1 the end.

Raises:  
**ValueError** – t is outside \[0, 1\].

Examples

``` python
edge.TangentAt(0)
edge.TangentAt(t=1)
```

Translated(*d: tuple\[float, float\]*, *\**, *id: str \| None = None*) → [Wire](geometry.md#planex.Wire "planex.Wire")\
Translated(*d: tuple\[float, float, float\]*, *\**, *id: str \| None = None*) → [Wire](geometry.md#planex.Wire "planex.Wire")  
The edge as a Wire of its own, moved by d: in the plane by (dh, dv), in 3d by (dx, dy, dz); the edge is not changed.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **d** – the displacement, (dh, dv) in the plane or (dx, dy, dz) in 3d.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
edge.Translated((10, 0))
edge.Translated((0, 0, 5))
```

Wire(*\**, *id: str \| None = None*) → [Wire](geometry.md#planex.Wire "planex.Wire")  
A copy of the edge as a Wire of its own, on its owner’s plane, with its properties.

Parameters:  
**id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Examples

``` python
edge.Wire()
```

*property* bounding_box  
tuple\[tuple\[float, float\], tuple\[float, float\]\]

Its bounding box in the plane, ((hmin, vmin), (hmax, vmax)).

Type:  
bounding_box

*property* center  
Vertex

The centre of an arc or circle; a line or spline raises ValueError.

Type:  
center

*property* col  
tuple\[float, float, float, float\] \| None

The colour (r, g, b, a), set as (r, g, b) or (r, g, b, a) of the edge; None if not set.

Type:  
col

*property* edge  
LineEdge \| ArcEdge \| CircleEdge \| SplineEdge

The edge’s geometry, a copy: a LineEdge, ArcEdge, CircleEdge or SplineEdge.

Type:  
edge

*property* end  
VertexView \| Vertex

The vertex at the end, with its id, its properties and its coordinates (h, v).

Type:  
end

*property* hpref  
float \| None

Netgen’s hpref of the edge; None if not set.

Type:  
hpref

*property* id  
str

The edge’s id: its object’s, then its own within it (Shape.3.Line.2).

Type:  
id

*property* layer  
int \| None

Netgen’s layer number of the edge; None if not set.

Type:  
layer

*property* mass  
float

The length of the edge.

Type:  
mass

*property* maxh  
float \| None

The largest mesh size (netgen’s maxh) of the edge; None if not set.

Type:  
maxh

*property* measures  
dict

The standard measurements and the stored ones, evaluated now, as a dict.

Type:  
measures

*property* midpoint  
Vertex

The point halfway along the edge.

Type:  
midpoint

*property* minh  
float \| None

The smallest mesh size (netgen’s minh) of the edge; None if not set.

Type:  
minh

*property* name  
str \| None

The name of the edge; None if not set.

Type:  
name

*property* partition  
list\[float\] \| None

The partition (the mesh points along an edge as increasing fractions of its length in (0, 1)) of the edge; None if not set.

Type:  
partition

*property* start  
VertexView \| Vertex

The vertex at the start, with its id, its properties and its coordinates (h, v).

Type:  
start

&nbsp;

*class* EdgesView  
The edges of a wire, face or shape, a list-like view: a property set here is set on all of them.

Copy()  
Copies of everything selected as one Compound; the object they were selected from stays as it is.

Faces become Faces, edges Wires with their properties, sketch elements their Wires.

Raises:  
**TypeError** – a vertex or point is selected (\[v.Copy() for v in selection\] copies vertices).

Examples

``` python
shape.edges(name="slot").Copy()
```

Max(*key*)  
The element with the largest value of key, as a selection of one (none if it is empty).

Parameters:  
**key** – as Sorted: a measure, a coordinate, an axis or a direction.

Examples

``` python
selection.Max("area")
selection.Max((1, 1))
```

Min(*key*)  
The element with the smallest value of key, as a selection of one (none if it is empty).

Parameters:  
**key** – as Sorted: a measure, a coordinate, an axis or a direction.

Examples

``` python
selection.Min("length")
selection.Min(px.x)
```

Sorted(*key*, *reverse=False*)  
The elements sorted by key, smallest first.

Parameters:  
- **key** – a measure (“length”, “area”, “radius”, or another key of Measure()), a coordinate of the centre (“h”, “v”, “x”, “y”, “z”, px.x, px.h, …), an axis (px.X, the sketch’s H or V, netgen’s X), or a direction: the centre’s component along (dh, dv) in the plane or (dx, dy, dz) in 3d.

- **reverse** – True sorts largest first.

Raises:  
**ValueError** – an element has no such measure.

Examples

``` python
selection.Sorted("length")
selection.Sorted(px.Y, reverse=True)
```

*property* col  
tuple\[float, float, float, float\] \| None

The colour (r, g, b, a), set as (r, g, b) or (r, g, b, a) all edges share, None if they differ; set on all edges.

Type:  
col

*property* hpref  
float \| None

Netgen’s hpref all edges share, None if they differ; set on all edges.

Type:  
hpref

*property* layer  
int \| None

Netgen’s layer number all edges share, None if they differ; set on all edges.

Type:  
layer

*property* maxh  
float \| None

The largest mesh size (netgen’s maxh) all edges share, None if they differ; set on all edges.

Type:  
maxh

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

&nbsp;

*class* FaceView  
A face of a shape, a px.Face for isinstance: its properties, edges and vertices are the shape’s, a change is made there; what only reads works on Copy(). It holds while the shape’s edges are unchanged.

Chamfer(*\*args*, *\*\*kwargs*)  
Refused: a face of a shape is not changed alone; Copy().Chamfer(…) changes a copy.

Parameters:  
- **args** – the arguments of Face.Chamfer.

- **kwargs** – its keyword arguments.

Raises:  
**TypeError** – always.

Examples

``` python
faceView.Copy().Chamfer(...)
```

Chamfered(*at: [VertexView](#planex.VertexView "planex.VertexView") \| Point \| str \| list\[[VertexView](#planex.VertexView "planex.VertexView") \| Point \| str\]*, *d: float*, */*, *d2: float \| None = None*, *angle: float \| None = None*, *reverse: bool = False*) → [Face](geometry.md#planex.Face "planex.Face")  
A copy with the corners chamfered as Chamfer does; this Face is not changed.

Parameters:  
- **at** – the corners: a vertex (face.vertices\[0\]), a point (h, v) on a vertex, a vertex name (every vertex with that name), or a list or selection of these; there is no default.

- **d** – the length on the first edge, a length \> 0; also on the second if d2 and angle are None.

- **d2** – the length on the second edge, a length \> 0; None: not given.

- **angle** – with d, the angle in degrees between the chamfer and the first edge, measured at its cut point towards the corner, between 0 and 180; None: not given. d2 and angle exclude each other.

- **reverse** – True makes the second edge the first.

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

CloseGaps(*tol: float*) → [Face](geometry.md#planex.Face "planex.Face")  
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

Copy(*\**, *id: str \| None = None*) → [Face](geometry.md#planex.Face "planex.Face")  
The face as a Face of its own, independent of the shape.

Parameters:  
**id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Examples

``` python
f = shape.faces[0].Copy()
```

Distance(*target: Point \| Point3 \| [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| [EdgeView](#planex.EdgeView "planex.EdgeView") \| [VertexView](#planex.VertexView "planex.VertexView") \| [FaceView](#planex.FaceView "planex.FaceView") \| TopoDS_Shape \| np.ndarray*, */*, *project: bool = False*, *components: str \| None = None*, *signed: bool = False*) → float \| tuple\[float, ...\] \| np.ndarray  
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

Fillet(*\*args*, *\*\*kwargs*)  
Refused: a face of a shape is not changed alone; Copy().Fillet(…) changes a copy.

Parameters:  
- **args** – the arguments of Face.Fillet.

- **kwargs** – its keyword arguments.

Raises:  
**TypeError** – always.

Examples

``` python
faceView.Copy().Fillet(...)
```

Filleted(*at: [VertexView](#planex.VertexView "planex.VertexView") \| Point \| str \| list\[[VertexView](#planex.VertexView "planex.VertexView") \| Point \| str\]*, *r: float*, */*) → [Face](geometry.md#planex.Face "planex.Face")  
A copy with the corners rounded as Fillet does; this Face is not changed.

Parameters:  
- **at** – the corners: a vertex (face.vertices\[0\]), a point (h, v) on a vertex, a vertex name (every vertex with that name), or a list or selection of these; there is no default.

- **r** – the radius, a length \> 0.

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

FitArcs(*\*args*, *\*\*kwargs*)  
Refused: a face of a shape is not changed alone; Copy().FitArcs(…) changes a copy.

Parameters:  
- **args** – the arguments of Face.FitArcs.

- **kwargs** – its keyword arguments.

Raises:  
**TypeError** – always.

Examples

``` python
faceView.Copy().FitArcs(...)
```

Identify(*\*args*, *\*\*kwargs*)  
Refused: a face of a shape is not changed alone; Copy().Identify(…) changes a copy.

Parameters:  
- **args** – the arguments of Face.Identify.

- **kwargs** – its keyword arguments.

Raises:  
**TypeError** – always.

Examples

``` python
faceView.Copy().Identify(...)
```

Intersections(*other: [Wire](geometry.md#planex.Wire "planex.Wire")*, */*) → list\[[Vertex](geometry.md#planex.Vertex "planex.Vertex")\]  
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

Measure(*name: str \| [FaceView](#planex.FaceView "planex.FaceView") \| [EdgeView](#planex.EdgeView "planex.EdgeView") \| None = None*, *to: str \| Point \| Point3 \| [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| None = None*, *project: bool = False*) → dict\
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

MergeCollinearEdges(*\*args*, *\*\*kwargs*)  
Refused: a face of a shape is not changed alone; Copy().MergeCollinearEdges(…) changes a copy.

Parameters:  
- **args** – the arguments of Face.MergeCollinearEdges.

- **kwargs** – its keyword arguments.

Raises:  
**TypeError** – always.

Examples

``` python
faceView.Copy().MergeCollinearEdges(...)
```

Mirror(*\*args*, *\*\*kwargs*)  
Refused: a face of a shape is not changed alone; Copy().Mirror(…) changes a copy.

Parameters:  
- **args** – the arguments of Face.Mirror.

- **kwargs** – its keyword arguments.

Raises:  
**TypeError** – always.

Examples

``` python
faceView.Copy().Mirror(...)
```

Mirrored(*point: Point*, */*, *dir: tuple\[float, float\]*) → [Face](geometry.md#planex.Face "planex.Face")\
Mirrored(*point: Point3*, */*, *normal: tuple\[float, float, float\]*) → [Face](geometry.md#planex.Face "planex.Face")  
A copy mirrored at the line through point along dir in its plane, or at the plane through point with normal in 3d; this Face is not changed.

In 3d its plane is mirrored and keeps a right-handed coordinate system, so its coordinates in the plane are mirrored at the plane’s first axis.

Parameters:  
- **point** – a point of the mirror line, (h, v), or of the mirror plane in 3d, (x, y, z).

- **dir** – the direction of the mirror line in the plane, (dh, dv).

- **normal** – the normal of the mirror plane in 3d, (dx, dy, dz).

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

Offset(*d: float*, *inplace: bool = True*) → [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape")  
Grows the faces by d: the band of width [\|d\|](#id1) along their boundaries added for d \> 0, taken away for d \< 0.

The band is Wire.Offset of their edges, round around convex corners; the faces keep their properties, the new edges those of the edges they come from.

Parameters:  
- **d** – the width of the band, a length; d \> 0 grows, d \< 0 shrinks, 0 changes nothing.

- **inplace** – True changes this Face and returns it; False returns the offset as a new Shape and leaves this one.

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

Plot(*\*args*, *\*\*kwargs*)  
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

Plot3d(*\*args*, *\*\*kwargs*)  
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

Project(*shape: TopoDS_Shape*, */*) → list\[[Wire](geometry.md#planex.Wire "planex.Wire")\]  
The edges of the OCC shape projected straight onto this plane, as a list of wires, as px.Project.

Parameters:  
**shape** – an OCC shape.

Examples

``` python
face.Project(box)
```

RemoveIdentification(*\*args*, *\*\*kwargs*)  
Refused: a face of a shape is not changed alone; Copy().RemoveIdentification(…) changes a copy.

Parameters:  
- **args** – the arguments of Face.RemoveIdentification.

- **kwargs** – its keyword arguments.

Raises:  
**TypeError** – always.

Examples

``` python
faceView.Copy().RemoveIdentification(...)
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

RemoveSmallEdges(*\*args*, *\*\*kwargs*)  
Refused: a face of a shape is not changed alone; Copy().RemoveSmallEdges(…) changes a copy.

Parameters:  
- **args** – the arguments of Face.RemoveSmallEdges.

- **kwargs** – its keyword arguments.

Raises:  
**TypeError** – always.

Examples

``` python
faceView.Copy().RemoveSmallEdges(...)
```

Rotate(*\*args*, *\*\*kwargs*)  
Refused: a face of a shape is not changed alone; Copy().Rotate(…) changes a copy.

Parameters:  
- **args** – the arguments of Face.Rotate.

- **kwargs** – its keyword arguments.

Raises:  
**TypeError** – always.

Examples

``` python
faceView.Copy().Rotate(...)
```

Rotated(*center: Point*, *angle: float*) → [Face](geometry.md#planex.Face "planex.Face")\
Rotated(*point: Point3*, */*, *dir: tuple\[float, float, float\]*, *angle: float*) → [Face](geometry.md#planex.Face "planex.Face")  
A copy rotated by angle around center in its plane, or around the axis through point along dir in 3d; this Face is not changed.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **center** – the centre of the rotation in the plane, (h, v).

- **angle** – the angle in degrees.

- **point** – a point of the axis in 3d, (x, y, z).

- **dir** – the direction of the axis in 3d, (dx, dy, dz).

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
face.Rotated((0, 0), 90)
face.Rotated((0, 0, 0), (0, 0, 1), 45)
```

Scale(*\*args*, *\*\*kwargs*)  
Refused: a face of a shape is not changed alone; Copy().Scale(…) changes a copy.

Parameters:  
- **args** – the arguments of Face.Scale.

- **kwargs** – its keyword arguments.

Raises:  
**TypeError** – always.

Examples

``` python
faceView.Copy().Scale(...)
```

Scaled(*center: Point*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*) → [Face](geometry.md#planex.Face "planex.Face")\
Scaled(*point: Point3*, */*, *factor: float*, *scale_maxh: bool = False*, *scale_minh: bool = False*) → [Face](geometry.md#planex.Face "planex.Face")  
A copy scaled by factor around center in its plane, or around point in 3d; this Face is not changed.

The mesh sizes maxh and minh stay unless asked for.

Parameters:  
- **center** – the centre of the scaling in the plane, (h, v).

- **point** – the centre of the scaling in 3d, (x, y, z).

- **factor** – the scale factor, \> 0.

- **scale_maxh** – True also scales maxh.

- **scale_minh** – True also scales minh.

Raises:  
**ValueError** – factor is not positive and finite, or a coordinate is not finite.

Examples

``` python
face.Scaled((0, 0), 2)
face.Scaled((0, 0), 0.5, scale_maxh=True)
```

Simplified(*fit_splines: bool \| dict \| None = None*, *remove_edges_smaller: float \| None = None*, *close_gaps_smaller: float \| None = None*, *remove_free_edges: bool \| None = None*, *merge_collinear_closer: float \| None = None*, *remove_faces_smaller: float \| None = None*, *merge_small_faces_instead: bool \| None = None*, *fit_arcs: bool \| dict \| None = None*) → [Face](geometry.md#planex.Face "planex.Face")  
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

Raises:  
**TypeError** – fit_splines or fit_arcs has another form or names an unknown option.

Examples

``` python
face.Simplified()
face.Simplified(fit_arcs=True, remove_edges_smaller=1e-6)
```

Simplify(*\*args*, *\*\*kwargs*)  
Refused: a face of a shape is not changed alone; Copy().Simplify(…) changes a copy.

Parameters:  
- **args** – the arguments of Face.Simplify.

- **kwargs** – its keyword arguments.

Raises:  
**TypeError** – always.

Examples

``` python
faceView.Copy().Simplify(...)
```

SmallEdges(*tol: float*) → list\[int\]  
The indices of the edges no longer than tol.

Parameters:  
**tol** – the largest length counted.

Examples

``` python
face.SmallEdges(1e-3)
```

Split(*cutter: [Wire](geometry.md#planex.Wire "planex.Wire") \| [EdgeView](#planex.EdgeView "planex.EdgeView") \| [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch") \| TopoDS_Shape*, */*) → list\[[Shape](geometry.md#planex.Shape "planex.Shape")\]  
Cuts every face and wire by the cutter into one Shape per piece.

What the cutter does not separate stays as it is; the pieces keep the face’s properties, the new edges the cutter’s.

Parameters:  
**cutter** – the edges that cut, a Wire, edge, Face, Shape, sketch or OCC shape; projected into the plane if it lies elsewhere.

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

ToSVG(*\*args*, *\*\*kwargs*)  
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

ToShapely(*\*args*, *\*\*kwargs*)  
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

Translate(*\*args*, *\*\*kwargs*)  
Refused: a face of a shape is not changed alone; Copy().Translate(…) changes a copy.

Parameters:  
- **args** – the arguments of Face.Translate.

- **kwargs** – its keyword arguments.

Raises:  
**TypeError** – always.

Examples

``` python
faceView.Copy().Translate(...)
```

Translated(*d: tuple\[float, float\]*) → [Face](geometry.md#planex.Face "planex.Face")\
Translated(*dh: float*, *dv: float*) → [Face](geometry.md#planex.Face "planex.Face")\
Translated(*d: tuple\[float, float, float\]*) → [Face](geometry.md#planex.Face "planex.Face")\
Translated(*dx: float*, *dy: float*, *dz: float*) → [Face](geometry.md#planex.Face "planex.Face")  
A copy moved by d: in the plane by (dh, dv), in 3d by (dx, dy, dz); this Face is not changed.

In 3d only its plane moves; its coordinates in the plane stay.

Parameters:  
- **d** – the displacement, (dh, dv) in the plane or (dx, dy, dz) in 3d.

- **dh** – the displacement along the plane’s first axis.

- **dv** – the displacement along the plane’s second axis.

- **dx** – the displacement along x in 3d.

- **dy** – the displacement along y in 3d.

- **dz** – the displacement along z in 3d.

Raises:  
**ValueError** – a value is not finite.

Examples

``` python
face.Translated((10, 0))
face.Translated(0, 0, 5)
```

Trim(*cutter: [Wire](geometry.md#planex.Wire "planex.Wire") \| [EdgeView](#planex.EdgeView "planex.EdgeView") \| [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch") \| TopoDS_Shape*, *\**, *keep_closest_to: Point \| Point3 \| None = None*, *remove_closest_to: Point \| Point3 \| None = None*, *keep_largest: bool = False*, *keep_smallest: bool = False*, *keep_left: bool = False*, *keep_right: bool = False*) → [Shape](geometry.md#planex.Shape "planex.Shape")  
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

Wire() → [Wire](geometry.md#planex.Wire "planex.Wire")  
The single loop of a face without holes.

Raises:  
**ValueError** – the face has holes; .wires gives the loops, .outer the outline.

Examples

``` python
face.Wire()
```

WriteBrep(*\*args*, *\*\*kwargs*)  
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

WriteDXF(*\*args*, *\*\*kwargs*)  
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

WriteGeo(*\*args*, *\*\*kwargs*)  
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

WriteStep(*\*args*, *\*\*kwargs*)  
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

The colour (r, g, b, a), set as (r, g, b) or (r, g, b, a) of this face; None if not set; set in the shape.

Type:  
col

*property* edges  
EdgesView

The shape’s edges of this face, a view: a change is made in the shape, on an edge shared with another face for that one too.

Type:  
edges

*property* faces  
list\[FaceView\]

A list of this face alone.

Type:  
faces

*property* hpref  
float \| None

Netgen’s hpref of this face; None if not set; set in the shape.

Type:  
hpref

*property* id  
str

The face’s id: its shape’s, then Face.k by its place in shape.faces.

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

Netgen’s layer number of this face; None if not set; set in the shape.

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

The largest mesh size (netgen’s maxh) of this face; None if not set; set in the shape.

Type:  
maxh

*property* measures  
dict

The standard measurements and the stored ones, evaluated now, as a dict.

Type:  
measures

*property* minh  
float \| None

The smallest mesh size (netgen’s minh) of this face; None if not set; set in the shape.

Type:  
minh

*property* name  
str \| None

The name of this face; None if not set; set in the shape.

Type:  
name

*property* outer  
Wire

The outer loop, a Wire, its edges in counterclockwise order.

Type:  
outer

*property* quad_dominated  
bool \| None

The quad_dominated flag (True: netgen meshes with mostly quadrilaterals) of this face; None if not set; set in the shape.

Type:  
quad_dominated

*property* splines  
list\[SplineEdge\]

The splines among the edges, copies.

Type:  
splines

*property* vertices  
VerticesView

The shape’s vertices of this face, each once, a view.

Type:  
vertices

*property* wires  
list\[Wire\]

The loops, the outline and the holes, a list of wires.

Type:  
wires

&nbsp;

*class* FacesView  
The faces of a shape, a list-like view: a property set here is set on all of them.

Copy()  
Copies of everything selected as one Compound; the object they were selected from stays as it is.

Faces become Faces, edges Wires with their properties, sketch elements their Wires.

Raises:  
**TypeError** – a vertex or point is selected (\[v.Copy() for v in selection\] copies vertices).

Examples

``` python
shape.edges(name="slot").Copy()
```

Max(*key*)  
The element with the largest value of key, as a selection of one (none if it is empty).

Parameters:  
**key** – as Sorted: a measure, a coordinate, an axis or a direction.

Examples

``` python
selection.Max("area")
selection.Max((1, 1))
```

Min(*key*)  
The element with the smallest value of key, as a selection of one (none if it is empty).

Parameters:  
**key** – as Sorted: a measure, a coordinate, an axis or a direction.

Examples

``` python
selection.Min("length")
selection.Min(px.x)
```

Sorted(*key*, *reverse=False*)  
The elements sorted by key, smallest first.

Parameters:  
- **key** – a measure (“length”, “area”, “radius”, or another key of Measure()), a coordinate of the centre (“h”, “v”, “x”, “y”, “z”, px.x, px.h, …), an axis (px.X, the sketch’s H or V, netgen’s X), or a direction: the centre’s component along (dh, dv) in the plane or (dx, dy, dz) in 3d.

- **reverse** – True sorts largest first.

Raises:  
**ValueError** – an element has no such measure.

Examples

``` python
selection.Sorted("length")
selection.Sorted(px.Y, reverse=True)
```

*property* col  
tuple\[float, float, float, float\] \| None

The colour (r, g, b, a), set as (r, g, b) or (r, g, b, a) all faces share, None if they differ; set on all faces.

Type:  
col

*property* hpref  
float \| None

Netgen’s hpref all faces share, None if they differ; set on all faces.

Type:  
hpref

*property* layer  
int \| None

Netgen’s layer number all faces share, None if they differ; set on all faces.

Type:  
layer

*property* maxh  
float \| None

The largest mesh size (netgen’s maxh) all faces share, None if they differ; set on all faces.

Type:  
maxh

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

&nbsp;

*class* VertexView  
A vertex of a wire, face or shape, where edges end; it acts as its point (h, v), and its properties go to the OCC vertex, e.g. hpref for refinement towards a corner.

Copy() → [Vertex](geometry.md#planex.Vertex "planex.Vertex")  
The vertex’s point as a Vertex; copy.copy and copy.deepcopy give the same.

Examples

``` python
p = vertex.Copy()
```

Distance(*target: Point \| Point3 \| [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| [EdgeView](#planex.EdgeView "planex.EdgeView") \| [VertexView](#planex.VertexView "planex.VertexView") \| [FaceView](#planex.FaceView "planex.FaceView") \| TopoDS_Shape \| np.ndarray*, */*, *project: bool = False*, *components: str \| None = None*, *signed: bool = False*) → float \| tuple\[float, ...\] \| np.ndarray  
The smallest distance from the vertex to the target.

A target in another plane is measured in 3d, or projected into this plane with project.

Parameters:  
- **target** – a point, (h, v) in the plane or (x, y, z) in 3d, a Face, Shape, Wire, edge, vertex, face of a shape or OCC shape; many points as an (n, 2) or (n, 3) array.

- **project** – True projects a target in another plane into this one.

- **components** – letters of ‘hvxyz’: the components of the shortest connection from here to the target instead of its length.

- **signed** – only False: a vertex has no inside.

Returns:  
a float; with components a tuple of them, one letter a float; many points an array (n,), with k components (n, k), one letter (n,).

Raises:  
- **ValueError** – signed=True; components has other letters.

- **TypeError** – the target has another form.

Examples

``` python
vertex.Distance((5, 0))
vertex.Distance(other, components="hv")
```

*property* angle  
float \| None

The angle at the vertex in degrees: inside the face where it bounds one (up to 360), else between its two edges; None unless two edges meet there.

Type:  
angle

*property* h  
float

Where it is along the plane’s first axis.

Type:  
h

*property* hpref  
float \| None

Netgen’s hpref of the vertex; None if not set.

Type:  
hpref

*property* id  
str

The vertex’s id: its object’s, then its own (Shape.3.Vertex.2, or the one set: vertex.id = “corner”), kept on the edge ends there like its name; the ends (Line.1.end) name it too.

Type:  
id

*property* maxh  
float \| None

The largest mesh size (netgen’s maxh) of the vertex; None if not set.

Type:  
maxh

*property* name  
str \| None

The name of the vertex; None if not set.

Type:  
name

*property* p  
Vertex

Its point in the plane, a Vertex.

Type:  
p

*property* radius  
float \| None

The smallest radius of the arcs ending at the vertex (a fillet’s), None if none does.

Type:  
radius

*property* v  
float

Where it is along the plane’s second axis.

Type:  
v

*property* x  
float

Its x coordinate in 3d.

Type:  
x

*property* y  
float

Its y coordinate in 3d.

Type:  
y

*property* z  
float

Its z coordinate in 3d.

Type:  
z

&nbsp;

*class* VerticesView  
The vertices of a wire, face or shape, a list-like view: a property set here is set on all of them.

Copy()  
Copies of everything selected as one Compound; the object they were selected from stays as it is.

Faces become Faces, edges Wires with their properties, sketch elements their Wires.

Raises:  
**TypeError** – a vertex or point is selected (\[v.Copy() for v in selection\] copies vertices).

Examples

``` python
shape.edges(name="slot").Copy()
```

Max(*key*)  
The element with the largest value of key, as a selection of one (none if it is empty).

Parameters:  
**key** – as Sorted: a measure, a coordinate, an axis or a direction.

Examples

``` python
selection.Max("area")
selection.Max((1, 1))
```

Min(*key*)  
The element with the smallest value of key, as a selection of one (none if it is empty).

Parameters:  
**key** – as Sorted: a measure, a coordinate, an axis or a direction.

Examples

``` python
selection.Min("length")
selection.Min(px.x)
```

Sorted(*key*, *reverse=False*)  
The elements sorted by key, smallest first.

Parameters:  
- **key** – a measure (“length”, “area”, “radius”, or another key of Measure()), a coordinate of the centre (“h”, “v”, “x”, “y”, “z”, px.x, px.h, …), an axis (px.X, the sketch’s H or V, netgen’s X), or a direction: the centre’s component along (dh, dv) in the plane or (dx, dy, dz) in 3d.

- **reverse** – True sorts largest first.

Raises:  
**ValueError** – an element has no such measure.

Examples

``` python
selection.Sorted("length")
selection.Sorted(px.Y, reverse=True)
```

*property* hpref  
float \| None

Netgen’s hpref all vertices share, None if they differ; set on all vertices.

Type:  
hpref

*property* maxh  
float \| None

The largest mesh size (netgen’s maxh) all vertices share, None if they differ; set on all vertices.

Type:  
maxh

*property* name  
str \| None

The name all vertices share, None if they differ; set on all vertices.

Type:  
name
