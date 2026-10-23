# Installation

The package is called `planex2d` on PyPI and `planex` in Python.


<p><b>planex2d v0.1 has no prebuilt wheels yet.</b> pip builds it from the source distribution on PyPI against
netgen's OpenCASCADE, so install netgen-occt first and build without build isolation:</p>
<pre>pip install netgen-mesher netgen-occt-devel scikit-build-core
pip install --no-build-isolation planex2d</pre>
<p>The build needs a C++20 compiler and CMake. Optional dependencies such as matplotlib, plotly, shapely or
ezdxf are not installed with planex2d: install the ones you need, as listed in
[Optional packages](#optional-packages) below.</p>


```python
import planex as px
```

planex2d needs Python 3.11 or newer, numpy, and the OpenCASCADE and Netgen libraries of the `netgen-mesher` package,
which `import planex` loads. pip installs numpy along with planex2d, but not `netgen-mesher`: install it first, as in
the box above. A core that runs without netgen and OpenCASCADE is on the [Roadmap](roadmap.md).


## Optional packages

The packages below are optional: a function that needs one of them imports it only when it is called.

| Package | For |
|---|---|
| `matplotlib` | `Plot` with matplotlib, field maps of measurements |
| `plotly` | interactive `Plot` figures |
| `shapely` | `FromShapely`, `ToShapely` |
| `ezdxf` | `FromDXF`, `WriteDXF` |
| `ngsolve` | finite element solves on netgen meshes of planex2d geometry, field plots |
| `webgui_jupyter_widgets` | 3D views of planex2d and OCC objects in Jupyter |
| `gmsh` | meshing the `.geo` files `WriteGeo` writes (planex2d itself only writes the file) |
| `dolfinx` | FEniCSx fields drawn by `Plot` (from conda-forge, there are no pip wheels) |

Without any of them, planex2d objects still show as SVG pictures in a notebook.

To build planex2d yourself, see [Building and contributing](building.md).
