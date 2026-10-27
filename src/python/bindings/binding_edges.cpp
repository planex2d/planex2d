// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"
#include "binding_curve.hpp"
#include "curves/ellipse.hpp"

// an edge view's motions: the edge as a Wire of its own, moved
static const char *EdgeMotionDoc(const MotionDoc &d) {
    std::string text;
    for (const std::string &f : d.forms)
        text += d.copyCall + "(" + f + ") -> Wire\n";
    text += "\nThe edge as a Wire of its own, " + d.past + " " + d.how + "; the edge is not changed.\n\n" + d.more + "Args:\n" +
            d.args + "\nRaises:\n" + d.raises + "\nExamples:";
    for (const std::string &e : d.examples)
        text += "\n    edge." + d.copyCall + e;
    return Doc(text);
}

static MotionDoc EdgeTranslateDoc() {
    MotionDoc d = TranslateDoc();
    d.forms = {"d: tuple[float, float]", "d: tuple[float, float, float]"};
    d.args = "    d: the displacement, (dh, dv) in the plane or (dx, dy, dz) in 3d.\n";
    d.examples = {"((10, 0))", "((0, 0, 5))"};
    return d;
}

void BindEdges(py::module_ &m) {
    py::options options;
    options.disable_function_signatures();
    auto edgeref = py::class_<EdgeView>(m, "EdgeView", "An edge of a wire, face or shape: its geometry, and properties set on it change the edge there.");
    edgeref
        .def_property_readonly("edge", [](const EdgeView &r) -> Edge { return r.Get(); },
                               "edge: LineEdge | ArcEdge | CircleEdge | SplineEdge\n\nThe edge's geometry, a copy: a LineEdge, ArcEdge, CircleEdge or SplineEdge.")
        .def_property_readonly("id", [](const EdgeView &r) {
            r.Get();
            return r.wire->IdOf(r.i);
        }, "id: str\n\nThe edge's id: its object's, then its own within it (Shape.3.Line.2).")
        .def_property_readonly("_plane", [](const EdgeView &r) { return PlaneOf(*r.wire); }, "for the selectors: (origin, u, v) in 3d")
        .def("Wire", &EdgeView::AsWire,
             "Wire() -> Wire\n\n"
             "A copy of the edge as a Wire of its own, on its owner's plane, with its properties.\n\n"
             "Examples:\n"
             "    edge.Wire()")
        .def("Translated", [](const EdgeView &r, std::array<double, 2> d) { Wire w = r.AsWire(); return w.Translate(Point2(d)), w; },
             py::arg("d"), EdgeMotionDoc(EdgeTranslateDoc()))
        .def("Translated", [](const EdgeView &r, std::array<double, 3> d) { Wire w = r.AsWire(); return w.Translate(Vec3(d)), w; },
             py::arg("d"), "")
        .def("Rotated", [](const EdgeView &r, std::array<double, 2> c, double angle) {
            Wire w = r.AsWire();
            return w.Rotate(Point2(c), angle), w;
        }, py::arg("center"), py::arg("angle"), EdgeMotionDoc(RotateDoc()))
        .def("Rotated", [](const EdgeView &r, std::array<double, 3> p, std::array<double, 3> d, double angle) {
            Wire w = r.AsWire();
            return w.Rotate(AxisOf(p, d), angle), w;
        }, py::arg("point"), py::pos_only(), py::arg("dir"), py::arg("angle"), "")
        .def("Mirrored", [](const EdgeView &r, std::array<double, 2> p, std::array<double, 2> d) {
            Wire w = r.AsWire();
            return w.Mirror(Point2(p), Point2(d)), w;
        }, py::arg("point"), py::pos_only(), py::arg("dir"), EdgeMotionDoc(MirrorDoc()))
        .def("Mirrored", [](const EdgeView &r, std::array<double, 3> p, std::array<double, 3> n) {
            Wire w = r.AsWire();
            return w.Mirror(MirrorPlane(p, n)), w;
        }, py::arg("point"), py::pos_only(), py::arg("normal"), "")
        .def("Scaled", [](const EdgeView &r, std::array<double, 2> c, double s, bool scaleMaxh, bool scaleMinh) {
            Wire w = r.AsWire();
            return w.Scale(Point2(c), s, scaleMaxh, scaleMinh), w;
        }, py::arg("center"), py::arg("factor"), py::arg("scale_maxh") = false, py::arg("scale_minh") = false,
           EdgeMotionDoc(ScaleDoc()))
        .def("Scaled", [](const EdgeView &r, std::array<double, 3> p, double s, bool scaleMaxh, bool scaleMinh) {
            Wire w = r.AsWire();
            return w.Scale(Pnt3(p), s, scaleMaxh, scaleMinh), w;
        }, py::arg("point"), py::pos_only(), py::arg("factor"), py::arg("scale_maxh") = false, py::arg("scale_minh") = false, "")
        .def("_vertex", [](const EdgeView &r, bool end) {
            r.Get();
            return VertexView{r.wire, 2 * r.i + (end ? 1 : 0)};
        }, py::keep_alive<0, 1>(), py::arg("end"), "for .start, .end and the selectors: the vertex at the start or the end")
        .def_property_readonly("midpoint", [](const EdgeView &r) { return PointAtFraction(r.Get(), 0.5); }, "midpoint: Vertex\n\nThe point halfway along the edge.")
        .def_property_readonly("center", [](const EdgeView &r) {
            const std::optional<CircleCarrier> k = CarrierOf(r.Get());
            if (!k)
                throw py::value_error("center: only arcs and circles have a center");
            return k->center;
        }, "center: Vertex\n\nThe center of an arc or circle; a line or spline raises ValueError.")
        .def("PointAt", [](const EdgeView &r, double t) {
            if (!(t >= 0.0 && t <= 1.0))
                throw py::value_error("PointAt(" + std::to_string(t) + "): t is a fraction of the length in [0, 1]");
            return PointAtFraction(r.Get(), t);
        }, py::arg("t"),
           "PointAt(t: float) -> Vertex\n\n"
           "The point at the fraction t of the edge's length.\n\n"
           "Args:\n"
           "    t: the fraction of the length in [0, 1]: 0 the start, 0.5 the midpoint, 1 the end.\n\n"
           "Raises:\n"
           "    ValueError: t is outside [0, 1].\n\n"
           "Examples:\n"
           "    edge.PointAt(0.5)\n"
           "    edge.PointAt(t=0.25)")
        .def("Sample", [](const EdgeView &r, double tol) {
            CheckSampleTol("Sample", tol);
            return VertexArray(SamplePoints(r.Get(), tol, "Sample"));
        }, py::arg("tol"),
           "Sample(tol: float) -> np.ndarray\n\n"
           "An (n, 2) array of points from the start to the end, each chord within tol of the edge.\n\n"
           "A line gives its ends, an arc points by its sagitta, a spline points along its exact curve.\n\n"
           "Args:\n"
           "    tol: the largest distance of a chord from the edge, > 0.\n\n"
           "Raises:\n"
           "    ValueError: tol is not positive and finite, or below the rounding of an arc's or a spline's coordinates.\n\n"
           "Examples:\n"
           "    edge.Sample(1e-3)\n"
           "    edge.Sample(tol=0.01)")
        .def("TangentAt", [](const EdgeView &r, double t) {
            if (!(t >= 0.0 && t <= 1.0))
                throw py::value_error("TangentAt(" + std::to_string(t) + "): t is a fraction of the length in [0, 1]");
            return TangentAtFraction(r.Get(), t);
        }, py::arg("t"),
           "TangentAt(t: float) -> Vertex\n\n"
           "The unit tangent in the edge's direction at the fraction t of its length.\n\n"
           "Args:\n"
           "    t: the fraction of the length in [0, 1]: 0 the start, 1 the end.\n\n"
           "Raises:\n"
           "    ValueError: t is outside [0, 1].\n\n"
           "Examples:\n"
           "    edge.TangentAt(0)\n"
           "    edge.TangentAt(t=1)")
        .def_property_readonly("mass", [](const EdgeView &r) { return ExactLengthOf(r.Get()); },
                               "mass: float\n\nThe length of the edge.")
        .def_property_readonly("bounding_box", [](const EdgeView &r) { return BoxTuple(BoxOf(r.Get())); },
                               "bounding_box: tuple[tuple[float, float], tuple[float, float]]\n\nIts bounding box in the plane, ((hmin, vmin), (hmax, vmax)).")
        .def("Distance", [](const EdgeView &r, const py::object &target, bool project, const std::optional<std::string> &components,
                            bool onCarrier, bool sign) -> py::object {
            if (sign)
                throw py::value_error("Distance(signed=True): an edge has no inside");
            if (std::optional<PointRows> pts = PointBatch(target, "Distance"))
                return DistancesOf(onCarrier ? CarrierOf(r, *pts) : ThingOf(r.AsWire()), *pts, project, components);
            return DistanceOf(onCarrier ? CarrierOf(r, target, project) : ThingOf(r.AsWire()), target, project, components);
        }, py::arg("target"), py::pos_only(), py::arg("project") = false, py::arg("components") = py::none(), py::arg("on_carrier") = false,
           py::arg("signed") = false,
           "Distance(target: np.ndarray | list[tuple[float, float]] | list[tuple[float, float, float]], /, project: bool = False, components: str | None = None, on_carrier: bool = False, "
           "signed: bool = False) -> np.ndarray\n"
           "Distance(target: Point | Point3 | Face | Shape | Wire | EdgeView | VertexView | FaceView | TopoDS_Shape, /, project: bool = False, components: None = None, on_carrier: bool = False, "
           "signed: bool = False) -> float\n"
           "Distance(target: Point | Point3 | Face | Shape | Wire | EdgeView | VertexView | FaceView | TopoDS_Shape, /, project: bool = False, *, components: str, on_carrier: bool = False) -> float | "
           "tuple[float, ...]\n\n"
           "The smallest distance from the edge to the target, or from the whole line or circle it lies on.\n\n"
           "A target in another plane is measured in 3d, or projected into this plane with project.\n\n"
           "Args:\n"
           "    target: a point, (h, v) in the plane or (x, y, z) in 3d, a Face, Shape, Wire, edge, vertex, face of a "
           "shape or OCC shape; many points as an (n, 2) or (n, 3) array.\n"
           "    project: True projects a target in another plane into this one.\n"
           "    components: letters of 'hvxyz': the components of the shortest connection from here to the target "
           "instead of its length.\n"
           "    on_carrier: True measures to the whole line or circle the edge lies on.\n"
           "    signed: only False: an edge has no inside.\n\n"
           "Returns:\n"
           "    a float; with components a tuple of them, one letter a float; many points an array (n,), with k "
           "components (n, k), one letter (n,).\n\n"
           "Raises:\n"
           "    ValueError: signed=True; components has other letters; on_carrier for a spline, or with a target off the "
           "plane without project.\n"
           "    TypeError: the target has another form.\n\n"
           "Examples:\n"
           "    edge.Distance((5, 0))\n"
           "    edge.Distance((5, 3), on_carrier=True)")
        .def("ClosestPoint", [](const EdgeView &r, const py::object &point) { return ClosestPointOf(ThingOf(r.AsWire()), point); },
             py::arg("point"), py::pos_only(), ClosestPointDoc("edge"))
        .def("Measure", [](const EdgeView &r, const py::object &to, bool project) {
            return MeasureWith(ThingOf(r.AsWire()), to, project, [](const std::string &n) -> Thing {
                throw py::value_error("Measure(to='" + n + "'): an edge has no names to look up");
            });
        }, py::arg("to") = py::none(), py::arg("project") = false,
           "Measure(to: Point | Point3 | Face | Shape | Wire | EdgeView | None = None, project: bool = False) -> dict\n\n"
           "The measures of the edge as a dict, with the keys of Wire.Measure.\n\n"
           "Args:\n"
           "    to: anything Distance takes: adds the distance to it.\n"
           "    project: True projects a target of to in another plane into this one.\n\n"
           "Raises:\n"
           "    ValueError: to is a name: an edge has no names to look up.\n\n"
           "Examples:\n"
           "    edge.Measure()\n"
           "    edge.Measure(to=(0, 0))");
    EdgeFields([&](const char *name, auto get, auto set) {
        using V = SetterValueOf<decltype(set)>;
        edgeref.def_property(name, [get](const EdgeView &r) { return get(PropsOrDefault(r.Get())); },
                             [set](EdgeView &r, const V &v) { UpdateProps(r.Get(), [&](ShapeProperties &p) { set(p, v); }); },
                             FieldDoc(name, " of the edge; None if not set."));
    });
    auto edgelist = py::class_<EdgesView>(m, "EdgesView",
                                         "The edges of a wire, face or shape, a list-like view: a property set here is set on all of them.");
    edgelist
        .def("__len__", [](const EdgesView &l) { return l.Indices().size(); })
        .def("__getitem__", [](const py::object &self, const py::object &index) {
            const EdgesView &l = self.cast<const EdgesView &>();
            const std::vector<size_t> at = l.Indices();
            return ListItem(self, index, at.size(), [&](size_t i) { return py::cast(EdgeView{l.wire, at[i]}); });
        }, py::arg("index"));
    EdgeFields([&](const char *name, auto get, auto set) {
        using V = SetterValueOf<decltype(set)>;
        edgelist.def_property(name, [get](const EdgesView &l) { return Common(*l.wire, l.Indices(), get); }, [set](EdgesView &l, const V &v) {
            for (size_t i : l.Indices())
                UpdateProps((*l.wire)[i], [&](ShapeProperties &p) { set(p, v); });
        }, FieldDoc(name, " all edges share, None if they differ; set on all edges."));
    });

    auto vertexref = py::class_<VertexView>(m, "VertexView",
                                           "A vertex of a wire, face or shape, where edges end; it acts as its point (h, v), "
                                           "and its properties go to the OCC vertex, e.g. hpref for refinement towards a corner.");
    vertexref
        .def_property_readonly("_plane", [](const VertexView &v) { return PlaneOf(*v.wire); }, "for the selectors: (origin, u, v) in 3d")
        .def_property("id", [](const VertexView &v) {
            const std::vector<size_t> at = v.Endpoints();
            v.wire->AssignVertexIds();
            return v.wire->Id() + "." + *v.wire->VertexProps(at)->id;
        }, [](VertexView &v, const std::string &id) {
            CheckUserId(id);
            const std::vector<size_t> at = v.Endpoints();
            if (const Properties p = v.wire->VertexProps(at); p && p->id == id)
                return;
            if (v.wire->IsIdTaken(id))
                throw py::value_error("id '" + id + "' is taken");
            UpdateVertex(*v.wire, at, [&](ShapeProperties &p) { p.id = id; });
        }, "id: str\n\nThe vertex's id: its object's, then its own (Shape.3.Vertex.2, or the one set: vertex.id = \"corner\"), kept on "
           "the edge ends there like its name; the ends (Line.1.end) name it too.")
        .def_property_readonly("p", [](const VertexView &v) { return v.Point(); }, "p: Vertex\n\nIts point in the plane, a Vertex.")
        .def_property_readonly("x", [](const VertexView &v) { return v.wire->Axes().ToXYZ(v.Point()).x; }, "x: float\n\nIts x coordinate in 3d.")
        .def_property_readonly("y", [](const VertexView &v) { return v.wire->Axes().ToXYZ(v.Point()).y; }, "y: float\n\nIts y coordinate in 3d.")
        .def_property_readonly("z", [](const VertexView &v) { return v.wire->Axes().ToXYZ(v.Point()).z; }, "z: float\n\nIts z coordinate in 3d.")
        .def("Distance", [](const VertexView &v, const py::object &target, bool project, const std::optional<std::string> &components,
                            bool sign) {
            if (sign)
                throw py::value_error("Distance(signed=True): a vertex has no inside");
            return DistanceOf(ThingOf(v), target, project, components);
        }, py::arg("target"), py::pos_only(), py::arg("project") = false, py::arg("components") = py::none(), py::arg("signed") = false,
           "Distance(target: np.ndarray | list[tuple[float, float]] | list[tuple[float, float, float]], /, project: bool = False, components: str | None = None, signed: bool = False) -> "
           "np.ndarray\n"
           "Distance(target: Point | Point3 | Face | Shape | Wire | EdgeView | VertexView | FaceView | TopoDS_Shape, /, project: bool = False, components: None = None, signed: bool = False) -> float\n"
           "Distance(target: Point | Point3 | Face | Shape | Wire | EdgeView | VertexView | FaceView | TopoDS_Shape, /, project: bool = False, *, components: str) -> float | tuple[float, ...]\n\n"
           "The smallest distance from the vertex to the target.\n\n"
           "A target in another plane is measured in 3d, or projected into this plane with project.\n\n"
           "Args:\n"
           "    target: a point, (h, v) in the plane or (x, y, z) in 3d, a Face, Shape, Wire, edge, vertex, face of a "
           "shape or OCC shape; many points as an (n, 2) or (n, 3) array.\n"
           "    project: True projects a target in another plane into this one.\n"
           "    components: letters of 'hvxyz': the components of the shortest connection from here to the target "
           "instead of its length.\n"
           "    signed: only False: a vertex has no inside.\n\n"
           "Returns:\n"
           "    a float; with components a tuple of them, one letter a float; many points an array (n,), with k "
           "components (n, k), one letter (n,).\n\n"
           "Raises:\n"
           "    ValueError: signed=True; components has other letters.\n"
           "    TypeError: the target has another form.\n\n"
           "Examples:\n"
           "    vertex.Distance((5, 0))\n"
           "    vertex.Distance(other, components=\"hv\")")
        .def("__repr__", [](const VertexView &v) {
            const Vertex p = v.Point();
            return "VertexView(" + std::to_string(p.x) + ", " + std::to_string(p.y) + ")";
        });
    VertexFields([&](const char *name, auto get, auto set) {
        using V = SetterValueOf<decltype(set)>;
        vertexref.def_property(name, [get](const VertexView &v) { return get(PropsOrDefault(v.wire->VertexProps(v.Endpoints()))); },
                               [set](VertexView &v, const V &x) { UpdateVertex(*v.wire, v.Endpoints(), [&](ShapeProperties &p) { set(p, x); }); },
                               FieldDoc(name, " of the vertex; None if not set."));
    });
    auto vertexlist = py::class_<VerticesView>(m, "VerticesView",
                                                 "The vertices of a wire, face or shape, a list-like view: a property set here is set "
                                                 "on all of them.");
    vertexlist
        .def("__len__", [](const VerticesView &l) { return l.Vertices().size(); })
        .def("__getitem__", [](const py::object &self, const py::object &index) {
            const VerticesView &l = self.cast<const VerticesView &>();
            const std::vector<std::vector<size_t>> all = l.Vertices();
            return ListItem(self, index, all.size(), [&](size_t i) { return py::cast(VertexView{l.wire, all[i].front()}); });
        }, py::arg("index"));
    VertexFields([&](const char *name, auto get, auto set) {
        using V = SetterValueOf<decltype(set)>;
        vertexlist.def_property(name, [get](const VerticesView &l) -> decltype(get(ShapeProperties{})) {
            decltype(get(ShapeProperties{})) value;
            bool first = true;
            for (const std::vector<size_t> &at : l.Vertices()) {
                auto v = get(PropsOrDefault(l.wire->VertexProps(at)));
                if (!first && v != value)
                    return {};
                value = v;
                first = false;
            }
            return value;
        }, [set](VerticesView &l, const V &x) {
            for (const std::vector<size_t> &at : l.Vertices())
                UpdateVertex(*l.wire, at, [&](ShapeProperties &p) { set(p, x); });
        }, FieldDoc(name, " all vertices share, None if they differ; set on all vertices."));
    });

    m.def("Line", [](const py::object &start, const py::object &end, const py::object &axes) {
        const AxisSystem a = AxesArg(axes, "Line");
        return SingleEdge(planex2d::Line(HVArg(a, start, "Line"), HVArg(a, end, "Line")), a);
    }, py::arg("start"), py::arg("end"), py::arg("plane") = py::none(),
       "Line(start: Point, end: Point, plane: Plane | None = None) -> Wire\n\n"
       "A wire of one line from start to end.\n\n"
       "Args:\n"
       "    start: the start (h, v) in the plane, or (x, y, z) on it.\n"
       "    end: the end (h, v) in the plane, or (x, y, z) on it.\n"
       "    plane: the plane the wire lies on: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar "
       "OCC shape (its plane); default the xy plane.\n\n"
       "Raises:\n"
       "    TypeError: plane has another form.\n"
       "    ValueError: an (x, y, z) point is off the plane.\n\n"
       "Examples:\n"
       "    px.Line((0, 0), (10, 0))\n"
       "    px.Line((0, 0), (0, 5), plane=px.YZ)");
    m.def("Arc", [](const py::object &startArg, const py::object &endArg, const py::object &centerArg, bool reverse, OptDouble tol,
                    const py::object &axes) {
        const AxisSystem a = AxesArg(axes, "Arc");
        const Vertex start = HVArg(a, startArg, "Arc"), end = HVArg(a, endArg, "Arc"), center = HVArg(a, centerArg, "Arc");
        CheckArcRadii("Arc", start, end, center, tol);
        return SingleEdge(planex2d::Arc(start, end, center, !reverse), a);
    }, py::arg("start"), py::arg("end"), py::arg("center"), py::arg("reverse") = false, py::arg("tol") = py::none(),
       py::arg("plane") = py::none(),
       "Arc(start: Point, end: Point, center: Point, reverse: bool = False, tol: float | None = None, plane: Plane | None = "
       "None) -> Wire\n\n"
       "A wire of one arc from start to end around center, counterclockwise, or clockwise with reverse=True.\n\n"
       "Start and end lie equally far from the center within tol; the center is then moved onto their bisector.\n\n"
       "Args:\n"
       "    start: the start (h, v) in the plane, or (x, y, z) on it.\n"
       "    end: the end (h, v) in the plane, or (x, y, z) on it.\n"
       "    center: the center (h, v) in the plane, or (x, y, z) on it.\n"
       "    reverse: True runs clockwise from start to end.\n"
       "    tol: the largest difference of the distances of start and end from the center; None: the rounding.\n"
       "    plane: the plane the wire lies on: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar "
       "OCC shape (its plane); default the xy plane.\n\n"
       "Raises:\n"
       "    ValueError: start and end differ in their distance from the center by more than tol, or tol is negative or "
       "not finite; an (x, y, z) point is off the plane.\n"
       "    TypeError: plane has another form.\n\n"
       "Examples:\n"
       "    px.Arc((1, 0), (0, 1), (0, 0))\n"
       "    px.Arc((1, 0), (0, 1), (0, 0), reverse=True)");
    m.def("Circle", [](const py::object &center, double r, bool reverse, const py::object &axes) {
        const AxisSystem a = AxesArg(axes, "Circle");
        CheckCircleRadius("Circle", r);
        return SingleEdge(planex2d::Circle(HVArg(a, center, "Circle"), r, !reverse), a);
    }, py::arg("center"), py::arg("r"), py::arg("reverse") = false, py::arg("plane") = py::none(),
       "Circle(center: Point, r: float, reverse: bool = False, plane: Plane | None = None) -> Wire\n\n"
       "A wire of one circle of radius r around center, counterclockwise, or clockwise with reverse=True.\n\n"
       "The circle is one edge with no vertex; where other edges cross or touch it, it is cut into arcs. Its origin, "
       "where the edge starts and ends, is the point r from the center along +h.\n\n"
       "Args:\n"
       "    center: the center (h, v) in the plane, or (x, y, z) on it.\n"
       "    r: the radius, > 0.\n"
       "    reverse: True runs clockwise.\n"
       "    plane: the plane the wire lies on: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar "
       "OCC shape (its plane); default the xy plane.\n\n"
       "Raises:\n"
       "    ValueError: r is not positive and finite; the (x, y, z) center is off the plane.\n"
       "    TypeError: plane has another form.\n\n"
       "Examples:\n"
       "    px.Circle((0, 0), 1)\n"
       "    px.Circle((5, 0), 2, reverse=True).Face()");
    auto ellipse = [](const Vertex &center, const Vertex &major, const Vertex &minor, double t0, double t1, const AxisSystem &axes) {
        Wire w(axes);
        for (planex2d::Spline &s : planex2d::Spline::FromCurve(BSplineCurve(EllipseData(center, major, minor, t0, t1), false)))
            w.Add(std::move(s));
        return w;
    };
    m.def("Ellipse", [ellipse](const py::object &center, double major, double minor, double angle, bool reverse, const py::object &axes) {
        const AxisSystem a = AxesArg(axes, "Ellipse");
        std::ostringstream text;
        text << std::setprecision(12) << "Ellipse: ";
        for (const auto &[which, r] : {std::pair{"major", major}, std::pair{"minor", minor}})
            if (!(std::isfinite(r) && r > 0.0)) {
                text << "the " << which << " radius is " << r << ", it must be positive and finite";
                throw py::value_error(text.str());
            }
        if (minor > major) {
            text << "the minor radius " << minor << " is larger than the major radius " << major
                 << "; the major radius is the larger one, along angle";
            throw py::value_error(text.str());
        }
        if (!std::isfinite(angle)) {
            text << "the angle is " << angle << ", it must be finite";
            throw py::value_error(text.str());
        }
        const double t = Radians(angle);
        const Vertex u{std::cos(t), std::sin(t)};
        return ellipse(HVArg(a, center, "Ellipse"), u * major, Vertex{-u.y, u.x} * (reverse ? -minor : minor), 0.0,
                       2.0 * std::numbers::pi, a);
    }, py::arg("center"), py::arg("major"), py::arg("minor"), py::arg("angle") = 0.0, py::arg("reverse") = false,
       py::arg("plane") = py::none(),
       "Ellipse(center: Point, major: float, minor: float, angle: float = 0, reverse: bool = False, plane: Plane | None = "
       "None) -> Wire\n\n"
       "A wire of one ellipse around center, counterclockwise, or clockwise with reverse=True.\n\n"
       "The ellipse is one closed spline edge that is exactly the ellipse (a rational quadratic b-spline); where other "
       "edges cross or touch it, it is cut. Its origin, where the edge starts and ends, is the end of the major axis in "
       "the direction angle.\n\n"
       "Args:\n"
       "    center: the center (h, v) in the plane, or (x, y, z) on it.\n"
       "    major: the major radius, the semi-axis along angle, > 0.\n"
       "    minor: the minor radius, the semi-axis across it, > 0 and at most major.\n"
       "    angle: the major axis's direction in degrees from h.\n"
       "    reverse: True runs clockwise.\n"
       "    plane: the plane the wire lies on: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar "
       "OCC shape (its plane); default the xy plane.\n\n"
       "Raises:\n"
       "    ValueError: a radius is not positive and finite; minor is larger than major; angle is not finite; the "
       "(x, y, z) center is off the plane.\n"
       "    TypeError: plane has another form.\n\n"
       "Examples:\n"
       "    px.Ellipse((0, 0), 3, 2)\n"
       "    px.Ellipse((5, 0), 4, 1, angle=30).Face()");
    m.def("_Ellipse", [ellipse](const Vertex &center, const Vertex &major, const Vertex &minor, double start, double end,
                                const py::object &axes) {
        return ellipse(center, major, minor, start, end, AxesArg(axes, "Ellipse"));
    }, py::arg("center"), py::arg("major"), py::arg("minor"), py::arg("start") = 0.0, py::arg("end") = 2.0 * std::numbers::pi,
       py::arg("plane") = py::none(),
       "for the importers: the ellipse center + cos t major + sin t minor for t from start to end (radians, start < end, at "
       "most one turn), exactly");
    m.def("_Arc", [](const Vertex &start, const Vertex &end, const Vertex &center, bool ccw, const py::object &axes) {
        return SingleEdge(planex2d::Arc(start, end, center, ccw), AxesArg(axes, "Arc"));
    }, py::arg("start"), py::arg("end"), py::arg("center"), py::arg("ccw") = true, py::arg("plane") = py::none(),
       "for the importers: Arc without the radius check, a center off the bisector is moved onto it");
    m.def("_Circle", [](const Vertex &center, const Vertex &origin, bool ccw, const py::object &axes) {
        CheckCircleRadius("Circle", Norm(origin - center));
        return SingleEdge(planex2d::Circle(center, origin, ccw), AxesArg(axes, "Circle"));
    }, py::arg("center"), py::arg("origin"), py::arg("ccw") = true, py::arg("plane") = py::none(),
       "for the importers: a Circle starting at origin, a point on it");
    m.def("FromPolyline", [](const py::object &points, bool closed, const py::object &axes, const Name &n, const Size &mh, const Col &c,
                         const Size &mi, const Size &hp, const Layer &la, const Partition &pa) {
        const AxisSystem a = AxesArg(axes, "FromPolyline");
        const PointRows rows = PlaneRows(a, points, "FromPolyline");
        const Properties props = PROPS;
        const double *x = rows.data();
        std::vector<Vertex> kept;
        for (py::ssize_t i = 0; i < rows.shape(0); ++i)
            if (const Vertex p{x[2 * i], x[2 * i + 1]}; kept.empty() || !(p == kept.back()))
                kept.push_back(p);
        if (closed && kept.size() > 1 && kept.back() == kept.front())
            kept.pop_back();
        if (kept.size() < 2)
            throw py::value_error("FromPolyline: the points hold " + std::to_string(kept.size()) + " different point(s), a line needs two");
        Wire w(a);
        auto add = [&](const Vertex &a, const Vertex &b) {
            planex2d::Line l(a, b);
            l.props = props;
            w.Add(std::move(l));
        };
        for (size_t k = 0; k + 1 < kept.size(); ++k)
            add(kept[k], kept[k + 1]);
        if (closed)
            add(kept.back(), kept.front());
        w.CheckChain("FromPolyline");
        return w;
    }, py::arg("points"), py::pos_only(), py::arg("closed") = false, py::kw_only(), py::arg("plane") = py::none(), py::arg("name") = py::none(),
       py::arg("maxh") = py::none(), py::arg("col") = py::none(), py::arg("minh") = py::none(), py::arg("hpref") = py::none(),
       py::arg("layer") = py::none(), py::arg("partition") = py::none(),
       "FromPolyline(points: np.ndarray | list[Point], /, closed: bool = False, *, plane: Plane | None = None, name: str | "
       "None = None, maxh: float | None = None, col: tuple[float, ...] | None = None, minh: float | None = None, hpref: "
       "float | None = None, layer: int | None = None, partition: list[float] | None = None) -> Wire\n\n"
       "A wire of lines through the points, the properties on every line.\n\n"
       "Args:\n"
       "    points: an (n, 2) or (n, 3) array or a sequence of (h, v) or (x, y, z) on the plane; a point equal to the one before is skipped.\n"
       "    closed: True joins the last point to the first; a last point equal to the first adds no line.\n"
       "    plane: the plane the wire lies on: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar "
       "OCC shape (its plane); default the xy plane.\n"
       "    name: the name of the lines.\n"
       "    maxh: the largest mesh size on the lines (netgen's maxh).\n"
       "    col: the color (r, g, b) or (r, g, b, a), each in 0..1.\n"
       "    minh: the smallest mesh size on the lines (netgen's minh).\n"
       "    hpref: netgen's hpref.\n"
       "    layer: netgen's layer number.\n"
       "    partition: the mesh points along each line as increasing fractions of its length in (0, 1).\n\n"
       "Raises:\n"
       "    ValueError: fewer than two different points; the lines cross, overlap or revisit a point (px.Wires of the "
       "lines cuts them into clean wires); a mesh size is not positive and finite.\n"
       "    TypeError: plane has another form.\n\n"
       "Examples:\n"
       "    px.FromPolyline([(0, 0), (10, 0), (10, 5)])\n"
       "    px.FromPolyline([(0, 0), (10, 0), (10, 5)], closed=True, name=\"outline\", maxh=0.5)");
    auto spline = [](const py::iterable &points, bool periodic, double tol, const std::optional<std::map<size_t, Vertex>> &tangents,
                     const py::object &axes, const py::object &style) {
        const AxisSystem a = AxesArg(axes, "Spline");
        Wire w(a);
        for (planex2d::Spline &s : planex2d::Spline::Through(PlanePoints(a, points, "Spline"), periodic, tol, tangents.value_or(std::map<size_t, Vertex>{}),
                                                             StyleArg(style, "Spline")))
            w.Add(std::move(s));
        return w;
    };
    m.def("_Spline", spline, py::arg("points"), py::pos_only(), py::arg("periodic") = false, py::arg("tol") = 1e-8,
          py::arg("tangents") = py::none(), py::arg("plane") = py::none(), py::arg("style") = py::none(),
          "for the importers: Spline without the self-crossing check, the result goes through px.Wires");
    m.def("Spline", [spline](const py::iterable &points, bool periodic, double tol, const std::optional<std::map<size_t, Vertex>> &tangents,
                             const py::object &axes, const py::object &style) {
        Wire w = spline(points, periodic, tol, tangents, axes, style);
        w.CheckChain("Spline");
        return w;
    }, py::arg("points"), py::pos_only(), py::arg("periodic") = false, py::arg("tol") = 1e-8, py::arg("tangents") = py::none(),
       py::arg("plane") = py::none(), py::arg("style") = py::none(),
       "Spline(points: list[Point] | np.ndarray, /, periodic: bool = False, tol: float = 1e-08, tangents: dict[int, tuple[float, "
       "float]] | None = None, plane: Plane | None = None, style: str | None = None) -> Wire\n\n"
       "A wire of one spline through the points, one closed edge if periodic.\n\n"
       "Style 'averaged' is planex's interpolation, its knots averaged from the points' parameters, using only the "
       "tangents' directions; periodic, the C2 periodic cubic, the same curve whichever point the list starts with. Style "
       "'clamped' is the curve of OCC's Geom2dAPI_Interpolate as netgen calls it, a knot at every point (periodic: "
       "OCC's, which depends on the start point and has a kink there).\n\n"
       "Args:\n"
       "    points: the points (h, v), or (x, y, z) on the plane, the spline runs through, in order (a list or an (n, 2) or (n, 3) array); a periodic spline lists its start once.\n"
       "    periodic: True closes the spline smoothly through the first point.\n"
       "    tol: points closer than this to the next count as the same point.\n"
       "    tangents: {index of a point: its tangent direction (dh, dv)}; None for none.\n"
       "    plane: the plane the wire lies on: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar "
       "OCC shape (its plane); default the xy plane.\n"
       "    style: 'averaged' or 'clamped'; None takes px.config.spline_style.\n\n"
       "Raises:\n"
       "    ValueError: the curve crosses itself; fewer than 2 points (3 if periodic); two neighboring points coincide; "
       "a tangent is zero or at no point; a periodic spline repeats its first point at the end; style is not "
       "'averaged' or 'clamped'; a value is not finite.\n"
       "    TypeError: plane has another form.\n\n"
       "Examples:\n"
       "    px.Spline([(0, 0), (1, 1), (2, 0)])\n"
       "    px.Spline([(0, 0), (1, 1), (2, 0)], tangents={0: (0, 1)}, style=\"clamped\")");
    m.def("Curve", [](const py::object &point, const py::object &range, const py::object &axes) {
        const std::string text = CurveText("Curve", point, range);
        py::dict names;
        names["numbers_only"] = "px.Curve has no dimensions";
        const Curve<double, Vertex> shape = IsCurveCallable(point) ? CallableShape(point, range, text) : ParsedCurve(point, range, text, names).Shape({});
        Wire w(AxesArg(axes, "Curve"));
        for (planex2d::Spline &s : planex2d::Spline::FromCurve(shape))
            w.Add(std::move(s));
        w.CheckChain("Curve");
        return w;
    }, py::arg("point"), py::arg("range"), py::pos_only(), py::kw_only(), py::arg("plane") = py::none(),
       "Curve(point: tuple[str, str] | Callable[[float], Point], range: tuple[float, float], /, *, plane: Plane | None = None) -> Wire\n\n"
       "A wire of one spline edge, fitted to the parametric curve to about 1e-12 of its size.\n\n"
       "The point is (h, v) as two expressions in the parameter t: numbers, units, pi, + - * / ^, sqrt, abs, min, max, sin, "
       "cos, tan, asin, acos, atan2 (angles in degrees), exp, log (natural), sinh, cosh, tanh, asinh, acosh, atanh, and h in v or v in h (the other coordinate at the same t); or a "
       "callable t -> (h, v), a tuple, list or array, called once per t (fitted by a cubic to 1e-10 of its size). An end at "
       "its start closes it into one closed edge. The formula is not kept: the wire holds the spline.\n\n"
       "Args:\n"
       "    point: (h, v), two expressions in t, (\"t\", \"t^2\") for v = h^2; or a callable t -> (h, v).\n"
       "    range: (t0, t1), two numbers.\n"
       "    plane: the plane the wire lies on: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar "
       "OCC shape (its plane); default the xy plane.\n\n"
       "Raises:\n"
       "    TypeError: a point that is no two expressions or callable, a range that is no two numbers, a callable that gives "
       "no point.\n"
       "    ValueError: a name the formula cannot use (px.Curve takes no dimensions), h and v using each other or a "
       "coordinate itself, a formula out of its domain in the range, an empty range, a curve of no length, a curve that "
       "crosses itself.\n\n"
       "Examples:\n"
       "    px.Curve((\"t\", \"t^2\"), (0, 1))\n"
       "    px.Curve((\"10*cos(t)\", \"5*sin(t)\"), (0, 360))");
    auto bspline = [](const py::iterable &poles, const std::vector<double> &knots, int degree,
                      const std::optional<std::vector<double>> &weights, bool periodic, const py::object &axes) {
        const AxisSystem a = AxesArg(axes, "BSpline");
        BSplineData b{PlanePoints(a, poles, "BSpline"), knots, weights ? *weights : std::vector<double>{}, degree};
        Wire w(a);
        for (planex2d::Spline &s : planex2d::Spline::FromCurve(BSplineCurve(b, periodic)))
            w.Add(std::move(s));
        return w;
    };
    m.def("_BSpline", bspline, py::arg("poles"), py::arg("knots"), py::arg("degree"), py::arg("weights") = py::none(),
          py::arg("periodic") = false, py::arg("plane") = py::none(),
          "for the importers: BSpline without the self-crossing check, the result goes through px.Wires");
    m.def("BSpline", [bspline](const py::iterable &poles, const std::vector<double> &knots, int degree,
                               const std::optional<std::vector<double>> &weights, bool periodic, const py::object &axes) {
        Wire w = bspline(poles, knots, degree, weights, periodic, axes);
        w.CheckChain("BSpline");
        return w;
    }, py::arg("poles"), py::arg("knots"), py::arg("degree"), py::arg("weights") = py::none(), py::arg("periodic") = false,
       py::arg("plane") = py::none(),
       "BSpline(poles: list[Point], knots: list[float], degree: int, weights: list[float] | None = None, periodic: bool = "
       "False, plane: Plane | None = None) -> Wire\n\n"
       "A wire of one spline given as a b-spline, one closed edge if it is closed.\n\n"
       "Args:\n"
       "    poles: the control points (h, v), or (x, y, z) on the plane.\n"
       "    knots: the flattened knots, each repeated by its multiplicity.\n"
       "    degree: the degree of the curve.\n"
       "    weights: one weight per pole for a rational curve; None: not rational.\n"
       "    periodic: True for a periodic b-spline; its knots are one period, the first knot's copies to the last's, the "
       "last repeated as often as the first, as many before the last as there are poles.\n"
       "    plane: the plane the wire lies on: a Plane, netgen Axes, a sketch, Shape, Face, Wire, Compound or planar "
       "OCC shape (its plane); default the xy plane.\n"
       "Raises:\n"
       "    ValueError: the curve crosses itself.\n"
       "    ValueError: the poles, knots, degree and weights do not form a b-spline.\n"
       "    TypeError: plane has another form.\n\n"
       "Examples:\n"
       "    px.BSpline([(0, 0), (1, 2), (3, 2), (4, 0)], [0, 0, 0, 0, 1, 1, 1, 1], 3)\n"
       "    px.BSpline([(1, 0), (1, 1), (0, 1)], [0, 0, 0, 1, 1, 1], 2, weights=[1, 0.7071, 1])");
    BindViewCopy(edgeref, [](const EdgeView &r) { return r.AsWire(); }, "Copy() -> Wire\n\n"
                 "The edge as a Wire of its own, with its properties; copy.copy and copy.deepcopy give the same.\n\n"
                 "Examples:\n"
                 "    w = edge.Copy()");
    BindViewCopy(vertexref, [](const VertexView &v) { return v.Point(); }, "Copy() -> Vertex\n\n"
                 "The vertex's point as a Vertex; copy.copy and copy.deepcopy give the same.\n\n"
                 "Examples:\n"
                 "    p = vertex.Copy()");
    BindCoordinates(vertexref, [](const VertexView &v) { return v.Point(); });
}
