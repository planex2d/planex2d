// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"

void BindFace(py::class_<Face> &face) {
    py::options options;
    options.disable_function_signatures();
    auto ref = py::return_value_policy::reference_internal;
    face
        .def(py::init<>(),
             "Face()\n"
             "Face(wires: list[Wire])\n"
             "Face(view: FaceView)\n\n"
             "The region bounded by the wires, a copy of a face of a shape, or an empty face.\n\n"
             "Holes come by nesting; stretches drawn twice by closed wires cancel, an open wire's stretch on another edge "
             "is left out.\n\n"
             "Args:\n"
             "    wires: the wires that bound the region.\n"
             "    view: a face of a shape (shape.faces[0]).\n\n"
             "Raises:\n"
             "    ValueError: the region has several parts; px.Shape(wires) keeps them all.\n\n"
             "Examples:\n"
             "    px.Face([outline, hole])\n"
             "    px.Face(shape.faces[0])")
        .def(py::init([](const WireList &wires) { return wires.Region().SingleFace("Face(wires)"); }), py::arg("wires"), "")
        .def_property_readonly("lines", &Face::Lines, "lines: list[LineEdge]\n\nThe lines among the edges, copies.")
        .def_property_readonly("arcs", &Face::Arcs, "arcs: list[ArcEdge]\n\nThe arcs among the edges, copies.")
        .def_property_readonly("circles", &Face::Circles, "circles: list[CircleEdge]\n\nThe circles among the edges, copies.")
        .def_property_readonly("splines", &Face::Splines, "splines: list[SplineEdge]\n\nThe splines among the edges, copies.")
        .def_property_readonly("edges", py::cpp_function([](Face &f) { return EdgesView{&f.Edges()}; }, py::keep_alive<0, 1>()),
                               "edges: EdgesView\n\nThe edges, a view of the face's edges: a property set on it is set on all of them.")
        .def_property_readonly("vertices", py::cpp_function([](Face &f) { return VerticesView{&f.Edges()}; }, py::keep_alive<0, 1>()),
                               "vertices: VerticesView\n\nThe vertices, each once, in the order the edges reach them.")
        .def_property_readonly("mass", &Face::Area, "mass: float\n\nThe area, holes subtracted; splines on their exact curve.")
        .def_property_readonly("wires", &Face::Wires, "wires: list[Wire]\n\nThe loops, the outline and the holes, a list of wires.")
        .def_property_readonly("faces", [](const py::object &self) { return py::list(py::make_tuple(self)); },
                               "faces: list[Face]\n\nA list of this face alone.")
        .def("Wire", [](const Face &f) {
            WireList wires = f.Wires();
            if (wires.size() != 1)
                throw py::value_error("Wire(): the face has " + std::to_string(wires.size()) + " loops (the outline and holes); "
                                      ".wires gives them, .outer the outline");
            return wires.front();
        }, "Wire() -> Wire\n\n"
           "The single loop of a face without holes.\n\n"
           "Raises:\n"
           "    ValueError: the face has holes; .wires gives the loops, .outer the outline.\n\n"
           "Examples:\n"
           "    face.Wire()")
        .def("Offset", [](const py::object &self, double d, bool inplace, OffsetForm corners) -> py::object {
            Face &f = self.cast<Face &>();
            Shape r = OffsetFaces(Shape(f), d, CornersOf(corners));
            if (!inplace)
                return py::cast(std::move(r));
            FaceList faces = r.Faces();
            if (faces.size() != 1)
                throw py::value_error("Offset(" + std::to_string(d) + "): the offset is " + std::to_string(faces.size()) +
                                      " faces, which one face cannot hold; Offset(d, inplace=False) returns them as a Shape");
            ReplaceKeepingId(f, std::move(faces.front()));
            return self;
        }, py::arg("d"), py::arg("inplace") = true, py::arg("corners") = OffsetForm::Round,
           OffsetDoc("Face", "Face | Shape", "in place, the offset is several faces; inplace=False returns them as a Shape;", "face"))
        .def("OpenEnds", &Face::OpenEnds, OpenEndsDoc("face"))
        .def("IsClosed", &Face::IsClosed, IsClosedDoc("face"))
        .def("SmallEdges", &Face::SmallEdges, py::arg("tol"), SmallEdgesDoc("face"))
        .def("Intersections", [](const Face &f, const Wire &other) { return Intersections(f.Edges(), other); }, py::arg("other"), py::pos_only(),
             IntersectionsDoc("face", "its edges"))
        .def("Simplify", [](Face &face, EXPORT_PARAMS) -> Face & { return face.Simplify(EXPORT_OPTIONS); }, EXPORT_ARGS, ref,
             ExportDoc("Simplify", "Face",
                       "Defeatures the face in place as the OCC exports do: gaps closed, collinear lines merged, arcs and "
                       "splines fitted, small edges and edges that touch no other edge at either end removed, as enabled.",
                       "face", false))
        .def("Simplified", [](const Face &face, EXPORT_PARAMS) { Face r = face; return r.Simplify(EXPORT_OPTIONS), r; }, EXPORT_ARGS,
             ExportDoc("Simplified", "Face", "A copy defeatured as Simplify does it; this face is not changed.", "face", false))
        .def("CloseGaps", &Face::RemoveSmallGaps, py::arg("tol"), ref, CloseGapsDoc("Face", "face", ""))
        .def("RemoveSmallEdges", &Face::RemoveSmallEdges, py::arg("tol"), ref, RemoveSmallEdgesDoc("Face", "face"))
        .def("MergeCollinearEdges", [](Face &f, OptDouble tol) -> Face & {
            return f.MergeCollinearEdges(Or(tol, config::state->cleanup.merge_collinear_closer));
        }, py::arg("tol") = py::none(), ref, MergeCollinearEdgesDoc("Face", "", "face"))
        .def("FitArcs", [](Face &f, OptDouble tol, OptDouble l, OptDouble a, OptDouble ig) -> Face & {
            return f.FitArcs(Arcs(true, l, a, tol, ig));
        }, ARCFIT_ARGS, ref,
           FitArcsDoc("Face", "Replaces chains of at least three edges shorter than max_edge_length by arcs of at most 180 "
                              "degrees (or lines) that stay within tol of the chain, in place.", "face"));
    face.def_property_readonly("outer", &Face::Outer, "outer: Wire\n\nThe outer loop, a Wire, its edges in counterclockwise order.")
        .def_property_readonly("inner", &Face::Holes, "inner: list[Wire]\n\nThe inner loops (the holes), a list of wires, their edges in clockwise order.")
        .def("Contains", [](const Face &f, const py::object &other, bool boundary) {
            return ContainsAny(Planar{f.Edges(), Wire(f.Axes())}, f.Axes(), other, boundary,
                               [&] { return RelateOperand{2, f.Edges(), {}}; });
        }, py::arg("other"), py::pos_only(), py::arg("boundary") = true, ContainsDoc("face", "the face"));
    face.def("Polylines", [](const Face &f, double tol) {
        CheckSampleTol("Polylines", tol);
        py::list out;
        AddFaceLoops(f.Edges(), tol, out);
        return out;
    }, py::arg("tol"), PolylinesDoc("face"));
    face.def("Split", [](const Face &f, const py::object &cutter) { return SplitShape(Shape(f), CutterIn(f.Axes(), cutter)); },
             py::arg("cutter"), py::pos_only(), SplitDoc("face"))
        .def("Trim", [](const Face &f, TRIM_PARAMS) {
            return TrimShape(Shape(f), CutterIn(f.Axes(), cutter), RuleOf(f.Axes(), kc, rc, kl, ks, kleft, kright));
        }, TRIM_ARGS, TrimDoc("face"));
    BindRelations<Face>(face, [](const Face &f) { return RelateOperand{2, f.Edges(), {}}; });
    BindMeasures<Face>(face, [](const Face &f) { return ThingOf(f); },
                       [](const Face &f, const std::optional<std::string> &n) { return NamedIn(f.Axes(), {f}, f.Edges(), n); });
    BindMotion<Face>(face);
    FaceFields([&](const char *name, auto get, auto set) {
        using V = SetterValueOf<decltype(set)>;
        face.def_property(name, [get](const Face &f) { return get(PropsOrDefault(f.Props())); }, [set](Face &f, const V &v) {
            f.SetProps(ChangedProperties(f.Props(), [&](ShapeProperties &q) { set(q, v); }));
        }, FieldDoc(name, " of the face; None if not set."));
    });
    BindCopy<Face>(face);
    BindCorners<Face>(face, [](Face &f) -> Wire & { return f.Edges(); });
}
