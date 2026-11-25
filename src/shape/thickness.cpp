// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/shape/thickness.hpp"

#include <limits>
#include <queue>
#include <tuple>

namespace planex2d {

    std::vector<ThicknessProbe::Corner> ThicknessProbe::Corners() const {
        std::map<std::pair<double, double>, std::vector<std::pair<size_t, bool>>> at;
        for (size_t i = 0; i < n; ++i) {
            const Vertex s = Wire::EndOf(region[i], false), e = Wire::EndOf(region[i], true);
            at[{s.x, s.y}].push_back({i, true});
            at[{e.x, e.y}].push_back({i, false});
        }
        std::vector<Corner> out;
        for (const auto &[p, meeting] : at) {
            if (meeting.size() != 2)
                continue;
            Corner c{Vertex{p.first, p.second}, {meeting[0].first, meeting[1].first}, {}};
            for (size_t k = 0; k < 2; ++k) {
                const auto [i, start] = meeting[k];
                const Vertex t = TangentAtFraction(region[i], start ? 0.0 : 1.0);
                c.leaving[k] = start ? t : t * -1.0;
            }
            out.push_back(c);
        }
        return out;
    }

    std::vector<ThicknessProbe::Contact> ThicknessProbe::Contacts(const Vertex &c, double r, double within) {
        nearby.clear();
        grid.InBox(BBox::Of(c, c).Grown(r + 2.0 * within), nearby);
        std::vector<Contact> out;
        for (uint32_t j : nearby) {
            if (exact && std::holds_alternative<Spline>(region[j])) {
                if (const Nearest m = Near(j, c); m.d <= r + within)
                    out.push_back({m.a, j});
                continue;
            }
            std::visit(
                [&](const auto &x) { ForEachPart(x, [&](const auto &q) { Touch(q, c, r, within, j, out); }); },
                region[j]);
        }
        return out;
    }

    // the point of the ridge of the distance to the boundary (the medial axis) on the segment c - h w to
    // c + h w, where that distance is largest (golden section), and the distance
    std::pair<Vertex, double> ThicknessProbe::Ridge(const Vertex &c, const Vertex &w, double h) {
        const double g = (std::sqrt(5.0) - 1.0) / 2.0;
        double a = -h, b = h;
        double x = b - g * (b - a), y = a + g * (b - a);
        double fx = Clearance(c + w * x), fy = Clearance(c + w * y);
        for (int it = 0; it < 60 && b - a > 1e-14 * size; ++it)
            if (fx > fy) {
                b = y;
                y = x;
                fy = fx;
                x = b - g * (b - a);
                fx = Clearance(c + w * x);
            } else {
                a = x;
                x = y;
                fx = fy;
                y = a + g * (b - a);
                fy = Clearance(c + w * y);
            }
        const Vertex m = c + w * (0.5 * (a + b));
        return {m, Clearance(m)};
    }

    // along the ridge the disc lies on, growing, away from its two touching points farthest apart, to the first disc
    // whose touching points face each other, or the disc where the ridge grows no more; the steps stay within a quarter
    // of the radius, so the ridge's search stays inside. It ends at a pair of lines whose wall is known
    std::optional<std::pair<Vertex, double>> ThicknessProbe::FacingAlong(Vertex c, double r, std::vector<Contact> touch,
                                                                         std::vector<std::pair<uint32_t, uint32_t>> &passed) {
        double h = 0.05 * r + 1e-6 * size;
        for (int step = 0; step < 400 && !touch.empty(); ++step) {
            if (const std::optional<std::pair<uint32_t, uint32_t>> lines = LinesOf(touch)) {
                if (const auto known = walls.find(*lines); known != walls.end())
                    return known->second;
                if (passed.empty() || passed.back() != *lines)
                    passed.push_back(*lines);
            }
            Vertex grow = c - touch[0].at;
            double widest = 2.0;
            for (size_t i = 0; i < touch.size(); ++i)
                for (size_t k = i + 1; k < touch.size(); ++k) {
                    const Vertex a = Normalized(c - touch[i].at), b = Normalized(c - touch[k].at);
                    if (Dot(a, b) < widest) {
                        widest = Dot(a, b);
                        grow = a + b;
                    }
                }
            grow = Normalized(grow);
            const Vertex across{-grow.y, grow.x};
            h = std::min(h, 0.25 * r);
            const auto [next, grown] = Ridge(c + grow * h, across, 2.0 * h);
            if (grown <= r) {
                h *= 0.5;
                if (h < 1e-12 * size)
                    return std::pair{c, r};
                continue;
            }
            std::vector<Contact> reached = Contacts(next, grown, ridgeSlack);
            if (IsFacing(next, reached)) {
                double lo = 0.0, hi = h;
                std::pair<Vertex, double> first{next, grown};
                for (int it = 0; it < 30; ++it) {
                    const double m = 0.5 * (lo + hi);
                    const auto [cm, rm] = Ridge(c + grow * m, across, 2.0 * m);
                    if (IsFacing(cm, Contacts(cm, rm, ridgeSlack))) {
                        hi = m;
                        first = {cm, rm};
                    } else
                        lo = m;
                }
                return first;
            }
            c = next;
            r = grown;
            touch = std::move(reached);
            h *= 1.5;
        }
        return std::nullopt;
    }

    // a reflex corner's inward normals at it, each away from the other edge: the disc touching there leans
    // anywhere between them
    void ThicknessProbe::FindReflexCorners() {
        for (const Corner &c : Corners()) {
            if (InsideAngle(c) <= 180.0 + 1e-9)
                continue;
            ReflexCorner r{c.at, {c.edges[0], c.edges[1]}, {}};
            for (size_t k = 0; k < 2; ++k) {
                const Vertex normal{-c.leaving[k].y, c.leaving[k].x};
                r.normals[k] = Dot(normal, c.leaving[1 - k]) < 0.0 ? normal : normal * -1.0;
            }
            reflex.push_back(r);
        }
    }

    std::optional<ThicknessSample> ThicknessProbe::From(const Vertex &p, const Vertex &inward) {
        const Line ray(p + inward * nudge, p + inward * (4.0 * size));
        double reach = 4.0 * size;
        size_t hit = n;
        Vertex where = p;
        nearby.clear();
        grid.InBox(ray.Box(), nearby);
        for (uint32_t j : nearby)
            std::visit([&](const auto &x) {
                ForEachPart(x, [&](const auto &q) {
                    for (const Vertex &c : CommonPoints(ray, q, tol))
                        if (const double s = Dot(c - p, inward); s > 10.0 * tol && s < reach) {
                            reach = s;
                            hit = j;
                            where = c;
                        }
                });
            }, region[j]);
        if (hit == n)
            return std::nullopt;
        if (!ball)
            return !sharp.empty() && IsHeld({p, where}, 0.5 * reach)
                       ? std::nullopt
                       : std::optional(ThicknessSample{p, where, (p + where) * 0.5, reach});
        // the largest disc touching at p: between 0 and half the ray's reach, 60 halvings at most
        double lo = 0.0, hi = 0.5 * reach;
        for (int it = 0; it < 60 && hi - lo > 1e-14 * size; ++it) {
            const double r = 0.5 * (lo + hi);
            (IsDiscInside(p + inward * r, r) ? lo : hi) = r;
        }
        const Vertex c = p + inward * lo;
        const std::vector<Contact> touch = Contacts(c, lo, slack);
        if (IsFacing(c, touch)) {
            const auto [other, j] = Touching(c, lo, p);
            if (j == n || (!sharp.empty() && IsHeld(PointsOf(touch), lo)))
                return std::nullopt;
            return ThicknessSample{p, other.a, c, 2.0 * lo};
        }
        const std::optional<std::pair<Vertex, double>> wall = WallOf(c, lo, touch);
        if (!wall)
            return std::nullopt;
        const auto &[centre, r] = *wall;
        const std::vector<Contact> reached = Contacts(centre, r, ridgeSlack);
        if (!sharp.empty() && IsHeld(PointsOf(reached), r))
            return std::nullopt;
        Vertex farthest = reached.front().at;
        for (const Contact &q : reached)
            if (Norm(q.at - p) > Norm(farthest - p))
                farthest = q.at;
        return ThicknessSample{p, farthest, centre, 2.0 * r, true};
    }

    std::pair<Nearest, size_t> ThicknessProbe::Touching(const Vertex &c, double r, const Vertex &p) {
        nearby.clear();
        grid.InBox(BBox::Of(c, c).Grown(r * (1.0 + 1e-6) + tol), nearby);
        std::pair<Nearest, size_t> best{Nearest{}, n};
        for (uint32_t j : nearby) {
            const Nearest m = Near(j, c);
            if (Norm(m.a - p) > 0.1 * r && m.d < best.first.d)
                best = {m, j};
        }
        return best;
    }

    ThicknessProbe::ThicknessProbe(const Wire &region, bool ball, double corner): region(region), ball(ball), n(region.Size()), grid(region.EdgeGrid()) {
        const BBox box = region.Box();
        size = NormInf(box.Size());
        tol = Tol(std::max(box.NormInf(), size));
        nudge = 1e-9 * size;
        // 1e-9: a disc's radius from the halving in From is good to 1e-14 of the size; touching within this
        slack = 1e-9 * size;
        // 1e-11: a point of a ridge from its golden section is good to about 1e-13 of the size; the first disc
        // along it that faces comes before the wall by this
        ridgeSlack = 1e-11 * size;
        for (const Edge &e : region)
            if (const Spline *s = std::get_if<Spline>(&e))
                deviation = std::max(deviation, s->ApproximationTol());
        if (corner > 0.0)
            FindSharpCorners(corner);
        if (ball)
            FindReflexCorners();
    }

    // on the exact curves (the samples follow the approximation, within spline_tol of them)
    std::optional<ThicknessSample> ThicknessProbe::ExactAt(size_t i, double t) {
        Vertex p = PointAtFraction(region[i], t), tangent = TangentAtFraction(region[i], t);
        if (const Spline *s = std::get_if<Spline>(&region[i])) {
            tangent = s->ExactTangent(p);
            p = s->ExactNearestPoint(p);
        }
        exact = true;
        const std::optional<ThicknessSample> out = Along(i, p, tangent);
        exact = false;
        return out;
    }

    std::pair<size_t, double> ThicknessProbe::EdgeAt(const Vertex &p) {
        nearby.clear();
        grid.InBox(BBox::Of(p, p).Grown(tol), nearby);
        std::pair<size_t, double> best{0, 0.0};
        double d = std::numeric_limits<double>::infinity();
        for (uint32_t j : nearby)
            if (const Nearest m = Near(j, p); m.d < d) {
                d = m.d;
                best = {j, LengthFraction(region[j], m.a)};
            }
        return best;
    }

    // the grid's cells reach past the box, so an edge they return farther than the box is not yet the nearest; on the
    // exact curves only the edges whose approximation comes that near, to twice its deviation
    double ThicknessProbe::Clearance(const Vertex &c) {
        double d = std::numeric_limits<double>::infinity();
        for (double r = 0.01 * size; r < 8.0 * size; r *= 4.0) {
            nearby.clear();
            grid.InBox(BBox::Of(c, c).Grown(r), nearby);
            for (uint32_t j : nearby)
                d = std::min(d, Approximated(j, c).d);
            if (d <= r)
                break;
        }
        if (!exact)
            return d;
        const double within = d + 2.0 * deviation;
        nearby.clear();
        grid.InBox(BBox::Of(c, c).Grown(within), nearby);
        double e = std::numeric_limits<double>::infinity();
        for (uint32_t j : nearby)
            if (Approximated(j, c).d <= within)
                e = std::min(e, Near(j, c).d);
        return e;
    }

    // the largest disc inside, up to 1e-3 of the size: its center, the point inside farthest from the boundary
    // (found as polylabel finds the pole of inaccessibility: cells over the box, split while a point in one
    // could lie farther than the best found by more than that), and where it touches the boundary; nothing for
    // a region without inside
    std::optional<std::pair<Vertex, Vertex>> ThicknessProbe::Pole() {
        struct Cell {
            Vertex c;
            double h, d;
            double Bound() const { return d + h * std::numbers::sqrt2; }
        };
        const auto cell = [&](const Vertex &c, double h) {
            return Cell{c, h, region.Contains(c) ? Clearance(c) : -Clearance(c)};
        };
        const auto lower = [](const Cell &a, const Cell &b) { return a.Bound() < b.Bound(); };
        std::priority_queue<Cell, std::vector<Cell>, decltype(lower)> cells(lower);
        const BBox box = region.Box();
        const Vertex extent = box.Size();
        const double side = std::min(extent.x, extent.y);
        if (!(side > 0.0))
            return std::nullopt;
        for (double x = box.x0; x < box.x1; x += side)
            for (double y = box.y0; y < box.y1; y += side)
                cells.push(cell({x + 0.5 * side, y + 0.5 * side}, 0.5 * side));
        Cell best = cell(box.Center(), 0.0);
        while (!cells.empty()) {
            const Cell c = cells.top();
            cells.pop();
            if (c.d > best.d)
                best = c;
            if (c.Bound() - best.d <= 1e-3 * size)
                continue;
            const double h = 0.5 * c.h;
            for (const Vertex &o : {Vertex{-h, -h}, Vertex{h, -h}, Vertex{-h, h}, Vertex{h, h}})
                cells.push(cell(c.c + o, h));
        }
        if (!(best.d > 0.0))
            return std::nullopt;
        nearby.clear();
        grid.InBox(BBox::Of(best.c, best.c).Grown(best.d + tol), nearby);
        Nearest touch;
        for (uint32_t j : nearby)
            if (const Nearest m = Near(j, best.c); m.d < touch.d)
                touch = m;
        return std::pair{best.c, touch.a};
    }

    // perEdge: the number of samples on each edge; best: the thinnest or the thickest sample, as the refinement asks
    struct BoundarySamples {
        struct Place {
            size_t edge;
            double t;
        };
        std::vector<ThicknessSample> samples;
        std::vector<Place> at;
        std::vector<size_t> perEdge;
        size_t best = 0;

        std::pair<double, double> Bracket(size_t edge, double t) const {
            const double spacing = 1.0 / (double)perEdge[edge];
            return {std::max(0.0, t - spacing), std::min(1.0, t + spacing)};
        }
    };

    // golden section: [a, b] narrowed towards the least f, steps times at most
    template <class F>
    static void NarrowToLeast(F f, double &a, double &b, int steps) {
        const double g = (std::sqrt(5.0) - 1.0) / 2.0;
        for (int it = 0; it < steps && b - a > 1e-15; ++it) {
            const double c = b - g * (b - a), d = a + g * (b - a);
            if (f(c) < f(d))
                b = d;
            else
                a = c;
        }
    }

    // every local minimum within 1.5 times the thinnest sample (a sample walked to its wall is its wall already)
    static std::vector<size_t> ThinCandidates(const std::vector<ThicknessSample> &samples, size_t best) {
        std::vector<size_t> candidates;
        for (size_t j = 0; j < samples.size(); ++j) {
            const double v = samples[j].thickness, margin = 1e-9 * v;
            const bool dip = (j == 0 || v < samples[j - 1].thickness - margin) && (j + 1 == samples.size() || v < samples[j + 1].thickness - margin);
            if (!samples[j].walked && v <= 1.5 * samples[best].thickness && (j == best || dip))
                candidates.push_back(j);
        }
        return candidates;
    }

    static ThicknessSample Thinnest(ThicknessProbe &probe, const BoundarySamples &s, bool spline) {
        const auto narrow = [&](size_t j, size_t e, double &a, double &b, int steps) {
            NarrowToLeast([&](double u) {
                const std::optional<ThicknessSample> x = probe.At(e, u);
                return x ? x->thickness : s.samples[j].thickness;
            }, a, b, steps);
        };
        ThicknessSample result = s.samples[s.best];
        size_t edge = s.at[s.best].edge, winner = s.best;
        double where = s.at[s.best].t, lo = 0.0, hi = 0.0;
        // 20 steps take the bracket of two sample spacings to 1e-4 of one; the thinnest found then goes on to rounding
        // (it may lie at a knife edge's tip, at the end of its edge)
        for (size_t j : ThinCandidates(s.samples, s.best)) {
            const size_t e = s.at[j].edge;
            auto [a, b] = s.Bracket(e, s.at[j].t);
            narrow(j, e, a, b, 20);
            if (const std::optional<ThicknessSample> x = probe.At(e, 0.5 * (a + b)); x && x->thickness < result.thickness) {
                result = *x;
                edge = e;
                where = 0.5 * (a + b);
                winner = j;
                lo = a;
                hi = b;
            }
        }
        if (hi > lo) {
            narrow(winner, edge, lo, hi, 60);
            if (const std::optional<ThicknessSample> x = probe.At(edge, 0.5 * (lo + hi)); x && x->thickness <= result.thickness) {
                result = *x;
                where = 0.5 * (lo + hi);
            }
        }
        if (spline)
            if (const std::optional<ThicknessSample> x = probe.ExactAt(edge, where))
                result = *x;
        return result;
    }

    // within a sample spacing either side of the thickest sample, on its edge; with ball where the largest disc inside
    // touches, which a sample can miss (a disc that touches reflex corners only leans anywhere between their inward
    // normals, which no sample inside an edge reaches): across the directions at such a corner around the one toward the
    // disc's center (its size over all of them peaks sharply there, and higher again toward the edges), else along its edge
    static ThicknessSample Thickest(ThicknessProbe &probe, const BoundarySamples &s, bool spline, bool ball) {
        const ThicknessSample &best = s.samples[s.best];
        const BoundarySamples::Place bestAt = s.at[s.best];
        std::optional<size_t> atCorner;
        size_t edge = bestAt.edge;
        auto [a, b] = s.Bracket(edge, bestAt.t);
        if (ball)
            if (const std::optional<std::pair<Vertex, Vertex>> pole = probe.Pole()) {
                const auto &[centre, touch] = *pole;
                atCorner = probe.ReflexCornerAt(touch);
                if (atCorner) {
                    const double toward = probe.CornerDirectionTo(*atCorner, centre);
                    a = std::max(0.0, toward - 0.05);
                    b = std::min(1.0, toward + 0.05);
                } else {
                    double t = 0.0;
                    std::tie(edge, t) = probe.EdgeAt(touch);
                    std::tie(a, b) = s.Bracket(edge, t);
                }
            }
        const auto sample = [&](double t) { return atCorner ? probe.AtCorner(*atCorner, t) : probe.At(edge, t); };
        NarrowToLeast([&](double t) {
            const std::optional<ThicknessSample> x = sample(t);
            return -(x ? x->thickness : best.thickness);
        }, a, b, 80);
        const std::optional<ThicknessSample> x = sample(0.5 * (a + b));
        const bool refined = x && x->thickness > best.thickness;
        ThicknessSample result = refined ? *x : best;
        // the result on the exact curves where the region has a spline; a sample at a reflex corner stays, its point is
        // the vertex, which the approximation sets
        if (spline && !(refined && atCorner))
            if (const std::optional<ThicknessSample> exact =
                    refined ? probe.ExactAt(edge, 0.5 * (a + b)) : probe.ExactAt(bestAt.edge, bestAt.t))
                result = *exact;
        return result;
    }

    // refine: -1 the thinnest, 1 the thickest sample, refined and appended last
    std::vector<ThicknessSample> Thickness(const Wire &region, size_t samples, bool ball, double corner, int refine) {
        if (region.Empty())
            return {};
        ThicknessProbe probe(region, ball, corner);
        const double total = region.Length();
        BoundarySamples s;
        for (const Edge &e : region)
            s.perEdge.push_back(std::max<size_t>(1, (size_t)std::lround((double)samples * ExactLengthOf(e) / total)));
        const auto better = [&](double a, double b) { return refine < 0 ? a < b : a > b; };
        for (size_t i = 0; i < region.Size(); ++i)
            for (size_t k = 0, n = s.perEdge[i]; k < n; ++k) {
                const double t = ((double)k + 0.5) / (double)n;
                const std::optional<ThicknessSample> x = probe.At(i, t);
                if (!x)
                    continue;
                if (refine != 0 && (s.samples.empty() || better(x->thickness, s.samples[s.best].thickness)))
                    s.best = s.samples.size();
                s.samples.push_back(*x);
                s.at.push_back({i, t});
            }
        if (refine == 0 || s.samples.empty())
            return std::move(s.samples);
        const bool spline =
            std::any_of(region.begin(), region.end(), [](const Edge &e) { return std::holds_alternative<Spline>(e); });
        s.samples.push_back(refine < 0 ? Thinnest(probe, s, spline) : Thickest(probe, s, spline, ball));
        return std::move(s.samples);
    }

}
