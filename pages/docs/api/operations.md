# Boolean operations and transformations

Operations on lists of parts and the transformations. The operators `+`, `-` and `*` and the methods of the objects are listed with their classes. See [Boolean operations](../core/booleans/index.md) for the guide.

Unify(*parts: list\[[Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| [Compound](geometry.md#planex.Compound "planex.Compound")\]*, */*, *multithreaded: bool = True*, *\**, *id: str \| None = None*) → [Shape](geometry.md#planex.Shape "planex.Shape")  
The union of all faces and shapes, or of all wires (their connected chains), as a balanced tree of pairwise unions.

A region has the properties of the faces it holds, merged in list order; compounds give their parts.

Parameters:  
- **parts** – faces, shapes, wires and compounds.

- **multithreaded** – True runs the pairwise unions in parallel.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**TypeError** – an item is not a Face, Shape, Wire or Compound.

Examples

``` python
px.Unify([a, b, c])
px.Unify(parts, multithreaded=False)
```

&nbsp;

Subtract(*parts: list\[[Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Wire](geometry.md#planex.Wire "planex.Wire")\]*, */*, *multithreaded: bool = True*, *\**, *id: str \| None = None*) → [Shape](geometry.md#planex.Shape "planex.Shape")  
The first part minus the union of all others; every region keeps the properties of the first one’s face.

Parameters:  
- **parts** – faces, shapes and wires, the first the one subtracted from.

- **multithreaded** – True runs the pairwise operations in parallel.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**TypeError** – an item is not a Face, Shape or Wire (a Compound: px.Unify merges it first).

Examples

``` python
px.Subtract([plate, hole])
px.Subtract([plate, *holes], multithreaded=False)
```

&nbsp;

Intersect(*parts: list\[[Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Wire](geometry.md#planex.Wire "planex.Wire")\]*, */*, *multithreaded: bool = True*, *\**, *id: str \| None = None*) → [Shape](geometry.md#planex.Shape "planex.Shape")  
The intersection of all faces and shapes, as a balanced tree of pairwise intersections.

Every region keeps the first one’s properties, maxh and minh the smallest of the faces it lies in.

Parameters:  
- **parts** – faces, shapes and wires.

- **multithreaded** – True runs the pairwise intersections in parallel.

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**TypeError** – an item is not a Face, Shape or Wire (a Compound: px.Unify merges it first).

Examples

``` python
px.Intersect([a, b])
px.Intersect(parts, multithreaded=False)
```

&nbsp;

Glue(*parts: list\[[Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [Wire](geometry.md#planex.Wire "planex.Wire") \| [Compound](geometry.md#planex.Compound "planex.Compound")\]*, */*, *\**, *id: str \| None = None*) → [Shape](geometry.md#planex.Shape "planex.Shape")  
The parts as one shape, as netgen’s occ.Glue: everything cut against everything, every area covered by a different set of faces a face of its own (A - B, A \* B, B - A).

The faces’ properties merge in list order, the first winning an overlap; a part lying entirely inside another keeps its own region and properties; touching faces share their edge; a wire through a face splits it into two faces with its properties, a wire piece inside a face that separates nothing stays an internal edge of it (the mesh follows it). Where parts share an edge their edge properties merge the same way.

Parameters:  
- **parts** – faces, shapes, wires and compounds (their contents).

- **id** – the id of what it returns (of a list’s items id.1, id.2, …, of a dict’s id.key); default the kind that made it and a number, given when first asked for.

Raises:  
**TypeError** – an item is not a Face, Shape, Wire or Compound.

Examples

``` python
px.Glue([air, iron])
px.Glue([plate, px.Line((1, -1), (1, 3))])
```

&nbsp;

Translation(*dh: float*, *dv: float*) → Transformation\
Translation(*d: tuple\[float, float\]*) → Transformation  
The move by (dh, dv) in the plane.

Parameters:  
- **dh** – the move along h.

- **dv** – the move along v.

- **d** – the move (dh, dv).

Examples

``` python
px.Translation(10, 0)
px.Translation((10, 0))
```

&nbsp;

Rotation(*angle: float*, *center: Point = (0, 0)*) → Transformation  
The turn by angle counterclockwise about center in the plane.

Parameters:  
- **angle** – the angle in degrees.

- **center** – the centre (h, v).

Examples

``` python
px.Rotation(90)
px.Rotation(45, center=(5, 0))
```

&nbsp;

GetTransformation(*a: [Wire](geometry.md#planex.Wire "planex.Wire") \| [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView") \| [EdgesView](selections.md#planex.EdgesView "planex.EdgesView") \| [Selection](selections.md#planex.Selection "planex.Selection") \| [LineEdge](geometry.md#planex.LineEdge "planex.LineEdge") \| [ArcEdge](geometry.md#planex.ArcEdge "planex.ArcEdge") \| list*, *b: [Wire](geometry.md#planex.Wire "planex.Wire") \| [Face](geometry.md#planex.Face "planex.Face") \| [Shape](geometry.md#planex.Shape "planex.Shape") \| [EdgeView](selections.md#planex.EdgeView "planex.EdgeView") \| [EdgesView](selections.md#planex.EdgesView "planex.EdgesView") \| [Selection](selections.md#planex.Selection "planex.Selection") \| [LineEdge](geometry.md#planex.LineEdge "planex.LineEdge") \| [ArcEdge](geometry.md#planex.ArcEdge "planex.ArcEdge") \| list*, */*, *reverse: bool = False*) → Transformation  
The map of the plane taking the chain of lines and arcs a onto b, vertex onto vertex.

A straight chain, a single arc or a circle maps by a similarity (turn, uniform scale, move), other chains by the affine map of the vertices; every vertex and edge midpoint must land. Of the possible pairings it takes the one closest to a translation (orientation kept, the smallest turn).

Parameters:  
- **a** – the edges mapped: a wire, face, shape, edge view, edges view, selection, LineEdge, ArcEdge, or a list of them.

- **b** – the edges a lands on, in the same forms.

- **reverse** – True takes the best pairing running the other way.

Raises:  
- **ValueError** – a or b has no edges, has a spline, branches or is not one connected chain; they have different numbers of vertices; no map takes a onto b.

- **TypeError** – a or b has no edges to take.

Examples

``` python
px.GetTransformation(face.edges[0], face.edges[2])
px.GetTransformation(left, right, reverse=True)
```
