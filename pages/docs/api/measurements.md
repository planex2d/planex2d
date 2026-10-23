# Measurements

The analysis functions of `planex.measurements`. The measurements of every object, such as `mass`, `center` and `Measure()`, are listed with their classes. See [Measuring](../core/measuring/index.md) for the guide.

measurements to call or store: obj.Measure(f), obj.Measure(f, id=”gap”) and obj.measures.

A measurement is a callable f(obj, \*\*params) returning its value. One that can be drawn also takes plot=True (and samples=n) and then returns {“points”: \[(h, v), …\], “values”: \[…\], “label”: …, “value”: …, “marks”: \[…\]} for px.PlotField(f, obj); with “discs”: \[(h, v, r, value), …\] a point inside takes the largest value of the discs covering it instead of the interpolation.

Clearance(*a: str \| Callable \| Any*, *b: str \| Callable \| Any*) → Callable  
A measurement: the smallest distance between the elements a and b of the object.

Parameters:  
- **a** – the first elements: an id, a name of edges or faces, a callable of the object returning them, or elements.

- **b** – the second elements, as a.

Returns:  
the measurement f(obj), for obj.Measure(f).

Examples

``` python
shape.Measure(px.measurements.Clearance("pad", "trace"))
sk.Measure(px.measurements.Clearance("Circle.1", "Line.4"), id="gap")
```

&nbsp;

Hausdorff(*a: str \| Callable \| Any*, *b: str \| Callable \| Any*, */*, *samples: int = 64*) → Callable  
A measurement: the Hausdorff distance between the elements a and b of the object.

The Hausdorff distance is the largest distance from a point of a to b or of b to a: how far apart two curves are at their worst.

Parameters:  
- **a** – the first elements: an id, a name of edges or faces, a callable of the object returning them, or elements.

- **b** – the second elements, as a.

- **samples** – the samples per edge before the search for the largest distance refines.

Returns:  
the measurement f(obj), for obj.Measure(f).

Examples

``` python
shape.Measure(px.measurements.Hausdorff("Line.1", "Arc.2"))
sk.Measure(px.measurements.Hausdorff("inner", "outer", samples=128), id="gap")
```

&nbsp;

HydraulicDiameter(*\*args: Any*) → Callable  
A measurement: the hydraulic diameter 4 A / P, four times the area over the perimeter (every edge wetted).

Returns:  
the measurement f(obj), for obj.Measure(f).

Raises:  
**TypeError** – given an object: the measurement is HydraulicDiameter(), with brackets.

Examples

``` python
face.Measure(px.measurements.HydraulicDiameter())
px.measurements.HydraulicDiameter()(face)
```

&nbsp;

MaxThickness(*method: str = 'ball'*, *samples: int = 400*, *ignore_corners: float \| None = None*) → Callable  
A measurement: the thickest wall of the faces, as MinThickness; px.PlotField draws it.

Parameters:  
- **method** – “ball” (the largest disc inside) or “ray” (along the inward normal), as MinThickness.

- **samples** – the number of samples along the boundary.

- **ignore_corners** – leaves out where a convex corner sharper than this many degrees pinches the disc or ray.

Returns:  
the measurement f(obj, plot=False, samples=samples), for obj.Measure(f) and px.PlotField(f, obj).

Raises:  
**ValueError** – method is neither “ball” nor “ray”.

Examples

``` python
face.Measure(px.measurements.MaxThickness())
shape.Measure(px.measurements.MaxThickness(samples=1000), id="thickest")
```

&nbsp;

MinThickness(*method: str = 'ball'*, *samples: int = 400*, *ignore_corners: float \| None = None*) → Callable  
A measurement: the thinnest wall of the faces; px.PlotField draws it.

A sharp corner is infinitely thin: without ignore_corners a warning says when the minimum is such a corner’s tip and which value leaves it out.

Parameters:  
- **method** – “ball”: at each sample the diameter of the largest disc inside that touches the boundary there; “ray”: the distance along the inward normal to the other side, which never finds less than the disc and can miss thin spots at tapers.

- **samples** – the number of samples along the boundary.

- **ignore_corners** – leaves out where a convex corner sharper than this many degrees pinches the disc or ray.

Returns:  
the measurement f(obj, plot=False, samples=samples), for obj.Measure(f) and px.PlotField(f, obj); it raises ValueError when no sample finds a wall.

Raises:  
**ValueError** – method is neither “ball” nor “ray”.

Examples

``` python
face.Measure(px.measurements.MinThickness())
px.PlotField(px.measurements.MinThickness(method="ray", ignore_corners=60), shape)
```

&nbsp;

TorsionConstant(*order: int = 3*, *maxh: float \| None = None*) → Callable  
A measurement: the St. Venant torsion constant J of the faces, a cross-section in the xy plane.

J is computed by NGSolve from the warping function w: J = integral of x^2 + y^2 + x dw/dy - y dw/dx about the centroid, with laplace(w) = 0 and dw/dn = y n_x - x n_y on every boundary (holes included). The measurement raises ImportError without NGSolve.

Parameters:  
- **order** – the polynomial order of the finite elements and of the curved mesh.

- **maxh** – the mesh size; None takes a twentieth of the larger side of the bounding box.

Returns:  
the measurement f(obj), for obj.Measure(f).

Examples

``` python
face.Measure(px.measurements.TorsionConstant())
face.Measure(px.measurements.TorsionConstant(order=4, maxh=0.001))
```
