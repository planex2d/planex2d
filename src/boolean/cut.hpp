// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "core/config.hpp"
#include "core/vertex.hpp"
#include "core/grid.hpp"
#include "core/bbox.hpp"
#include "curves/edge.hpp"
#include "curves/line.hpp"
#include "curves/arc.hpp"
#include "curves/circle.hpp"
#include "curves/spline.hpp"
#include "core/timer.hpp"

namespace planex2d {

    // at most 6: two lines or circles meet in 2 points, an overlap has 4 endpoints; two circles' crossings and up to 4
    // endpoints near them
    class Points {
        private:
            std::array<Vertex, 6> pts;
            size_t n = 0;

        public:
            void Add(const Vertex &x, double tol) {
                for (size_t i = 0; i < n; ++i)
                    if (pts[i].IsClose(x, tol))
                        return;
                pts[n++] = x;
            }
            size_t Size() const { return n; }
            bool Empty() const { return n == 0; }
            const Vertex &operator[](size_t i) const { return pts[i]; }
            auto begin() const { return pts.begin(); }
            auto end() const { return pts.begin() + (long)n; }
    };

    template <class A, class B>
    void AddEndpointsOn(const A &a, const B &b, double tol, Points &cand) {
        for (const Vertex &p : {b.Start(), b.End()})
            if (a.OnCarrier(p, tol))
                cand.Add(p, tol);
    }

    // endpoints on the other carrier are used as they are (bit exact), the formula only without them
    inline Points Candidates(const Line &a, const Line &b, double tol) {
        Points c;
        Vertex r = a.End() - a.Start();
        Vertex s = b.End() - b.Start();
        const Line &longer = Dot(r, r) >= Dot(s, s) ? a : b;
        const Line &shorter = &longer == &a ? b : a;
        if (longer.OnCarrier(shorter.Start(), tol) && longer.OnCarrier(shorter.End(), tol)) {
            for (const Vertex &p : {b.Start(), b.End(), a.Start(), a.End()})
                c.Add(p, tol);
            return c;
        }
        AddEndpointsOn(a, b, tol, c);
        AddEndpointsOn(b, a, tol, c);
        double rs = Cross(r, s);
        if (c.Empty() && rs != 0.0)
            c.Add(a.Start() + r * (Cross(b.Start() - a.Start(), s) / rs), tol);
        return c;
    }

    // via the foot of the center: a quadratic in the line parameter loses precision with the line length
    // the foot from the center: from a line's far start it rounds at the line's length, from the center at the arc's
    // scale (exact where the line's direction is, as an axis-parallel line's)
    inline Points Candidates(const Line &a, const Arc &b, double tol) {
        Points c;
        Vertex r = a.End() - a.Start();
        Vertex u = r / Norm(r);
        const Vertex n{-u.y, u.x};
        Vertex sc = b.Center() - a.Start();
        const double side = Dot(sc, n);
        Vertex f = b.Center() - n * side;
        double h = std::abs(side);
        double R = b.Radius();
        AddEndpointsOn(b, a, tol, c);
        AddEndpointsOn(a, b, tol, c);
        // tangency by distance: the half chord would split a double root into two points ~sqrt(eps) apart
        if (std::abs(h - R) <= tol) {
            if (c.Empty())
                c.Add(f, tol);
            return c;
        }
        if (c.Size() == 1) {
            c.Add(f * 2.0 - c[0], tol);
        } else if (c.Empty() && h < R) {
            double half = std::sqrt((R - h) * (R + h));
            c.Add(f - u * half, tol);
            c.Add(f + u * half, tol);
        }
        return c;
    }

    inline Points Candidates(const Arc &a, const Arc &b, double tol) {
        Points c;
        Vertex dc = b.Center() - a.Center();
        double d = Norm(dc);
        double ra = a.Radius();
        double rb = b.Radius();
        if (d <= tol) {
            if (std::abs(ra - rb) <= tol)
                for (const Vertex &p : {b.Start(), b.End(), a.Start(), a.End()})
                    c.Add(p, tol);
            return c;
        }
        Vertex u = dc / d;
        AddEndpointsOn(b, a, tol, c);
        AddEndpointsOn(a, b, tol, c);
        double outer = std::abs(d - (ra + rb));
        double inner = std::abs(d - std::abs(ra - rb));
        double l = ((ra - rb) * (ra + rb) + d * d) / (2.0 * d);
        double h2 = (ra - l) * (ra + l);
        // nearly touching: the touch point, also next to endpoints within tol of both circles that lie off one of the
        // arcs. Where it lies off an arc itself, the arcs may still cross nearby (the circles within tol of each other
        // far along them): then the crossings. Not for circles that nearly coincide (centres a few tol apart), where
        // neither means anything
        if (outer <= tol || inner <= tol) {
            if (!c.Empty() && d <= 1e3 * tol)
                return c;
            const Vertex touch = a.Center() + u * (inner < outer && ra < rb ? -ra : ra);
            if (h2 > 0.0 && (a.Locate(touch, tol) == Location::Off || b.Locate(touch, tol) == Location::Off)) {
                const Vertex f = a.Center() + u * l;
                const double h = std::sqrt(h2);
                c.Add(f + Vertex{-u.y * h, u.x * h}, tol);
                c.Add(f - Vertex{-u.y * h, u.x * h}, tol);
            } else
                c.Add(touch, tol);
            return c;
        }
        if (h2 < 0.0)
            return c;
        double h = std::sqrt(h2);
        Vertex f = a.Center() + u * l;
        Vertex n{-u.y * h, u.x * h};
        // the circles stay within tol of each other for tol / sin(crossing angle) along them: where that is long they
        // nearly coincide and the endpoints on the other circle decide (an overlap)
        if (tol * ra * rb > 1e-3 * std::min(ra, rb) * d * h) {
            if (c.Size() == 1) {
                const Vertex g = a.Center() + u * Dot(c[0] - a.Center(), u);
                c.Add(g * 2.0 - c[0], tol);
            } else if (c.Empty()) {
                c.Add(f + n, tol);
                c.Add(f - n, tol);
            }
            return c;
        }
        // each crossing besides the endpoints on the other carrier; Add keeps an endpoint within tol of it (bit exact)
        for (const Vertex &x : {f + n, f - n})
            c.Add(x, tol);
        return c;
    }

    inline Points Candidates(const Arc &a, const Line &b, double tol) { return Candidates(b, a, tol); }

    enum class PairKind : unsigned char { Crossing, Touch, Overlap };

    struct PairPoint {
        Vertex at;
        PairKind kind = PairKind::Crossing;
        Location la = Location::Off;
        Location lb = Location::Off;
    };

    // what one pair of parts shares, decided once: up to two crossings, or the ends of up to two overlapping stretches
    // (two arcs of one circle)
    struct PairDecision {
        std::array<PairPoint, 4> pts;
        size_t n = 0;
        std::array<std::pair<size_t, size_t>, 2> stretches;
        size_t ns = 0;
        bool sameDirection = true;

        size_t Add(const PairPoint &p, double tol) {
            for (size_t i = 0; i < n; ++i)
                if (pts[i].at.IsClose(p.at, tol))
                    return i;
            pts[n] = p;
            return n++;
        }
    };

    // x lies on e's carrier by the pair's decision: where along e, by the parameter alone. In an overlap (sameCarrier,
    // the pair is decided to share the carrier) an end within tol along it is that end, however far x lies across it (a
    // copy shifted by about tol: its corners are within tol of the carrier but just over tol apart)
    inline Location Position(const Line &e, const Vertex &x, double tol, bool sameCarrier = false) {
        if (x.IsClose(e.Start(), tol))
            return Location::AtStart;
        if (x.IsClose(e.End(), tol))
            return Location::AtEnd;
        const double l = e.Length();
        const double s = Dot(x - e.Start(), e.End() - e.Start()) / l;
        if (sameCarrier && std::abs(s) <= tol)
            return Location::AtStart;
        if (sameCarrier && std::abs(s - l) <= tol)
            return Location::AtEnd;
        return s > 0.0 && s < l ? Location::Inside : Location::Off;
    }
    inline Location Position(const Arc &e, const Vertex &x, double tol, bool sameCarrier = false) {
        if (x.IsClose(e.Start(), tol))
            return Location::AtStart;
        if (x.IsClose(e.End(), tol))
            return Location::AtEnd;
        const double t = e.Param(x);
        if (sameCarrier && std::min(t, 2.0 * std::numbers::pi - t) * e.Radius() <= tol)
            return Location::AtStart;
        if (sameCarrier && std::abs(t - e.Sweep()) * e.Radius() <= tol)
            return Location::AtEnd;
        return t > 0.0 && t < e.Sweep() ? Location::Inside : Location::Off;
    }

    // where x lies along e by the pair's decision
    template <class E>
    double ParamOn(const E &e, const Vertex &x, double tol) {
        const Location l = Position(e, x, tol);
        return l == Location::AtStart ? 0.0 : l == Location::AtEnd ? e.Param(e.End()) : e.Param(x);
    }
    // how far apart p and q lie along e
    inline double Along(const Line &e, const Vertex &p, const Vertex &q, double tol) {
        return std::abs(ParamOn(e, q, tol) - ParamOn(e, p, tol)) / e.Length();
    }
    inline double Along(const Arc &e, const Vertex &p, const Vertex &q, double tol) {
        return std::abs(ParamOn(e, q, tol) - ParamOn(e, p, tol)) * e.Radius();
    }

    template <class A, class B>
    size_t AddPairPoint(PairDecision &d, const A &a, const B &b, const Vertex &x, PairKind kind, double tol,
                        bool sameCarrier = false) {
        return d.Add({x, kind, Position(a, x, tol, sameCarrier), Position(b, x, tol, sameCarrier)}, tol);
    }

    // how far along the curves a computed crossing of a and b at x may lie from the true one: the rounding over the sine
    // of the angle they cross at
    template <class A, class B>
    double CrossingUncertainty(const A &a, const B &b, const Vertex &x, double tol) {
        return tol / config::state->k / std::abs(Cross(a.template Tangent<false>(x), b.template Tangent<false>(x)));
    }

    // one of the first n points lies within d of x
    inline bool IsAnEnd(const Points &at, size_t n, const Vertex &x, double d) {
        for (size_t i = 0; i < n; ++i)
            if (Norm(at[i] - x) <= d)
                return true;
        return false;
    }

    // the pair's points at xs, the carriers' crossings or touch point: every end of either edge within tol of the other
    // edge (a T-junction where it lies) and each x where it lies on both edges; a point replaces another only within
    // tol of it, an end before an x, and a crossing only within its own uncertainty. Two of them apart along both edges bound the stretch both edges share (the two
    // farthest apart); else the first is the pair's point there. other: the pair's second crossing, the ends nearer to
    // it are its own
    template <class A, class B>
    void AddMeeting(PairDecision &d, const A &a, const B &b, std::initializer_list<Vertex> xs, const Vertex *other,
                    PairKind kind, double tol) {
        const Vertex &x = *xs.begin();
        Points at;
        auto mine = [&](const Vertex &p) { return other == nullptr || Norm(p - x) < Norm(p - *other); };
        for (const Vertex &p : {a.Start(), a.End()})
            if (b.OnCarrier(p, tol) && Position(b, p, tol) != Location::Off && mine(p))
                at.Add(p, tol);
        for (const Vertex &p : {b.Start(), b.End()})
            if (a.OnCarrier(p, tol) && Position(a, p, tol) != Location::Off && mine(p))
                at.Add(p, tol);
        const size_t ends = at.Size();
        for (const Vertex &y : xs) {
            if (Position(a, y, tol) == Location::Off || Position(b, y, tol) == Location::Off)
                continue;
            if (kind == PairKind::Crossing && IsAnEnd(at, ends, y, CrossingUncertainty(a, b, y, tol)))
                continue;
            at.Add(y, tol);
        }
        size_t i = 0, j = 0;
        double farthest = 0.0;
        for (size_t p = 0; p < at.Size(); ++p)
            for (size_t q = p + 1; q < at.Size(); ++q)
                if (const double l = Norm(at[q] - at[p]); l > farthest) {
                    farthest = l;
                    i = p;
                    j = q;
                }
        // a stretch has its ends apart along both edges; points apart only across them are one point along the curves
        if (i == j || Along(a, at[i], at[j], tol) <= tol || Along(b, at[i], at[j], tol) <= tol) {
            AddPairPoint(d, a, b, at.Empty() ? x : at[0], kind, tol);
            return;
        }
        d.sameDirection = (ParamOn(a, at[i], tol) < ParamOn(a, at[j], tol)) == (ParamOn(b, at[i], tol) < ParamOn(b, at[j], tol));
        const size_t pi = AddPairPoint(d, a, b, at[i], PairKind::Overlap, tol);
        const size_t pj = AddPairPoint(d, a, b, at[j], PairKind::Overlap, tol);
        d.stretches[d.ns++] = {pi, pj};
    }

    // two crossings: within tol of each other between them (the lens no deeper than tol) one shared stretch, else each
    // on its own
    template <class A, class B>
    void AddCrossings(PairDecision &d, const A &a, const B &b, const Vertex &x0, const Vertex &x1, double depth, double tol) {
        if (depth <= tol) {
            AddMeeting(d, a, b, {x0, x1}, nullptr, PairKind::Crossing, tol);
            return;
        }
        AddMeeting(d, a, b, {x0}, &x1, PairKind::Crossing, tol);
        AddMeeting(d, a, b, {x1}, &x0, PairKind::Crossing, tol);
    }

    // b on a's carrier as [lo, hi] in a's parameter, a itself [0, end], scale the length of a parameter step: each end
    // of the common stretch is an endpoint of a or b, the ends only decide. Two ends within tol along the carrier are
    // one vertex, a's
    template <class A, class B>
    void AddStretch(PairDecision &d, const A &a, const B &b, double end, double scale, double lo, double hi,
                    const Vertex &plo, const Vertex &phi, double tol) {
        const Vertex p = lo * scale > tol ? plo : a.Start();
        const Vertex q = (end - hi) * scale > tol ? phi : a.End();
        if (p.IsClose(q, tol)) {
            AddPairPoint(d, a, b, p, PairKind::Touch, tol, true);
            return;
        }
        if (std::min(hi, end) <= std::max(lo, 0.0))
            return;
        const size_t i = AddPairPoint(d, a, b, p, PairKind::Overlap, tol, true);
        const size_t j = AddPairPoint(d, a, b, q, PairKind::Overlap, tol, true);
        d.stretches[d.ns++] = {i, j};
    }

    inline PairDecision Decide(const Line &a, const Line &b, double tol) {
        PairDecision d;
        const Vertex r = a.End() - a.Start();
        const Vertex s = b.End() - b.Start();
        const Line &longer = Dot(r, r) >= Dot(s, s) ? a : b;
        const Line &shorter = &longer == &a ? b : a;
        if (longer.OnCarrier(shorter.Start(), tol) && longer.OnCarrier(shorter.End(), tol)) {
            const double rr = Dot(r, r);
            if (rr == 0.0)
                return d;
            const double t0 = Dot(b.Start() - a.Start(), r) / rr;
            const double t1 = Dot(b.End() - a.Start(), r) / rr;
            d.sameDirection = t0 <= t1;
            const double l = Norm(r);
            if (d.sameDirection)
                AddStretch(d, a, b, 1.0, l, t0, t1, b.Start(), b.End(), tol);
            else
                AddStretch(d, a, b, 1.0, l, t1, t0, b.End(), b.Start(), tol);
            return d;
        }
        const double rs = Cross(r, s);
        if (rs == 0.0)
            return d;
        const Vertex x = a.Start() + r * (Cross(b.Start() - a.Start(), s) / rs);
        AddMeeting(d, a, b, {x}, nullptr, PairKind::Crossing, tol);
        return d;
    }

    // via the foot of the center: a quadratic in the line parameter loses precision with the line length. Tangent where
    // the distance equals the radius within its own rounding (tol without k); a line apart from the circle by up to tol
    // touches it on the circle, the line takes the gap
    inline PairDecision Decide(const Line &a, const Arc &b, double tol) {
        PairDecision d;
        const Vertex r = a.End() - a.Start();
        const Vertex u = r / Norm(r);
        const Vertex sc = b.Center() - a.Start();
        const Vertex f = a.Start() + u * Dot(sc, u);
        const double h = std::abs(Cross(sc, u));
        const double R = b.Radius();
        if (h >= R - tol / config::state->k) {
            if (h - R <= tol)
                AddMeeting(d, a, b, {b.Center() + (f - b.Center()) * (R / h)}, nullptr, PairKind::Touch, tol);
            return d;
        }
        const double half = std::sqrt((R - h) * (R + h));
        AddCrossings(d, a, b, f - u * half, f + u * half, R - h, tol);
        return d;
    }

    inline PairDecision Decide(const Arc &a, const Arc &b, double tol) {
        PairDecision d;
        const Vertex dc = b.Center() - a.Center();
        const double dd = Norm(dc);
        const double ra = a.Radius();
        const double rb = b.Radius();
        if (dd <= tol && std::abs(ra - rb) <= tol) {
            d.sameDirection = a.CCW() == b.CCW();
            const double lo = a.Param(d.sameDirection ? b.Start() : b.End());
            const double hi = lo + b.Sweep();
            const Vertex &plo = d.sameDirection ? b.Start() : b.End();
            const Vertex &phi = d.sameDirection ? b.End() : b.Start();
            AddStretch(d, a, b, a.Sweep(), ra, lo, hi, plo, phi, tol);
            AddStretch(d, a, b, a.Sweep(), ra, lo - 2.0 * std::numbers::pi, hi - 2.0 * std::numbers::pi, plo, phi, tol);
            return d;
        }
        if (dd == 0.0)
            return d;
        const Vertex u = dc / dd;
        const double outer = dd - (ra + rb);
        const double inner = std::abs(ra - rb) - dd;
        // tangent where the distance equals the sum or difference of the radii within its own rounding (tol without k);
        // circles apart by up to tol touch on the arc of the smaller scale, the other takes the gap
        const double rounding = tol / config::state->k;
        if (outer >= -rounding || inner >= -rounding) {
            if (std::max(outer, inner) > tol)
                return d;
            const bool outside = outer >= inner;
            const Vertex dir = outside || ra > rb ? u : -u;
            const Vertex onA = a.Center() + dir * ra;
            const Vertex onB = outside ? b.Center() - u * rb : b.Center() + dir * rb;
            AddMeeting(d, a, b, {a.LengthScale() <= b.LengthScale() ? onA : onB}, nullptr, PairKind::Touch, tol);
            return d;
        }
        const double l = ((ra - rb) * (ra + rb) + dd * dd) / (2.0 * dd);
        const double h2 = (ra - l) * (ra + l);
        const Vertex f = a.Center() + u * l;
        if (h2 <= 0.0) {
            AddMeeting(d, a, b, {f}, nullptr, PairKind::Touch, tol);
            return d;
        }
        const double h = std::sqrt(h2);
        const Vertex n{-u.y * h, u.x * h};
        AddCrossings(d, a, b, f + n, f - n, std::min(-outer, -inner), tol);
        return d;
    }

    inline PairDecision Decide(const Arc &a, const Line &b, double tol) {
        PairDecision d = Decide(b, a, tol);
        for (size_t k = 0; k < d.n; ++k)
            std::swap(d.pts[k].la, d.pts[k].lb);
        return d;
    }

    // the record of the cut: per pair the points it made (on each edge Inside: split there, AtStart/AtEnd: that end),
    // and the ends it put onto a vertex they do not equal
    struct PairEvent {
        Vertex at;
        PairKind kind;
        Location la;
        Location lb;
        bool sameDirection;
    };

    struct Identified {
        Vertex from;
        Vertex to;
    };

    struct CutRecord {
        std::vector<PairEvent> events;
        std::vector<Identified> identified;
    };

    // a part's point on the whole edge: a spline's parts meet inside it, a closed spline's seam is inside it too (it has
    // no vertex, as a circle)
    template <class E>
    Location OnEdge(const E &e, const Vertex &x, Location part, double tol) {
        if constexpr (std::is_same_v<E, Spline>) {
            if (part == Location::Off)
                return Location::Off;
            if (e.IsClosed())
                return Location::Inside;
            if (x.IsClose(e.Start(), tol))
                return Location::AtStart;
            if (x.IsClose(e.End(), tol))
                return Location::AtEnd;
            return Location::Inside;
        } else {
            return part;
        }
    }

    // the parameter of x along e, -1 where a spline cannot place it; splitTol grows to how far x lies off the part
    template <class E, class P>
    double AlongEdge(const E &e, const P &part, const Vertex &x, Location l, double &splitTol) {
        if (l == Location::AtStart)
            return 0.0;
        if (l == Location::AtEnd)
            return e.Param(e.End());
        if constexpr (std::is_same_v<E, Spline>) {
            splitTol = std::max(splitTol, 2.0 * part.CarrierDistance(x));
            // a closed spline's seam: its end where the part is the last one, so that a stretch on it does not wrap round
            if (e.IsClosed() && x.IsClose(e.Start(), splitTol))
                return x.IsClose(part.End(), splitTol) && !x.IsClose(part.Start(), splitTol) ? (double)e.Approximation().size() : 0.0;
            return e.Param(x, splitTol);
        } else {
            return e.Param(x);
        }
    }

    enum class CutResult { Untouched, Crossing, Overlap, Congruent };

    // a and b keep their first piece in place, the others are appended. aShared marks the pieces of a lying on b (a itself
    // first, empty: none), bShared that b as a whole lies on a
    struct CutPieces {
        EdgeList a, b;
        std::vector<bool> aShared;
        bool bShared = false;

        bool AShared(size_t k) const { return k < aShared.size() && aShared[k]; }
    };

    // a spline locates the point with the caller's tol: its own may be smaller and miss it
    template <class E>
    double ParamOf(const E &e, const Vertex &x, double tol) {
        if constexpr (std::is_same_v<E, Spline>)
            return e.Param(x, tol);
        else
            return e.Param(x);
    }

    // the points within tol of both parts: endpoints on the other carrier and the computed crossings
    template <class A, class B>
    Points CommonPoints(const A &pa, const B &pb, double tol) {
        Points on;
        if (!pa.Box().Overlaps(pb.Box(), tol))
            return on;
        for (const Vertex &x : Candidates(pa, pb, tol))
            if (pa.Locate(x, tol) != Location::Off && pb.Locate(x, tol) != Location::Off)
                on.Add(x, tol);
        return on;
    }

    struct Interval {
        double t0;
        double t1;

        auto operator<=>(const Interval &) const = default;
    };

    class Intervals {
        private:
            std::vector<Interval> iv;

        public:
            void Add(double a, double b) { iv.push_back(a < b ? Interval{a, b} : Interval{b, a}); }
            void Merge() {
                if (iv.empty())
                    return;
                std::sort(iv.begin(), iv.end());
                size_t k = 0;
                for (size_t i = 1; i < iv.size(); ++i) {
                    if (iv[i].t0 <= iv[k].t1)
                        iv[k].t1 = iv[i].t1 > iv[k].t1 ? iv[i].t1 : iv[k].t1;
                    else
                        iv[++k] = iv[i];
                }
                iv.resize(k + 1);
            }
            // open: the ends are the split points of the overlap
            bool IsInside(double t) const {
                for (const Interval &i : iv)
                    if (t > i.t0 && t < i.t1)
                        return true;
                return false;
            }
            // a short piece's middle parameter rounds onto an end of the stretch it lies in
            bool Covers(double t) const {
                for (const Interval &i : iv)
                    if (t >= i.t0 && t <= i.t1)
                        return true;
                return false;
            }
            bool Empty() const { return iv.empty(); }
    };

    template <class E>
    void Replace(E &e, std::vector<E> pieces, EdgeList &out) {
        e = std::move(pieces.front());
        for (size_t k = 1; k < pieces.size(); ++k)
            out.emplace_back(std::move(pieces[k]));
    }

    // a spline puts a split point within tol of one of its own vertices onto that vertex: the record moves the vertex
    // onto the decided point
    template <class E>
    std::vector<E> SplitDecided(const E &e, const SplitPoints &pts, double tol, CutRecord &rec) {
        std::vector<E> pieces = Split(e, pts, tol);
        if constexpr (std::is_same_v<E, Spline>) {
            for (const PointOnCurve &x : pts) {
                Vertex at = e.Start();
                double nearest = Norm(e.Start() - x.p);
                auto consider = [&](const Vertex &v) {
                    const double d = Norm(v - x.p);
                    if (d < nearest) {
                        nearest = d;
                        at = v;
                    }
                };
                consider(e.End());
                for (size_t k = 0; k + 1 < pieces.size(); ++k)
                    consider(pieces[k].End());
                // Split takes a point onto a vertex it has within the spline's own tolerance, which can exceed the pair's:
                // an end of the spline too (the seam of a closed one's halves), and the other edge's pieces end there
                if (at != x.p)
                    rec.identified.push_back({at, x.p});
            }
        }
        return pieces;
    }

    // a stretch of the pair ending where the circle's half ends while the other edge goes on is a seam of the halves, no
    // point of the circle
    inline bool IsSeam(const PairPoint &p, Location onCircle, Location onOther) {
        return p.kind == PairKind::Overlap && onCircle != Location::Inside && onOther == Location::Inside;
    }

    struct Meetings {
        std::vector<Vertex> a, b;
    };

    // the points where the parts of a and b meet, as the pairs' decisions put them on a and on b, without the seams of
    // a circle's halves; a circle is cut at them (CutCircles)
    inline Meetings MeetingsOf(const Edge &a, const Edge &b, double tol) {
        Meetings m;
        std::visit([&](const auto &x, const auto &y) {
            ForEachPart(x, [&](const auto &pa) {
                ForEachPart(y, [&](const auto &pb) {
                    if (!pa.Box().Overlaps(pb.Box(), tol))
                        return;
                    const PairDecision d = Decide(pa, pb, tol);
                    for (size_t k = 0; k < d.n; ++k) {
                        const PairPoint &p = d.pts[k];
                        if (p.la == Location::Off || p.lb == Location::Off)
                            continue;
                        if (!IsSeam(p, p.la, p.lb))
                            m.a.push_back(p.at);
                        if (!IsSeam(p, p.lb, p.la))
                            m.b.push_back(p.at);
                    }
                });
            });
        }, a, b);
        return m;
    }

    // a circle meets another edge only where CutCircles cut it into arcs: the circle itself is left alone against
    // anything but a circle of its own place, the same curve twice
    template <class A, class B>
        requires(IsCircle<A> != IsCircle<B>)
    CutResult Cut(A &, B &, CutPieces &, CutRecord &, double = 0.0) {
        return CutResult::Untouched;
    }

    inline CutResult Cut(Circle &a, Circle &b, CutPieces &out, CutRecord &, double minTol = 0.0) {
        const double la = a.LengthScale();
        const double lb = b.LengthScale();
        if (!a.IsCongruent(b, std::max(Tol(la > lb ? la : lb), minTol)))
            return CutResult::Untouched;
        out.bShared = true;
        return CutResult::Congruent;
    }

    // f(pa, pb) for the parts of a and b, in the order of a's parts, then b's; two splines pass only the pairs whose boxes
    // may come within tol, found in a grid of b's pieces, not every pair
    template <class A, class B, class F>
    void ForPartPairs(const A &a, const B &b, double tol, F f) {
        if constexpr (std::is_same_v<A, Spline> && std::is_same_v<B, Spline>) {
            const std::span<const PrimitiveEdge> pb = b.Approximation();
            std::vector<BBox> boxes;
            for (const PrimitiveEdge &p : pb)
                boxes.push_back(BoxOf(p));
            Grid grid = Grid::OfBoxes(boxes);
            std::vector<uint32_t> nearby;
            for (const PrimitiveEdge &p : a.Approximation()) {
                nearby.clear();
                grid.InBox(BoxOf(p).Grown(2.0 * tol), nearby);
                std::sort(nearby.begin(), nearby.end());
                for (uint32_t j : nearby)
                    std::visit([&](const auto &x, const auto &y) { f(x, y); }, p, pb[j]);
            }
        } else {
            ForEachPart(a, [&](const auto &pa) { ForEachPart(b, [&](const auto &pb) { f(pa, pb); }); });
        }
    }

    // minTol: the tolerance the caller snaps with afterwards; what it will merge must already count as common here.
    // rec: collects the pair decisions (identified vertices, events) for the caller
    template <class A, class B>
        requires(!IsCircle<A> && !IsCircle<B>)
    CutResult Cut(A &a, B &b, CutPieces &out, CutRecord &rec, double minTol = 0.0) {
        static Timer t("planex Cut", false);
        Region reg(t);
        double la = a.LengthScale();
        double lb = b.LengthScale();
        const double tol = std::max(Tol(la > lb ? la : lb), minTol);
        if (!a.Box().Overlaps(b.Box(), tol))
            return CutResult::Untouched;
        SplitPoints sa, sb;
        Intervals oa, ob;
        double ta = tol;
        double tb = tol;
        auto identify = [&](const Vertex &end, const Vertex &x) {
            if (end != x)
                rec.identified.push_back({end, x});
        };
        auto decided = [&](const auto &pa, const auto &pb) {
            if (!pa.Box().Overlaps(pb.Box(), tol))
                return;
            const PairDecision d = Decide(pa, pb, tol);
            std::array<double, 4> ua{}, ub{};
            std::array<bool, 4> on{};
            for (size_t k = 0; k < d.n; ++k) {
                const PairPoint &p = d.pts[k];
                const Location ea = OnEdge(a, p.at, p.la, tol);
                const Location eb = OnEdge(b, p.at, p.lb, tol);
                if (ea == Location::Off || eb == Location::Off)
                    continue;
                ua[k] = AlongEdge(a, pa, p.at, ea, ta);
                ub[k] = AlongEdge(b, pb, p.at, eb, tb);
                if (ua[k] < 0.0 || ub[k] < 0.0)
                    continue;
                on[k] = true;
                if (ea == Location::Inside)
                    sa.Add(ua[k], p.at);
                else
                    identify(ea == Location::AtStart ? a.Start() : a.End(), p.at);
                if (eb == Location::Inside)
                    sb.Add(ub[k], p.at);
                else
                    identify(eb == Location::AtStart ? b.Start() : b.End(), p.at);
                rec.events.push_back({p.at, p.kind, ea, eb, d.sameDirection});
            }
            for (size_t k = 0; k < d.ns; ++k) {
                const auto [i, j] = d.stretches[k];
                if (on[i] && on[j]) {
                    oa.Add(ua[i], ua[j]);
                    ob.Add(ub[i], ub[j]);
                }
            }
        };
        ForPartPairs(a, b, tol, decided);

        if (oa.Empty()) {
            if (sa.IsEmpty() && sb.IsEmpty())
                return CutResult::Untouched;
            Replace(a, SplitDecided(a, sa, ta, rec), out.a);
            Replace(b, SplitDecided(b, sb, tb, rec), out.b);
            return CutResult::Crossing;
        }

        oa.Merge();
        ob.Merge();
        sa.RemoveIf([&](const PointOnCurve &s) { return oa.IsInside(s.t); });
        sb.RemoveIf([&](const PointOnCurve &s) { return ob.IsInside(s.t); });
        std::vector<A> pa = SplitDecided(a, sa, ta, rec);
        std::vector<B> pb = SplitDecided(b, sb, tb, rec);
        std::vector<bool> fa, fb;
        bool alla = true;
        bool allb = true;
        for (const A &p : pa) {
            bool f = oa.Covers(ParamOf(a, p.MidPoint(), tol));
            fa.push_back(f);
            alla = alla && f;
        }
        for (const B &p : pb) {
            bool f = ob.Covers(ParamOf(b, p.MidPoint(), tol));
            fb.push_back(f);
            allb = allb && f;
        }
        auto kept = [](auto pieces, const std::vector<bool> &overlapping) {
            decltype(pieces) k;
            for (size_t i = 0; i < pieces.size(); ++i)
                if (!overlapping[i])
                    k.push_back(std::move(pieces[i]));
            return k;
        };

        if (alla && allb) {
            out.bShared = true;
            return CutResult::Congruent;
        }
        if (alla) {
            out.aShared = {true};
            Replace(b, kept(std::move(pb), fb), out.b);
            return CutResult::Overlap;
        }
        if (allb) {
            out.bShared = true;
            Replace(a, kept(std::move(pa), fa), out.a);
            return CutResult::Overlap;
        }
        out.aShared = std::move(fa);
        Replace(a, std::move(pa), out.a);
        Replace(b, kept(std::move(pb), fb), out.b);
        return CutResult::Overlap;
    }

}
