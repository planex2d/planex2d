# Plotting

Drawing planex2d objects, fields and meshes with matplotlib and plotly, and in 3D. See [Plotting](../core/plotting/index.md) for the guide.

Plot(*obj: [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| [Compound](geometry.md#planex.Compound "planex.Compound") \| [Sketch](sketches.md#planex.Sketch "planex.Sketch") \| [QuickSketch](sketches.md#planex.QuickSketch "planex.QuickSketch") \| str \| Any*, *plane: [Plane](geometry.md#planex.Plane "planex.Plane") \| None = None*, */*, *ax: Any = None*, *line_col: Color = 'black'*, *arc_col: Color = '#ff7f0e'*, *spline_col: Color = '#2ca02c'*, *face_col: Color \| None = '#dde6f0'*, *mark_edges_smaller: float \| None = None*, *mark_free_edges: bool = False*, *highlight: Ids \| Any = None*, *legend: bool = True*, *show_constraints: bool \| None = None*, *show_ends: bool = False*, *show_direction: bool = False*, *show_parent: bool \| None = None*, *backend: str \| None = None*) → Any  
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

&nbsp;

Plot3d(*objects: Any*, */*, *plane_size: float \| None = None*, *backend: str = 'webgpu'*, *\*\*kwargs: Any*) → Any  
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

&nbsp;

PlotField(*field: Any*, *mesh: Any = None*, *obj: Any = None*, */*, *on: str = 'faces'*, *ax: Any = None*, *backend: str \| None = None*, *samples: int \| None = None*, *vectors: bool = False*, *min: float \| None = None*, *max: float \| None = None*, *contours: int \| list\[float\] \| None = None*, *field_lines: bool \| float = False*, *colormap: str \| Any = 'default'*, *legend: bool = True*) → Any  
Draws a field over a planex object with matplotlib or plotly.

An NGSolve or dolfinx field is evaluated at the centres of square pixels; pixels the mesh does not cover stay empty (NGSolve’s 2d search still counts a point up to about 1e-6 of an element’s size outside it); a vector or complex field shows its magnitude. A 2d mesh needs the object on the XY plane; on a 3d mesh (with volume elements) every pixel is evaluated at its 3d point on the object’s plane and a vector field is coloured by its full length. A measurement is called on obj with plot=True and samples=samples; its values colour the faces (interpolated between its points) or the edges, with a colour bar, its value in the title and its marks.

Parameters:  
- **field** – an NGSolve GridFunction or CoefficientFunction, a dolfinx.fem.Function (scalar or vector, real or complex; interpolate a UFL expression into one first), or a measurement that can be drawn (planex.measurements, e.g. MinThickness()).

- **mesh** – the ngsolve.Mesh or dolfinx mesh to evaluate on; a GridFunction or dolfinx Function brings its own, a CoefficientFunction needs one; a dolfinx mesh must not be distributed over MPI processes. Left out, the second positional argument is obj.

- **obj** – the region: a 2d planex object (the pixels inside its faces are evaluated, its outlines drawn over the colours), an OCC shape flat in its own plane, or a plane (px.Plane, netgen Axes) whose cut of the mesh’s OCC geometry is the region; None: the XY cut of a 3d mesh’s geometry, a 2d mesh’s geometry itself, or a mesh without an OCC geometry (dolfinx, read from a file) with its boundary as the outline.

- **on** – “faces” or “edges”: what a measurement colours; NGSolve and dolfinx fields colour the faces.

- **ax** – as px.Plot.

- **backend** – as px.Plot.

- **samples** – the pixels along the longer side of the region’s box (default 400); for a measurement its samples.

- **vectors** – True adds the arrows of a real vector field (its part in the plane) on a coarse grid.

- **min** – the low end of the colour range; None takes it from the values.

- **max** – the high end of the colour range; None takes it from the values.

- **contours** – thin dark lines over the colours of the faces: a number n of levels evenly spaced inside the colour range, or a list of levels.

- **field_lines** – True or a density (as matplotlib’s streamplot: 2 is twice as dense) draws the field lines of a real vector field with small arrowheads.

- **colormap** – “default” (planex’s map, low values red to high values blue; “default_r” the other way round), a matplotlib colormap or its name (“viridis”, “coolwarm_r”), or a list of colours.

- **legend** – True shows the legend.

Returns:  
the matplotlib Axes or the plotly Figure.

Raises:  
- **TypeError** – mesh and obj are given and mesh is no mesh; a measurement that does not take plot=True.

- **ValueError** – on is neither “faces” nor “edges”, min \> max, a measurement with a mesh, without obj or with vectors, on=”edges” with a field, vectors or field_lines with a complex field or one of the wrong number of components, a 2d mesh with an object off the XY plane, or contours, field_lines or colormap of another form.

Examples

``` python
px.PlotField(gf, face, contours=10)
px.PlotField(MinThickness(), shape, on="edges")
```

&nbsp;

PlotMesh(*mesh: Any*, *obj: Any = None*, */*, *ax: Any = None*, *backend: str \| None = None*, *legend: bool = True*) → Any  
Draws the element edges of an NGSolve or dolfinx mesh with matplotlib or plotly.

A 2d mesh is drawn as it is (curved on a curved NGSolve mesh, straight on a dolfinx one), a 3d one where the object’s plane cuts its elements (curved elements cut straight).

Parameters:  
- **mesh** – an ngsolve.Mesh or a dolfinx mesh.

- **obj** – a planex object (its outlines drawn under the mesh; with a 2d mesh on the XY plane), an OCC shape flat in its own plane, or a plane (px.Plane, netgen Axes) with its cut of the mesh’s OCC geometry; None as px.PlotField.

- **ax** – as px.Plot.

- **backend** – as px.Plot.

- **legend** – True shows the legend.

Returns:  
the matplotlib Axes or the plotly Figure.

Raises:  
- **TypeError** – mesh is no mesh.

- **ValueError** – the mesh is not 2d or 3d with volume elements, or a 2d mesh with an object off the XY plane.

Examples

``` python
px.PlotMesh(mesh)
px.PlotMesh(mesh, px.Plane(normal=(0, 1, 0)), backend="plotly")
```
