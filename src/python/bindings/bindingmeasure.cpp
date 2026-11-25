// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "bindingmeasure.hpp"
#include "bindingviews.hpp"
#include "bindingargs.hpp"

std::optional<Planar> TargetInPlane(const AxisSystem &axes, const py::handle &target, bool project, std::optional<Vertex> &point) {
    if (std::optional<std::vector<double>> p = AsPoint(target)) {
        point = p->size() == 2 ? Vertex{(*p)[0], (*p)[1]} : axes.ToHV({(*p)[0], (*p)[1], (*p)[2]});
        return Planar{Wire(axes), Wire(axes)};
    }
    if (std::optional<Thing> t = AsThing(target)) {
        if (t->point)
            point = *t->point;
        if (t->axes.IsClose(axes))
            return t->AsPlanar();
        if (project)
            return Planar{Wire(axes), occHooks->across(t->AsPlanar(), t->axes, axes)};
        return std::nullopt;
    }
    if (project)
        if (std::optional<Projection> p = occHooks->projected(target, axes, false)) {
            if (p->wire.Empty() && p->points.size() == 1)
                point = p->points.front().p;
            return Planar{Wire(axes), p->wire};
        }
    return std::nullopt;
}

Pair3 NearestPair(const Thing &self, const py::handle &target, bool project) {
    const Planar s = self.AsPlanar();
    if (s.Empty() && !self.point)
        throw py::value_error("Distance: nothing to measure from");
    if (std::optional<std::vector<double>> p = AsPoint(target); p && p->size() == 3 && !project) {
        const Vector3 q{(*p)[0], (*p)[1], (*p)[2]};
        const Vector3 a = self.axes.ToXYZ(self.point ? *self.point : NearestTo(s, self.axes.ToHV(q)).a);
        return {Norm(a - q), a, q};
    }
    std::optional<Vertex> point;
    if (std::optional<Planar> t = TargetInPlane(self.axes, target, project, point)) {
        if (self.point && point)
            return Pair3Of(self.axes, Nearest{Norm(*point - *self.point), *self.point, *point});
        if (self.point)
            return Pair3Of(self.axes, NearestTo(*t, *self.point).Swapped());
        if (point)
            return Pair3Of(self.axes, NearestTo(s, *point));
        return Pair3Of(self.axes, NearestBetween(s, *t));
    }
    return occHooks->nearest(s, self.point, self.axes, target);
}

py::array_t<double> DistancesOf(const Thing &self, const PointRows &pts, bool project, const std::optional<std::string> &components) {
    Prepared set(self.AsPlanar());
    if (set.s.Empty() && !self.point)
        throw py::value_error("Distance: nothing to measure from");
    if (components)
        CheckComponents(*components);
    const auto n = (size_t)(pts.shape(0)), c = (size_t)(pts.shape(1));
    const size_t k = components ? components->size() : 0;
    py::array_t<double> out = components && k != 1 ? py::array_t<double>({pts.shape(0), (py::ssize_t)k})
                                                   : py::array_t<double>(pts.shape(0));
    const double *x = pts.data();
    double *o = out.mutable_data();
    ForRows(n, set.index, [&](PlanarIndex &q, size_t i) {
        const Pair3 p = PairAt(self, q, x + c * i, c, project);
        if (!components) {
            o[i] = p.d;
            return;
        }
        const Vector3 d = p.b - p.a;
        for (size_t j = 0; j < k; ++j)
            o[k * i + j] = ComponentOf(self.axes, d, (*components)[j]);
    });
    return out;
}

// negative inside the faces
py::object SignedDistance(const Wire &region, const Wire &boundary, const py::handle &target, const std::optional<std::string> &components) {
    if (components)
        throw py::value_error("Distance(signed=True): a signed distance has no components");
    const std::optional<PointRows> pts = PointBatch(target, "Distance");
    const std::optional<std::vector<double>> p = pts ? std::nullopt : AsPoint(target);
    if (!pts && !p)
        throw py::value_error("Distance(signed=True): the target is a point (h, v) or an (n, 2) array of them");
    if ((pts && pts->shape(1) == 3) || (p && p->size() == 3))
        throw py::value_error("Distance(signed=True): the points lie in the plane, (h, v) or an (n, 2) array");
    if (boundary.Empty())
        throw py::value_error("Distance: nothing to measure from");
    PlanarIndex index(region, boundary, region.Tol());
    if (p)
        return py::float_(index.SignedDistance({(*p)[0], (*p)[1]}));
    py::array_t<double> out(pts->shape(0));
    const double *x = pts->data();
    double *o = out.mutable_data();
    ForRows((size_t)(pts->shape(0)), index, [&](PlanarIndex &q, size_t i) { o[i] = q.SignedDistance({x[2 * i], x[2 * i + 1]}); });
    return out;
}

Thing CarrierThing(const EdgeView &r, const py::handle &target, bool project) {
    const Wire &owner = *r.wire;
    std::optional<Vertex> point;
    std::optional<Planar> t = TargetInPlane(owner.Axes(), target, project, point);
    if (!t)
        throw py::value_error("on_carrier: the target must lie in the plane (a point, a thing on the same plane, or project=True)");
    BBox box = BoxOf(r.Get());
    box.Grow(t->region.Box());
    box.Grow(t->curves.Box());
    if (point)
        box.Grow(BBox::Of(*point, *point));
    return ThingOf(CarrierOver(r.Get(), r.wire->Axes(), box));
}

py::array_t<double> ClosestPointsOf(const Thing &self, const PointRows &pts) {
    Prepared set(self.AsPlanar());
    if (set.s.Empty())
        throw py::value_error("ClosestPoint: nothing to measure from");
    const auto n = (size_t)(pts.shape(0)), c = (size_t)(pts.shape(1));
    py::array_t<double> out({pts.shape(0), pts.shape(1)});
    const double *x = pts.data();
    double *o = out.mutable_data();
    ForRows(n, set.index, [&](PlanarIndex &q, size_t i) {
        const double *r = x + c * i;
        double *w = o + c * i;
        if (c == 2) {
            const Vertex a = q.NearestTo({r[0], r[1]}).a;
            w[0] = a.x;
            w[1] = a.y;
            return;
        }
        const Vector3 a = self.axes.ToXYZ(q.NearestTo(self.axes.ToHV({r[0], r[1], r[2]})).a);
        w[0] = a.x;
        w[1] = a.y;
        w[2] = a.z;
    });
    return out;
}

py::object ClosestPointOf(const Thing &self, const py::handle &point) {
    if (std::optional<PointRows> pts = PointBatch(point, "ClosestPoint"))
        return ClosestPointsOf(self, *pts);
    std::optional<std::vector<double>> p = AsPoint(point);
    if (!p)
        throw py::type_error("ClosestPoint: a point of two plane coordinates or three in 3d");
    const Planar s = self.AsPlanar();
    if (s.Empty())
        throw py::value_error("ClosestPoint: nothing to measure from");
    if (p->size() == 2)
        return Tuple2(NearestTo(s, {(*p)[0], (*p)[1]}).a);
    Vertex uv = self.axes.ToHV({(*p)[0], (*p)[1], (*p)[2]});
    return Tuple3(self.axes.ToXYZ(NearestTo(s, uv).a));
}

py::array_t<bool> ContainsMany(const Planar &s, const AxisSystem &axes, const PointRows &pts, bool boundary) {
    Prepared set(s);
    const auto n = (size_t)(pts.shape(0)), c = (size_t)(pts.shape(1));
    py::array_t<bool> out(pts.shape(0));
    const double *x = pts.data();
    bool *o = out.mutable_data();
    const double tol = set.s.Tolerance();
    ForRows(n, set.index, [&](PlanarIndex &q, size_t i) {
        const double *r = x + c * i;
        Vertex uv{r[0], r[1]};
        if (c == 3) {
            const Vector3 p{r[0], r[1], r[2]};
            uv = axes.ToHV(p);
            if (!axes.IsOn(p, tol)) {
                o[i] = false;
                return;
            }
        }
        const Where w = q.Locate(uv);
        o[i] = w == Where::Inside || (w == Where::Boundary && boundary);
    });
    return out;
}

py::object ContainsOf(const Planar &s, const AxisSystem &axes, const py::handle &point, bool boundary) {
    if (std::optional<PointRows> pts = PointBatch(point, "Contains"))
        return ContainsMany(s, axes, *pts, boundary);
    std::optional<std::vector<double>> p = AsPoint(point);
    if (!p)
        throw py::type_error("Contains: a point of two plane coordinates or three in 3d");
    Vertex uv{(*p)[0], (*p)[1]};
    if (p->size() == 3) {
        const Vector3 q{(*p)[0], (*p)[1], (*p)[2]};
        uv = axes.ToHV(q);
        if (!axes.IsOn(q, s.Tolerance()))
            return py::bool_(false);
    }
    Where w = Locate(s, uv);
    return py::bool_(w == Where::Inside || (w == Where::Boundary && boundary));
}

RelateOperand RelateOperandOf(const py::handle &h, const AxisSystem &axes, const std::string &call) {
    auto onPlane = [&](const AxisSystem &other) {
        if (!other.IsClose(axes))
            throw py::value_error(call + ": the two lie on different planes; move them onto one plane first");
    };
    if (std::optional<Face> copy; IsA<Face>(h)) {
        const Face &f = AsFace(h, copy);
        onPlane(f.Axes());
        return {2, f.Edges(), {}};
    }
    if (IsA<Shape>(h)) {
        const Shape &s = h.cast<const Shape &>();
        onPlane(s.Axes());
        return ShapeOperand(s, call);
    }
    if (IsA<Wire>(h)) {
        const Wire &w = h.cast<const Wire &>();
        onPlane(w.Axes());
        return {1, w, {}};
    }
    if (py::hasattr(h, "parts"))
        throw py::type_error(call + ": a Compound has no relations, merge it first with px.Unify(comp)");
    std::optional<std::vector<double>> p = AsPoint(h);
    if (!p)
        throw py::type_error(call + ": other is a Face, Shape, Wire or a point");
    Vertex uv{(*p)[0], (*p)[1]};
    if (p->size() == 3) {
        const Vector3 q{(*p)[0], (*p)[1], (*p)[2]};
        uv = axes.ToHV(q);
        if (!axes.IsOn(q))
            throw py::value_error(call + ": the point lies off the plane");
    }
    return {0, Wire(axes), uv};
}

py::dict MeasureDict(const Thing &t) {
    const Measures m = MeasuresOf(t.faces, t.edges);
    py::dict d;
    d["faces"] = t.faces.size();
    d["area"] = m.area;
    d["edges"] = t.edges.Size();
    d["length"] = m.length;
    if (t.point) {
        d["type"] = "point";
        d["center"] = Tuple3(t.axes.ToXYZ(*t.point));
        d["bounding_box"] = BoxTuple(BBox::Of(*t.point, *t.point));
        return d;
    }
    d["type"] = m.type;
    d["bounding_box"] = BoxTuple(m.box);
    if (m.mass.mass == 0.0)
        return d;
    d["center"] = Tuple3(t.axes.ToXYZ(m.mass.center));
    d["inertia"] = MatrixTuple(Inertia(m.mass, t.axes));
    const Section sec = SectionOf(m.mass);
    d["Ih"] = sec.iu;
    d["Iv"] = sec.iv;
    d["Ihv"] = sec.iuv;
    d["I1"] = sec.i1;
    d["I2"] = sec.i2;
    d["principal_angle"] = sec.angle;
    if (m.circle) {
        d["center"] = Tuple3(t.axes.ToXYZ(m.circle->center));
        d["radius"] = m.circle->radius;
        d["angle"] = Degrees(std::min(m.circle->sweep, 2.0 * std::numbers::pi));
    }
    return d;
}

// the angle between two lines in [0, 90] degrees
py::dict MeasureBetween(const Thing &a, const py::handle &target, const std::optional<Thing> &b, bool project) {
    Pair3 p = NearestPair(a, target, project);
    py::dict d;
    d["type"] = "between";
    d["distance"] = p.d;
    const Vector3 v = p.b - p.a;
    d["du"] = Dot(v, a.axes.XDirection());
    d["dv"] = Dot(v, a.axes.YDirection());
    d["dx"] = v.x;
    d["dy"] = v.y;
    d["dz"] = v.z;
    d["points"] = py::make_tuple(Tuple3(p.a), Tuple3(p.b));
    if (b)
        if (std::optional<Vector3> da = LineDirection(a), db = LineDirection(*b); da && db) {
            double angle = Degrees(AngleBetween(*da, *db));
            d["angle"] = std::min(angle, 180.0 - angle);
        }
    return d;
}
