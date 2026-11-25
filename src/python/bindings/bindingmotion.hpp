// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

template <class T>
inline auto &IdOwner(T &t) {
    if constexpr (std::is_same_v<T, Face>)
        return t.IdWire();
    else if constexpr (std::is_same_v<T, Shape>)
        return t.Edges();
    else
        return t;
}
template <class T>
constexpr bool HasObjectId = requires(T &t) { IdOwner(t).SetIdKind(std::string()); };

template <class T>
inline T CopyOf(const T &t, const char *kind) {
    T r(t);
    if constexpr (HasObjectId<T>)
        if (IdOwner(r).IdKind() != "Sketch")
            IdOwner(r).SetIdKind(kind);
    return r;
}

template <class V, class F>
inline void BindViewCopy(py::class_<V> &cls, F copy, const char *doc) {
    cls.def("Copy", copy, doc)
        .def("__copy__", copy)
        .def("__deepcopy__", [copy](const V &v, const py::dict &) { return copy(v); }, py::arg("memo"));
}

template <class T, class F>
inline void BindCoordinates(py::class_<T> &cls, F hv) {
    cls.def_property_readonly("h", [hv](const T &t) { return hv(t).x; }, "h: float\n\nWhere it is along the plane's first axis.")
        .def_property_readonly("v", [hv](const T &t) { return hv(t).y; }, "v: float\n\nWhere it is along the plane's second axis.")
        .def("__len__", [](const T &) { return 2; })
        .def("__getitem__", [hv](const T &t, py::ssize_t k) {
            if (k < -2 || k > 1)
                throw py::index_error("a point has two coordinates, h and v");
            const Vertex p = hv(t);
            return k % 2 == 0 ? p.x : p.y;
        }, py::arg("k"))
        .def("__iter__", [hv](const T &t) {
            const Vertex p = hv(t);
            return py::iter(py::make_tuple(p.x, p.y));
        })
        .def("__array__", [hv](const T &t, const py::object &dtype, const py::object &) {
            const Vertex p = hv(t);
            return py::module_::import("numpy").attr("array")(py::make_tuple(p.x, p.y), dtype);
        }, py::arg("dtype") = py::none(), py::arg("copy") = py::none())
        .def("__eq__", [hv](const T &t, const Vertex &o) { return hv(t) == o; }, py::is_operator(), py::arg("other"));
}

// t becomes r in place: the same object, its id kept
template <class T>
inline void ReplaceKeepingId(T &t, T r) {
    const std::string id = IdOwner(t).AssignedId(), kind = IdOwner(t).IdKind();
    t = std::move(r);
    IdOwner(t).SetIdKind(kind);
    IdOwner(t).SetId(id);
}

template <class T, class C>
inline void BindCopy(C &cls) {
    const std::string name = NameOf(cls);
    cls.def("Copy", [](const T &t) { return CopyOf(t, "Copy"); },
            Doc("Copy() -> " + name + "\n\nAn independent copy; copy.copy and copy.deepcopy give the same.\n\nExamples:\n    b = a.Copy()"))
        .def("__copy__", [](const T &t) { return CopyOf(t, "Copy"); })
        .def("__deepcopy__", [](const T &t, const py::dict &) { return CopyOf(t, "Copy"); }, py::arg("memo"));
    if constexpr (HasObjectId<T>)
        cls.def_property("id", [](T &t) { return IdOwner(t).Id(); }, [](T &t, const std::string &id) {
            CheckUserId(id);
            IdOwner(t).SetId(id);
        }, "id: str\n\nThe object's id: the kind that made it and a number, given when first asked for (Sketch.1, Shape.3), or set "
           "(an ASCII letter, then letters, digits, _ and single inner spaces); its elements' ids follow it (Shape.3.Line.2).")
            .def("_SetId", [](T &t, const std::string &id) { IdOwner(t).SetId(id); }, py::arg("id"),
                 "for the Python layer: a scoped id it makes from a user's (board.F.Cu), not checked as a user id")
            .def("_SetIdKind", [](T &t, const std::string &kind) { IdOwner(t).SetIdKind(kind); }, py::arg("kind"),
                 "for the Python layer: the kind its auto id counts in (DXF, KiCad, ...)")
            .def("_Same", [](T &t) {
                T r(t);
                IdOwner(r).SetId(IdOwner(t).AssignedId());
                return r;
            }, "for the Python layer: a copy that keeps the id (a Compound's part)");
    if constexpr (requires(T &t) { IdOwner(t).AssignIds(true); })
        cls.def("_RenumberIds", [](T &t) {
            IdOwner(t).AssignIds(true);
            IdOwner(t).AssignVertexIds(true);
        }, "for the Python layer: its edges and vertices numbered anew");
}

template <class T>
inline void AddCopy(py::module_ &m, const char *name) {
    auto cls = py::reinterpret_borrow<py::class_<T>>(m.attr(name));
    BindCopy<T>(cls);
}

template <class T, bool mirror = true, class C>
inline void BindMotion(C &cls) {
    auto ref = py::return_value_policy::reference_internal;
    const std::string name = NameOf(cls), x = VariableOf(name);
    const MotionDoc rotate = RotateDoc(), translate = TranslateDoc(), scale = ScaleDoc();
    cls.def("Rotate", [](T &t, std::array<double, 2> c, double angle) -> T & { t.Rotate(Point2(c), angle); return t; },
            py::arg("center"), py::arg("angle"), ref, rotate.Of(name, false))
        .def("Rotate", [](T &t, std::array<double, 3> p, std::array<double, 3> d, double angle) -> T & {
            t.Rotate(AxisOf(p, d), angle);
            return t;
        }, py::arg("point"), py::pos_only(), py::arg("dir"), py::arg("angle"), ref, "")
        .def("Rotated", [](const T &t, std::array<double, 2> c, double angle) { T r = CopyOf(t, "Rotate"); return r.Rotate(Point2(c), angle), r; },
             py::arg("center"), py::arg("angle"), rotate.Of(name, true))
        .def("Rotated", [](const T &t, std::array<double, 3> p, std::array<double, 3> d, double angle) {
            T r = CopyOf(t, "Rotate");
            r.Rotate(AxisOf(p, d), angle);
            return r;
        }, py::arg("point"), py::pos_only(), py::arg("dir"), py::arg("angle"), "")
        .def("Translate", [](T &t, std::array<double, 2> d) -> T & { t.Translate(Point2(d)); return t; }, py::arg("d"), ref,
             translate.Of(name, false))
        .def("Translate", [](T &t, double dh, double dv) -> T & { t.Translate(Vertex{dh, dv}); return t; }, py::arg("dh"),
             py::arg("dv"), ref, "")
        .def("Translate", [](T &t, std::array<double, 3> d) -> T & { t.Translate(Vec3(d)); return t; }, py::arg("d"), ref, "")
        .def("Translate", [](T &t, double dx, double dy, double dz) -> T & { t.Translate(Vec3({dx, dy, dz})); return t; },
             py::arg("dx"), py::arg("dy"), py::arg("dz"), ref, "")
        .def("Translated", [](const T &t, std::array<double, 2> d) { T r = CopyOf(t, "Translate"); return r.Translate(Point2(d)), r; }, py::arg("d"),
             translate.Of(name, true))
        .def("Translated", [](const T &t, double dh, double dv) { T r = CopyOf(t, "Translate"); return r.Translate(Vertex{dh, dv}), r; },
             py::arg("dh"), py::arg("dv"), "")
        .def("Translated", [](const T &t, std::array<double, 3> d) { T r = CopyOf(t, "Translate"); return r.Translate(Vec3(d)), r; }, py::arg("d"), "")
        .def("Translated", [](const T &t, double dx, double dy, double dz) {
            T r = CopyOf(t, "Translate");
            return r.Translate(Vec3({dx, dy, dz})), r;
        }, py::arg("dx"), py::arg("dy"), py::arg("dz"), "");
    if constexpr (mirror)
        cls.def("Mirror", [](T &t, std::array<double, 2> p, std::array<double, 2> d) -> T & {
            t.Mirror(Point2(p), Point2(d));
            return t;
        }, py::arg("point"), py::pos_only(), py::arg("dir"), ref, MirrorDoc().Of(name, false))
        .def("Mirror", [](T &t, std::array<double, 3> p, std::array<double, 3> n) -> T & {
            t.Mirror(MirrorPlane(p, n));
            return t;
        }, py::arg("point"), py::pos_only(), py::arg("normal"), ref, "")
        .def("Mirrored", [](const T &t, std::array<double, 2> p, std::array<double, 2> d) {
            T r = CopyOf(t, "Mirror");
            return r.Mirror(Point2(p), Point2(d)), r;
        }, py::arg("point"), py::pos_only(), py::arg("dir"), MirrorDoc().Of(name, true))
        .def("Mirrored", [](const T &t, std::array<double, 3> p, std::array<double, 3> n) {
            T r = CopyOf(t, "Mirror");
            return r.Mirror(MirrorPlane(p, n)), r;
        }, py::arg("point"), py::pos_only(), py::arg("normal"), "");
    cls.def("Scale", [](T &t, std::array<double, 2> c, double s, bool scaleMaxh, bool scaleMinh) -> T & {
            t.Scale(Point2(c), s, scaleMaxh, scaleMinh);
            return t;
        }, py::arg("center"), py::arg("factor"), py::arg("scale_maxh") = false, py::arg("scale_minh") = false, ref, scale.Of(name, false))
        .def("Scale", [](T &t, std::array<double, 3> p, double s, bool scaleMaxh, bool scaleMinh) -> T & {
            t.Scale(Pnt3(p), s, scaleMaxh, scaleMinh);
            return t;
        }, py::arg("point"), py::pos_only(), py::arg("factor"), py::arg("scale_maxh") = false, py::arg("scale_minh") = false, ref, "")
        .def("Scaled", [](const T &t, std::array<double, 2> c, double s, bool scaleMaxh, bool scaleMinh) {
            T r = CopyOf(t, "Scale");
            return r.Scale(Point2(c), s, scaleMaxh, scaleMinh), r;
        }, py::arg("center"), py::arg("factor"), py::arg("scale_maxh") = false, py::arg("scale_minh") = false, scale.Of(name, true))
        .def("Scaled", [](const T &t, std::array<double, 3> p, double s, bool scaleMaxh, bool scaleMinh) {
            T r = CopyOf(t, "Scale");
            return r.Scale(Pnt3(p), s, scaleMaxh, scaleMinh), r;
        }, py::arg("point"), py::pos_only(), py::arg("factor"), py::arg("scale_maxh") = false, py::arg("scale_minh") = false, "")
        .def("Normal", [](const T &t) { return Tuple3(t.Axes().Normal()); },
             Doc("Normal() -> tuple[float, float, float]\n\nThe normal of its plane in 3d.\n\nExamples:\n    " + x + ".Normal()"))
        .def_property_readonly("bounding_box", [](const T &t) { return BoxTuple(t.Box()); },
                               "bounding_box: tuple[tuple[float, float], tuple[float, float]] | None\n\nIts bounding box in the plane, ((hmin, vmin), (hmax, vmax)); None if it is empty.")
        .def("ToXYZ", [](const T &t, const Vertex &p) { return Tuple3(t.Axes().ToXYZ(p)); }, py::arg("point"), py::pos_only(),
             Doc("ToXYZ(point: Point, /) -> tuple[float, float, float]\n"
                 "ToXYZ(points: list[Point], /) -> list[tuple[float, float, float]]\n\n"
                 "The 3d coordinates of a point (h, v) of its plane, or of a list of points.\n\n"
                 "Args:\n"
                 "    point: (h, v) in the plane.\n"
                 "    points: a list of (h, v).\n\n"
                 "Examples:\n"
                 "    " + x + ".ToXYZ((1, 2))\n"
                 "    " + x + ".ToXYZ([(0, 0), (1, 2)])"))
        .def("ToXYZ", [](const T &t, const std::vector<Vertex> &hv) {
            py::list out;
            for (const Vertex &p : hv)
                out.append(Tuple3(t.Axes().ToXYZ(p)));
            return out;
        }, py::arg("points"), py::pos_only(), "")
        .def("ToHV", [](const T &t, const py::object &p) { return ToHVOf(t.Axes(), p); }, py::arg("point"), py::pos_only(),
             Doc("ToHV(point: Point3, /) -> tuple[float, float]\n"
                 "ToHV(points: list[Point3], /) -> list[tuple[float, float]]\n\n"
                 "The plane coordinates (h, v) of the projection of a 3d point onto its plane, or of a list of points.\n\n"
                 "Args:\n"
                 "    point: (x, y, z), or netgen's Pnt (a face's center).\n"
                 "    points: a list of them.\n\n"
                 "Examples:\n"
                 "    " + x + ".ToHV((1, 2, 0))\n"
                 "    " + x + ".ToHV([(0, 0, 0), (1, 2, 0)])"));
}

void AddCorners(const Wire &w, const py::handle &at, const std::string &call, std::vector<Vertex> &out);

inline std::vector<Vertex> CornerPoints(const Wire &w, const py::handle &at, const std::string &call) {
    std::vector<Vertex> corners;
    AddCorners(w, at, call, corners);
    if (corners.empty())
        throw py::value_error(call + ": no corner given; at is a vertex, a point (h, v), a vertex name or a list of them");
    return corners;
}

inline bool CurvatureOf(sketch::Kind continuity, const std::string &call) {
    if (continuity != sketch::Kind::Tangent && continuity != sketch::Kind::Curvature)
        throw py::value_error(call + ": continuity is px.Tangent or px.Curvature, not px." + sketch::KindName(continuity));
    return continuity == sketch::Kind::Curvature;
}
inline void FilletCorners(Wire &w, const std::vector<Vertex> &corners, double r, bool curvature, const std::string &call) {
    if (curvature)
        w.CutCorners(corners, CurvatureFilletRule{{r}}, call);
    else
        w.CutCorners(corners, FilletRule{r}, call);
}

template <class T, class C, class W>
inline void BindCorners(C &cls, W wireOf) {
    auto ref = py::return_value_policy::reference_internal;
    const std::string name = NameOf(cls);
    cls.def("Fillet", [wireOf](T &t, const py::object &at, double r, sketch::Kind continuity) -> T & {
            const bool curvature = CurvatureOf(continuity, "Fillet");
            Wire &w = wireOf(t);
            FilletCorners(w, CornerPoints(w, at, "Fillet"), r, curvature, "Fillet");
            return t;
        }, py::arg("at"), py::arg("r"), py::arg("continuity") = sketch::Kind::Tangent, py::pos_only(), ref, CornerDoc(false, false, name))
        .def("Filleted", [wireOf](T &t, const py::object &at, double r, sketch::Kind continuity) {
            const bool curvature = CurvatureOf(continuity, "Filleted");
            const std::vector<Vertex> corners = CornerPoints(wireOf(t), at, "Filleted");
            T copy = CopyOf(t, "Fillet");
            FilletCorners(wireOf(copy), corners, r, curvature, "Filleted");
            return copy;
        }, py::arg("at"), py::arg("r"), py::arg("continuity") = sketch::Kind::Tangent, py::pos_only(), CornerDoc(false, true, name))
        .def("Chamfer", [wireOf](T &t, const py::object &at, double d, OptDouble d2, OptDouble angle, bool reverse) -> T & {
            Wire &w = wireOf(t);
            w.CutCorners(CornerPoints(w, at, "Chamfer"), ChamferRule{d, d2, angle, reverse}, "Chamfer");
            return t;
        }, py::arg("at"), py::arg("d"), py::pos_only(), py::arg("d2") = py::none(), py::arg("angle") = py::none(),
             py::arg("reverse") = false, ref, CornerDoc(true, false, name))
        .def("Chamfered", [wireOf](T &t, const py::object &at, double d, OptDouble d2, OptDouble angle, bool reverse) {
            const std::vector<Vertex> corners = CornerPoints(wireOf(t), at, "Chamfered");
            T copy = CopyOf(t, "Chamfer");
            wireOf(copy).CutCorners(corners, ChamferRule{d, d2, angle, reverse}, "Chamfered");
            return copy;
        }, py::arg("at"), py::arg("d"), py::pos_only(), py::arg("d2") = py::none(), py::arg("angle") = py::none(),
             py::arg("reverse") = false, CornerDoc(true, true, name));
}
