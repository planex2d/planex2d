// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <set>
#include "binding.hpp"
#include "binding_curve.hpp"
#include "planex/curves/interpolate.hpp"
#include "planex/sketch/sketch.hpp"
#include "bindingsketchdocs.hpp"
#include "bindingsketchargs.hpp"
#include "bindingsketchnew.hpp"
#include "bindingsketchread.hpp"
#include "bindingsketchdraw.hpp"

template <class S>
inline S &SketchMove(S &s, const py::args &raw, const py::kwargs &rawKw) {
    const py::tuple args = UnpenArgs(s.Axes(), raw, "Move");
    const py::dict kw = UnpenKeywords(s, rawKw, "Move");
    Prepare(s, args);
    const CallArgs a = Split(args, "Move");
    const AutoScope<S> scope(s, a.autoFlag, "Move");
    HelpersOf(s, kw, "Move", {"dir"});
    std::optional<Vertex> dir;
    if (kw.contains("dir") && !kw["dir"].is_none()) {
        dir = PointArg(kw["dir"], "Move(dir=)");
        if (!(Norm(*dir) > 0.0))
            throw py::value_error("Move(dir=): the direction has no length");
    }
    if (Numbers(a, 2))
        s.MoveTo(s.Position() + Vertex{a.values[0].cast<double>(), a.values[1].cast<double>()}, a.flags);
    else if (Numbers(a, 1))
        s.MoveTo(s.Position() + s.Heading() * a.values[0].cast<double>(), a.flags);
    else if (a.values.size() == 1 && IsRef(a.values[0]))
        s.MoveTo(RefIn(s, a.values[0], "Move"), a.flags);
    else if (a.values.size() == 1)
        s.MoveTo(PointArg(s.Axes(), a.values[0], "Move"), a.flags);
    else
        throw py::type_error("Move((h, v)), Move(id), Move(point), Move(dh, dv) or Move(l), then flags, dir=(dh, dv)");
    if (dir)
        s.Direction(dir->x, dir->y);
    return s;
}

template <class S>
inline void BindSketchPen(py::class_<S> &sketch) {
    auto ref = py::return_value_policy::reference_internal;
    const std::string sk = NameOf(sketch);
    sketch
        .def_property_readonly("origin", [](const S &) { return std::string("origin"); }, "origin: str\n\nThe id of the sketch's origin.")
        .def_property_readonly("pen", [](const S &s) { return SketchPen{s.Position(), s.Heading()}; },
                               "pen: SketchPen\n\nThe pen as it is now, a snapshot (sk.pen.position, sk.pen.direction); a drawing call takes "
                               "it where a point (its position) or a direction (its direction) is asked for.")
        .def_property_readonly("chain_start", [](const S &s) { return Tuple2(s.StartPosition()); },
                               "chain_start: tuple[float, float]\n\nWhere the chain being drawn started, (h, v): Close() goes back there.")
        .def("Move", NamingIds("Move", &SketchMove<S>), ref, SketchMoveDoc(sk, HasConstraints<S>));
}

template <class S>
inline S &SketchSpline(S &s, const py::args &args, const py::kwargs &kw) {
    const CallArgs a = Split(args, "Spline");
    const AutoScope<S> scope(s, a.autoFlag, "Spline");
    HelpersOf(s, kw, "Spline", {"periodic", "tol", "tangents", "start_from_localpos", "style", "tensions", "curvature_radii"});
    if (a.values.size() != 1)
        throw py::type_error("Spline(points), then flags; periodic=, tangents=, start_from_localpos=, style=, tensions=, curvature_radii=");
    std::vector<Vertex> points;
    for (const py::handle &p : a.values[0])
        points.push_back(PointArg(s.Axes(), p, "Spline"));
    std::map<size_t, Vertex> tangents;
    if (kw.contains("tangents") && !kw["tangents"].is_none())
        for (const auto &[k, t] : kw["tangents"].cast<py::dict>())
            tangents[k.cast<size_t>()] = PointArg(s.Axes(), t, "Spline");
    const auto numbers = [&](const char *key) {
        return kw.contains(key) && !kw[key].is_none() ? std::optional(kw[key].cast<std::map<size_t, double>>()) : std::nullopt;
    };
    s.Spline(points, KwOpt<bool>(kw, "periodic").value_or(false), tangents, KwOpt<bool>(kw, "start_from_localpos").value_or(true),
             a.flags, OptionsOf(kw), StyleArg(kw.contains("style") ? py::object(kw["style"]) : py::none(), "Spline"),
             TangentSettingsArg(numbers("tensions"), numbers("curvature_radii"), "Spline"));
    return s;
}

template <class S>
inline S &SketchBSpline(S &s, const py::args &args, const py::kwargs &kw) {
    const CallArgs a = Split(args, "BSpline");
    const AutoScope<S> scope(s, a.autoFlag, "BSpline");
    HelpersOf(s, kw, "BSpline", {"knots", "degree", "periodic"});
    if (a.values.empty() || a.values.size() > 3)
        throw py::type_error("BSpline(poles, knots=None, degree=3), then flags; periodic=");
    std::vector<Vertex> poles;
    for (const py::handle &p : a.values[0])
        poles.push_back(PointArg(s.Axes(), p, "BSpline"));
    const py::object knots = a.values.size() > 1 ? a.values[1] : kw.contains("knots") ? py::object(kw["knots"]) : py::none();
    const py::object degree = a.values.size() > 2 ? a.values[2] : kw.contains("degree") ? py::object(kw["degree"]) : py::int_(3);
    s.BSpline(poles, knots.is_none() ? std::nullopt : std::optional(knots.cast<std::vector<double>>()), degree.cast<size_t>(),
              KwOpt<bool>(kw, "periodic").value_or(false), a.flags, OptionsOf(kw));
    return s;
}

template <class S>
inline S &SketchCurve(S &s, const py::args &args, const py::kwargs &kw) {
    const CallArgs a = Split(args, "Curve");
    const AutoScope<S> scope(s, a.autoFlag, "Curve");
    HelpersOf(s, kw, "Curve", {"tol"});
    if (a.values.size() != 2)
        throw py::type_error("Curve(point, range), then flags: the point (h, v) as two expressions in t or a callable t -> (h, v), "
                             "the range (t0, t1)");
    const std::string text = CurveText("Curve", a.values[0], a.values[1]);
    const std::optional<double> tol = KwOpt<double>(kw, "tol");
    if (IsCurveCallable(a.values[0])) {
        s.Curve(CallableShape(a.values[0], a.values[1], text, tol), std::nullopt, a.flags, OptionsOf(kw));
        return s;
    }
    CheckFormulaTol(tol, text);
    sketch::CurveFormula f = ParsedCurve(a.values[0], a.values[1], text, CurveNames(s));
    if constexpr (HasConstraints<S>)
        s.CurveDimensions(f);
    const Curve<double, Vertex> shape = f.Shape(f.used);
    s.Curve(shape, std::move(f), a.flags, OptionsOf(kw));
    return s;
}

template <class S>
inline S &SketchRectangleCorners(S &s, const py::args &args, const py::kwargs &kw) {
    if (kw.contains("id_wire"))
        throw py::type_error("Rectangle(id_wire=...): name the rectangle with id=");
    HelpersOf(s, kw, "Rectangle", {});
    if (args.size() != 2 && args.size() != 3)
        throw py::type_error("Rectangle takes l and w, two opposite corners or three points (p0 to p1 a side, p2 on the "
                             "opposite side), not " + std::to_string(args.size()) + " arguments");
    std::vector<Vertex> corners;
    std::vector<std::optional<std::string>> at;
    for (const py::handle c : args) {
        if (py::isinstance<py::str>(c))
            throw py::type_error("Rectangle('" + c.cast<std::string>() + "', ...): a corner is a point (h, v) or a selected "
                                 "point such as sk('A'), not an id string");
        if (py::hasattr(c, "_items") || py::hasattr(c, "_ref_in")) {
            const std::vector<std::string> ids = RefsIn(s, c, "Rectangle");
            if (ids.size() != 1)
                throw py::value_error("Rectangle: a corner is one point; the selection holds " + std::to_string(ids.size()));
            corners.push_back(s.Location(ids[0]));
            at.push_back(ids[0]);
        } else {
            corners.push_back(PointArg(s.Axes(), c, "Rectangle"));
            at.push_back(std::nullopt);
        }
    }
    s.Rectangle(corners, at, OptionsOf(kw));
    return s;
}

template <class S>
inline S &SketchRectangleC(S &s, const py::args &args, const py::kwargs &kw) {
    if (kw.contains("id_wire"))
        throw py::type_error("RectangleC(id_wire=...): a rectangle is the wire of its four lines under its own id (RectangleC.1, "
                             "lines RectangleC.1.Line.1 to .Line.4); name it with id=, RectangleC(l, w, id='plate')");
    HelpersOf(s, kw, "RectangleC", {});
    const auto size = [&](size_t k) {
        if (!IsNumber(args[k]))
            throw py::type_error("RectangleC: l and w are numbers; RectangleC(l, w) or RectangleC(center, l, w)");
        return args[k].cast<double>();
    };
    if (args.size() == 2) {
        s.RectangleC(std::nullopt, std::nullopt, size(0), size(1), OptionsOf(kw));
        return s;
    }
    if (args.size() != 3)
        throw py::type_error("RectangleC takes l and w, or the center first: RectangleC(l, w), RectangleC(center, l, w)");
    const py::handle c = args[0];
    if (py::isinstance<py::str>(c))
        throw py::type_error("RectangleC('" + c.cast<std::string>() + "', ...): the center is a point (h, v) or a selected point "
                             "such as sk('A'), not an id string");
    if (py::hasattr(c, "_items") || py::hasattr(c, "_ref_in")) {
        const std::vector<std::string> ids = RefsIn(s, c, "RectangleC");
        if (ids.size() != 1)
            throw py::value_error("RectangleC: the center is one point; the selection holds " + std::to_string(ids.size()));
        s.RectangleC(std::nullopt, ids[0], size(1), size(2), OptionsOf(kw));
    } else
        s.RectangleC(PointArg(s.Axes(), c, "RectangleC"), std::nullopt, size(1), size(2), OptionsOf(kw));
    return s;
}

template <class S>
inline void BindSketchElements(py::class_<S> &sketch) {
    auto ref = py::return_value_policy::reference_internal;
    const std::string sk = NameOf(sketch);
    sketch
        .def("Line", &DrawLine<S>, ref, SketchLineDoc(sk))
        .def("Arc", &DrawArc<S>, ref, SketchArcDoc(sk))
        .def("Slot", [](S &s, const py::args &args, const py::kwargs &kw) -> S & { return DrawSlot(s, "Slot", args, kw); }, ref, SketchSlotDoc(sk))
        .def("ArcSlot", [](S &s, const py::args &args, const py::kwargs &kw) -> S & { return DrawSlot(s, "ArcSlot", args, kw); }, ref,
             SketchArcSlotDoc(sk))
        .def("Keyhole", [](S &s, const py::args &args, const py::kwargs &kw) -> S & { return DrawSlot(s, "Keyhole", args, kw); }, ref,
             SketchKeyholeDoc(sk))
        .def("Polygon", [](S &s, const py::args &args, const py::kwargs &kw) -> S & { return DrawPolygon(s, "Polygon", args, kw); }, ref,
             SketchPolygonDoc(sk))
        .def("Hexagon", [](S &s, const py::args &args, const py::kwargs &kw) -> S & { return DrawPolygon(s, "Hexagon", args, kw); }, ref,
             SketchHexagonDoc(sk))
        .def("Circle", [](S &s, const py::args &args, const py::kwargs &kw) -> S & { return DrawCircle(s, args, kw); }, ref, SketchCircleDoc(sk))
        .def("Spline", &SketchSpline<S>, ref, SketchSplineDoc(sk))
        .def("BSpline", &SketchBSpline<S>, ref, SketchBSplineDoc(sk))
        .def("Curve", &SketchCurve<S>, ref, SketchCurveDoc(sk, HasConstraints<S>))
        .def("Rectangle", [](S &s, double l, double w, const py::kwargs &kw) -> S & {
            if (kw.contains("id_wire"))
                throw py::type_error("Rectangle(id_wire=...): a rectangle is the wire of its four lines under its own id (Rectangle.1, "
                                     "lines Rectangle.1.Line.1 to .Line.4); name it with id=, Rectangle(l, w, id='plate')");
            HelpersOf(s, kw, "Rectangle", {});
            s.Rectangle(l, w, OptionsOf(kw));
            return s;
        }, py::arg("l"), py::arg("w"), ref, SketchRectangleDoc(sk))
        .def("Rectangle", &SketchRectangleCorners<S>, ref, "")
        .def("RectangleC", &SketchRectangleC<S>, ref, SketchRectangleCDoc(sk))
        .def("Add", [](S &s, const py::object &geometry) -> S & {
            Wire w = OneGeometry(geometry, "Add");
            w = s.InSketchAxes(std::move(w), "Add");
            s.Atomic([&] {
                const size_t first = s.ElementCount();
                s.AddProjection({w, {}}, false, std::nullopt, "Projection", std::nullopt, false);
                if constexpr (HasConstraints<S>)
                    s.RelateAdded(first);
            });
            return s;
        }, py::arg("geometry"), py::pos_only(), ref, SketchAddDoc(sk));
}

template <class S>
inline void BindSketchChains(py::class_<S> &sketch) {
    auto ref = py::return_value_policy::reference_internal;
    const std::string sk = NameOf(sketch);
    sketch
        .def("Direction", [](S &s, const py::args &args) -> S & {
            if (args.size() == 2 && IsNumber(args[0]) && IsNumber(args[1]))
                s.Direction(args[0].cast<double>(), args[1].cast<double>());
            else if (args.size() == 1) {
                const Vertex d = PointArg(args[0], "Direction");
                s.Direction(d.x, d.y);
            } else
                throw py::type_error("Direction((h, v)) or Direction(dh, dv)");
            return s;
        }, ref, SketchDirectionDoc(sk))
        .def("Turn", &S::Turn, py::arg("angle"), ref, SketchTurnDoc(sk))
        .def("Close", [](S &s, const py::args &args, const py::kwargs &kw) -> S & {
            const CallArgs a = Split(args, "Close");
            const AutoScope<S> scope(s, a.autoFlag, "Close");
            if (!a.values.empty())
                throw py::type_error("Close takes only flags");
            HelpersOf(s, kw, "Close", {"id_wire"});
            s.Close(a.flags, OptionsOf(kw), KwOpt<std::string>(kw, "id_wire"));
            return s;
        }, ref, SketchCloseDoc(sk))
        .def("Finish", &S::Finish, py::arg("id_wire") = py::none(), ref, SketchFinishDoc(sk))
        .def_property_readonly("wires", [](const S &s) {
            py::dict out;
            for (const auto &[id, elements] : s.Book().wires)
                out[py::str(id)] = elements;
            return out;
        }, "wires: dict[str, list[str]]\n\nThe wires, {wire id: [element ids]}: every pen chain (Chain.1, Chain.2, ..., or its id_wire), every "
           "Rectangle and every Offset.")
        .def("Vertex", NamingIds("Vertex", +[](S &s, const std::string &id, const py::object &point) -> S & {
            CheckUserId(id);
            s.NamedPoint(id, point.is_none() ? s.Position() : PointArg(s.Axes(), Unpen(point, false), "Vertex"));
            return s;
        }), py::arg("id"), py::arg("point") = py::none(), ref, SketchVertexDoc(sk))
        .def_property_readonly("points", [](S &s) {
            py::dict out;
            for (const auto &[id, p] : s.NamedPoints())
                out[py::str(id)] = Tuple2(p);
            return out;
        }, "points: dict[str, tuple[float, float]]\n\nThe points with ids given to them, {id: (h, v)}.");
}

template <class S>
inline void BindSketchSelections(py::class_<S> &sketch) {
    sketch
        .def("_SetProperty", &SetSketchProperty<S>, py::arg("ids"), py::arg("field"), py::arg("value"),
             "test hook of the selections' properties: field (name, maxh, minh, hpref, layer, col, partition, construction) of "
             "the elements ids name set to value; points take name, maxh, hpref and are left out of the others")
        .def("_PointElement", &S::PointElementAt, py::arg("ref"),
             "test hook of the selections: the point element a point reference names (id.start, id.end, id.center), or None")
        .def("_KindById", &S::KindById, py::arg("id"),
             "for the selections: the kind (Point, Line, Arc, ...) of the element the id names, or None")
        .def("_Chosen", &S::Chosen, py::arg("id"),
             "for the selections: [(kind, id)] of the element the id names and the elements under it, or []")
        .def("_RenamePoint", &S::RenamePoint, py::arg("old"), py::arg("id"), "for the Python layer: point.id = id")
        .def("_FromEnd", [](const S &s, const std::string &id) { return s.Book().FromEnd(id); }, py::arg("id"), "for the selections: Chain.-1 as the id it counts to")
        .def("_Spans", &S::Spans, "for the selections: the cut elements kept as the spans of their pieces (Line.3 of Line.3.1, ...)")
        .def("_Properties", &SketchProperties<S>, py::arg("ids"), py::arg("field"),
             "test hook of the selections' properties: the values of field of the elements ids name that take it");
}

template <class S>
inline S &SketchLinearPattern(S &s, const py::object &ids, const py::object &direction, size_t count, double spacing, const py::object &direction2,
                              const std::optional<size_t> &count2, const std::optional<double> &spacing2, const py::object &keep, const py::object &internal,
                              const std::optional<std::string> &id, bool reverse, bool reverse2) {
    const std::vector<std::string> list = RefsIn(s, ids, "LinearPattern");
    std::optional<sketch::PatternDir2> second;
    if (!direction2.is_none() || count2 || spacing2) {
        if (direction2.is_none() || !count2 || !spacing2)
            throw py::type_error("LinearPattern: dir2=, count2= and spacing2= go together, a second direction takes all three");
        second = sketch::PatternDir2{RefIn(s, direction2, "LinearPattern"), *count2, *spacing2, reverse2};
    } else if (reverse2)
        throw py::type_error("LinearPattern: reverse2= turns the second direction round; give dir2=, count2= and spacing2= with it");
    s.AddPattern(list, RefIn(s, direction, "LinearPattern"), count, spacing, true, TieArg(keep, "LinearPattern", "auto_constraint"),
                 TieArg(internal, "LinearPattern", "internal_constraints"), UserId(id), false, second, reverse);
    return s;
}

template <class S>
inline S &SketchOffset(S &s, const py::object &source, const py::object &d, const py::object &sides, OffsetForm ends, OffsetForm corners, const py::kwargs &kw) {
    HelpersOf(s, kw, "Offset", {});
    const std::optional<bool> bothSides = IsPlainAuto(sides, "Offset", "both_sides") ? std::nullopt
                                                                                       : std::optional(TieArg(sides, "Offset", "both_sides"));
    const std::vector<std::string> ids = RefsIn(s, source, "Offset");
    std::optional<Vertex> point;
    double distance;
    if (IsNumber(d))
        distance = d.cast<double>();
    else if ((py::isinstance<py::tuple>(d) || py::isinstance<py::list>(d)) && py::len(d) == 2 && IsNumber(d[py::int_(0)])) {
        distance = d[py::int_(0)].cast<double>();
        const py::object p = d[py::int_(1)];
        point = IsRef(p) ? s.Location(RefIn(s, p, "Offset")) : PointArg(s.Axes(), p, "Offset");
    } else
        throw py::type_error("Offset(source, d): d is a distance, or (d, point) with the point on the side wanted");
    s.Offset(ids, distance, point, bothSides, {EndsOf(ends), CornersOf(corners)}, OptionsOf(kw), KwOpt<bool>(kw, "construction"));
    return s;
}

template <class S>
inline void BindSketchCopies(py::class_<S> &sketch) {
    auto ref = py::return_value_policy::reference_internal;
    const std::string sk = NameOf(sketch);
    sketch
        .def("SymmetryLine", &AddSymmetryLine<S>, py::arg("a"), py::arg("b"), py::pos_only(), py::kw_only(), py::arg("reverse") = false,
             py::arg("id") = py::none(), ref, SketchSymmetryLineDoc(sk))
        .def("Mirror", NamingIds("Mirror", +[](S &s, const py::object &axis) -> S & {
            Prepare(s, axis);
            s.Mirror(RefIn(s, axis, "Mirror"));
            return s;
        }), py::arg("axis"), ref, SketchMirrorDoc(sk))
        .def("Mirror", NamingIds("Mirror", +[](S &s, const py::object &ids, const py::object &axis) -> S & {
            Prepare(s, axis);
            const std::vector<std::string> list = RefsIn(s, ids, "Mirror");
            s.Mirror(list, RefIn(s, axis, "Mirror"));
            return s;
        }), py::arg("ids"), py::arg("axis"), ref, "")
        .def("Mirrored", [](const S &s, const py::object &axis) {
            S r = CopyOf(s, "Mirror");
            Prepare(r, axis);
            return r.Mirror(RefIn(r, axis, "Mirrored")), r;
        }, py::arg("axis"), SketchMirroredDoc(sk, HasConstraints<S>))
        .def("Mirrored", NamingIds("Mirrored", +[](S &s, const py::object &ids, const py::object &axis, const py::object &tie,
                                                   const py::object &internal, const std::optional<std::string> &id) -> S & {
            Prepare(s, axis);
            const std::vector<std::string> list = RefsIn(s, ids, "Mirrored");
            s.Mirrored(list, RefIn(s, axis, "Mirrored"), TieArg(tie, "Mirrored", "auto_constraint"), TieArg(internal, "Mirrored", "internal_constraints"),
                       UserId(id));
            return s;
        }), py::arg("ids"), py::arg("axis"), py::arg("auto_constraint") = py::bool_(HasConstraints<S>),
           py::arg("internal_constraints") = py::bool_(HasConstraints<S>),
           py::arg("id") = py::none(), ref, "")
        .def("CircularPattern", NamingIds("CircularPattern", +[](S &s, const py::object &ids, const py::object &center, size_t count,
                                   const py::object &angleArg, const py::object &keep, const py::object &internal,
                                   const std::optional<std::string> &id) -> S & {
            Prepare(s, center);
            const std::vector<std::string> list = RefsIn(s, ids, "CircularPattern");
            const std::optional<double> angle = NumberOrAuto(angleArg, "CircularPattern", "angle");
            s.AddPattern(list, RefIn(s, center, "CircularPattern"), count, angle.value_or(360.0 / double(count)), false,
                         TieArg(keep, "CircularPattern", "auto_constraint"), TieArg(internal, "CircularPattern", "internal_constraints"),
                         UserId(id), !angle.has_value());
            return s;
        }), py::arg("ids"), py::arg("center"), py::arg("count"), py::arg("angle") = PlainAuto(),
             py::arg("auto_constraint") = py::bool_(HasConstraints<S>), py::arg("internal_constraints") = py::bool_(HasConstraints<S>),
             py::arg("id") = py::none(), ref, SketchCircularPatternDoc(sk, HasConstraints<S>))
        .def("LinearPattern", NamingIds("LinearPattern", &SketchLinearPattern<S>), py::arg("ids"), py::arg("dir"), py::arg("count"), py::arg("spacing"), py::arg("dir2") = py::none(), py::arg("count2") = py::none(),
             py::arg("spacing2") = py::none(), py::arg("auto_constraint") = py::bool_(HasConstraints<S>),
             py::arg("internal_constraints") = py::bool_(HasConstraints<S>),
             py::arg("id") = py::none(), py::kw_only(), py::arg("reverse") = false, py::arg("reverse2") = false, ref,
             SketchLinearPatternDoc(sk, HasConstraints<S>))
        .def("Offset", NamingIds("Offset", &SketchOffset<S>), py::arg("source"), py::pos_only(), py::arg("d"), py::arg("both_sides") = PlainAuto(), py::arg("ends") = OffsetForm::Round,
             py::arg("corners") = OffsetForm::Round, ref, SketchOffsetDoc(sk));
}

template <class S>
inline S &SketchTrim(S &s, const py::object &a, const py::object &b, bool reverse, const py::kwargs &kw) {
    NoMoreKeywords(kw, "Trim", "the point goes with the element: Trim((a, point), b) trims the piece of a nearest it");
    Prepare(s, py::make_tuple(a, b));
    NamingTheCall([&] {
        const auto [id, at] = PieceArg(s, a, "Trim");
        if (!s.MembersOf(id).empty()) {
            if (reverse)
                throw py::value_error("Trim: reverse= picks among one element's pieces, not along a chain or wire; give "
                                      "the point of the run to trim: Trim(('" + id + "', point), b)");
            s.TrimChain(id, CutterIds(s, b, "Trim"), at.value_or(s.Position()));
        } else
            s.Cut(id, CutterIds(s, b, "Trim"), at, reverse, true);
    }, [&] { return CallText("Trim", {{"a", a}, {"b", b}}); });
    return s;
}

template <class S>
inline S &SketchSplit(S &s, const py::object &a, const py::object &b, const py::object &tie, const py::kwargs &kw) {
    const bool tied = TieArg(tie, "Split", "auto_constraint");
    NoMoreKeywords(kw, "Split", "every piece is numbered ('Line.3.1', 'Line.3.2', ...), none keeps a's id: give a alone");
    Prepare(s, py::make_tuple(a, b));
    NamingTheCall([&] {
        const auto [id, at] = PieceArg(s, a, "Split");
        if (at)
            throw py::value_error("Split((a, point), b): every piece is numbered ('Line.3.1', 'Line.3.2', ...), none keeps "
                                  "a's id; give a alone");
        s.Cut(id, CutterIds(s, b, "Split"), std::nullopt, false, false, tied);
    }, [&] { return CallText("Split", {{"a", a}, {"b", b}}); });
    return s;
}

template <class S>
inline void BindSketchEdits(py::class_<S> &sketch) {
    auto ref = py::return_value_policy::reference_internal;
    const std::string sk = NameOf(sketch);
    sketch
        .def("Fillet", [](S &s, const py::object &at, double r, sketch::Kind continuity, const py::object &id, const py::kwargs &kw) -> S & {
            const bool curvature = CurvatureOf(continuity, "Fillet");
            NamingTheCall([&] { s.Fillet(SketchCornerPoints(s, at, "Fillet"), r, curvature, CornerIds(id, "Fillet"), CornerProps(kw, "Fillet")); },
                          [&] { return CallText("Fillet", {{"at", at}, {"r", py::float_(r)}}); });
            return s;
        }, py::arg("at"), py::arg("r"), py::arg("continuity") = sketch::Kind::Tangent, py::pos_only(), py::kw_only(), py::arg("id") = py::none(), ref,
           SketchCornerDoc(false, sk, HasConstraints<S>))
        .def("Connect", [](S &s, const py::object &a, const py::object &b, sketch::Kind continuity, bool reverse, const py::kwargs &kw) -> S & {
            if (continuity != sketch::Kind::Coincident && continuity != sketch::Kind::Tangent && continuity != sketch::Kind::Curvature)
                throw py::value_error(std::string("Connect: continuity is px.Coincident, px.Tangent or px.Curvature, not px.") + sketch::KindName(continuity));
            HelpersOf(s, kw, "Connect", {});
            const std::string ida = RefIn(s, a, "Connect"), idb = RefIn(s, b, "Connect");
            NamingTheCall([&] { s.Connect(ida, idb, continuity, reverse, OptionsOf(kw)); },
                          [&] { return CallText("Connect", {{"a", a}, {"b", b}}); });
            return s;
        }, py::arg("a"), py::arg("b"), py::arg("continuity") = sketch::Kind::Tangent, py::pos_only(), py::arg("reverse") = false, ref, ConnectDoc(sk, HasConstraints<S>))
        .def("Chamfer", [](S &s, const py::object &at, double d, OptDouble d2, OptDouble angle, bool reverse, const py::object &id,
                           const py::kwargs &kw) -> S & {
            NamingTheCall([&] {
                s.Chamfer(SketchCornerPoints(s, at, "Chamfer"), ChamferRule{d, d2, angle, reverse}, CornerIds(id, "Chamfer"),
                          CornerProps(kw, "Chamfer"));
            },
                          [&] { return CallText("Chamfer", {{"at", at}, {"d", py::float_(d)}}); });
            return s;
        }, py::arg("at"), py::arg("d"), py::pos_only(), py::arg("d2") = py::none(), py::arg("angle") = py::none(),
             py::arg("reverse") = false, py::kw_only(), py::arg("id") = py::none(), ref, SketchCornerDoc(true, sk, HasConstraints<S>))
        .def("Extend", [](S &s, const py::object &a, const py::object &b) -> S & {
            Prepare(s, py::make_tuple(a, b));
            NamingTheCall([&] {
                const std::string id = RefIn(s, a, "Extend");
                if (IsNumber(b))
                    s.Extend(id, {}, b.cast<double>());
                else
                    s.Extend(id, CutterIds(s, b, "Extend"), std::nullopt);
            }, [&] { return CallText("Extend", {{"a", a}, {"b", b}}); });
            return s;
        }, py::arg("a"), py::arg("b"), py::pos_only(), ref, SketchExtendDoc(sk, HasConstraints<S>))
        .def("Trim", &SketchTrim<S>, py::arg("a"), py::arg("b") = py::none(), py::pos_only(), py::kw_only(), py::arg("reverse") = false, ref, SketchTrimDoc(sk))
        .def("Split", &SketchSplit<S>, py::arg("a"), py::arg("b") = py::none(), py::pos_only(), py::kw_only(), py::arg("auto_constraint") = py::bool_(true), ref,
             SketchSplitDoc(sk))
        .def("SetOrigin", NamingIds("SetOrigin", +[](S &s, const py::object &point) -> S & {
            Prepare(s, point);
            s.SetOrigin(IsRef(point) ? s.Location(RefIn(s, point, "SetOrigin")) : PointArg(s.Axes(), point, "SetOrigin"));
            return s;
        }), py::arg("point"), py::pos_only(), ref, SketchSetOriginDoc(sk))
        .def("Remove", +[](S &s, const py::object &id) -> S & { return s.Remove(RefIn(s, id, "Remove")); }, py::arg("id"), ref, SketchRemoveDoc(sk));
}

template <class S>
inline auto SketchDiagnose(S &s, const py::object &gapArg) {
    const std::optional<double> gap = NumberOrAuto(gapArg, "Diagnose", "gap");
    if (gap && !(*gap >= 0.0))
        throw py::value_error("Diagnose(gap=" + py::repr(gapArg).cast<std::string>() + "): the gap is a length, 0 or more");
    const sketch::Faults found = s.Diagnose(gap);
    const auto listed = [&](const std::vector<sketch::Fault> &faults) {
        py::list out;
        for (const sketch::Fault &f : faults) {
            py::tuple ids(f.elements.size());
            for (size_t k = 0; k < f.elements.size(); ++k)
                ids[k] = s.Book().elementIds[f.elements[k]];
            py::dict d;
            d["ids"] = ids;
            d["at"] = Tuple2(f.at);
            out.append(d);
        }
        return out;
    };
    py::dict out;
    out["open_ends"] = listed(found.openEnds);
    out["missing_coincident"] = listed(found.missingCoincident);
    out["crossings"] = listed(found.crossings);
    out["overlaps"] = listed(found.overlaps);
    out["degenerate"] = listed(found.degenerate);
    return out;
}

template <class S>
inline void BindSketchQueries(py::class_<S> &sketch) {
    sketch
        .def_property_readonly("_axes", &S::Axes, "the plane, for plane= of other objects")
        .def("Location", NamingIds("Location", +[](S &s, const py::object &id) {
            Prepare(s, id);
            return Tuple2(s.Location(RefIn(s, id, "Location")));
        }), py::arg("id"), py::pos_only(), SketchLocationDoc())
        .def_property_readonly("locations", [](S &s) {
            py::dict out;
            for (const auto &[ref, p] : s.Locations())
                out[py::str(ref)] = Tuple2(p);
            return out;
        }, "locations: dict[str, tuple[float, float]]\n\nEvery point reference with where it is, solved, {ref: (h, v)}.")
        .def("Geometry", NamingIds("Geometry", +[](S &s, const py::object &r) { return GeometryOf(s, r); }), py::arg("id"), SketchGeometryDoc())
        .def("Measure", NamingIds("Measure", +[](S &s, const py::object &name, const py::object &to, bool project,
                                                 const py::object &at) {
            const std::optional<std::string> id = name.is_none() ? std::nullopt : std::optional(RefIn(s, name, "Measure"));
            const py::object target = py::isinstance<py::str>(to) ? py::str(RefIn(s, to, "Measure")) : to;
            if (!at.is_none())
                return MeasureAt(id ? NamedInSketch(s, id) : Thing{s.Axes(), {}, s.Edges(), {}}, at, target,
                                 [&](size_t i) { return ElementOfEdge(s, id, i); });
            return MeasureWith(NamedInSketch(s, id), target, project,
                               [&](const std::string &n) { return NamedInSketch(s, n); });
        }), py::arg("name") = py::none(), py::arg("to") = py::none(), py::arg("project") = false, py::arg("at") = py::none(), SketchMeasureDoc())
        .def("Diagnose", &SketchDiagnose<S>, py::arg("gap") = PlainAuto(), SketchDiagnoseDoc())
        .def_property_readonly("_elements", [](const S &s) {
            py::dict out;
            for (const auto &[kind, ids] : s.Elements())
                if (kind != "Pattern" && kind != "Offset")
                    out[py::str(kind)] = ids;
            for (const auto &[kind, ids] : s.Book().CallGroups())
                out[py::str(kind)] = ids;
            return out;
        }, "for sk.elements: the element ids by kind, then the groups by the call that made them")
        .def("__repr__", [](S &s) {
            const std::string elements = std::to_string(s.ElementCount()) + " elements";
            if constexpr (HasConstraints<S>)
                return "Sketch(" + elements + ", " + std::to_string(s.ConstraintCount()) + " constraints, " + s.Status() + ", " +
                       std::to_string(s.Dof()) + " degrees of freedom)";
            else
                return "QuickSketch(" + elements + ")";
        });
}

template <class S>
inline Wire SketchWire(S &s, const py::object &element, bool unify, const py::object &propagate) {
    const std::optional<Continuity> continuity = PropagationOf(propagate, "Wire");
    if (element.is_none()) {
        if (continuity)
            throw py::value_error("Wire(propagate=...): propagates from an element; give its id, "
                                  "sk.Wire(id, propagate=...)");
        s.RequireFull("Wire()");
        return OneSketchWire(s, unify, unify ? "Wire()" : "Wire(unify=False)");
    }
    Prepare(s, element);
    s.RequireFull("Wire()");
    const std::string local = RefIn(s, element, "Wire"), call = "Wire('" + local + "')";
    const Wire w = continuity ? s.PropagatedWire(local, *continuity) : s.ElementWire(local);
    Wire chain = OrderChain(w.Edges(), w.Axes(), w.Edges().JoinTol(std::nullopt), call, "sk.Wires() gives the chains");
    chain.CheckChain(call);
    return chain;
}

template <class S>
inline void BindSketchExports(py::class_<S> &sketch) {
    sketch
        .def("Face", [](S &s) {
            s.RequireFull("Face()");
            return s.Face();
        }, SketchFaceDoc())
        .def("Shape", [](S &s) {
            s.RequireFull("Shape()");
            return s.Shape();
        }, SketchShapeDoc())
        .def("Wire", NamingIds("Wire", &SketchWire<S>), py::arg("element") = py::none(), py::pos_only(), py::kw_only(), py::arg("unify") = true,
           py::arg("propagate") = PlainMinimal(), SketchWireDoc())
        .def("Wires", [](S &s, bool unify) {
            s.RequireFull("Wires()");
            return s.Wires(unify, unify ? "Wires()" : "Wires(unify=False)");
        }, py::arg("unify") = true, SketchWiresDoc())
        .def("Construction", [](S &s) {
            s.RequireFull("Construction()");
            return s.Edges(true);
        }, SketchConstructionDoc());
}

template <class S>
inline void BindSketchHooks(py::class_<S> &sketch) {
    sketch
        .def("_Drawing", &DrawingOf<S>, "for the plots: every element with its geometry, state ('full', 'under', 'over') and whether it is construction")
        .def("_Location", NamingIds("_Location", +[](S &s, const py::object &r) {
            Prepare(s, r);
            return Tuple2(s.Location(RefIn(s, r, "_Location")));
        }),
             py::arg("ref"), "for tests: where a point is")
        .def("_Name", &S::NameOf, py::arg("id"), "the name an element carries, or None")
        .def("_LastId", [](const S &s) { return s.Book().LastId(); }, "for px.WorkPlane: the id of the element made last")
        .def("_Wire", &S::ElementsWire, py::arg("ids"), "for px.WorkPlane: the solved edges of the elements, in their order")
        .def("_Geometry", NamingIds("_Geometry", +[](S &s, const py::object &r) { return GeometryOf(s, r); }), py::arg("id"),
             "for tests: as Geometry");
}

template <class S>
inline void BindDrawing(py::class_<S> &sketch) {
    sketchOwnsId.push_back(+[](py::handle h, const std::string &id) { return IsA<S>(h) && h.cast<S &>().AssignedId() == id; });
    BindMotion<S, false>(sketch);
    BindCopy<S>(sketch);
    BindSketchPen(sketch);
    BindSketchElements(sketch);
    BindSketchChains(sketch);
    BindSketchSelections(sketch);
    BindSketchCopies(sketch);
    BindSketchEdits(sketch);
    BindSketchQueries(sketch);
    BindSketchExports(sketch);
    BindSketchHooks(sketch);
}
