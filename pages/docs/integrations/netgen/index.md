# netgen and NGSolve


[![The potential flow around a cylinder in a channel, solved with NGSolve on the mesh of a planex2d face](index_files/index_1.png)<span>A potential flow on the mesh of a planex2d face</span>](../../integrations/netgen/meshing.md)
[![The temperature of a cold plate with four bores, a planex2d measurement solved by NGSolve](index_files/index_2.png)<span>A cold plate&#x27;s temperature as a measurement</span>](../../integrations/netgen/field-solvers.md)
[![An auxetic lattice tiled from a level set unit cell, extruded and loaded, its displacement in color](index_files/index_3.png)<span>An auxetic lattice from a level set, loaded</span>](../../integrations/netgen/levelset.md)


planex2d faces and shapes become netgen geometries in one call. The geometry reaches netgen through OpenCASCADE, in the
build that netgen ships as `netgen-occt`, so everything on the [OpenCASCADE](../occ/index.md) pages applies here too,
and every plane argument takes netgen's `occ.Axes` besides a `px.Plane`. Face names turn into materials, edge names into
boundary conditions, and vertex names into point names. `maxh` and `hpref` on faces, edges and vertices set the mesh
size and refine towards corners. Faces glued into a shape share their edges, so the regions of the mesh fit together
without gaps. Results come back onto the geometry: `px.PlotField` draws NGSolve and FEniCSx fields on the exact planex2d
geometry, also in cuts through 3D parts. A measurement can pass the part to netgen for meshing and to NGSolve for
solving, and `px.FromLevelSet` turns a level set function into a planex2d shape.

<p class="px-label">In this section</p>

## Getting started

- [Coming from netgen's WorkPlane](workplane.md)
- [Naming and marking](naming.md)
- [Identifications](identifications.md)


[![Coming from netgen&#x27;s WorkPlane](index_files/index_4.png)<b>Coming from netgen&#x27;s WorkPlane</b><span>Drawing code for netgen&#x27;s WorkPlane run unchanged on px.WorkPlane, or taken over with px.FromOCC.</span>](workplane.md)
[![Naming and marking](index_files/index_5.png)<b>Naming and marking</b><span>Names, mesh sizes and refinement set while drawing or afterwards, and how netgen reads them.</span>](naming.md)
[![Identifications](index_files/index_6.png)<b>Identifications</b><span>Periodic unit cells and rotational sectors, with the transformation found automatically.</span>](identifications.md)


## Meshing and fields

- [Meshing a face](meshing.md)
- [Fields and cuts](fields.md)
- [Solids and meshes in the webgui](webgui.md)
- [Field solvers as measurements](field-solvers.md)
- [From level sets](levelset.md)


[![Meshing a face](index_files/index_7.png)<b>Meshing a face</b><span>A named planex2d face meshed by netgen, with a potential flow, regions and corner refinement.</span>](meshing.md)
[![Fields and cuts](index_files/index_8.png)<b>Fields and cuts</b><span>Temperatures, fluxes, arrows and field lines on curved meshes and in cuts through 3D parts.</span>](fields.md)
[![Solids and meshes in the webgui](index_files/index_9.png)<b>Solids and meshes in the webgui</b><span>OpenCASCADE solids from planex2d faces and the meshes netgen makes of them.</span>](webgui.md)
[![Field solvers as measurements](index_files/index_10.png)<b>Field solvers as measurements</b><span>Peak temperature and stress concentration as numbers on a sketch, checked against closed forms.</span>](field-solvers.md)
[![From level sets](index_files/index_11.png)<b>From level sets</b><span>The region where a level set function is positive, as a planex2d shape, shown on an auxetic lattice.</span>](levelset.md)
