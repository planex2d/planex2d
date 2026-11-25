// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "bindingbase.hpp"
#include "bindingdocs.hpp"
#include "bindingviews.hpp"
#include "bindingargs.hpp"

// f(state, i) for every row i < n, without the GIL; many rows on the executor's threads, each with its own copy of state
template <class S, class F>
inline void ForRows(size_t n, S &state, F f) {
    py::gil_scoped_release release;
#ifdef PLANEX_THREADS
    // rows per task: enough work to outweigh scheduling it
    constexpr size_t chunk = 4096;
    if (n >= 2 * chunk && Executor().num_workers() >= 2 && Executor().this_worker_id() < 0) {
        tf::Executor &executor = Executor();
        std::vector<std::optional<S>> own(executor.num_workers());
        tf::Taskflow flow;
        for (size_t first = 0; first < n; first += chunk)
            flow.emplace([&, first] {
                std::optional<S> &s = own[(size_t)executor.this_worker_id()];
                if (!s)
                    s.emplace(state);
                for (size_t i = first; i < std::min(n, first + chunk); ++i)
                    f(*s, i);
            });
        executor.run(flow).get();
        return;
    }
#endif
    for (size_t i = 0; i < n; ++i)
        f(state, i);
}

inline Pair3 Pair3Of(const AxisSystem &axes, const Nearest &n) { return {n.d, axes.ToXYZ(n.a), axes.ToXYZ(n.b)}; }

std::optional<Planar> TargetInPlane(const AxisSystem &axes, const py::handle &target, bool project, std::optional<Vertex> &point);

Pair3 NearestPair(const Thing &self, const py::handle &target, bool project);

inline void CheckComponents(const std::string &components) {
    if (components.find_first_not_of("hvxyz") != std::string::npos)
        throw py::value_error("components are letters of 'hvxyz', got '" + components + "'");
}

inline double ComponentOf(const AxisSystem &axes, const Vector3 &d, char c) {
    switch (c) {
        case 'h': return Dot(d, axes.XDirection());
        case 'v': return Dot(d, axes.YDirection());
        case 'x': return d.x;
        case 'y': return d.y;
        default: return d.z;
    }
}

inline py::object Components(const AxisSystem &axes, const Pair3 &p, const std::string &components) {
    CheckComponents(components);
    const Vector3 d = p.b - p.a;
    py::list out;
    for (char c : components)
        out.append(ComponentOf(axes, d, c));
    if (out.size() == 1)
        return out[0];
    return py::tuple(out);
}

struct Prepared {
    Planar s;
    Wire all;
    PlanarIndex index;

    explicit Prepared(Planar p) : s(std::move(p)), all(s.All()), index(s.region, all, s.Tolerance()) {}
    Prepared(const Prepared &) = delete;
    Prepared &operator=(const Prepared &) = delete;
};

inline Pair3 PairAt(const Thing &self, PlanarIndex &q, const double *r, size_t c, bool project) {
    if (c == 3 && !project) {
        const Vector3 t{r[0], r[1], r[2]};
        const Vector3 a = self.axes.ToXYZ(self.point ? *self.point : q.NearestTo(self.axes.ToHV(t)).a);
        return {Norm(a - t), a, t};
    }
    const Vertex p = c == 3 ? self.axes.ToHV({r[0], r[1], r[2]}) : Vertex{r[0], r[1]};
    const Nearest n = self.point ? Nearest{Norm(p - *self.point), *self.point, p} : q.NearestTo(p);
    return Pair3Of(self.axes, n);
}

py::array_t<double> DistancesOf(const Thing &self, const PointRows &pts, bool project, const std::optional<std::string> &components);

inline py::object DistanceOf(const Thing &self, const py::handle &target, bool project, const std::optional<std::string> &components) {
    if (std::optional<PointRows> pts = PointBatch(target, "Distance"))
        return DistancesOf(self, *pts, project, components);
    Pair3 p = NearestPair(self, target, project);
    if (!components)
        return py::float_(p.d);
    return Components(self.axes, p, *components);
}

py::object SignedDistance(const Wire &region, const Wire &boundary, const py::handle &target, const std::optional<std::string> &components);

template <class T>
inline py::object SignedDistanceOf(const T &x, const py::handle &target, const std::optional<std::string> &components) {
    if constexpr (std::is_same_v<T, Face>) {
        return SignedDistance(x.Edges(), x.Edges(), target, components);
    } else if constexpr (std::is_same_v<T, Shape>) {
        if (!x.Empty() && !x.HasFaces())
            throw py::value_error("Distance(signed=True): the shape has no faces, so no inside");
        return SignedDistance(x.FaceRegion(), x.Outline(), target, components);
    } else {
        throw py::value_error("Distance(signed=True): a wire has no inside");
    }
}

Thing CarrierThing(const EdgeView &r, const py::handle &target, bool project);

inline Thing CarrierThing(const EdgeView &r, const PointRows &pts) {
    const AxisSystem &axes = r.wire->Axes();
    const auto n = (size_t)(pts.shape(0)), c = (size_t)(pts.shape(1));
    const double *x = pts.data();
    BBox box = BoxOf(r.Get());
    for (size_t i = 0; i < n; ++i)
        box.Grow(c == 2 ? Vertex{x[2 * i], x[2 * i + 1]} : axes.ToHV({x[3 * i], x[3 * i + 1], x[3 * i + 2]}));
    return ThingOf(CarrierOver(r.Get(), r.wire->Axes(), box));
}

py::array_t<double> ClosestPointsOf(const Thing &self, const PointRows &pts);

py::object ClosestPointOf(const Thing &self, const py::handle &point);

py::array_t<bool> ContainsMany(const Planar &s, const AxisSystem &axes, const PointRows &pts, bool boundary);

py::object ContainsOf(const Planar &s, const AxisSystem &axes, const py::handle &point, bool boundary);

RelateOperand RelateOperandOf(const py::handle &h, const AxisSystem &axes, const std::string &call);

inline bool IsGeometry(const py::handle &h) { return IsA<Face>(h) || IsA<Shape>(h) || IsA<Wire>(h); }

inline RelateMatrix RelationOf(const RelateOperand &a, const py::handle &other, const std::string &call) {
    const RelateOperand b = RelateOperandOf(other, a.edges.Axes(), call);
    py::gil_scoped_release release;
    return Relate(a, b);
}

template <class T, class C, class F>
inline void BindRelations(C &cls, F operandOf) {
    const std::string x = VariableOf(NameOf(cls));
    cls.def("Relate", [operandOf](const T &self, const py::object &other, const std::optional<std::string> &pattern) -> py::object {
        if (pattern)
            CheckRelatePattern(*pattern);
        const RelateMatrix m = RelationOf(operandOf(self), other, "Relate");
        return pattern ? py::object(py::bool_(m.Matches(*pattern))) : py::object(py::str(m.Text()));
    }, py::arg("other"), py::pos_only(), py::arg("pattern") = py::none(),
       // asterisks after a space as reST literals: Sphinx reads them as emphasis
       Doc("Relate(other: Face | Shape | Wire | Point, /, pattern: str | None = None) -> str | bool\n\n"
           "The DE-9IM matrix of this and other: nine characters for the intersections of the interiors, boundaries "
           "and exteriors (rows this, columns other), each the dimension 0, 1, 2 or F for none. A face is a region, a "
           "wire lines (the ends of its open chains the boundary), a point a point.\n\n"
           "Args:\n"
           "    other: a Face, Shape (of faces or of wires), Wire, or a point (h, v) or (x, y, z) on the plane.\n"
           "    pattern: nine characters of ``F``, ``T``, ``*``, ``0``, ``1``, ``2``: True if the matrix matches it.\n\n"
           "Raises:\n"
           "    TypeError: other is none of these.\n"
           "    ValueError: other lies on another plane; a shape has faces and wires; a pattern is not nine of ``F``, ``T``, ``*``, ``0``, ``1``, ``2``.\n\n"
           "Examples:\n"
           "    " + x + ".Relate(other)\n"
           "    " + x + ".Relate(other, 'T*F**F***')"));
    struct Named {
        const char *name, *text;
        bool (*test)(const RelateMatrix &, int, int);
    };
    static const Named named[] = {
        {"Intersects", "True if this and other have a point in common.",
         [](const RelateMatrix &m, int, int) { return !m.Matches("FF*FF****"); }},
        {"IsDisjoint", "True if this and other have no point in common.",
         [](const RelateMatrix &m, int, int) { return m.Matches("FF*FF****"); }},
        {"Touches", "True if this and other meet only on their boundaries.",
         [](const RelateMatrix &m, int, int) { return Touches(m); }},
        {"Crosses", "True if this and other cross: their interiors meet in a lower dimension than the larger of the two "
                    "has, and each has a part outside the other (lines crossing at points, a line through a face).",
         [](const RelateMatrix &m, int da, int db) { return Crosses(m, da, db); }},
        {"Overlaps", "True if this and other, of one dimension, share a part of that dimension and each has a part "
                     "outside the other.",
         [](const RelateMatrix &m, int da, int db) { return Overlaps(m, da, db); }},
        {"Covers", "True if no point of other lies outside this.",
         [](const RelateMatrix &m, int, int) { return Covers(m); }},
        {"IsCoveredBy", "True if no point of this lies outside other.",
         [](const RelateMatrix &m, int, int) { return Covers(m.Transposed()); }},
        {"IsWithin", "True if this lies in other and their interiors meet.",
         [](const RelateMatrix &m, int, int) { return m.Matches("T*F**F***"); }},
        {"Equals", "True if this and other are the same point set.",
         [](const RelateMatrix &m, int, int) { return m.Matches("T*F**FFF*"); }},
    };
    for (const Named &n : named)
        cls.def(n.name, [operandOf, test = n.test, name = std::string(n.name)](const T &self, const py::object &other) {
            const RelateOperand a = operandOf(self);
            const RelateOperand b = RelateOperandOf(other, a.edges.Axes(), name);
            py::gil_scoped_release release;
            return test(Relate(a, b), a.dim, b.dim);
        }, py::arg("other"), py::pos_only(),
           Doc(std::string(n.name) + "(other: Face | Shape | Wire | Point, /) -> bool\n\n" + n.text +
               " By the DE-9IM matrix, see Relate.\n\n"
               "Args:\n"
               "    other: a Face, Shape (of faces or of wires), Wire, or a point (h, v) or (x, y, z) on the plane.\n\n"
               "Raises:\n"
               "    TypeError: other is not a Face, Shape, Wire or point.\n"
               "    ValueError: other lies on another plane.\n\n"
               "Examples:\n"
               "    " + x + "." + n.name + "(other)"));
}

template <class F>
inline py::object ContainsAny(const Planar &s, const AxisSystem &axes, const py::handle &other, bool boundary, F operand) {
    if (IsGeometry(other))
        return py::bool_(RelationOf(operand(), other, "Contains").Matches("T*****FF*"));
    return ContainsOf(s, axes, other, boundary);
}

inline py::tuple MatrixTuple(const Matrix3 &m) {
    return py::make_tuple(py::make_tuple(m[0][0], m[0][1], m[0][2]), py::make_tuple(m[1][0], m[1][1], m[1][2]),
                          py::make_tuple(m[2][0], m[2][1], m[2][2]));
}

inline MassProps MassOfThing(const Thing &t) {
    Planar s = t.AsPlanar();
    return MassOf(s.region, s.curves);
}

inline py::object CenterOf(const Thing &t) {
    MassProps p = MassOfThing(t);
    if (p.mass == 0.0)
        return py::none();
    return Tuple3(t.axes.ToXYZ(p.center));
}

py::dict MeasureDict(const Thing &t);

inline std::optional<Vector3> LineDirection(const Thing &t) {
    if (!t.faces.empty() || t.point || t.edges.Size() != 1)
        return std::nullopt;
    const Line *l = std::get_if<Line>(&t.edges[0]);
    if (!l)
        return std::nullopt;
    return t.axes.ToXYZ(l->End()) - t.axes.ToXYZ(l->Start());
}

py::dict MeasureBetween(const Thing &a, const py::handle &target, const std::optional<Thing> &b, bool project);

inline Thing NamedIn(const AxisSystem &axes, const FaceList &faces, const Wire &edges, const std::optional<std::string> &name) {
    Thing t{axes, {}, Wire(axes), {}};
    for (const Face &f : faces)
        if (!name || (f.Props() && f.Props()->name == *name))
            t.faces.push_back(f);
    for (const Edge &e : edges)
        if (!name || (PropsOf(e) && PropsOf(e)->name == *name))
            t.edges.Add(e);
    return t;
}

template <class F>
inline py::dict MeasureWith(const Thing &a, const py::object &to, bool project, F named) {
    if (to.is_none())
        return MeasureDict(a);
    if (!py::isinstance<py::str>(to))
        return MeasureBetween(a, to, AsThing(to), project);
    const std::string name = to.cast<std::string>();
    Thing b = named(name);
    if (b.point)
        return MeasureBetween(a, py::cast(std::vector<double>{b.point->x, b.point->y}), b, project);
    if (b.faces.empty() && b.edges.Empty())
        throw py::value_error("Measure(to='" + name + "'): nothing has that name");
    return MeasureBetween(a, py::cast(Shape(b.axes, b.faces, {b.edges})), b, project);
}

template <class F>
inline py::dict MeasureAt(const Thing &t, const py::handle &at, const py::object &to, F edgeOf) {
    if (!to.is_none())
        throw py::type_error("Measure(at=..., to=...): at= measures a place along this, to= the distance to a target; "
                             "give one of them");
    const MeasuredChain chain(t.edges.Edges(), "Measure(at=...)");
    ChainPlace c;
    double distance = 0.0;
    if (IsNumber(at))
        c = chain.AtLength(at.cast<double>());
    else {
        const Vertex p = HVArg(t.axes, at, "Measure(at=...)");
        c = chain.Nearest(p);
        distance = Norm(p - c.point);
    }
    const Vertex normal{-c.tangent.y, c.tangent.x};
    const auto inSpace = [&](const Vertex &d) { return Tuple3(t.axes.XDirection() * d.x + t.axes.YDirection() * d.y); };
    py::dict d;
    d["type"] = "at";
    d["point"] = Tuple3(t.axes.ToXYZ(c.point));
    d["point_hv"] = Tuple2(c.point);
    d["tangent"] = inSpace(c.tangent);
    d["tangent_hv"] = Tuple2(c.tangent);
    d["normal"] = inSpace(normal);
    d["normal_hv"] = Tuple2(normal);
    d["curvature"] = c.curvature;
    d["arc_length"] = c.arcLength;
    d["distance"] = distance;
    d["edge"] = edgeOf(c.edge);
    return d;
}

template <class T, class N>
inline Thing MeasuredThing(const T &x, const py::object &name, N named) {
    if (name.is_none())
        return named(x, std::nullopt);
    if (py::isinstance<py::str>(name)) {
        const std::string n = name.cast<std::string>();
        Thing t = named(x, n);
        if (t.faces.empty() && t.edges.Empty())
            throw py::value_error("Measure('" + n + "'): no face or edge has that name or id");
        return t;
    }
    const std::optional<Thing> t = AsThing(name);
    if (!t)
        throw py::type_error("Measure: name is a name, an id or a selected face or edge, not " + py::repr(name).cast<std::string>());
    return *t;
}

template <class T, class C, class F, class N>
inline void BindMeasures(C &cls, F thingOf, N named) {
    const std::string var = VariableOf(NameOf(cls));
    cls.def_property_readonly("center", [thingOf](const T &x) { return CenterOf(thingOf(x)); },
                              "center: tuple[float, float, float] | None\n\nThe center of mass in 3d, (x, y, z): of the area for faces, of the length for wires; None if it is empty.")
        .def_property_readonly("inertia", [thingOf](const T &x) -> py::object {
            Thing t = thingOf(x);
            MassProps p = MassOfThing(t);
            if (p.mass == 0.0)
                return py::none();
            return MatrixTuple(Inertia(p, t.axes));
        }, "inertia: tuple[tuple[float, float, float], tuple[float, float, float], tuple[float, float, float]] | None\n\nThe inertia tensor about the center in 3d for unit density (per area for faces, per length for wires), three rows "
           "of three; None if it is empty.")
        .def("Distance", [thingOf](const T &x, const py::object &target, bool project, const std::optional<std::string> &components,
                                   bool sign) {
            if (sign)
                return SignedDistanceOf(x, target, components);
            return DistanceOf(thingOf(x), target, project, components);
        }, py::arg("target"), py::pos_only(), py::arg("project") = false, py::arg("components") = py::none(), py::arg("signed") = false,
           DistanceDoc(var))
        .def("ClosestPoint", [thingOf](const T &x, const py::object &point) { return ClosestPointOf(thingOf(x), point); },
             py::arg("point"), py::pos_only(), ClosestPointDoc(var))
        ;
    if constexpr (std::is_same_v<T, Wire>)
        cls.def("Measure", [named](const py::object &self, const py::object &name, const py::object &to, bool project,
                                   const py::object &at) {
            const T &x = self.cast<const T &>();
            const Thing t = MeasuredThing(x, name, named);
            if (at.is_none())
                return MeasureWith(t, to, project, [&](const std::string &n) { return named(x, n); });
            return MeasureAt(t, at, to, [&](size_t i) -> py::object {
                if (name.is_none())
                    return self.attr("edges")[py::int_(i)];
                return IsA<EdgeView>(name) ? name : py::none();
            });
        }, py::arg("name") = py::none(), py::arg("to") = py::none(), py::arg("project") = false, py::arg("at") = py::none(),
           MeasureDoc(var, true));
    else
        cls.def("Measure", [named](const T &x, const py::object &name, const py::object &to, bool project) {
            return MeasureWith(MeasuredThing(x, name, named), to, project, [&](const std::string &n) { return named(x, n); });
        }, py::arg("name") = py::none(), py::arg("to") = py::none(), py::arg("project") = false, MeasureDoc(var));
}
