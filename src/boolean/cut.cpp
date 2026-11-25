// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/boolean/cut.hpp"

#include <numbers>

namespace planex2d {

    // endpoints on the other carrier are used as they are (bit exact), the formula only without them
    Points Candidates(const Line &a, const Line &b, double tol) {
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

    // via the foot of the center (a quadratic in the line parameter loses precision with the line length), taken from
    // the center: from a line's far start it rounds at the line's length, from the center at the arc's scale (exact
    // where the line's direction is, as an axis-parallel line's)
    Points Candidates(const Line &a, const Arc &b, double tol) {
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

    Points Candidates(const Arc &a, const Arc &b, double tol) {
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
        // ra - l without cancellation, as in Decide
        const double reach = d - ra;
        const double depth = (rb - reach) * (rb + reach) / (2.0 * d);
        double l = ra - depth;
        double h2 = depth * (ra + l);
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

    // x lies on e's carrier by the pair's decision: where along e, by the parameter alone. In an overlap (sameCarrier,
    // the pair is decided to share the carrier) an end within tol along it is that end, however far x lies across it (a
    // copy shifted by about tol: its corners are within tol of the carrier but just over tol apart)
    Location Position(const Line &e, const Vertex &x, double tol, bool sameCarrier) {
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

    Location Position(const Arc &e, const Vertex &x, double tol, bool sameCarrier) {
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

    PairDecision Decide(const Line &a, const Line &b, double tol) {
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
    PairDecision Decide(const Line &a, const Arc &b, double tol) {
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

    // either way round the same points: the formulas below round differently with a and b swapped, and near tangency
    // the crossings move by about radius / half the lens times that
    PairDecision Decide(const Arc &a, const Arc &b, double tol) {
        const Vertex ca = a.Center(), cb = b.Center();
        if (cb.x < ca.x || (cb.x == ca.x && (cb.y < ca.y || (cb.y == ca.y && b.Radius() < a.Radius())))) {
            PairDecision d = Decide(b, a, tol);
            for (size_t k = 0; k < d.n; ++k)
                std::swap(d.pts[k].la, d.pts[k].lb);
            return d;
        }
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
        // ra - l from how far b's circle reaches into a's: as ra minus l it cancels where b is far smaller than a (a
        // corner rounded with radius 1e-9 on a circle of radius 0.25: the half chord came out 0 or five times too long,
        // and the pair untouched)
        const double reach = dd - ra;
        const double depth = (rb - reach) * (rb + reach) / (2.0 * dd);
        const double l = ra - depth;
        const double h2 = depth * (ra + l);
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

    void Intervals::Merge() {
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

    // the points where the parts of a and b meet, as the pairs' decisions put them on a and on b, without the seams of
    // a circle's halves; a circle is cut at them (CutCircles)
    Meetings MeetingsOf(const Edge &a, const Edge &b, double tol) {
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

}
