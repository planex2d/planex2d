// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"

// one row per edge of a kind, cols values each, as a float64 array (n, cols)
inline py::array_t<double> Rows(const std::vector<double> &values, size_t cols) {
    py::array_t<double> a({(py::ssize_t)(values.size() / cols), (py::ssize_t)cols});
    std::copy(values.begin(), values.end(), a.mutable_data());
    return a;
}

// the construction data of the edges, one array per kind in the plane's (h, v), and the order along the wire
inline py::dict NumpyOf(const Wire &w) {
    std::vector<double> lines, arcs, circles;
    std::vector<int64_t> order;
    py::list splines;
    auto at = [&](int64_t kind, size_t row) {
        order.push_back(kind);
        order.push_back((int64_t)row);
    };
    for (const Edge &e : w)
        std::visit([&](const auto &x) {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, Line>) {
                at(0, lines.size() / 4);
                lines.insert(lines.end(), {x.Start().x, x.Start().y, x.End().x, x.End().y});
            } else if constexpr (std::is_same_v<T, Arc>) {
                at(1, arcs.size() / 7);
                arcs.insert(arcs.end(), {x.Start().x, x.Start().y, x.End().x, x.End().y, x.Center().x, x.Center().y, x.CCW() ? 1.0 : -1.0});
            } else if constexpr (std::is_same_v<T, Circle>) {
                at(2, circles.size() / 6);
                circles.insert(circles.end(), {x.Center().x, x.Center().y, x.Radius(), x.Start().x, x.Start().y, x.CCW() ? 1.0 : -1.0});
            } else {
                at(3, splines.size());
                const BSplineData b = x.BSpline();
                splines.append(py::dict(py::arg("degree") = b.degree, py::arg("poles") = VertexArray(b.poles),
                                        py::arg("knots") = py::array_t<double>((py::ssize_t)b.knots.size(), b.knots.data()),
                                        py::arg("weights") = b.weights.empty() ? py::object(py::none())
                                                                               : py::object(py::array_t<double>((py::ssize_t)b.weights.size(), b.weights.data()))));
            }
        }, e);
    py::array_t<int64_t> o({(py::ssize_t)(order.size() / 2), (py::ssize_t)2});
    std::copy(order.begin(), order.end(), o.mutable_data());
    return py::dict(py::arg("lines") = Rows(lines, 4), py::arg("arcs") = Rows(arcs, 7), py::arg("circles") = Rows(circles, 6),
                    py::arg("splines") = splines, py::arg("order") = o);
}

inline Wire WireOfNumpy(const py::dict &d, const AxisSystem &axes) {
    auto rows = [&](const char *key, size_t cols) {
        if (!d.contains(key))
            throw py::value_error(std::string("FromNumpy: the data has no '") + key + "'; ToNumpy gives lines, arcs, circles, splines and order");
        auto a = py::array_t<double, py::array::c_style | py::array::forcecast>::ensure(d[key]);
        if (!a || (a.size() > 0 && (a.ndim() != 2 || (size_t)a.shape(1) != cols)))
            throw py::value_error(std::string("FromNumpy: '") + key + "' must be an (n, " + std::to_string(cols) + ") array");
        return a;
    };
    const auto lines = rows("lines", 4), arcs = rows("arcs", 7), circles = rows("circles", 6);
    if (!d.contains("splines") || !d.contains("order"))
        throw py::value_error("FromNumpy: the data has no 'splines' or 'order'; ToNumpy gives lines, arcs, circles, splines and order");
    const py::list splines = d["splines"].cast<py::list>();
    const auto order = py::array_t<int64_t, py::array::c_style | py::array::forcecast>::ensure(d["order"]);
    if (!order || (order.size() > 0 && (order.ndim() != 2 || order.shape(1) != 2)))
        throw py::value_error("FromNumpy: 'order' must be an (n, 2) array of kind and row");
    const size_t sizes[] = {(size_t)(lines.size() / 4), (size_t)(arcs.size() / 7), (size_t)(circles.size() / 6), splines.size()};
    Wire w(axes);
    const int64_t *o = order.data();
    for (py::ssize_t i = 0; i < order.size() / 2; ++i) {
        const int64_t kind = o[2 * i], row = o[2 * i + 1];
        if (kind < 0 || kind > 3 || row < 0 || (size_t)row >= sizes[kind])
            throw py::value_error("FromNumpy: order row " + std::to_string(i) + " names no edge");
        if (kind == 0) {
            const double *v = lines.data() + 4 * row;
            w.Add(Line({v[0], v[1]}, {v[2], v[3]}));
        } else if (kind == 1) {
            const double *v = arcs.data() + 7 * row;
            w.Add(Arc({v[0], v[1]}, {v[2], v[3]}, {v[4], v[5]}, v[6] > 0.0));
        } else if (kind == 2) {
            const double *v = circles.data() + 6 * row;
            w.Add(Circle({v[0], v[1]}, Vertex{v[3], v[4]}, v[5] > 0.0));
        } else {
            const py::dict sd = splines[(size_t)row].cast<py::dict>();
            std::vector<Vertex> poles;
            for (const py::handle &p : sd["poles"])
                poles.push_back({p[py::int_(0)].cast<double>(), p[py::int_(1)].cast<double>()});
            const py::object weights = sd["weights"];
            BSplineData b{poles, sd["knots"].cast<std::vector<double>>(),
                          weights.is_none() ? std::vector<double>{} : weights.cast<std::vector<double>>(), sd["degree"].cast<int>()};
            for (planex2d::Spline &s : planex2d::Spline::FromCurve(BSplineCurve(b, false)))
                w.Add(std::move(s));
        }
    }
    return w;
}

// before any method: px.Round is the default of Offset's ends= and corners=
void BindOffsetForms(py::module_ &m) {
    py::options options;
    options.disable_function_signatures();
    py::enum_<OffsetForm> forms(m, "OffsetForm",
                                "The forms of an offset's ends (px.Round, px.Flat, px.Square) and corners (px.Round, px.Sharp, px.Chamfer).");
    for (OffsetForm f : {OffsetForm::Round, OffsetForm::Flat, OffsetForm::Square, OffsetForm::Sharp, OffsetForm::Chamfer})
        forms.value(OffsetFormName(f), f);
    forms.export_values();
}

void BindWire(py::module_ &m, py::class_<Wire> &wire) {
    py::options options;
    options.disable_function_signatures();
    auto ref = py::return_value_policy::reference_internal;
    wire
        .def(py::init<>(),
             "Wire()\n"
             "Wire(edges: Wire | EdgeView | LineEdge | ArcEdge | CircleEdge | SplineEdge | list[Wire | EdgeView | LineEdge | ArcEdge | CircleEdge | SplineEdge], /, tol: float | None = None)\n\n"
             "The wire of edges that form exactly one chain, in any order; without edges an empty wire, the start for Add.\n\n"
             "Ends within tol are joined bit-exactly; nothing is cut or reversed: the edges are in chain order but each "
             "keeps its own direction (a spline is one edge), so wire.edges[i].start is not always the end of edge i - 1.\n\n"
             "Args:\n"
             "    edges: an edge or wire, or a list of them (px.Line, px.Arc, px.Spline, edge views, wires).\n"
             "    tol: the largest gap joined; None: the rounding.\n\n"
             "Raises:\n"
             "    ValueError: the edges form several chains, branch, cross or overlap (px.Wires builds clean wires from "
             "them), lie on different planes, or tol is negative or not finite.\n"
             "    TypeError: an item is not an edge or wire.\n\n"
             "Examples:\n"
             "    px.Wire([px.Line((0, 0), (1, 0)), px.Line((1, 1), (1, 0))])\n"
             "    px.Wire(edges, tol=1e-6)")
        .def(py::init([](const py::object &edges, OptDouble tol) {
            CheckJoinTol(tol, "Wire");
            std::optional<AxisSystem> axes;
            EdgeList list = EdgesArg(edges, "Wire(edges)", axes);
            return ChainOf(list, axes.value_or(AxisSystem()), tol, "Wire(edges)");
        }), py::arg("edges"), py::pos_only(), py::arg("tol") = py::none(), "")
        .def_property_readonly("edges", py::cpp_function([](Wire &w) { return EdgesView{&w}; }, py::keep_alive<0, 1>()),
                               "edges: EdgesView\n\nThe edges in chain order, a view of the wire's edges: a property set on it is set on all of them.")
        .def_property_readonly("vertices", py::cpp_function([](Wire &w) { return VerticesView{&w}; }, py::keep_alive<0, 1>()),
                               "vertices: VerticesView\n\nThe vertices, each once, in the order the edges reach them.")
        .def_property_readonly("lines", &Wire::All<Line>, "lines: list[LineEdge]\n\nThe lines among the edges, copies.")
        .def_property_readonly("arcs", &Wire::All<Arc>, "arcs: list[ArcEdge]\n\nThe arcs among the edges, copies.")
        .def_property_readonly("circles", &Wire::All<Circle>, "circles: list[CircleEdge]\n\nThe circles among the edges, copies.")
        .def_property_readonly("splines", &Wire::All<Spline>, "splines: list[SplineEdge]\n\nThe splines among the edges, copies.")
        .def("Add", [](Wire &w, const py::object &edge, OptDouble tol) -> Wire & {
            CheckJoinTol(tol, "Add");
            std::optional<AxisSystem> axes;
            EdgeList piece = EdgesArg(edge, "Add", axes);
            if (!piece.empty() && axes && !w.Empty() && !w.Axes().IsClose(*axes))
                throw py::value_error("Add: the edge lies on another plane than the wire; move it onto the wire's plane first");
            if (!piece.empty() && axes && w.Empty())
                w.SetAxes(*axes);
            w.Extend(piece, tol, "Add");
            return w;
        }, py::arg("edge"), py::arg("tol") = py::none(), ref,
           "Add(edge: Wire | EdgeView | LineEdge | ArcEdge | CircleEdge | SplineEdge | list[Wire | EdgeView | LineEdge | ArcEdge | CircleEdge | SplineEdge], tol: float | None = None) -> Wire\n\n"
           "Extends the chain in place: the edge or wire is appended at the wire's end or prepended at its start.\n\n"
           "It joins in either direction and keeps its own; touching both ends closes the wire. Its ends within tol of "
           "the wire's are moved onto them; the wire's edges stay as they are.\n\n"
           "Args:\n"
           "    edge: an edge (px.Line, px.Arc, px.Spline, an edge view) or a wire.\n"
           "    tol: the largest gap joined; None: the rounding.\n\n"
           "Returns:\n"
           "    this Wire.\n\n"
           "Raises:\n"
           "    ValueError: the wire is closed, the edge touches neither free end, it would cross, overlap or branch the "
           "wire (px.Wires builds several wires), it lies on another plane, or tol is negative or not finite; nothing "
           "changes then.\n"
           "    TypeError: edge is not an edge or wire.\n\n"
           "Examples:\n"
           "    w = px.Wire().Add(px.Line((0, 0), (1, 0))).Add(px.Line((1, 0), (1, 1)))\n"
           "    w.Add(px.Line((1, 1), (0, 0)), tol=1e-6)")
        .def("__add__", [](const Wire &a, const py::object &b) {
            if (py::isinstance<Face>(b) || py::isinstance<FaceView>(b))
                throw py::type_error("wire + face is not supported");
            return Combined(Operands(WireShape(a), b), &Unify, false);
        }, "__add__(other: Wire | Shape) -> Shape\n\n"
           "The union of the wires as a Shape of their connected chains: crossings split both edges, an overlapping "
           "stretch stays once.\n\n"
           "Raises:\n"
           "    TypeError: other is a face, or not a Wire or Shape.\n\n"
           "Examples:\n"
           "    wire + other")
        .def("__mul__", [](const Wire &a, const py::object &b) { return Combined(Operands(WireShape(a), b), &Intersect, false); },
             "__mul__(other: Face | Shape) -> Shape\n\n"
             "The pieces of the wire inside the faces (on their boundary counts as inside), cut where it meets their "
             "boundary, as a Shape of the connected chains.\n\n"
             "Examples:\n"
             "    wire * face")
        .def("__sub__", [](const Wire &a, const py::object &b) { return Combined(Operands(WireShape(a), b), &Subtract, false); },
             "__sub__(other: Face | Shape) -> Shape\n\n"
             "The pieces of the wire outside the faces, as a Shape of the connected chains; w * f and w - f together are w.\n\n"
             "Examples:\n"
             "    wire - face")
        .def_property_readonly("mass", &Wire::Length, "mass: float\n\nThe total length of the edges.")
        .def_property_readonly("wires", &Wire::Wires, "wires: list[Wire]\n\nThe connected chains of the edges in traversal order, a list of wires.")
        .def_property_readonly("faces", [](const Wire &) { return FaceList{}; }, "faces: list[Face]\n\nAn empty list: a wire has no faces.")
        .def("Face", [](const Wire &w) {
            return InitialClean(w, std::vector<bool>(w.Size(), !w.HasEvenDegrees(w.Tol()))).SingleFace("Face()");
        }, "Face() -> Face\n\n"
           "The region bounded by the edges: overlapping stretches removed (of an open wire one copy stays), crossings "
           "cut.\n\n"
           "Raises:\n"
           "    ValueError: the region has several parts; px.Shape([wire]) keeps them all.\n\n"
           "Examples:\n"
           "    face = wire.Face()")
        .def("Offset", [](const Wire &w, double d, OffsetForm ends, OffsetForm corners) {
            return Shape(w.Offset(d, EndsOf(ends), CornersOf(corners)), nullptr);
        }, py::arg("d"), py::arg("ends") = OffsetForm::Round, py::arg("corners") = OffsetForm::Round,
             "Offset(d: float, ends: OffsetForm = px.Round, corners: OffsetForm = px.Round) -> Shape\n\n"
             "The band within distance d of the edges, as a Shape: the offsets of the edges to both sides, joined around "
             "the corners and closed at the open ends.\n\n"
             "A spline's offset is one spline within config.spline_tol of the exact offset. The ends of an open wire are a "
             "half circle (px.Round), the line across (px.Flat) or flat d beyond the end (px.Square). A corner's outside is "
             "an arc of radius d about it (px.Round), the two offsets extended along their end tangents until they meet "
             "(px.Sharp, without a limit: a corner of angle theta between its edges reaches d / sin(theta / 2) out from "
             "it) or the line between their ends (px.Chamfer). Where the band overlaps itself it is one region.\n\n"
             "Args:\n"
             "    d: the distance from the edges, a length > 0.\n"
             "    ends: px.Round, px.Flat or px.Square.\n"
             "    corners: px.Round, px.Sharp or px.Chamfer.\n\n"
             "Raises:\n"
             "    ValueError: d is not positive and finite, ends is px.Sharp or px.Chamfer, corners is px.Flat or "
             "px.Square.\n\n"
             "Examples:\n"
             "    band = wire.Offset(0.5)\n"
             "    track = wire.Offset(0.2, ends=px.Flat, corners=px.Sharp)")
        .def("Intersections", [](const Wire &w, const Wire &other) { return Intersections(w, other); }, py::arg("other"), py::pos_only(),
             IntersectionsDoc("wire", "its edges"))
        .def("OpenEnds", &Wire::OpenEnds, OpenEndsDoc("wire"))
        .def("IsClosed", &Wire::IsClosed, IsClosedDoc("wire"))
        .def("SmallEdges", &Wire::SmallEdges, py::arg("tol"), SmallEdgesDoc("wire"))
        .def("CloseGaps", [](Wire &w, double tol) -> Wire & {
            Wire r = w;
            r.RemoveSmallGaps(tol);
            r.CheckChain("CloseGaps");
            w = std::move(r);
            return w;
        }, py::arg("tol"), ref,
             CloseGapsDoc("Wire", "wire", "It closes the wire if its ends are that close.\n\n"
                                          "Raises:\n"
                                          "    ValueError: an end would land on the wire's own inner vertex; nothing changes then.\n\n"))
        .def("RemoveSmallEdges", [](Wire &w, double tol) -> Wire & { w.RemoveSmallEdges(tol); return w; }, py::arg("tol"), ref,
             RemoveSmallEdgesDoc("Wire", "wire"))
        .def("MergeCollinearEdges", [](Wire &w, OptDouble tol) -> Wire & {
            w.MergeCollinearEdges(Or(tol, config::state->cleanup.merge_collinear_closer));
            return w;
        }, py::arg("tol") = py::none(), ref, MergeCollinearEdgesDoc("Wire", "", "wire"))
        .def("FitArcs", [](Wire &w, OptDouble tol, OptDouble l, OptDouble a, OptDouble ig) -> Wire & {
            w.FitArcs(Arcs(true, l, a, tol, ig));
            return w;
        }, ARCFIT_ARGS, ref,
           FitArcsDoc("Wire", "Replaces chains of at least three edges shorter than max_edge_length by arcs of at most 180 "
                              "degrees (or lines) that stay within tol of the chain, in place.", "wire"))
        .def("Simplify", [](Wire &w, EXPORT_PARAMS) -> Wire & { return w.Simplify(WIRE_EXPORT_OPTIONS); }, EXPORT_ARGS, ref,
             ExportDoc("Simplify", "Wire",
                       "Defeatures the wire in place as the OCC exports do: gaps closed, collinear lines merged, arcs and "
                       "splines fitted, small edges removed, as enabled.\n\n"
                       "Edges that touch no other edge at either end are removed only with remove_free_edges=True.",
                       "wire", true))
        .def("Simplified", [](const Wire &w, EXPORT_PARAMS) { return w.Cleaned(WIRE_EXPORT_OPTIONS); }, EXPORT_ARGS,
             ExportDoc("Simplified", "Wire", "A copy defeatured as Simplify does it; this wire is not changed.", "wire", true));
    wire.def("Polylines", [](const Wire &w, double tol) {
        CheckSampleTol("Polylines", tol);
        py::list out;
        AddWireLoops(w, tol, out);
        return out;
    }, py::arg("tol"), PolylinesDoc("wire"));
    wire.def("ToPolyline", [](const Wire &w, double tol) {
        CheckSampleTol("ToPolyline", tol);
        py::list out;
        AddWireLoops(w, tol, out);
        if (out.size() != 1)
            throw py::value_error("ToPolyline: the wire is " + std::to_string(out.size()) + " chains; Polylines(tol) gives them as a list");
        return py::object(out[0]);
    }, py::arg("tol"),
       Doc("ToPolyline(tol: float) -> np.ndarray\n\n"
           "The wire as one (n, 2) array of plane coordinates, each chord within tol of its edge; a closed wire ends at "
           "its first point. The points run the way the wire's first edge runs, so a reversed wire gives the reversed "
           "polyline. px.FromPolyline makes a wire of it again.\n\n"
           "Args:\n"
           "    tol: the largest distance of a chord from its edge, a length > 0.\n\n"
           "Raises:\n"
           "    ValueError: tol is not positive and finite, or below the rounding of an arc's or a spline's coordinates; "
           "the wire is several chains.\n\n"
           "Examples:\n"
           "    points = wire.ToPolyline(0.01)\n"
           "    px.FromPolyline(points)"));
    wire.def("Split", [](const Wire &w, const py::object &cutter) {
        return SplitShape(Shape(w.Axes(), {}, {w}), CutterIn(w.Axes(), cutter));
    }, py::arg("cutter"), py::pos_only(), SplitDoc("wire"))
        .def("Trim", [](const Wire &w, TRIM_PARAMS) {
            return TrimShape(Shape(w.Axes(), {}, {w}), CutterIn(w.Axes(), cutter), RuleOf(w.Axes(), kc, rc, kl, ks, kleft, kright));
        }, TRIM_ARGS, TrimDoc("wire"));
    BindRelations<Wire>(wire, [](const Wire &w) { return RelateOperand{1, w, {}}; });
    wire.def("ToNumpy", &NumpyOf,
             "ToNumpy() -> dict\n\n"
             "The construction data of the edges in the plane's (h, v), float64 arrays, one row per edge: 'lines' (n, 4) "
             "start, end; 'arcs' (n, 7) start, end, center, +1 counterclockwise or -1; 'circles' (n, 6) center, radius, "
             "start, +1 or -1; 'splines' a list of dicts (degree, poles (m, 2), knots, weights or None); 'order' (N, 2) "
             "int, for each edge along the wire its kind (0 lines, 1 arcs, 2 circles, 3 splines) and row. Properties "
             "are not included; px.FromNumpy builds the wire back.\n\n"
             "Examples:\n"
             "    d = wire.ToNumpy()\n"
             "    d['lines'][:, :2]");
    m.def("FromNumpy", [](const py::dict &data, const py::object &axes) { return WireOfNumpy(data, AxesArg(axes, "FromNumpy")); },
          py::arg("data"), py::pos_only(), py::arg("plane") = py::none(),
          "FromNumpy(data: dict, /, plane: Plane | None = None) -> Wire\n\n"
          "The wire of Wire.ToNumpy's data, its edges in the order 'order' gives. Lines, arcs and circles come back "
          "exactly, splines on their exact curve with a new approximation; no properties.\n\n"
          "Args:\n"
          "    data: the dict of Wire.ToNumpy.\n"
          "    plane: the plane of the wire; default the xy plane.\n\n"
          "Raises:\n"
          "    ValueError: an array has the wrong shape, or 'order' names no edge.\n\n"
          "Examples:\n"
          "    px.FromNumpy(wire.ToNumpy())\n"
          "    px.FromNumpy(d, plane=px.XZ)");
    wire.def("Contains", [](const Wire &w, const py::object &other) {
        return RelationOf(RelateOperand{1, w, {}}, other, "Contains").Matches("T*****FF*");
    }, py::arg("other"), py::pos_only(),
             "Contains(other: Face | Shape | Wire | Point, /) -> bool\n\n"
             "True if other lies on the wire and their interiors meet (DE-9IM, see Relate): a point on an edge, not at "
             "an open end.\n\n"
             "Args:\n"
             "    other: a Face, Shape, Wire, or a point (h, v) or (x, y, z) on the plane.\n\n"
             "Raises:\n"
             "    TypeError: other is not a Face, Shape, Wire or point.\n"
             "    ValueError: other lies on another plane.\n\n"
             "Examples:\n"
             "    wire.Contains((1, 0))\n"
             "    wire.Contains(other_wire)");
    BindMeasures<Wire>(wire, [](const Wire &w) { return ThingOf(w); },
                       [](const Wire &w, const std::optional<std::string> &n) { return NamedIn(w.Axes(), {}, w, n); });
    BindMotion<Wire>(wire);
    BindCopy<Wire>(wire);
    BindCorners<Wire>(wire, [](Wire &w) -> Wire & { return w; });
    BindEdgeProperties<Wire>(wire, [](Wire &w) -> Wire & { return w; });
    m.def("Wires", [](const py::object &edges, OptDouble tol) {
        CheckJoinTol(tol, "Wires");
        std::optional<AxisSystem> axes;
        EdgeList list = EdgesArg(edges, "Wires", axes);
        return WiresOf(list, axes.value_or(AxisSystem()), tol);
    }, py::arg("edges"), py::pos_only(), py::arg("tol") = py::none(),
          "Wires(edges: Wire | EdgeView | LineEdge | ArcEdge | CircleEdge | SplineEdge | list[Wire | EdgeView | LineEdge | ArcEdge | CircleEdge | SplineEdge], /, tol: float | None = None) -> list[Wire]\n\n"
          "Loose edges as a list of clean wires: ends within tol joined, the edges cut against each other (crossings, "
          "T-junctions, an overlapping stretch kept once), then the chains.\n\n"
          "Nothing is merged, a gap above tol stays open. An outline with a rib ending on it gives the outline cut at the "
          "rib's end, still one closed wire, and the rib as its own wire.\n\n"
          "Args:\n"
          "    edges: an edge or wire, or a list of them (px.Line, px.Arc, px.Spline, edge views, wires).\n"
          "    tol: the largest gap joined; None: the rounding.\n\n"
          "Raises:\n"
          "    ValueError: the edges lie on different planes, or tol is negative or not finite.\n"
          "    TypeError: an item is not an edge or wire.\n\n"
          "Examples:\n"
          "    px.Wires([px.Line((0, 0), (2, 0)), px.Line((1, -1), (1, 1))])\n"
          "    px.Wires(edges, tol=1e-6)");
    m.def("_RegionOfEdges", [](const py::object &edges, bool chains) {
        std::optional<AxisSystem> axes;
        EdgeList list = EdgesArg(edges, "_RegionOfEdges", axes);
        Wire bag(axes.value_or(AxisSystem()));
        for (Edge &e : list)
            bag.Add(std::move(e));
        return Shape((chains ? bag.Wires() : WireList{bag}).Region(), nullptr);
    }, py::arg("edges"), py::pos_only(), py::arg("chains") = true,
          "for the readers: the regions of loose edges as px.Shape of their uncut chains (chains=True) or of them as "
          "one group, stretches drawn twice by closed groups cancelling");
}
