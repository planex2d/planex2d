// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <type_traits>
#include <variant>
#include <vector>


#include "planex/curves/circlecarrier.hpp"
#include "planex/shape/shape.hpp"

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

    double TrigAntiderivative(int m, int n, double t);

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
    Moments BoundaryMoments(const Vertex &c, double r, double t0, double t1);
    inline double AngleOf(const Vertex &d) { return std::atan2(d.y, d.x); }
    inline Moments BoundaryMoments(const Line &l, bool forward, const Vertex &o) {
        return forward ? BoundaryMoments(l.Start() - o, l.End() - o) : BoundaryMoments(l.End() - o, l.Start() - o);
    }
    inline Moments BoundaryMoments(const Arc &a, bool forward, const Vertex &o) {
        const double t0 = AngleOf(a.Start() - a.Center());
        const double t1 = t0 + (a.CCW() ? a.Sweep() : -a.Sweep());
        return forward ? BoundaryMoments(a.Center() - o, a.Radius(), t0, t1) : BoundaryMoments(a.Center() - o, a.Radius(), t1, t0);
    }
    Moments BoundaryMoments(const Spline &s, bool forward, const Vertex &o);
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
    Moments CurveMoments(const Wire &w, const Vertex &o);

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
    inline Nearest NearestOf(const Line &x, const Line &y) {
        Nearest n;
        NearestEnds(x, y, n);
        return n;
    }
    Nearest NearestOf(const Line &x, const Arc &y);
    inline Nearest NearestOf(const Arc &x, const Line &y) { return NearestOf(y, x).Swapped(); }
    Nearest NearestOf(const Arc &x, const Arc &y);
    template <class A, class B>
    Nearest NearestPieces(const A &x, const B &y) {
        const double tol = Tol(std::max(x.LengthScale(), y.LengthScale()));
        Points common = CommonPoints(x, y, tol);
        if (common.Size() > 0)
            return {0.0, common[0], common[0]};
        return NearestOf(x, y);
    }
    inline Vertex ExactNearestOn(const Edge &e, const Vertex &p) {
        return std::visit([&](const auto &x) {
            if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Spline>)
                return x.ExactNearestPoint(p);
            else
                return x.NearestPoint(p);
        }, e);
    }
    Nearest NearestEdges(const Edge &a, const Edge &b);
    Nearest OnExactCurves(const Edge &a, const Edge &b, Nearest n);
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

    // a: on the edges, b: p
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
    Nearest NearestBetween(const Wire &a, const Wire &b);

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

    // Boundary: on any edge, within the rounding tolerance
    enum class Where { Outside, Inside, Boundary };
    inline Where Locate(const Planar &s, const Vertex &p) {
        if (NearestTo(s.All(), p).d <= std::max(s.Tolerance(), Tol(NormInf(p))))
            return Where::Boundary;
        return s.Inside(p) ? Where::Inside : Where::Outside;
    }

    inline Nearest NearestTo(const Planar &s, const Vertex &p) {
        if (s.Inside(p))
            return {0.0, p, p};
        return NearestTo(s.All(), p);
    }

    // the wire must outlive it. A query marks the edges it has seen in the grid: every thread queries its own copy
    class EdgeIndex {
        private:
            const Wire &edges;
            BBox box;
            double cell;
            Grid grid;
            std::vector<uint32_t> nearby;

            static double CellOf(const Wire &w, const BBox &all);

        public:
            explicit EdgeIndex(const Wire &w)
                : edges(w), box(w.Empty() ? BBox::Of({}, {}) : w.Box()), cell(CellOf(w, box)), grid(box, cell) {
                for (size_t k = 0; k < w.Size(); ++k)
                    grid.Insert(k, BoxOf(w[k]));
            }
            bool Empty() const { return edges.Empty(); }
            const BBox &Box() const { return box; }
            bool Parity(const Vertex &p) {
                bool odd = false;
                grid.ForRay<false>(p, [&](uint32_t k) {
                    odd ^= RayParity<false, false>(edges[k], p);
                });
                return odd;
            }
            bool Near(const Vertex &p, double t);
            Nearest NearestTo(const Vertex &p);
    };

    // the wires must outlive it; every thread queries its own copy
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

    inline std::vector<Vertex> ChainPoints(const Wire &w) {
        std::vector<Vertex> pts;
        for (const Wire &chain : w.Wires())
            pts.push_back(StartOf(chain[0]));
        return pts;
    }
    Nearest NearestBetween(const Planar &a, const Planar &b);

    Wire CarrierOver(const Edge &e, const AxisSystem &axes, const BBox &box);

    struct OnCircle {
        Vertex center;
        double radius, sweep;
    };
    std::optional<OnCircle> CircleOfEdges(const Wire &w);

    struct Measures {
        double area = 0.0, length = 0.0;
        const char *type = "none";
        BBox box;
        MassProps mass;
        std::optional<OnCircle> circle;
    };
    Measures MeasuresOf(const FaceList &faces, const Wire &edges);

}
