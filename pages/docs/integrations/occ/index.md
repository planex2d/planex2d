# OpenCASCADE


[![NACA sections drawn by planex2d on planes along the span of a wing, before the loft](index_files/index_1.png)<span>Airfoil sections on their planes in space</span>](../../integrations/occ/loft.md)
[![The wing with winglets lofted through the planex2d sections in OpenCASCADE, mirrored](index_files/index_2.png)<span>The wing lofted through them</span>](../../integrations/occ/loft.md)
[![A C-clamp frame extruded from a planex2d sketch, with its screw hole and anvil pad](index_files/index_3.png)<span>A C-clamp from one sketch</span>](../../integrations/occ/clamp.md)
[![A unit cube with a square tunnel along (2, 1, 2), wide enough for a cube of side 1.04](index_files/index_4.png)<span><em class="px-tag">Example</em> Prince Rupert&#x27;s cube: a cube through a cube</span>](../../examples/prince-ruperts-cube.md)


A planex2d object lies on a plane in 3D, so its faces and wires become OpenCASCADE shapes as they are: exact lines,
arcs, full circles and B-splines with shared vertices. They can be extruded, revolved and lofted into solids or cut
from solids, and a sketch can be drawn on the face of a solid with the solid's edges projected into it. Names, colors
and mesh sizes travel with the edges and faces into the solid, where netgen reads them as boundaries, materials and
mesh sizes. The export can clean up the geometry on the way out, fit splines, and write arcs for tools that accept
nothing else. It needs netgen's OpenCASCADE (`pip install netgen-mesher`). Planes are `px.Plane` objects on these
pages; every plane argument also takes netgen's `occ.Axes`, so existing netgen code can pass its axes unchanged (see
[API conventions](../../core/conventions.md)).

<p class="px-label">In this section</p>

## Export

- [To OCC shapes](export.md)
- [Export options](export-options.md)
- [Splines as arcs](splines-as-arcs.md)


[![To OCC shapes](index_files/index_5.png)<b>To OCC shapes</b><span>Faces, wires and sketches as OpenCASCADE shapes, with names and colors kept.</span>](export.md)
[![Export options](index_files/index_6.png)<b>Export options</b><span>Cleanup on the way out, with fitted splines, joined lines and open wires.</span>](export-options.md)
[![Splines as arcs](index_files/index_7.png)<b>Splines as arcs</b><span>The chain of arcs planex2d computes with, for tools that accept only lines and arcs.</span>](splines-as-arcs.md)


## Modeling

- [A C-clamp, step by step](clamp.md)
- [Sketching on and projecting 3D geometry](projecting.md)
- [Lofting sections](loft.md)
- [Lofts with guide curves](guided-loft.md)


[![A C-clamp, step by step](index_files/index_8.png)<b>A C-clamp, step by step</b><span>A profile drawn with the pen, followed by the frame, screw hole, anvil pad, mesh and STEP file.</span>](clamp.md)
[![Sketching on and projecting 3D geometry](index_files/index_9.png)<b>Sketching on and projecting 3D geometry</b><span>A sketch on a face of a solid, with edges projected as targets for a marking groove.</span>](projecting.md)
[![Lofting sections](index_files/index_10.png)<b>Lofting sections</b><span>NACA sections on planes along the span, lofted into a wing.</span>](loft.md)
[![Lofts with guide curves](index_files/index_11.png)<b>Lofts with guide curves</b><span>Sections cut from guide curves, with a plain and a guided loft.</span>](guided-loft.md)


## Everything at a glance

| | |
|---|---|
| **Export** | `ToOCCShape()`, `ToOCCFace()`, `ToOCCWire()`, `ToOCCWires()`, with the cleanup options of `Simplify` (see [Export options](export-options.md)) |
| **Solids** | `px.Pad(profile, length)` and `px.Pocket(target, profile, length)`, with `length2`, `dir`, `reverse` and `until`; OpenCASCADE's own operations on the exported shapes, such as `Extrude` and `occ.ThruSections` |
| **Sketching on solids** | `px.QuickSketch(face)` and `px.Sketch(face)` on a planar face, `sk.Project(edge)` for its edges as references (see [Placing a sketch](../../core/sketch/placement.md)) |
| **Back into planex2d** | `px.FromOCC(shape or file)`, `px.Project(shape, plane)`, `px.Intersect3d(plane, shape)` |
| **Drawing** | `px.Plot3d(objects)` in 3D, `px.Plot(shape, plane)` for a section |
| **Meshing** | `px.OCCGeometry(face)` for netgen (see [netgen and NGSolve](../netgen/index.md)) |
