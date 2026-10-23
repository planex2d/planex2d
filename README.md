<p align="center">
  <img src="https://planex2d.org/docs/_static/brand/planex2d-mark.svg" alt="" width="96"><br>
  <img src="https://planex2d.org/docs/_static/brand/planex2d-wordmark.svg" alt="planex2d" width="300">
</p>

<p align="center">
  <b>Fast, exact 2D geometry for Python</b><br>
  <a href="https://planex2d.org/docs/">Documentation</a> ·
  <a href="https://planex2d.org/docs/core/installation.html">Installation</a> ·
  <a href="https://planex2d.org/docs/examples/index.html">Examples</a> ·
  <a href="https://planex2d.org/docs/core/notices.html">License and notices</a>
</p>

planex2d is free software under the GNU Lesser General Public License, version 2.1 or (at your option) any later
version. Its compiled module contains code from Eigen, pybind11 and Taskflow, and it is linked to Netgen and Open
CASCADE Technology; see [License and notices](https://planex2d.org/docs/core/notices.html) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). planex2d makes use of facilities provided by the Open CASCADE
Technology software.

## What it is

planex2d is a 2D geometry kernel for Python, written in C++. You draw outlines from lines, arcs, circles and splines,
combine them with exact Boolean operations, dimension them with constraints, and clean up drawings imported from
other tools. The resulting faces go to OpenCASCADE, netgen/NGSolve, gmsh and other tools for 3D modeling, meshing and
simulation.

## Installation

```bash
pip install planex2d
```

The package is called `planex2d` on PyPI and `planex` in Python (`import planex as px`). Version 0.1 has no prebuilt
wheels yet, so pip builds it from source; see [Installation](https://planex2d.org/docs/core/installation.html) for the two
extra steps this needs and for the optional packages.

## A first sketch

A `QuickSketch` draws with a pen: `Line` draws along the current direction, `Turn` changes the direction, `Arc`
continues with a tangent arc, and `Close` joins the outline to its start. `Circle` adds a hole. The result is an angle
bracket with an inner fillet and two holes.

```python
import planex as px

bracket = px.QuickSketch()
bracket.Line(60).Turn(90).Line(10).Turn(90).Line(40)
bracket.Arc(10, -90).Line(30).Turn(90).Line(10).Close()
bracket.Circle(45, 5, 2.5).Circle(5, 35, 2.5)
face = px.Face(bracket.Wires())
face.Plot()
```

<p align="center">
  <img src="https://planex2d.org/docs/_static/gallery/landing-turtle.svg" alt="The angle bracket drawn by the pen: two legs, an inner fillet of radius 10 and a hole in each leg" width="320">
</p>

## Where to go next

- [Quick sketches](https://planex2d.org/docs/core/sketch/index.html) and
  [constraint sketches](https://planex2d.org/docs/core/constraints/index.html): drawing, and dimensions and relations
  that a solver keeps satisfied.
- [Boolean operations](https://planex2d.org/docs/core/booleans/index.html), [measuring](https://planex2d.org/docs/core/measuring/index.html)
  and [cleaning up imported drawings](https://planex2d.org/docs/core/defeaturing/index.html).
- [Files](https://planex2d.org/docs/core/files.html): DXF, KiCad, SVG, STEP and more.
- [OpenCASCADE](https://planex2d.org/docs/integrations/occ/index.html) and
  [netgen/NGSolve](https://planex2d.org/docs/integrations/netgen/index.html): from faces to solids, meshes and simulations.
- [Examples](https://planex2d.org/docs/examples/index.html): mechanisms, machines, fields and manufacturing.
- [Benchmarks](https://planex2d.org/docs/core/comparison/index.html): planex2d next to OpenCASCADE and polygon libraries on
  the same tasks.

## License

planex2d is Copyright (C) 2026 Sebastian Hirnschall.

planex2d is free software; you can redistribute it and/or modify it under the terms of the GNU Lesser General Public
License as published by the Free Software Foundation; either version 2.1 of the License, or (at your option) any
later version.

planex2d is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied
warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License, the file
[LICENSE](LICENSE), for more details. The third-party notices are in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)
and on the page [License and notices](https://planex2d.org/docs/core/notices.html).
