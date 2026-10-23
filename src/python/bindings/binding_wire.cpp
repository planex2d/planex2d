// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"

void BindWire(py::module_ &m, py::class_<Wire> &wire) {
    py::options options;
    options.disable_function_signatures();
    auto ref = py::return_value_policy::reference_internal;
    wire
        .def(py::init<>(),
             "Wire()\n"
             "Wire(edges: Wire | EdgeView | list[Wire | EdgeView], /, tol: float | None = None)\n\n"
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
           "Add(edge: Wire | EdgeView | list[Wire | EdgeView], tol: float | None = None) -> Wire\n\n"
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
        .def("Offset", [](const Wire &w, double d) { return Shape(w.Offset(d), nullptr); }, py::arg("d"),
             "Offset(d: float) -> Shape\n\n"
             "The band within distance d of the edges, the union of the offsets of all edges, as a Shape.\n\n"
             "Args:\n"
             "    d: the distance from the edges, a length.\n\n"
             "Examples:\n"
             "    band = wire.Offset(0.5)")
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
            w.MergeCollinearEdges(Or(tol, config::cleanup.merge_collinear_closer));
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
             ExportDoc("Simplified", "Wire", "A copy defeatured as Simplify does it; this wire is not changed.", "wire", true))
        .def("ToOCCShape", [](const Wire &w, EXPORT_PARAMS) { return CompoundOf(ToOCCWires(w, false, WIRE_EXPORT_OPTIONS)); },
             EXPORT_ARGS, ExportDoc("ToOCCShape", "TopoDS_Shape", "The wires of the edges as an OCC compound, as ToOCCWires().", "wire", true))
        .def("ToOCCWire", [](const Wire &w, bool approximation, EXPORT_PARAMS) { return ToOCCWire(w, approximation, WIRE_EXPORT_OPTIONS); },
             py::arg("approximation") = false, EXPORT_ARGS,
             ExportDoc("ToOCCWire", "TopoDS_Shape", "The edges as a single OCC wire.", "wire", true, true))
        .def("ToOCCWires", [](const Wire &w, bool approximation, EXPORT_PARAMS) {
            return CompoundOf(ToOCCWires(w, approximation, WIRE_EXPORT_OPTIONS));
        }, py::arg("approximation") = false, EXPORT_ARGS,
           ExportDoc("ToOCCWires", "TopoDS_Shape", "The wires of the edges as an OCC compound.", "wire", true, true))
        .def("ToOCCFace", [](const Wire &w, EXPORT_PARAMS) { return TypedShape(ToOCCFace(w, EXPORT_OPTIONS)); }, EXPORT_ARGS,
             ExportDoc("ToOCCFace", "TopoDS_Shape",
                       "The faces of the closed wires of the edges, without removing internal edges: an OCC face, or a "
                       "compound of several.", "wire", true));
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
    BindMeasures<Wire>(wire, [](const Wire &w) { return ThingOf(w); },
                       [](const Wire &w, const std::optional<std::string> &n) { return NamedIn(w.Axes(), {}, w, n); });
    BindMotion<Wire>(wire);
    BindCopy<Wire>(wire);
    BindCorners<Wire>(wire, [](Wire &w) -> Wire & { return w; });
    wire.def("Project", [](const Wire &x, const TopoDS_Shape &shape) { return ProjectedWires(shape, x.Axes()); }, py::arg("shape"), py::pos_only(),
             ProjectDoc("wire"));
    BindEdgeProperties<Wire>(wire, [](Wire &w) -> Wire & { return w; });
    m.def("Wires", [](const py::object &edges, OptDouble tol) {
        CheckJoinTol(tol, "Wires");
        std::optional<AxisSystem> axes;
        EdgeList list = EdgesArg(edges, "Wires", axes);
        return WiresOf(list, axes.value_or(AxisSystem()), tol);
    }, py::arg("edges"), py::pos_only(), py::arg("tol") = py::none(),
          "Wires(edges: Wire | EdgeView | list[Wire | EdgeView], /, tol: float | None = None) -> list[Wire]\n\n"
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
