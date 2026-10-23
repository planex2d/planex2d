// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"

void BindEdgeGeometry(py::module_ &m) {
    py::options options;
    options.disable_function_signatures();
    py::class_<Line>(m, "LineEdge", "The geometry of a line; edges live in wires, made by px.Line.")
        .def(py::init<Vertex, Vertex>(), py::arg("start"), py::arg("end"),
             "LineEdge(start: Point, end: Point)\n\n"
             "The geometry of the line from start to end.\n\n"
             "Args:\n"
             "    start: the start (h, v).\n"
             "    end: the end (h, v).\n\n"
             "Examples:\n"
             "    line = px.LineEdge((0, 0), (10, 0))")
        .def_property_readonly("start", &Line::Start, "start: Vertex\n\nThe start, a Vertex.")
        .def_property_readonly("end", &Line::End, "end: Vertex\n\nThe end, a Vertex.")
        .def_readonly("from_spline", &Line::fromSpline, "from_spline: bool\n\nTrue for a piece of a spline's arc approximation.")
        .def("MidPoint", &Line::MidPoint,
             "MidPoint() -> Vertex\n\n"
             "The point halfway along the line.\n\n"
             "Examples:\n"
             "    line.MidPoint()")
        .def("Tangent", &Line::Tangent<>, py::arg("p"),
             "Tangent(p: Point) -> Vertex\n\n"
             "The unit tangent in the line's direction at its point p; (0, 0) if p is not on the line.\n\n"
             "Args:\n"
             "    p: a point (h, v) of the line.\n\n"
             "Examples:\n"
             "    line.Tangent(line.MidPoint())")
        .def("Offset", [](const Line &l, double d) { return Face(l.Offset(d)); }, py::arg("d"),
             "Offset(d: float) -> Face\n\n"
             "The face of the points within d of the line: its outline runs at distance d around it, round at the ends.\n\n"
             "Args:\n"
             "    d: the distance, > 0.\n\n"
             "Raises:\n"
             "    ValueError: d is not positive and finite.\n\n"
             "Examples:\n"
             "    line.Offset(0.5)")
        .def("Cut", &CutEdges<Line, Line>, py::arg("other"), py::pos_only(),
             "Cut(other: LineEdge | ArcEdge, /) -> tuple[CutResult, list[LineEdge | ArcEdge]]\n\n"
             "Cuts this line and other in place where they cross, touch or overlap: each keeps its first piece.\n\n"
             "Args:\n"
             "    other: a LineEdge or ArcEdge.\n\n"
             "Returns:\n"
             "    how they meet (a CutResult), and the further pieces: this line's, then other's without those lying "
             "on this line.\n\n"
             "Examples:\n"
             "    result, pieces = line.Cut(other)")
        .def("Cut", &CutEdges<Line, Arc>, py::arg("other"), py::pos_only(), "");

    py::class_<Arc>(m, "ArcEdge", "The geometry of an arc; edges live in wires, made by px.Arc.")
        .def(py::init([](const Vertex &start, const Vertex &end, const Vertex &center, bool reverse, OptDouble tol) {
            CheckArcRadii("ArcEdge", start, end, center, tol);
            return Arc(start, end, center, !reverse);
        }), py::arg("start"), py::arg("end"), py::arg("center"), py::arg("reverse") = false, py::arg("tol") = py::none(),
             "ArcEdge(start: Point, end: Point, center: Point, reverse: bool = False, tol: float | None = None)\n\n"
             "The geometry of the arc from start to end around center, counterclockwise, or clockwise with reverse=True.\n\n"
             "Start and end lie equally far from the center within tol; the center is then moved onto their bisector, as "
             "for px.Arc.\n\n"
             "Args:\n"
             "    start: the start (h, v).\n"
             "    end: the end (h, v).\n"
             "    center: the centre (h, v).\n"
             "    reverse: True runs clockwise from start to end.\n"
             "    tol: the largest difference of the distances of start and end from the center; None: the rounding.\n\n"
             "Raises:\n"
             "    ValueError: start and end differ in their distance from the center by more than tol, or tol is negative "
             "or not finite.\n\n"
             "Examples:\n"
             "    arc = px.ArcEdge((1, 0), (0, 1), (0, 0))\n"
             "    arc = px.ArcEdge((1, 0), (0, 1), (0, 0), reverse=True)")
        .def_property_readonly("start", &Arc::Start, "start: Vertex\n\nThe start, a Vertex.")
        .def_property_readonly("end", &Arc::End, "end: Vertex\n\nThe end, a Vertex.")
        .def_property_readonly("center", &Arc::Center, "center: Vertex\n\nThe centre, a Vertex.")
        .def_property_readonly("radius", &Arc::Radius, "radius: float\n\nThe radius.")
        .def_property_readonly("sweep", [](const Arc &a) { return Degrees(a.CCW() ? a.Sweep() : -a.Sweep()); },
                               "sweep: float\n\nThe angle from start to end in degrees, negative for a clockwise arc.")
        .def_property_readonly("ccw", &Arc::CCW, "ccw: bool\n\nTrue if the arc runs counterclockwise from start to end.")
        .def_readonly("from_spline", &Arc::fromSpline, "from_spline: bool\n\nTrue for a piece of a spline's arc approximation.")
        .def("MidPoint", &Arc::MidPoint,
             "MidPoint() -> Vertex\n\n"
             "The point halfway along the arc.\n\n"
             "Examples:\n"
             "    arc.MidPoint()")
        .def("Tangent", &Arc::Tangent<>, py::arg("p"),
             "Tangent(p: Point) -> Vertex\n\n"
             "The unit tangent in the arc's direction at its point p; (0, 0) if p is not on the arc.\n\n"
             "Args:\n"
             "    p: a point (h, v) of the arc.\n\n"
             "Examples:\n"
             "    arc.Tangent(arc.MidPoint())")
        .def("Offset", [](const Arc &a, double d) { return Face(a.Offset(d)); }, py::arg("d"),
             "Offset(d: float) -> Face\n\n"
             "The face of the points within d of the arc: its outline runs at distance d around it, round at the ends.\n\n"
             "Args:\n"
             "    d: the distance, > 0.\n\n"
             "Raises:\n"
             "    ValueError: d is not positive and finite.\n\n"
             "Examples:\n"
             "    arc.Offset(0.5)")
        .def("Cut", &CutEdges<Arc, Line>, py::arg("other"), py::pos_only(),
             "Cut(other: LineEdge | ArcEdge, /) -> tuple[CutResult, list[LineEdge | ArcEdge]]\n\n"
             "Cuts this arc and other in place where they cross, touch or overlap: each keeps its first piece.\n\n"
             "Args:\n"
             "    other: a LineEdge or ArcEdge.\n\n"
             "Returns:\n"
             "    how they meet (a CutResult), and the further pieces: this arc's, then other's without those lying "
             "on this arc.\n\n"
             "Examples:\n"
             "    result, pieces = arc.Cut(other)")
        .def("Cut", &CutEdges<Arc, Arc>, py::arg("other"), py::pos_only(), "");

    py::class_<Circle>(m, "CircleEdge", "The geometry of a full circle, one edge with no vertex; edges live in wires, made by px.Circle.")
        .def(py::init([](const Vertex &center, double r, bool reverse) {
            CheckCircleRadius("CircleEdge", r);
            return Circle(center, r, !reverse);
        }), py::arg("center"), py::arg("r"), py::arg("reverse") = false,
             "CircleEdge(center: Point, r: float, reverse: bool = False)\n\n"
             "The geometry of the circle of radius r around center, counterclockwise, or clockwise with reverse=True.\n\n"
             "Its origin, where start and end lie, is the point r from the center along +h.\n\n"
             "Args:\n"
             "    center: the centre (h, v).\n"
             "    r: the radius, > 0.\n"
             "    reverse: True runs clockwise.\n\n"
             "Raises:\n"
             "    ValueError: r is not positive and finite.\n\n"
             "Examples:\n"
             "    circle = px.CircleEdge((0, 0), 1)\n"
             "    circle = px.CircleEdge((0, 0), 1, reverse=True)")
        .def_property_readonly("center", &Circle::Center, "center: Vertex\n\nThe centre, a Vertex.")
        .def_property_readonly("radius", &Circle::Radius, "radius: float\n\nThe radius.")
        .def_property_readonly("ccw", &Circle::CCW, "ccw: bool\n\nTrue if the circle runs counterclockwise.")
        .def_property_readonly("origin", &Circle::Origin, "origin: Vertex\n\nThe point the circle starts and ends at, a Vertex: it has no vertex there.")
        .def_property_readonly("start", &Circle::Start, "start: Vertex\n\nThe start, the origin, a Vertex.")
        .def_property_readonly("end", &Circle::End, "end: Vertex\n\nThe end, the origin, a Vertex.")
        .def_property_readonly("approximation", &Circle::Halves, "approximation: list[ArcEdge]\n\nThe two half arcs planex computes with and exports.")
        .def("MidPoint", &Circle::MidPoint,
             "MidPoint() -> Vertex\n\n"
             "The point opposite the origin.\n\n"
             "Examples:\n"
             "    circle.MidPoint()")
        .def("Tangent", &Circle::Tangent<>, py::arg("p"),
             "Tangent(p: Point) -> Vertex\n\n"
             "The unit tangent in the circle's direction at its point p; (0, 0) if p is not on the circle.\n\n"
             "Args:\n"
             "    p: a point (h, v) of the circle.\n\n"
             "Examples:\n"
             "    circle.Tangent(circle.MidPoint())")
        .def("Offset", [](const Circle &c, double d) { return Face(c.Offset(d)); }, py::arg("d"),
             "Offset(d: float) -> Face\n\n"
             "The face of the points within d of the circle: the ring between the circles of radius r - d and r + d, "
             "the disc of radius r + d if d reaches r.\n\n"
             "Args:\n"
             "    d: the distance, > 0.\n\n"
             "Raises:\n"
             "    ValueError: d is not positive and finite.\n\n"
             "Examples:\n"
             "    circle.Offset(0.5)");

    py::class_<Spline>(m, "SplineEdge", "The geometry of a spline; edges live in wires, made by px.Spline or px.BSpline.")
        .def_property_readonly("start", &Spline::Start, "start: Vertex\n\nThe start, a Vertex.")
        .def_property_readonly("end", &Spline::End, "end: Vertex\n\nThe end, a Vertex.")
        .def_property_readonly("points", &Spline::Points, "points: list[Vertex]\n\nThe points it runs through, from the start to the end.")
        .def_property_readonly("napprox", [](const Spline &s) { return s.Approximation().size(); },
                               "napprox: int\n\nThe number of lines and arcs of its approximation.")
        .def_property_readonly("approximation", [](const Spline &s) {
            return std::vector<std::variant<Line, Arc>>(s.Approximation().begin(), s.Approximation().end());
        }, "approximation: list[LineEdge | ArcEdge]\n\nThe lines and arcs planex computes with, its approximation.")
        .def_property_readonly("bspline", [](const Spline &s) {
            BSplineData b = s.BSpline();
            py::list poles;
            for (const Vertex &p : b.poles)
                poles.append(Tuple2(p));
            return py::dict(py::arg("poles") = poles, py::arg("knots") = b.knots, py::arg("degree") = b.degree,
                            py::arg("weights") = b.weights.empty() ? py::object(py::none()) : py::cast(b.weights));
        }, "bspline: dict\n\nThe exact curve of this piece as a dict: poles, flattened knots, degree, weights (None if not rational).")
        .def("MidPoint", &Spline::MidPoint,
             "MidPoint() -> Vertex\n\n"
             "A point near the middle of the spline: the midpoint of the middle piece of its arc approximation.\n\n"
             "Examples:\n"
             "    spline.MidPoint()")
        .def("Sample", [](const Spline &sp, double tol) {
            CheckSampleTol("Sample", tol);
            CheckSampleLimit("Sample", sp, "spline", tol);
            return VertexArray(sp.Sample(tol));
        }, py::arg("tol"),
           "Sample(tol: float) -> np.ndarray\n\n"
           "An (n, 2) array of points along the exact curve from the start to the end, each chord within tol of it.\n\n"
           "Args:\n"
           "    tol: the largest distance of a chord from the curve, > 0.\n\n"
           "Raises:\n"
           "    ValueError: tol is not positive and finite, or below the rounding of the spline's coordinates.\n\n"
           "Examples:\n"
           "    spline.Sample(1e-3)")
        .def("Tangent", &Spline::Tangent, py::arg("p"),
             "Tangent(p: Point) -> Vertex\n\n"
             "The unit tangent in the spline's direction at its point p; (0, 0) if p is not on the spline.\n\n"
             "Args:\n"
             "    p: a point (h, v) of the spline.\n\n"
             "Examples:\n"
             "    spline.Tangent(spline.MidPoint())")
        .def("Offset", [](const Spline &sp, double d) { return sp.Offset(d).SingleFace("Offset(d)"); }, py::arg("d"),
             "Offset(d: float) -> Face\n\n"
             "The face of the points within d of the spline: its outline runs at distance d around it, round at the ends.\n\n"
             "Args:\n"
             "    d: the distance, > 0.\n\n"
             "Raises:\n"
             "    ValueError: d is not positive and finite.\n\n"
             "Examples:\n"
             "    spline.Offset(0.5)");
    AddCopy<Line>(m, "LineEdge");
    AddCopy<Arc>(m, "ArcEdge");
    AddCopy<Circle>(m, "CircleEdge");
    AddCopy<Spline>(m, "SplineEdge");
}
