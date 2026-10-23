# Experimental

Experimental parts, proofs of concept. See [Deformable shapes](../experimental/deformable/index.md) for the guide.

*class* DeformableShape(*shape: [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape")*, *h: float*, *min_distance: float \| None = None*, *clean: bool = False*, *topology_changed_callback: Callable\[\[[DeformableShape](#planex.DeformableShape "planex.DeformableShape")\], Any\] \| None = None*)  
A face as dense closed polylines whose vertices follow displacement arrays or NGSolve GridFunctions.

Every edge is sampled at most h apart; outer loops run counterclockwise, holes clockwise. Boundary parts that move closer than min_distance to each other are cut out.

Parameters:  
- **shape** – the Face or Shape whose faces are sampled.

- **h** – the largest segment length.

- **min_distance** – the smallest distance between boundary parts; None takes h.

- **clean** – True cleans the given shape of parts closer than min_distance right away.

- **topology_changed_callback** – called with the DeformableShape whenever SetDeformation, Clean or Defeature changes its loops.

Examples

``` python
px.DeformableShape(face, 0.01)
px.DeformableShape(shape, 0.01, min_distance=0.005, topology_changed_callback=print)
```

Clean() → bool  
Cuts the loops against each other and keeps the boundary of the material without parts too close.

The material is where the winding number is 1 or more; parts closer than min_distance to each other are removed. If anything changed, the current vertices become the reference.

Returns:  
True if anything changed.

Examples

``` python
deformableShape.Clean()
```

Commit() → [DeformableShape](#planex.DeformableShape "planex.DeformableShape")  
Makes the current vertices the reference.

Returns:  
this DeformableShape.

Examples

``` python
shape.Commit()
```

Copy() → [DeformableShape](#planex.DeformableShape "planex.DeformableShape")  
An independent copy; copy.copy and copy.deepcopy give the same.

Examples

``` python
b = a.Copy()
```

Defeature() → bool  
Removes boundary vertices closer than min_distance to another part of the boundary and joins the loose ends.

The loose ends are joined by straight segments, nearest first.

Returns:  
True if anything changed.

Examples

``` python
deformableShape.Defeature()
```

IsValid() → bool  
True if the polylines do not intersect themselves or each other.

Examples

``` python
shape.IsValid()
```

Plot(*plane: [Plane](geometry.md#planex.Plane "planex.Plane") \| None = None*, */*, *ax: Any = None*, *line_col: Color = 'black'*, *arc_col: Color = '#ff7f0e'*, *spline_col: Color = '#2ca02c'*, *face_col: Color \| None = '#dde6f0'*, *mark_edges_smaller: float \| None = None*, *mark_free_edges: bool = False*, *highlight: Ids \| Any = None*, *legend: bool = True*, *show_constraints: bool \| None = None*, *show_ends: bool = False*, *show_direction: bool = False*, *show_parent: bool \| None = None*, *backend: str \| None = None*) → Any  
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

ReferenceVertices() → np.ndarray  
The vertices the displacements refer to, an (n, 2) array.

Examples

``` python
shape.ReferenceVertices()
```

Refine() → [DeformableShape](#planex.DeformableShape "planex.DeformableShape")  
Halves the segments longer than h and drops interior vertices closer than h/4.

Returns:  
this DeformableShape.

Examples

``` python
shape.Refine()
```

RingSizes() → list\[int\]  
The number of vertices of every loop, in the order of Vertices().

Examples

``` python
shape.RingSizes()
```

SetDeformation(*d: ndarray \| Any*, *commit: bool = True*) → bool  
Moves the vertices to the reference vertices plus displacements, then refines and cleans the loops.

With commit the result becomes the new reference; without, the reference stays and the next call replaces the deformation (like mesh.SetDeformation) until the topology changes, which always commits.

Parameters:  
- **d** – the displacements, shape (n, 2), or a vector GridFunction on a mesh of the reference shape, evaluated on the mesh boundary at the reference vertices.

- **commit** – True makes the result the new reference.

Returns:  
True if the topology changed.

Raises:  
**ValueError** – d is an array not of shape (n, 2).

Examples

``` python
deformableShape.SetDeformation([(0, 0)] * len(deformableShape))
deformableShape.SetDeformation(gfu, commit=False)
```

Shape() → [Shape](geometry.md#planex.Shape "planex.Shape")  
The current outline as a Shape, one edge per original edge: a line where its points are collinear, else the spline through them.

Examples

``` python
shape.Shape()
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

VertexNames() → list\[str\]  
The name of the original edge every vertex lies on, in the order of Vertices().

Examples

``` python
shape.VertexNames()
```

Vertices() → np.ndarray  
The current vertices, an (n, 2) array.

Examples

``` python
shape.Vertices()
```

Wires() → list\[[Wire](geometry.md#planex.Wire "planex.Wire")\]  
The current polylines as wires of lines with the original edges’ properties.

Examples

``` python
shape.Wires()
```

*property* bounding_box  
tuple\[tuple\[float, float\], tuple\[float, float\]\]

Its bounding box in the plane, ((hmin, vmin), (hmax, vmax)).

Type:  
bounding_box

*property* h  
float

The largest segment length.

Type:  
h

*property* mass  
float

The area.

Type:  
mass

*property* min_distance  
float

The smallest distance between boundary parts.

Type:  
min_distance
