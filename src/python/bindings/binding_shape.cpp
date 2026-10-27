// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"

// the value all face groups share, none otherwise
template <class G>
static auto GroupsCommon(const Shape &s, G get) {
    return CommonOf(s.GroupCount(), [&](size_t k) { return s.GroupProps(k); }, get);
}

template <class S>
static void SetOnGroups(Shape &s, S change) {
    for (size_t k = 0; k < s.GroupCount(); ++k)
        s.SetGroupProps(k, ChangedProperties(s.GroupProps(k), change));
}

static void BindFaceViews(py::class_<FaceView> &faceView, py::class_<FacesView> &facesView) {
    facesView
        .def("__len__", [](FacesView &l) { return l.Current()->faces.size(); })
        .def("__getitem__", [](const py::object &self, const py::object &index) {
            FacesView &l = self.cast<FacesView &>();
            const std::shared_ptr<FaceIndex> at = l.Current();
            return ListItem(self, index, at->faces.size(), [&](size_t i) { return py::cast(FaceView{l.shape, i, at}); });
        }, py::arg("index"));
    FaceFields([&](const char *name, auto get, auto set) {
        using V = SetterValueOf<decltype(set)>;
        facesView.def_property(name, [get](const FacesView &l) { return GroupsCommon(*l.shape, get); },
                               [set](FacesView &l, const V &v) { SetOnGroups(*l.shape, [&](ShapeProperties &q) { set(q, v); }); },
                               FieldDoc(name, " all faces share, None if they differ; set on all faces."));
    });
    faceView
        .def("Copy", &FaceView::Copy,
             "Copy() -> Face\n\n"
             "The face as a Face of its own, independent of the shape.\n\n"
             "Examples:\n"
             "    f = shape.faces[0].Copy()")
        .def_property_readonly("id", [](const FaceView &v) {
            v.Get();
            return v.shape->Edges().Id() + ".Face." + std::to_string(v.i + 1);
        }, "id: str\n\nThe face's id: its shape's, then Face.k by its place in shape.faces.")
        .def_property_readonly("_plane", [](const FaceView &v) { return PlaneOf(v.shape->Edges()); }, "for the selectors: (origin, u, v) in 3d")
        .def_property_readonly("edges", py::cpp_function([](const FaceView &v) {
            return EdgesView{&v.shape->Edges(), v.EdgeIndices(), v.shape->Edges().Changes()};
        }, py::keep_alive<0, 1>()), "edges: EdgesView\n\nThe shape's edges of this face, a view: a change is made in the shape, on an edge "
                                    "shared with another face for that one too.")
        .def_property_readonly("vertices", py::cpp_function([](const FaceView &v) {
            return VerticesView{&v.shape->Edges(), v.EdgeIndices(), v.shape->Edges().Changes()};
        }, py::keep_alive<0, 1>()), "vertices: VerticesView\n\nThe shape's vertices of this face, each once, a view.")
        .def("__repr__", [](const FaceView &v) {
            const Name name = PropsOrDefault(v.Props()).name;
            return "FaceView(" + std::to_string(v.i) + (name ? ", name='" + *name + "'" : "") + ")";
        });
    FaceFields([&](const char *name, auto get, auto set) {
        using V = SetterValueOf<decltype(set)>;
        faceView.def_property(name, [get](const FaceView &v) { return get(PropsOrDefault(v.Props())); }, [set](FaceView &v, const V &x) {
            v.SetProps(ChangedProperties(v.Props(), [&](ShapeProperties &q) { set(q, x); }));
        }, FieldDoc(name, " of this face; None if not set; set in the shape."));
    });
}

void BindShape(py::module_ &m, py::class_<Shape> &shape, py::class_<Face> &face, py::class_<FaceView> &faceView,
               py::class_<FacesView> &facesView) {
    py::options options;
    options.disable_function_signatures();
    BindFaceViews(faceView, facesView);
    face.def(py::init([](const FaceView &v) { return v.Copy(); }), py::arg("view"), "");
    py::implicitly_convertible<FaceView, Face>();
    auto ref = py::return_value_policy::reference_internal;
    shape
        .def(py::init<>(),
             "Shape()\n"
             "Shape(face: Face)\n"
             "Shape(wires: list[Wire])\n\n"
             "A shape of one face, the regions bounded by the wires, or an empty shape.\n\n"
             "Holes come by nesting; stretches drawn twice by closed wires cancel, an open wire's stretch on another edge "
             "is left out.\n\n"
             "Args:\n"
             "    face: the one face.\n"
             "    wires: the wires that bound the regions.\n\n"
             "Examples:\n"
             "    px.Shape(face)\n"
             "    px.Shape([outline, hole])")
        .def(py::init<const Face &>(), py::arg("face"), "")
        .def(py::init([](const WireList &wires) { return Shape(wires.Region(), nullptr); }), py::arg("wires"), "")
        .def_property_readonly("lines", [](const Shape &s) { return s.Edges().All<Line>(); }, "lines: list[LineEdge]\n\nThe lines among the edges, copies.")
        .def_property_readonly("arcs", [](const Shape &s) { return s.Edges().All<Arc>(); }, "arcs: list[ArcEdge]\n\nThe arcs among the edges, copies.")
        .def_property_readonly("circles", [](const Shape &s) { return s.Edges().All<Circle>(); }, "circles: list[CircleEdge]\n\nThe circles among the edges, copies.")
        .def_property_readonly("splines", [](const Shape &s) { return s.Edges().All<Spline>(); }, "splines: list[SplineEdge]\n\nThe splines among the edges, copies.")
        .def_property_readonly("faces", py::cpp_function([](Shape &s) { return FacesView{&s}; }, py::keep_alive<0, 1>()),
                               "faces: FacesView\n\nThe faces, views whose properties, edges and vertices are the shape's.")
        .def_property_readonly("wires", &Shape::AllWires, "wires: list[Wire]\n\nThe loops of the faces and the connected chains of the wires, a list of wires.")
        .def("_FreeWires", [](const Shape &s) {
            WireList out;
            for (const Wire &part : s.WireParts())
                for (Wire &w : part.Wires())
                    out.push_back(std::move(w));
            return out;
        }, "the connected chains of the edges on no face")
        .def_property_readonly("vertices", py::cpp_function([](Shape &s) { return VerticesView{&s.Edges()}; }, py::keep_alive<0, 1>()),
                               "vertices: VerticesView\n\nThe vertices, each once, in the order the edges reach them; one vertex where parts meet.")
        .def_property_readonly("edges", py::cpp_function([](Shape &s) { return EdgesView{&s.Edges()}; }, py::keep_alive<0, 1>()),
                               "edges: EdgesView\n\nAll edges, each once, a view: a property set on it is set on all of them.")
        .def("Face", [](const Shape &s) {
            FaceList faces = s.Faces();
            if (faces.size() != 1 || s.WireCount() != 0)
                throw py::value_error("Face(): the shape has " + std::to_string(faces.size()) + " faces and " +
                                      std::to_string(s.WireCount()) + " wires; .faces gives the faces, .wires the wires");
            faces.front().Ids() = s.Ids();
            return faces.front();
        }, "Face() -> Face\n\n"
           "The single face of a shape of one face and no wires.\n\n"
           "Raises:\n"
           "    ValueError: the shape has several faces or wires; .faces gives the faces, .wires the wires.\n\n"
           "Examples:\n"
           "    shape.Face()")
        .def("Wire", [](const Shape &s) {
            WireList wires = s.AllWires();
            if (wires.size() != 1)
                throw py::value_error("Wire(): the edges form " + std::to_string(wires.size()) + " wires; .wires gives them");
            return wires.front();
        }, "Wire() -> Wire\n\n"
           "The single connected wire of all edges.\n\n"
           "Raises:\n"
           "    ValueError: the edges form several wires; .wires gives them.\n\n"
           "Examples:\n"
           "    shape.Wire()")
        .def_property_readonly("mass", &Shape::Area, "mass: float\n\nThe area of the faces.")
        .def("__add__", [](const Shape &a, const py::object &b) { return Combined(Operands(a, b), &Unify, false); },
             "__add__(other: Face | Shape | Wire) -> Shape\n\n"
             "The union: all faces merged into connected regions, or all wires into connected chains.\n\n"
             "Raises:\n"
             "    TypeError: other is not a Face, Shape or Wire.\n\n"
             "Examples:\n"
             "    shape + face")
        .def("__sub__", [](const Shape &a, const py::object &b) { return Combined(Operands(a, b), &Subtract, false); },
             "__sub__(other: Face | Shape | Wire) -> Shape\n\n"
             "Every face minus the other, keeping its properties.\n\n"
             "Raises:\n"
             "    TypeError: other is not a Face, Shape or Wire.\n\n"
             "Examples:\n"
             "    shape - hole")
        .def("__mul__", [](const Shape &a, const py::object &b) { return Combined(Operands(a, b), &Intersect, false); },
             "__mul__(other: Face | Shape | Wire) -> Shape\n\n"
             "The common part, every region with the first one's properties, maxh and minh the smallest of the faces it "
             "lies in.\n\n"
             "Raises:\n"
             "    TypeError: other is not a Face, Shape or Wire.\n\n"
             "Examples:\n"
             "    shape * face")
        .def("__xor__", [](const Shape &a, const py::object &b) { return Combined(Operands(a, b), &SymmetricDifference, false); },
             "__xor__(other: Face | Shape) -> Shape\n\n"
             "The symmetric difference: the regions in exactly one of the two, each keeping its part's properties.\n\n"
             "Raises:\n"
             "    TypeError: other is not a Face or Shape.\n\n"
             "Examples:\n"
             "    shape ^ face")
        .def("Offset", [](const py::object &self, double d, bool inplace, OffsetForm corners) -> py::object {
            Shape &s = self.cast<Shape &>();
            Shape r = OffsetFaces(s, d, CornersOf(corners));
            if (!inplace)
                return py::cast(std::move(r));
            ReplaceKeepingId(s, std::move(r));
            return self;
        }, py::arg("d"), py::arg("inplace") = true, py::arg("corners") = OffsetForm::Round,
           OffsetDoc("Shape", "Shape", "the faces share edges (glued); px.Unify merges them first;", "shape"))
        // sum() starts with 0 + shape
        .def("__radd__", [](const Shape &s, int zero) {
            if (zero != 0)
                throw py::type_error("can only add faces, shapes and wires");
            return s;
        })
        .def("CloseGaps", &Shape::RemoveSmallGaps, py::arg("tol"), ref, CloseGapsDoc("Shape", "shape", ""))
        .def("RemoveSmallEdges", &Shape::RemoveSmallEdges, py::arg("tol"), ref, RemoveSmallEdgesDoc("Shape", "shape"))
        .def("MergeCollinearEdges", [](Shape &s, OptDouble tol) -> Shape & {
            return s.MergeCollinearEdges(Or(tol, config::state->cleanup.merge_collinear_closer));
        }, py::arg("tol") = py::none(), ref, MergeCollinearEdgesDoc("Shape", " of one part", "shape"))
        .def("FitArcs", [](Shape &s, OptDouble tol, OptDouble l, OptDouble a, OptDouble ig) -> Shape & {
            return s.FitArcs(Arcs(true, l, a, tol, ig));
        }, ARCFIT_ARGS, ref,
           FitArcsDoc("Shape", "Replaces chains of at least three edges of one part shorter than max_edge_length by arcs of "
                               "at most 180 degrees (or lines) that stay within tol of the chain, in place.", "shape"))
        .def("Simplify", [](Shape &s, EXPORT_PARAMS) -> Shape & { return s.Simplify(EXPORT_OPTIONS); }, EXPORT_ARGS, ref,
             ExportDoc("Simplify", "Shape",
                       "Defeatures all edges at once in place as the OCC exports do: gaps closed, collinear lines merged, "
                       "arcs and splines fitted, small edges removed, as enabled.\n\n"
                       "Free edges are removed from the faces only.", "shape", false))
        .def("Simplified", [](const Shape &s, EXPORT_PARAMS) { return Shape(s).Simplify(EXPORT_OPTIONS); }, EXPORT_ARGS,
             ExportDoc("Simplified", "Shape", "A copy defeatured as Simplify does it; this shape is not changed.", "shape", false));
    for (auto [op, symbol] : {std::pair{"__add__", "+"}, std::pair{"__sub__", "-"}, std::pair{"__mul__", "*"}, std::pair{"__xor__", "^"}})
        face.def(op, [op, symbol](const Face &f, const py::object &other) {
            if (py::isinstance<Wire>(other) && std::string(op) != "__mul__")
                throw py::type_error(std::string("face ") + symbol + " wire is not supported");
            std::string o = op;
            return Combined(Operands(Shape(f), other),
                            o == "__add__" ? &Unify : o == "__sub__" ? &Subtract : o == "__mul__" ? &Intersect : &SymmetricDifference, false);
        });
    face.def("__radd__", [](const Face &f, int zero) {
        if (zero != 0)
            throw py::type_error("can only add faces, shapes and wires");
        return Shape(f);
    });
    shape.def("Contains", [](const Shape &s, const py::object &other, bool boundary) {
        return ContainsAny(Planar{FaceRegion(s), Wire(s.Axes())}, s.Axes(), other, boundary,
                           [&] { return ShapeOperand(s, "Contains"); });
    }, py::arg("other"), py::pos_only(), py::arg("boundary") = true,
              ContainsDoc("shape", "a face"));
    shape.def("Polylines", [](const Shape &s, double tol) {
        CheckSampleTol("Polylines", tol);
        py::list out;
        for (const Face &f : s.Faces())
            AddFaceLoops(f.Edges(), tol, out);
        for (const Wire &w : s.WireParts())
            AddWireLoops(w, tol, out);
        return out;
    }, py::arg("tol"), PolylinesDoc("shape"));
    shape.def("Split", [](const Shape &s, const py::object &cutter) { return SplitShape(s, CutterIn(s.Axes(), cutter)); },
              py::arg("cutter"), py::pos_only(), SplitDoc("shape"))
        .def("Trim", [](const Shape &s, TRIM_PARAMS) {
            return TrimShape(s, CutterIn(s.Axes(), cutter), RuleOf(s.Axes(), kc, rc, kl, ks, kleft, kright));
        }, TRIM_ARGS, TrimDoc("shape"));
    BindRelations<Shape>(shape, [](const Shape &s) { return ShapeOperand(s, "Relate"); });
    BindMeasures<Shape>(shape, [](const Shape &s) { return ThingOf(s); },
                        [](const Shape &s, const std::optional<std::string> &n) { return NamedIn(s.Axes(), s.Faces(), s.Edges(), n); });
    BindMotion<Shape>(shape);
    BindCopy<Shape>(shape);
    BindCorners<Shape>(shape, [](Shape &s) -> Wire & { return s.Edges(); });
    FaceFields([&](const char *name, auto get, auto set) {
        using V = SetterValueOf<decltype(set)>;
        shape.def_property(name, [get](const Shape &s) { return GroupsCommon(s, get); },
                           [set](Shape &s, const V &v) { SetOnGroups(s, [&](ShapeProperties &q) { set(q, v); }); },
                           FieldDoc(name, " all faces share, None if they differ; set on all faces."));
    });

    m.def("Glue", [](const py::iterable &parts) {
        ShapeList shapes = ShapesArg(parts);
        Identifications ids = shapes.MergedIds();
        py::gil_scoped_release release;
        Shape r = Glue(shapes);
        r.Ids() = std::move(ids);
        return Fresh(std::move(r));
    }, py::arg("parts"), py::pos_only(),
          "Glue(parts: list[Face | Shape | Wire | Compound], /) -> Shape\n\n"
          "The parts as one shape, as netgen's occ.Glue: everything cut against everything, every area covered by a "
          "different set of faces a face of its own (A - B, A * B, B - A).\n\n"
          "The faces' properties merge in list order, the first winning an overlap; a part lying entirely inside another "
          "keeps its own region and properties; touching faces "
          "share their edge; a wire through a face splits it into two faces with its properties, a wire piece inside a "
          "face that separates nothing stays an internal edge of it (the mesh follows it). Where parts share an edge "
          "their edge properties merge the same way.\n\n"
          "Args:\n"
          "    parts: faces, shapes, wires and compounds (their contents).\n\n"
          "Raises:\n"
          "    TypeError: an item is not a Face, Shape, Wire or Compound.\n\n"
          "Examples:\n"
          "    px.Glue([air, iron])\n"
          "    px.Glue([plate, px.Line((1, -1), (1, 3))])");
    m.def("_NextId", &NextId, py::arg("kind"), "for the Python layer: the next auto id of a kind");
    m.def("_Thickness", [](const py::object &obj, size_t samples, bool ball, double corner, int refine) {
        FaceList faces;
        if (py::isinstance<Shape>(obj))
            faces = obj.cast<const Shape &>().Faces();
        else if (py::isinstance<Face>(obj))
            faces = {obj.cast<const Face &>()};
        else if (py::isinstance<FaceView>(obj))
            faces = {obj.cast<const FaceView &>().Copy()};
        else
            throw py::type_error("thickness: of a Face, Shape or face view");
        std::vector<ThicknessSample> all;
        {
            py::gil_scoped_release release;
            for (const Face &f : faces)
                for (const ThicknessSample &s : Thickness(f.Edges(), samples, ball, corner, refine))
                    all.push_back(s);
        }
        py::list out;
        for (const ThicknessSample &s : all)
            out.append(py::make_tuple(s.at.x, s.at.y, s.other.x, s.other.y, s.thickness, s.centre.x, s.centre.y));
        return out;
    }, py::arg("obj"), py::arg("samples"), py::arg("ball"), py::arg("corner"), py::arg("refine") = 0,
          "for px.measurements: (h, v, h, v of the other side, thickness, h, v of the center) at samples along each face's boundary");
    m.def("_EmptyShape", [](const py::object &plane) { return Shape(AxesArg(plane, "Intersect3d")); }, py::arg("plane"),
          "the empty shape on a plane, for the cut of a mesh that misses it");
    m.def("ResetIdCounters", [] { IdCounters().clear(); },
          "ResetIdCounters() -> None\n\n"
          "Starts every kind of auto id at 1 again (Sketch.1, Shape.1, ...), so a script run from the top gets the same "
          "ids.\n\n"
          "Examples:\n"
          "    px.ResetIdCounters()");
    m.def("Unify", [](const py::iterable &parts, bool multithreaded) { return Combined(ShapesArg(parts), &Unify, multithreaded); },
          py::arg("parts"), py::pos_only(), py::arg("multithreaded") = true,
          "Unify(parts: list[Face | Shape | Wire | Compound] | Compound, /, multithreaded: bool = True) -> Shape\n\n"
          "The union of all faces and shapes, or of all wires (their connected chains), as a balanced tree of pairwise "
          "unions.\n\n"
          "A region has the properties of the faces it holds, merged in list order; compounds give their parts.\n\n"
          "Args:\n"
          "    parts: faces, shapes, wires and compounds, or one compound (its parts).\n"
          "    multithreaded: True runs the pairwise unions in parallel.\n\n"
          "Raises:\n"
          "    TypeError: an item is not a Face, Shape, Wire or Compound.\n\n"
          "Examples:\n"
          "    px.Unify([a, b, c])\n"
          "    px.Unify(parts, multithreaded=False)");
    m.def("Intersect", [](const py::iterable &parts, bool multithreaded) { return Combined(ShapesArg(parts, false), &Intersect, multithreaded); },
          py::arg("parts"), py::pos_only(), py::arg("multithreaded") = true,
          "Intersect(parts: list[Face | Shape | Wire], /, multithreaded: bool = True) -> Shape\n\n"
          "The intersection of all faces and shapes, as a balanced tree of pairwise intersections.\n\n"
          "Every region keeps the first one's properties, maxh and minh the smallest of the faces it lies in.\n\n"
          "Args:\n"
          "    parts: faces, shapes and wires.\n"
          "    multithreaded: True runs the pairwise intersections in parallel.\n\n"
          "Raises:\n"
          "    TypeError: an item is not a Face, Shape or Wire (a Compound: px.Unify merges it first).\n\n"
          "Examples:\n"
          "    px.Intersect([a, b])\n"
          "    px.Intersect(parts, multithreaded=False)");
    m.def("Subtract", [](const py::iterable &parts, bool multithreaded) { return Combined(ShapesArg(parts, false), &Subtract, multithreaded); },
          py::arg("parts"), py::pos_only(), py::arg("multithreaded") = true,
          "Subtract(parts: list[Face | Shape | Wire], /, multithreaded: bool = True) -> Shape\n\n"
          "The first part minus the union of all others; every region keeps the properties of the first one's face.\n\n"
          "Args:\n"
          "    parts: faces, shapes and wires, the first the one subtracted from.\n"
          "    multithreaded: True runs the pairwise operations in parallel.\n\n"
          "Raises:\n"
          "    TypeError: an item is not a Face, Shape or Wire (a Compound: px.Unify merges it first).\n\n"
          "Examples:\n"
          "    px.Subtract([plate, hole])\n"
          "    px.Subtract([plate, *holes], multithreaded=False)");
    m.def("SymmetricDifference", [](const py::iterable &parts, bool multithreaded) {
        return Combined(ShapesArg(parts, false), &SymmetricDifference, multithreaded);
    }, py::arg("parts"), py::pos_only(), py::arg("multithreaded") = true,
          "SymmetricDifference(parts: list[Face | Shape], /, multithreaded: bool = True) -> Shape\n\n"
          "The regions covered by an odd number of the parts, as a ^ b ^ c from left to right; every region keeps the "
          "properties of the part it comes from.\n\n"
          "Args:\n"
          "    parts: faces and shapes.\n"
          "    multithreaded: True runs the pairwise operations in parallel.\n\n"
          "Raises:\n"
          "    TypeError: an item is not a Face or Shape (a Compound: px.Unify merges it first).\n"
          "    ValueError: a part has wires.\n\n"
          "Examples:\n"
          "    px.SymmetricDifference([a, b])\n"
          "    px.SymmetricDifference([layer1, layer2, layer3])");
}
