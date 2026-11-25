// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/shape/measure.hpp"

#include <numbers>
#include <optional>
#include <stdexcept>

namespace planex2d {

    // the antiderivative of cos^m sin^n at t, m + n <= 4
    double TrigAntiderivative(int m, int n, double t) {
        const double c = std::cos(t), s = std::sin(t);
        switch (m * 10 + n) {
            case 0: return t;
            case 10: return s;
            case 1: return -c;
            case 20: return t / 2.0 + std::sin(2.0 * t) / 4.0;
            case 2: return t / 2.0 - std::sin(2.0 * t) / 4.0;
            case 11: return s * s / 2.0;
            case 30: return s - s * s * s / 3.0;
            case 3: return -c + c * c * c / 3.0;
            case 21: return -c * c * c / 3.0;
            case 12: return s * s * s / 3.0;
        }
        throw std::logic_error("no antiderivative");
    }

    // along the circle around c with radius r from angle t0 to t1. The same forms as for a line, the fan from the origin
    // (an integrand of degree k is f (x dy - y dx) / (k + 2)): on a loop of lines and arcs the terms only cancel if all
    // its pieces use one form
    Moments BoundaryMoments(const Vertex &c, double r, double t0, double t1) {
        auto I = [&](int m, int n) { return TrigAntiderivative(m, n, t1) - TrigAntiderivative(m, n, t0); };
        const double X = c.x, Y = c.y;
        // x dy - y dx = (A cos t + B sin t + C) dt
        const double A = r * X, B = r * Y, C = r * r;
        const double w10 = I(1, 0), w01 = I(0, 1), w00 = I(0, 0), w20 = I(2, 0), w02 = I(0, 2), w11 = I(1, 1);
        const double w30 = I(3, 0), w03 = I(0, 3), w21 = I(2, 1), w12 = I(1, 2);
        const double fan = A * w10 + B * w01 + C * w00;
        const double xfan = A * w20 + B * w11 + C * w10, yfan = A * w11 + B * w02 + C * w01;
        return {0.5 * fan,
                (X * fan + r * xfan) / 3.0,
                (Y * fan + r * yfan) / 3.0,
                (X * X * fan + 2.0 * X * r * xfan + r * r * (A * w30 + B * w21 + C * w20)) / 4.0,
                (Y * Y * fan + 2.0 * Y * r * yfan + r * r * (A * w12 + B * w03 + C * w02)) / 4.0,
                (X * Y * fan + X * r * yfan + Y * r * xfan + r * r * (A * w21 + B * w12 + C * w11)) / 4.0};
    }

    // on the exact curve in the same forms, closed to the spline's ends by lines as in Spline::ExactSignedArea
    Moments BoundaryMoments(const Spline &s, bool forward, const Vertex &o) {
        const auto [a, b] = s.ExactEnds();
        Moments m = BoundaryMoments(s.Start() - o, a - o);
        m += BoundaryMoments(b - o, s.End() - o);
        s.ForEachGaussPoint([&](const Vertex &p, const Vertex &t, double w) {
            const Vertex q = p - o;
            const double c = w * Cross(q, t);
            m += Moments{c / 2.0, q.x * c / 3.0, q.y * c / 3.0, q.x * q.x * c / 4.0, q.y * q.y * c / 4.0, q.x * q.y * c / 4.0};
        });
        return forward ? m : m * -1.0;
    }

    Moments CurveMoments(const Wire &w, const Vertex &o) {
        Moments m;
        for (const Edge &e : w)
            std::visit([&](const auto &x) {
                if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Spline>)
                    m += CurveMoments(x, o);
                else
                    ForEachPart(x, [&](const auto &p) { m += CurveMoments(p, o); });
            }, e);
        return m;
    }

    Nearest NearestOf(const Line &x, const Arc &y) {
        Nearest n;
        NearestEnds(x, y, n);
        const Vertex f = x.NearestPoint(y.Center());
        const double l = Norm(f - y.Center());
        if (l > 0.0)
            for (double s : {1.0, -1.0})
                if (Vertex q = y.Center() + (f - y.Center()) * (s * y.Radius() / l); y.IsInSweep(q))
                    n.Take(f, q);
        return n;
    }

    Nearest NearestOf(const Arc &x, const Arc &y) {
        Nearest n;
        NearestEnds(x, y, n);
        const Vertex d = y.Center() - x.Center();
        const double l = Norm(d);
        if (l > 0.0) {
            const Vertex u = Normalized(d);
            for (double s : {1.0, -1.0})
                for (double t : {1.0, -1.0}) {
                    const Vertex p = x.Center() + u * (s * x.Radius()), q = y.Center() + u * (t * y.Radius());
                    if (x.IsInSweep(p) && y.IsInSweep(q))
                        n.Take(p, q);
                }
        }
        return n;
    }

    Nearest NearestEdges(const Edge &a, const Edge &b) {
        Nearest n;
        std::visit([&](const auto &x, const auto &y) {
            ForEachPart(x, [&](const auto &p) {
                ForEachPart(y, [&](const auto &q) {
                    Nearest m = NearestPieces(p, q);
                    if (m.d < n.d)
                        n = m;
                });
            });
        }, a, b);
        if (n.d > 0.0 && (std::holds_alternative<Spline>(a) || std::holds_alternative<Spline>(b)))
            n = OnExactCurves(a, b, n);
        return n;
    }

    // each point the one nearest the other's, in turn, while that brings them closer (slow at a touch: 1 - curvature times gap per turn)
    Nearest OnExactCurves(const Edge &a, const Edge &b, Nearest n) {
        if (!std::holds_alternative<Spline>(a))
            return OnExactCurves(b, a, n.Swapped()).Swapped();
        const Spline &s = std::get<Spline>(a);
        if (const std::optional<CircleCarrier> k = std::holds_alternative<Spline>(b) || std::holds_alternative<Line>(b) ? std::nullopt : CarrierOf(b)) {
            const Vertex pa = s.ExactNearestPoint(k->center), r = pa - k->center;
            // within rounding inside the round: the touch itself
            if (const double l = Norm(r); l > 0.0 && l >= k->radius - Tol(k->LengthScale())) {
                const Vertex pb = k->center + r * (k->radius / l);
                const Arc *arc = std::get_if<Arc>(&b);
                if (!arc || arc->IsInSweep(pb))
                    n.Take(pa, pb);
            }
        } else if (const Line *line = std::get_if<Line>(&b)) {
            if (const std::optional<Vertex> pa = s.ExactTangentAlong(line->End() - line->Start(), n.a))
                n.Take(*pa, line->NearestPoint(*pa));
        }
        for (int i = 0; i < 100; ++i) {
            const Vertex pa = ExactNearestOn(a, n.b), pb = ExactNearestOn(b, pa);
            const double d = Norm(pb - pa);
            if (!(d < n.d))
                break;
            n = {d, pa, pb};
        }
        return n;
    }

    Nearest NearestBetween(const Wire &a, const Wire &b) {
        Nearest n;
        if (a.Empty() || b.Empty())
            return n;
        n = NearestTo(b, StartOf(a[0])).Swapped();
        Grid grid = b.EdgeGrid();
        std::vector<uint32_t> nearby;
        for (const Edge &e : a) {
            if (n.d == 0.0)
                break;
            const BBox box = BoxOf(e);
            nearby.clear();
            grid.InBox(box.Grown(n.d), nearby);
            for (uint32_t j : nearby)
                if (BoxGap(box, BoxOf(b[j])) < n.d) {
                    Nearest m = NearestEdges(e, b[j]);
                    if (m.d < n.d)
                        n = m;
                }
        }
        return n;
    }

    // about one edge per cell and few cells per edge, as EdgeGrid
    double EdgeIndex::CellOf(const Wire &w, const BBox &all) {
        if (w.Empty())
            return 1.0;
        std::vector<double> extent;
        for (const Edge &e : w) {
            const BBox b = BoxOf(e);
            extent.push_back(NormInf(b.Size()));
        }
        std::nth_element(extent.begin(), extent.begin() + (long)(extent.size() / 2), extent.end());
        return Grid::CellSize(all, w.Size(), extent[extent.size() / 2]);
    }

    // an edge within t of p; an arc is no nearer than its circle
    bool EdgeIndex::Near(const Vertex &p, double t) {
        nearby.clear();
        grid.InBox(BBox::Of(p, p).Grown(t), nearby);
        for (uint32_t k : nearby) {
            const Edge &e = edges[k];
            const Arc *a = std::get_if<Arc>(&e);
            if (BoxGap(BoxOf(e), p) <= t && (!a || a->OnCarrier(p, t)) && NearestOnEdge(e, p).d <= t)
                return true;
        }
        return false;
    }

    // of equally near edges the first
    Nearest EdgeIndex::NearestTo(const Vertex &p) {
        Nearest n;
        if (edges.Empty())
            return n;
        size_t first = 0;
        for (double r = BoxGap(box, p) + cell;; r = n.d < BBox::inf ? n.d : 2.0 * r) {
            nearby.clear();
            grid.InBox(BBox::Of(p, p).Grown(r), nearby);
            for (uint32_t k : nearby)
                if (BoxGap(BoxOf(edges[k]), p) <= n.d) {
                    const Nearest m = NearestOnEdge(edges[k], p);
                    if (m.d < n.d || (m.d == n.d && k < first)) {
                        n = m;
                        first = k;
                    }
                }
            if (n.d <= r)
                return n;
        }
    }

    Nearest NearestBetween(const Planar &a, const Planar &b) {
        Nearest n = NearestBetween(a.All(), b.All());
        if (n.d == 0.0)
            return n;
        for (const Vertex &p : ChainPoints(b.All()))
            if (a.Inside(p))
                return {0.0, p, p};
        for (const Vertex &p : ChainPoints(a.All()))
            if (b.Inside(p))
                return {0.0, p, p};
        return n;
    }

    Wire CarrierOver(const Edge &e, const AxisSystem &axes, const BBox &box) {
        Wire carrier(axes);
        if (const Line *l = std::get_if<Line>(&e)) {
            const Vertex mid = l->MidPoint(), dir = (l->End() - l->Start()) * (1.0 / l->Length());
            const Vertex center = box.Center();
            const double reach = Norm(box.Size()) + Norm(mid - center) + 1.0;
            carrier.Add(Line(mid - dir * reach, mid + dir * reach));
        } else if (const Arc *a = std::get_if<Arc>(&e)) {
            const Vertex opposite = a->Center() * 2.0 - a->Start();
            carrier.Add(Arc(a->Start(), opposite, a->Center(), true));
            carrier.Add(Arc(opposite, a->Start(), a->Center(), true));
        } else if (const Circle *c = std::get_if<Circle>(&e)) {
            for (const Arc &half : c->Halves())
                carrier.Add(half);
        } else {
            throw std::invalid_argument("on_carrier: a spline has no carrier");
        }
        return carrier;
    }

    std::optional<OnCircle> CircleOfEdges(const Wire &w) {
        std::optional<OnCircle> c;
        for (const Edge &e : w) {
            const std::optional<CircleCarrier> k = CarrierOf(e);
            if (!k)
                return std::nullopt;
            const Arc *a = std::get_if<Arc>(&e);
            const double tol = Tol(LengthScaleOf(e));
            if (!c)
                c = OnCircle{k->center, k->radius, 0.0};
            else if (Norm(k->center - c->center) > tol || std::abs(k->radius - c->radius) > tol)
                return std::nullopt;
            c->sweep += a ? a->Sweep() : 2.0 * std::numbers::pi;
        }
        return c;
    }

    static const char *KindOf(const Edge &e) {
        return std::holds_alternative<Line>(e) ? "line" : std::holds_alternative<Arc>(e) ? "arc" : std::holds_alternative<Circle>(e) ? "circle" : "spline";
    }

    Measures MeasuresOf(const FaceList &faces, const Wire &edges) {
        Measures m;
        Wire region(edges.Axes());
        for (const Face &f : faces) {
            m.area += f.Area();
            for (const Edge &e : f.Edges())
                region.Add(e);
        }
        m.length = edges.Length();
        m.circle = faces.empty() ? CircleOfEdges(edges) : std::nullopt;
        if (faces.size() == 1)
            m.type = "face";
        else if (!faces.empty())
            m.type = "faces";
        else if (m.circle)
            m.type = m.circle->sweep >= 2.0 * std::numbers::pi * (1.0 - 1e-12) ? "circle" : "arc";
        else if (edges.Size() == 1)
            m.type = KindOf(edges[0]);
        else if (!edges.Empty())
            m.type = "edges";
        m.box = region.Box();
        m.box.Grow(edges.Box());
        m.mass = MassOf(region, edges);
        return m;
    }

}
