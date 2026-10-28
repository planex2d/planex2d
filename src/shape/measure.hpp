// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>


#include "curves/circlecarrier.hpp"
#include "shape/shape.hpp"

namespace planex2d {

    // the integrals of 1, u, v, u^2, v^2, uv over a region or along curves, coordinates relative to a reference point
    struct Moments {
        double m = 0.0, su = 0.0, sv = 0.0, suu = 0.0, svv = 0.0, suv = 0.0;

        Moments &operator+=(const Moments &o) {
            m += o.m;
            su += o.su;
            sv += o.sv;
            suu += o.suu;
            svv += o.svv;
            suv += o.suv;
            return *this;
        }
        Moments operator*(double s) const { return {m * s, su * s, sv * s, suu * s, svv * s, suv * s}; }
    };

    // the antiderivative of cos^m sin^n at t, m + n <= 4
    inline double TrigAntiderivative(int m, int n, double t) {
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

    // Green's theorem along the line from a to b: the signed moments of the triangle (origin, a, b)
    inline Moments BoundaryMoments(const Vertex &a, const Vertex &b) {
        const double c = a.x * b.y - b.x * a.y;
        return {c / 2.0,
                (a.x + b.x) * c / 6.0,
                (a.y + b.y) * c / 6.0,
                (a.x * a.x + a.x * b.x + b.x * b.x) * c / 12.0,
                (a.y * a.y + a.y * b.y + b.y * b.y) * c / 12.0,
                (a.x * b.y + 2.0 * a.x * a.y + 2.0 * b.x * b.y + b.x * a.y) * c / 24.0};
    }
    // along the circle around c with radius r from angle t0 to t1. The same forms as for a line, the fan from the origin
    // (an integrand of degree k is f (x dy - y dx) / (k + 2)): on a loop of lines and arcs the terms only cancel if all
    // its pieces use one form
    inline Moments BoundaryMoments(const Vertex &c, double r, double t0, double t1) {
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
    inline double AngleOf(const Vertex &d) { return std::atan2(d.y, d.x); }
    // the piece run from start to end if forward, relative to o
    inline Moments BoundaryMoments(const Line &l, bool forward, const Vertex &o) {
        return forward ? BoundaryMoments(l.Start() - o, l.End() - o) : BoundaryMoments(l.End() - o, l.Start() - o);
    }
    inline Moments BoundaryMoments(const Arc &a, bool forward, const Vertex &o) {
        const double t0 = AngleOf(a.Start() - a.Center());
        const double t1 = t0 + (a.CCW() ? a.Sweep() : -a.Sweep());
        return forward ? BoundaryMoments(a.Center() - o, a.Radius(), t0, t1) : BoundaryMoments(a.Center() - o, a.Radius(), t1, t0);
    }
    // on the exact curve in the same forms, closed to the spline's ends by lines as in Spline::ExactSignedArea
    inline Moments BoundaryMoments(const Spline &s, bool forward, const Vertex &o) {
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
    inline Moments BoundaryMoments(const Edge &e, bool forward, const Vertex &o) {
        Moments m;
        std::visit([&](const auto &x) {
            if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Spline>)
                m = BoundaryMoments(x, forward, o);
            else
                ForEachPart(x, [&](const auto &p) { m += BoundaryMoments(p, forward, o); });
        }, e);
        return m;
    }

    // the region the closed loops bound, holes subtracted, about o
    inline Moments RegionMoments(const Wire &w, const Vertex &o) {
        Moments total;
        for (const NestedLoop &n : w.Nesting()) {
            Moments loop;
            for (auto [i, forward] : n.loop.edges)
                loop += BoundaryMoments(w[i], forward, o);
            total += loop * ((loop.m < 0.0 ? -1.0 : 1.0) * (n.depth % 2 == 0 ? 1.0 : -1.0));
        }
        return total;
    }

    inline Moments CurveMoments(const Line &l, const Vertex &o) {
        const Vertex a = l.Start() - o, b = l.End() - o;
        const double L = Norm(b - a);
        return {L, L * (a.x + b.x) / 2.0, L * (a.y + b.y) / 2.0, L * (a.x * a.x + a.x * b.x + b.x * b.x) / 3.0,
                L * (a.y * a.y + a.y * b.y + b.y * b.y) / 3.0, L * (2.0 * a.x * a.y + a.x * b.y + b.x * a.y + 2.0 * b.x * b.y) / 6.0};
    }
    inline Moments CurveMoments(const Arc &a, const Vertex &o) {
        const double t0 = AngleOf((a.CCW() ? a.Start() : a.End()) - a.Center()), t1 = t0 + a.Sweep();
        auto I = [&](int m, int n) { return TrigAntiderivative(m, n, t1) - TrigAntiderivative(m, n, t0); };
        const Vertex c = a.Center() - o;
        const double r = a.Radius(), s = a.Sweep();
        return {r * s, r * (c.x * s + r * I(1, 0)), r * (c.y * s + r * I(0, 1)),
                r * (c.x * c.x * s + 2.0 * c.x * r * I(1, 0) + r * r * I(2, 0)),
                r * (c.y * c.y * s + 2.0 * c.y * r * I(0, 1) + r * r * I(0, 2)),
                r * (c.x * c.y * s + c.x * r * I(0, 1) + c.y * r * I(1, 0) + r * r * I(1, 1))};
    }
    inline Moments CurveMoments(const Spline &s, const Vertex &o) {
        Moments m;
        s.ForEachGaussPoint([&](const Vertex &p, const Vertex &t, double w) {
            const Vertex q = p - o;
            const double l = w * Norm(t);
            m += Moments{l, q.x * l, q.y * l, q.x * q.x * l, q.y * q.y * l, q.x * q.y * l};
        });
        return m;
    }
    inline Moments CurveMoments(const Wire &w, const Vertex &o) {
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

    // mass, centre and the second moments about the centre in the plane
    struct MassProps {
        double mass = 0.0;
        Vertex center;
        double juu = 0.0, jvv = 0.0, juv = 0.0;
    };
    inline MassProps PropsOf(const Moments &m, const Vertex &o) {
        if (m.m == 0.0)
            return {};
        const double cu = m.su / m.m, cv = m.sv / m.m;
        return {m.m, o + Vertex{cu, cv}, m.suu - m.su * cu, m.svv - m.sv * cv, m.suv - m.su * cv};
    }
    // the reference point near the geometry keeps the sums of large coordinates from cancelling
    inline Vertex Reference(const Wire &a, const Wire &b) {
        BBox box = a.Box();
        box.Grow(b.Box());
        return box.IsEmpty() ? Vertex{} : box.Center();
    }
    // a region and curves: the region by area if it has one, else the curves by length
    inline MassProps MassOf(const Wire &region, const Wire &curves) {
        const Vertex o = Reference(region, curves);
        Moments m = RegionMoments(region, o);
        return m.m != 0.0 ? PropsOf(m, o) : PropsOf(CurveMoments(curves, o), o);
    }

    using Matrix3 = std::array<std::array<double, 3>, 3>;
    // the inertia tensor about the centre for unit density: (|r|^2 1 - r r^T) integrated, r = u e1 + v e2
    inline Matrix3 Inertia(const MassProps &p, const AxisSystem &axes) {
        const Vector3 &x = axes.XDirection(), &y = axes.YDirection();
        const std::array<double, 3> e1{x.x, x.y, x.z}, e2{y.x, y.y, y.z};
        const double trace = p.juu + p.jvv;
        Matrix3 m{};
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                m[i][j] = (i == j ? trace : 0.0) - p.juu * e1[i] * e1[j] - p.juv * (e1[i] * e2[j] + e2[i] * e1[j]) - p.jvv * e2[i] * e2[j];
        return m;
    }

    // second moments in the plane about the centre: iu about the u direction (from v^2), iv about v, the product,
    // the principal moments i1 >= i2 and the angle of i1's axis from u in degrees, in (-90, 90]
    struct Section {
        double iu, iv, iuv, i1, i2, angle;
    };
    inline Section SectionOf(const MassProps &p) {
        const double iu = p.jvv, iv = p.juu, iuv = p.juv;
        const double mean = 0.5 * (iu + iv), half = 0.5 * (iu - iv);
        const double rad = std::sqrt(half * half + iuv * iuv);
        const double angle = Degrees(0.5 * std::atan2(-2.0 * iuv, iu - iv));
        return {iu, iv, iuv, mean + rad, mean - rad, angle <= -90.0 ? angle + 180.0 : angle};
    }

    // the nearest points a on the first and b on the second
    struct Nearest {
        double d = std::numeric_limits<double>::infinity();
        Vertex a, b;

        void Take(const Vertex &p, const Vertex &q) {
            const double l = Norm(q - p);
            if (l < d)
                *this = {l, p, q};
        }
        Nearest Swapped() const { return {d, b, a}; }
    };

    template <class A, class B>
    void NearestEnds(const A &x, const B &y, Nearest &n) {
        for (const Vertex &p : {x.Start(), x.End()})
            n.Take(p, y.NearestPoint(p));
        for (const Vertex &q : {y.Start(), y.End()})
            n.Take(x.NearestPoint(q), q);
    }
    // the closest pair of two pieces: a common point, a pair with an end, or an interior pair whose connection is normal
    // to both: through the arc's centre normal to the line, or along the line of the two centres
    inline Nearest NearestOf(const Line &x, const Line &y) {
        Nearest n;
        NearestEnds(x, y, n);
        return n;
    }
    inline Nearest NearestOf(const Line &x, const Arc &y) {
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
    inline Nearest NearestOf(const Arc &x, const Line &y) { return NearestOf(y, x).Swapped(); }
    inline Nearest NearestOf(const Arc &x, const Arc &y) {
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
    template <class A, class B>
    Nearest NearestPieces(const A &x, const B &y) {
        const double tol = Tol(std::max(x.LengthScale(), y.LengthScale()));
        Points common = CommonPoints(x, y, tol);
        if (common.Size() > 0)
            return {0.0, common[0], common[0]};
        return NearestOf(x, y);
    }
    // the point of e nearest p, a spline's on its exact curve
    inline Vertex ExactNearestOn(const Edge &e, const Vertex &p) {
        return std::visit([&](const auto &x) {
            if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Spline>)
                return x.ExactNearestPoint(p);
            else
                return x.NearestPoint(p);
        }, e);
    }
    inline Nearest OnExactCurves(const Edge &a, const Edge &b, Nearest n);
    inline Nearest NearestEdges(const Edge &a, const Edge &b) {
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
    // a spline's pair from its pieces (n) onto the exact curves (a common point of the pieces stays): with a circle or
    // arc the spline's point nearest the centre and the round's point towards it; with a line the spline's point whose
    // tangent runs along the line and its foot; then, and else, each point the one nearest the other's, in turn, while
    // that brings them closer (slow at a touch: 1 - curvature times gap per turn)
    inline Nearest OnExactCurves(const Edge &a, const Edge &b, Nearest n) {
        if (!std::holds_alternative<Spline>(a))
            return OnExactCurves(b, a, n.Swapped()).Swapped();
        const Spline &s = std::get<Spline>(a);
        const auto take = [&](const Vertex &pa, const Vertex &pb) {
            if (const double d = Norm(pb - pa); d < n.d)
                n = {d, pa, pb};
        };
        if (const std::optional<CircleCarrier> k = std::holds_alternative<Spline>(b) || std::holds_alternative<Line>(b) ? std::nullopt : CarrierOf(b)) {
            const Vertex pa = s.ExactNearestPoint(k->center), r = pa - k->center;
            // within rounding inside the round: the touch itself
            if (const double l = Norm(r); l > 0.0 && l >= k->radius - Tol(k->LengthScale())) {
                const Vertex pb = k->center + r * (k->radius / l);
                const Arc *arc = std::get_if<Arc>(&b);
                if (!arc || arc->IsInSweep(pb))
                    take(pa, pb);
            }
        } else if (const Line *line = std::get_if<Line>(&b)) {
            if (const std::optional<Vertex> pa = s.ExactTangentAlong(line->End() - line->Start(), n.a))
                take(*pa, line->NearestPoint(*pa));
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
    inline Nearest NearestOnEdge(const Edge &e, const Vertex &p) {
        Nearest n;
        if (const Spline *s = std::get_if<Spline>(&e))
            n.Take(s->ExactNearestPoint(p), p);
        else
            std::visit([&](const auto &x) { ForEachPart(x, [&](const auto &q) { n.Take(q.NearestPoint(p), p); }); }, e);
        return n;
    }
    inline double BoxGap(const BBox &a, const BBox &b) {
        const double dx = std::max({0.0, a.x0 - b.x1, b.x0 - a.x1}), dy = std::max({0.0, a.y0 - b.y1, b.y0 - a.y1});
        return std::sqrt(dx * dx + dy * dy);
    }
    inline double BoxGap(const BBox &a, const Vertex &p) { return BoxGap(a, BBox::Of(p, p)); }

    // the nearest point of the edges to p, as the pair (on the edges, p)
    inline Nearest NearestTo(const Wire &w, const Vertex &p) {
        Nearest n;
        for (const Edge &e : w)
            if (BoxGap(BoxOf(e), p) < n.d) {
                Nearest m = NearestOnEdge(e, p);
                if (m.d < n.d)
                    n = m;
            }
        return n;
    }
    // the nearest points of two edge sets
    inline Nearest NearestBetween(const Wire &a, const Wire &b) {
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

    // a region (its closed loops, holes by nesting) and curves on one plane
    struct Planar {
        Wire region, curves;

        bool Empty() const { return region.Empty() && curves.Empty(); }
        Wire All() const {
            Wire w = region;
            for (const Edge &e : curves)
                w.Add(e);
            return w;
        }
        bool Inside(const Vertex &p) const { return !region.Empty() && region.Box().IsInside(p) && region.Contains(p); }
        double Tolerance() const {
            return Tol(std::max(region.LengthScale(), curves.LengthScale()));
        }
    };

    // p inside the region or on any edge (within the rounding tolerance), and which
    enum class Where { Outside, Inside, Boundary };
    inline Where Locate(const Planar &s, const Vertex &p) {
        if (NearestTo(s.All(), p).d <= std::max(s.Tolerance(), Tol(NormInf(p))))
            return Where::Boundary;
        return s.Inside(p) ? Where::Inside : Where::Outside;
    }

    // the nearest point of s to p: p itself inside the region
    inline Nearest NearestTo(const Planar &s, const Vertex &p) {
        if (s.Inside(p))
            return {0.0, p, p};
        return NearestTo(s.All(), p);
    }

    // the edges of a wire in a grid, for queries at many points; the wire must outlive it. A query marks the edges it
    // has seen in the grid: every thread queries its own copy
    class EdgeIndex {
        private:
            const Wire &edges;
            BBox box;
            double cell;
            Grid grid;
            std::vector<uint32_t> nearby;

            // about one edge per cell and few cells per edge, as EdgeGrid
            static double CellOf(const Wire &w, const BBox &all) {
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

        public:
            explicit EdgeIndex(const Wire &w)
                : edges(w), box(w.Empty() ? BBox::Of({}, {}) : w.Box()), cell(CellOf(w, box)), grid(box, cell) {
                for (size_t k = 0; k < w.Size(); ++k)
                    grid.Insert(k, BoxOf(w[k]));
            }
            bool Empty() const { return edges.Empty(); }
            const BBox &Box() const { return box; }
            // as Wire::Contains(p)
            bool Parity(const Vertex &p) {
                bool odd = false;
                grid.ForRay<false>(p, [&](uint32_t k) {
                    odd ^= RayParity<false, false>(edges[k], p);
                });
                return odd;
            }
            // an edge within t of p; an arc is no nearer than its circle
            bool Near(const Vertex &p, double t) {
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
            // as NearestTo(edges, p): of equally near edges the first
            Nearest NearestTo(const Vertex &p) {
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
    };

    // Locate and NearestTo of a planar set at many points: the region's edges for the parity and the edges measured to
    // (all of the set's, or those bounding the region), each in a grid. The wires must outlive it; every thread queries
    // its own copy
    class PlanarIndex {
        private:
            EdgeIndex region, all;
            double tol;

        public:
            PlanarIndex(const Wire &regionEdges, const Wire &allEdges, double t) : region(regionEdges), all(allEdges), tol(t) {}
            bool Inside(const Vertex &p) { return !region.Empty() && region.Box().IsInside(p) && region.Parity(p); }
            Where Locate(const Vertex &p) {
                if (all.Near(p, std::max(tol, Tol(NormInf(p)))))
                    return Where::Boundary;
                return Inside(p) ? Where::Inside : Where::Outside;
            }
            Nearest NearestTo(const Vertex &p) {
                if (Inside(p))
                    return {0.0, p, p};
                return all.NearestTo(p);
            }
            // the distance to the edges, negative inside the region, 0 on them as Locate tells
            double SignedDistance(const Vertex &p) {
                const Where w = Locate(p);
                if (w == Where::Boundary)
                    return 0.0;
                const double d = all.NearestTo(p).d;
                return w == Where::Inside ? -d : d;
            }
    };

    // a point of every connected chain: inside the other's region one of them lies if anything does, as long as the
    // edges do not meet
    inline std::vector<Vertex> ChainPoints(const Wire &w) {
        std::vector<Vertex> pts;
        for (const Wire &chain : w.Wires())
            pts.push_back(StartOf(chain[0]));
        return pts;
    }
    inline Nearest NearestBetween(const Planar &a, const Planar &b) {
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

}
