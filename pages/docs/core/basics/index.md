# Basics


[![A toy airliner built from sketches on planes in 3D, with NACA spline wings, lofted through its sections](index_files/index_1.png)<span><em class="px-tag">Example</em> A toy plane from sketches on planes in 3D</span>](../../examples/toy-plane.md)
[![A brake disc perforated by 24 copies of one hole](index_files/index_2.png)<span>A brake disc: one hole, 24 copies</span>](../../core/basics/motions.md)
[![A coating 0.5 thick offset from a NACA 0012 airfoil spline, its trailing edge enlarged](index_files/index_3.png)<span>A coating offset from an airfoil spline</span>](../../core/basics/splines.md)


planex2d has a small set of geometry objects, each with clearly defined properties. Edges can be lines, circular arcs
or full circles, or splines. Lines and circular edges use exact geometry.

A wire is one chain of edges, open or closed, that does not cross, overlap or branch. A figure eight, for example,
consists of two wires, and so does a T. A face is one region bounded by an outer loop and any holes, with no
overlapping regions, free edges or zero-length edges. A shape groups faces and wires that have been cut against each
other, and faces that meet share their edges. A compound is a simple collection of objects.

Every wire, face and shape lies in a plane that can be positioned anywhere in 3D. Names, colors and mesh sizes travel
with the geometry through Boolean operations and export. Geometry has no built-in unit. Lines and arcs are exact, and
rounding matters only where two independent computations have to agree. See [Objects in planex2d](objects.md) for an
overview of the geometry model.

<p class="px-label">In this section</p>

## Objects

- [Objects in planex2d](objects.md)
- [Lines and arcs](edges.md)
- [Splines](splines.md)
- [Wires and faces](wires-and-faces.md)
- [Faces, shapes and compounds](faces-shapes-compounds.md)


[![Objects in planex2d](index_files/index_4.png)<b>Objects in planex2d</b><span>The geometry objects, their guarantees and how to construct them.</span>](objects.md)
[![Lines and arcs](index_files/index_5.png)<b>Lines and arcs</b><span>Lines, circular arcs and full circles, with shared vertices and edge data.</span>](edges.md)
[![Splines](index_files/index_6.png)<b>Splines</b><span>Smooth curves in Boolean operations and offsets, from airfoils to cams.</span>](splines.md)
[![Wires and faces](index_files/index_7.png)<b>Wires and faces</b><span>Building wires and faces from edges, with holes determined by nesting.</span>](wires-and-faces.md)
[![Faces, shapes and compounds](index_files/index_8.png)<b>Faces, shapes and compounds</b><span>Boolean results, regions built from wires and faces that share edges.</span>](faces-shapes-compounds.md)


## In space

- [The plane in 3D](plane.md)
- [Moving, rotating, mirroring, scaling](motions.md)


[![The plane in 3D](index_files/index_9.png)<b>The plane in 3D</b><span>Working on any plane, including a solid face or another object&#x27;s plane.</span>](plane.md)
[![Moving, rotating, mirroring, scaling](index_files/index_10.png)<b>Moving, rotating, mirroring, scaling</b><span>Transformations, symmetry and patterns, from a T-slot nut to a brake disc.</span>](motions.md)


## Data

- [Names and colors](properties.md)
- [Units](units.md)
- [Tolerances](tolerances.md)


[![Names and colors](index_files/index_11.png)<b>Names and colors</b><span>Names, colors and mesh sizes on edges, faces and vertices, including their behaviour in Boolean operations.</span>](properties.md)
[![Units](index_files/index_12.png)<b>Units</b><span>Geometry has no built-in unit; learn how to interpret lengths and imported files.</span>](units.md)
[![Tolerances](index_files/index_13.png)<b>Tolerances</b><span>Where geometric calculations are exact and where floating-point rounding matters.</span>](tolerances.md)


## 3D

- [Pad and Pocket](pad-pocket.md)


[![Pad and Pocket](index_files/index_14.png)<b>Pad and Pocket</b><span>Solids from faces and sketches, extruded and cut with OpenCASCADE, named after the profile.</span>](pad-pocket.md)
