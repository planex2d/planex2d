// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "planex/core/config.hpp"
#include "planex/core/vertex.hpp"
#include "planex/core/grid.hpp"
#include "planex/core/bbox.hpp"
#include "planex/curves/edge.hpp"
#include "planex/curves/edgelist.hpp"
#include "planex/curves/line.hpp"
#include "planex/curves/arc.hpp"
#include "planex/curves/circle.hpp"
#include "planex/curves/spline.hpp"
#include "planex/core/timer.hpp"

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

    Points Candidates(const Line &a, const Line &b, double tol);

    Points Candidates(const Line &a, const Arc &b, double tol);

    Points Candidates(const Arc &a, const Arc &b, double tol);

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

    Location Position(const Line &e, const Vertex &x, double tol, bool sameCarrier = false);
    Location Position(const Arc &e, const Vertex &x, double tol, bool sameCarrier = false);

    template <class E>
    double ParamOn(const E &e, const Vertex &x, double tol) {
        const Location l = Position(e, x, tol);
        return l == Location::AtStart ? 0.0 : l == Location::AtEnd ? e.Param(e.End()) : e.Param(x);
    }
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

    inline bool IsAnEnd(const Points &at, size_t n, const Vertex &x, double d) {
        for (size_t i = 0; i < n; ++i)
            if (Norm(at[i] - x) <= d)
                return true;
        return false;
    }

    inline Vertex Between(const Line &e, const Vertex &p, const Vertex &q, double tol) {
        const Vertex r = e.End() - e.Start();
        return e.PointAtFraction(0.5 * (ParamOn(e, p, tol) + ParamOn(e, q, tol)) / Dot(r, r));
    }
    inline Vertex Between(const Arc &e, const Vertex &p, const Vertex &q, double tol) {
        return e.PointAt(0.5 * (ParamOn(e, p, tol) + ParamOn(e, q, tol)));
    }

    template <class E>
    bool LiesOn(const E &e, const Vertex &x, double tol) {
        return e.OnCarrier(x, tol) && Position(e, x, tol) != Location::Off;
    }

    // a point replaces another only within tol of it, an end before an x, a crossing only within its own uncertainty. Two
    // points apart along both edges bound a shared stretch where each edge's middle between them lies on the other (two
    // arcs the long way round share the ends, not the path). other: the pair's second crossing, the ends nearer to it
    // are its own
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
        if (!LiesOn(b, Between(a, at[i], at[j], tol), tol) || !LiesOn(a, Between(b, at[i], at[j], tol), tol)) {
            AddPairPoint(d, a, b, at[i], kind, tol);
            AddPairPoint(d, a, b, at[j], kind, tol);
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

    // b on a's carrier as [lo, hi] in a's parameter, a itself [0, end]; two ends within tol along the carrier are one vertex, a's
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

    PairDecision Decide(const Line &a, const Line &b, double tol);

    PairDecision Decide(const Line &a, const Arc &b, double tol);

    PairDecision Decide(const Arc &a, const Arc &b, double tol);

    inline PairDecision Decide(const Arc &a, const Line &b, double tol) {
        PairDecision d = Decide(b, a, tol);
        for (size_t k = 0; k < d.n; ++k)
            std::swap(d.pts[k].la, d.pts[k].lb);
        return d;
    }

    // on each edge Inside: split there, AtStart/AtEnd: that end
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
        // a (b) was split at a point off it beyond rounding: its pieces turn by up to tol
        bool aOff = false;
        bool bOff = false;

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
            void Merge();
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

    Meetings MeetingsOf(const Edge &a, const Edge &b, double tol);

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

    // in the order of a's parts, then b's; two splines pass only the pairs whose boxes may come within tol
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

    template <class E>
    struct OverlapPieces {
        std::vector<E> pieces;
        std::vector<bool> shared;
        bool allShared = true;
    };

    template <class E>
    OverlapPieces<E> SplitAtOverlaps(const E &e, SplitPoints &split, Intervals &overlap, double splitTol, double tol, CutRecord &rec) {
        overlap.Merge();
        split.RemoveIf([&](const PointOnCurve &s) { return overlap.IsInside(s.t); });
        OverlapPieces<E> r{SplitDecided(e, split, splitTol, rec)};
        for (const E &p : r.pieces) {
            r.shared.push_back(overlap.Covers(ParamOf(e, p.MidPoint(), tol)));
            r.allShared = r.allShared && r.shared.back();
        }
        return r;
    }

    // minTol: the tolerance the caller snaps with afterwards; what it will merge must already count as common here
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
        const double rounding = tol / config::state->k;
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
                if (ea == Location::Inside) {
                    sa.Add(ua[k], p.at);
                    out.aOff = out.aOff || a.Distance(p.at) > rounding;
                } else
                    identify(ea == Location::AtStart ? a.Start() : a.End(), p.at);
                if (eb == Location::Inside) {
                    sb.Add(ub[k], p.at);
                    out.bOff = out.bOff || b.Distance(p.at) > rounding;
                } else
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

        OverlapPieces<A> pa = SplitAtOverlaps(a, sa, oa, ta, tol, rec);
        OverlapPieces<B> pb = SplitAtOverlaps(b, sb, ob, tb, tol, rec);
        auto unshared = [](auto side) {
            decltype(side.pieces) k;
            for (size_t i = 0; i < side.pieces.size(); ++i)
                if (!side.shared[i])
                    k.push_back(std::move(side.pieces[i]));
            return k;
        };

        if (pa.allShared && pb.allShared) {
            out.bShared = true;
            return CutResult::Congruent;
        }
        if (pa.allShared) {
            out.aShared = {true};
            Replace(b, unshared(std::move(pb)), out.b);
            return CutResult::Overlap;
        }
        if (pb.allShared) {
            out.bShared = true;
            Replace(a, unshared(std::move(pa)), out.a);
            return CutResult::Overlap;
        }
        out.aShared = std::move(pa.shared);
        Replace(a, std::move(pa.pieces), out.a);
        Replace(b, unshared(std::move(pb)), out.b);
        return CutResult::Overlap;
    }

}
