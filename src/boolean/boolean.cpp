// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/boolean/boolean.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <variant>

#include "planex/wire/cleanup.hpp"

#ifdef PLANEX_THREADS
#include <taskflow/taskflow.hpp>
#endif

namespace planex2d {

    static void AddOperand(Arrangement &r, const Wire &w, unsigned char mask, const BBox &other, double tol) {
        for (const Edge &e : w) {
            r.c.Add(e);
            r.state.push_back({mask, BoxOf(e).Overlaps(other, tol)});
        }
    }

    // a vertex of a and one of b at the same place can differ by rounding, and a shared stretch keeps a's ends:
    // one point before the rays count, else a ray through it sees a boundary with a gap. only edges near the
    // other operand can have new vertices, all others were snapped before
    static double SnapNearOther(Arrangement &r, Grid &grid, const VertexMerges &ids) {
        static Timer tsnap("planex Boolean snap");
        Region regsnap(tsnap);
        auto nearOther = [&](size_t k) { return r.state[k].needsCutting; };
        double s = 0.0;
        for (size_t k = 0; k < r.c.Size(); ++k)
            if (nearOther(k))
                s = std::max(s, LengthScaleOf(r.c[k]));
        ApplyIdentified(r.c, r.state, ids, nearOther);
        ShareCongruent(r.c, grid, r.state, false, Tol(s));
        for (size_t k = 0; k < r.c.Size(); ++k)
            if (!r.state[k].excluded && nearOther(k))
                grid.Insert(k, BoxOf(r.c[k]));
        return Tol(s);
    }

    static void ClassifySides(Arrangement &r, Grid &grid, const CutRecord &rec, const VertexMerges &ids, double snapTol,
                              const BBox &boxa, const BBox &boxb) {
        static Timer tclassify("planex Boolean classify");
        Region regclassify(tclassify);
        const Wire &c = r.c;
        const std::vector<CutState> &state = r.state;
        auto inside = [&](size_t k) {
            const bool froma = state[k].mask == 1;
            const unsigned char other = froma ? 2 : 1;
            const Vertex m = MidPointOf(c[k]);
            return state[k].needsCutting && (froma ? boxb : boxa).IsInside(m) &&
                   (CrossingParity<false>(grid, c, state, m, other) & other) != 0;
        };
        const std::vector<signed char> side = SidesByRuns(c, state, rec, ids, snapTol, inside);
        for (size_t k = 0; k < c.Size(); ++k) {
            const CutState &s = state[k];
            if (s.excluded)
                continue;
            if (s.mask == 3) {
                const bool same = std::visit([&](const auto &x) {
                    Vertex m = x.MidPoint();
                    Vertex t = x.Tangent(m);
                    bool vertical = std::abs(t.x) >= std::abs(t.y);
                    unsigned char odd = vertical ? CrossingParity<true>(grid, c, state, m, 3, k)
                                                 : CrossingParity<false>(grid, c, state, m, 3, k);
                    return ((odd & 1) != 0) == ((odd & 2) != 0);
                }, c[k]);
                r.side[k] = same ? PieceSide::SharedSame : PieceSide::SharedOpposite;
                continue;
            }
            r.side[k] = (side[k] < 0 ? inside(k) : side[k] != 0) ? PieceSide::Inside : PieceSide::Outside;
        }
    }

    Arrangement Arranged(const Wire &a, const Wire &b, const BBox &boxa, const BBox &boxb, double tol, bool sides) {
        Arrangement r{Wire(a.Axes()), {}, {}};
        // the operands cut in an order of their own, by their boxes, not the caller's: a + b and b + a cut the same pairs
        // in the same order, so their pieces agree also where a crossing's place is poorly determined
        const bool bFirst = boxb.x0 != boxa.x0 ? boxb.x0 < boxa.x0
                            : boxb.y0 != boxa.y0 ? boxb.y0 < boxa.y0
                            : boxb.x1 != boxa.x1 ? boxb.x1 < boxa.x1
                                                 : boxb.y1 < boxa.y1;
        if (bFirst) {
            AddOperand(r, b, 2, boxa, tol);
            AddOperand(r, a, 1, boxb, tol);
        } else {
            AddOperand(r, a, 1, boxb, tol);
            AddOperand(r, b, 2, boxa, tol);
        }
        CutCircles(r.c, r.state);
        Grid grid = r.c.EdgeGrid();
        CutRecord rec;
        CutAll(r.c, grid, r.state, false, &rec);
        const VertexMerges ids(rec.identified);
        const double snapTol = SnapNearOther(r, grid, ids);
        r.side.assign(r.c.Size(), PieceSide::Outside);
        if (sides)
            ClassifySides(r, grid, rec, ids, snapTol, boxa, boxb);
        return r;
    }

    bool Keeps(BooleanOp op, bool froma, PieceSide side) {
        if (side == PieceSide::SharedSame || side == PieceSide::SharedOpposite)
            return (op == BooleanOp::Difference) != (side == PieceSide::SharedSame);
        const bool in = side == PieceSide::Inside;
        switch (op) {
            case BooleanOp::Union: return !in;
            case BooleanOp::Intersection: return in;
            case BooleanOp::Difference: return froma ? !in : in;
        }
        return false;
    }

    // a and b are clean regions: their edges meet only at shared endpoints
    Wire Boolean(const Wire &a, const Wire &b, BooleanOp op) {
        static Timer t("planex Boolean");
        Region reg(t);
        if (!a.Axes().IsClose(b.Axes()))
            throw std::invalid_argument("the operands lie on different planes; move them onto one plane first");
        const BBox boxa = a.Box(), boxb = b.Box();
        const double tol = Tol(std::max(boxa.NormInf(), boxb.NormInf()));
        if (a.Empty() || b.Empty() || !boxa.Overlaps(boxb, tol)) {
            Wire r(a.Axes());
            if (op != BooleanOp::Intersection)
                r = a;
            if (op == BooleanOp::Union)
                for (const Edge &e : b)
                    r.Add(e);
            return r;
        }
        Arrangement r = Arranged(a, b, boxa, boxb, tol, true);
        std::vector<bool> removed(r.c.Size());
        for (size_t k = 0; k < r.c.Size(); ++k)
            removed[k] = r.state[k].excluded || !Keeps(op, r.state[k].mask == 1, r.side[k]);
        r.c.Erase(removed);
        Erase(r.state, removed);
        // the cut changed vertices only near the other operand
        r.c.MergeOnCarriers([&](size_t k) { return r.state[k].needsCutting; });
        return r.c;
    }

    // z-order of the box centers (16 bits per axis): neighbours in the list are neighbours in the plane
    void SortSpatially(WireList &regions) {
        std::vector<BBox> boxes;
        BBox all;
        for (const Wire &r : regions) {
            boxes.push_back(r.Box());
            if (!r.Empty())
                all.Grow(boxes.back());
        }
        double sx = all.x1 > all.x0 ? 65535.0 / (all.x1 - all.x0) : 0.0;
        double sy = all.y1 > all.y0 ? 65535.0 / (all.y1 - all.y0) : 0.0;
        std::vector<std::pair<uint64_t, size_t>> keys;
        for (size_t i = 0; i < regions.size(); ++i) {
            uint64_t key = 0;
            if (!regions[i].Empty()) {
                const BBox &b = boxes[i];
                auto x = (uint64_t)((0.5 * (b.x0 + b.x1) - all.x0) * sx);
                auto y = (uint64_t)((0.5 * (b.y0 + b.y1) - all.y0) * sy);
                key = Spread(x) | Spread(y) << 1;
            }
            keys.emplace_back(key, i);
        }
        std::sort(keys.begin(), keys.end());
        WireList sorted;
        sorted.reserve(regions.size());
        for (const auto &[key, i] : keys)
            sorted.push_back(std::move(regions[i]));
        regions = std::move(sorted);
    }

#ifdef PLANEX_THREADS
    // the pairs of BalancedBoolean as a task graph: each boolean waits only for the two that wrote its inputs
    static void CombineOnThreads(WireList &regions, BooleanOp op) {
        const size_t n = regions.size();
        tf::Taskflow flow;
        std::vector<tf::Task> last(n);
        for (size_t stride = 1; stride < n; stride *= 2)
            for (size_t i = 0; i + stride < n; i += 2 * stride) {
                tf::Task t = flow.emplace([&regions, op, i, stride] { regions[i] = Boolean(regions[i], regions[i + stride], op); });
                if (!last[i].empty())
                    t.succeed(last[i]);
                if (!last[i + stride].empty())
                    t.succeed(last[i + stride]);
                last[i] = t;
            }
        Executor().run(flow).get();
    }
#endif

    // neighbours first, then pairs of pairs: slot i takes slot i + stride. sorted spatially the upper booleans only meet
    // at seams; the result does not depend on the number of threads
    Wire BalancedBoolean(WireList regions, BooleanOp op, bool multithreaded) {
        size_t n = regions.size();
        if (n == 0)
            return Wire();
        // two operands keep their order: on an overlap the first one's edge properties win
        if (n > 2)
            SortSpatially(regions);
#ifdef PLANEX_THREADS
        if (multithreaded && Executor().this_worker_id() < 0)
            CombineOnThreads(regions, op);
        else
#endif
            for (size_t stride = 1; stride < n; stride *= 2)
                for (size_t i = 0; i + stride < n; i += 2 * stride)
                    regions[i] = Boolean(regions[i], regions[i + stride], op);
        // each boolean snapped with the scale of the edges near its seam, the faces tell vertices apart with that of
        // the whole result: snapped once more with it, no edge is shorter than that
        Wire result = std::move(regions.front());
        if (n > 1)
            Snap(result);
        return result;
    }

    std::vector<Vertex> Intersections(const Wire &a, const Wire &b) {
        if (!a.Axes().IsClose(b.Axes()))
            throw std::invalid_argument("Intersections: the wires lie on different planes; move them onto one plane first");
        const double tol = Tol(std::max(a.LengthScale(), b.LengthScale()));
        Grid grid = b.EdgeGrid();
        std::vector<Vertex> pts;
        std::vector<uint32_t> nearby;
        // a point inside a stretch where a runs along b (the joints of a spline's pieces there) is no meeting point,
        // the stretch's ends are: a little before and after it along its edge of a, a still lies on b
        auto alongB = [&](const Vertex &q) {
            std::vector<uint32_t> close;
            grid.InBox(BBox::Of(q, q).Grown(tol), close);
            for (uint32_t j : close)
                if (Distance(q, b[j]) <= tol)
                    return true;
            return false;
        };
        auto insideOverlap = [&](const Edge &e, const Vertex &p) {
            constexpr double step = 1e-4;
            const double t = LengthFraction(e, p);
            const bool inner = IsClosedEdge(e) || (t > step && t < 1.0 - step);
            return inner && alongB(PointAtFraction(e, t - step)) && alongB(PointAtFraction(e, t + step));
        };
        for (const Edge &ea : a) {
            nearby.clear();
            grid.InBox(BoxOf(ea).Grown(tol), nearby);
            for (uint32_t j : nearby)
                std::visit([&](const auto &x, const auto &y) {
                    ForEachPart(x, [&](const auto &pa) {
                        ForEachPart(y, [&](const auto &pb) {
                            for (const Vertex &p : CommonPoints(pa, pb, tol))
                                if (!insideOverlap(ea, p))
                                    pts.push_back(p);
                        });
                    });
                }, ea, b[j]);
        }
        std::sort(pts.begin(), pts.end(), [](const Vertex &p, const Vertex &q) { return p.x < q.x; });
        std::vector<Vertex> unique;
        for (const Vertex &p : pts) {
            bool seen = false;
            for (size_t k = unique.size(); k > 0 && p.x - unique[k - 1].x <= tol && !seen; --k)
                seen = unique[k - 1].IsClose(p, tol);
            if (!seen)
                unique.push_back(p);
        }
        return unique;
    }

    // pairs sharing a mask bit are not cut; the state stays aligned with the wire
    void Arrange(Wire &c, std::vector<CutState> &state) {
        CutCircles(c, state);
        Grid grid = c.EdgeGrid();
        CutRecord rec;
        CutAll(c, grid, state, false, &rec);
        ApplyIdentified(c, state, VertexMerges(rec.identified), [](size_t) { return true; });
        ShareCongruent(c, grid, state, false, c.Tol());
        std::vector<bool> removed(c.Size());
        for (size_t k = 0; k < c.Size(); ++k)
            removed[k] = state[k].excluded;
        c.Erase(removed);
        Erase(state, removed);
    }

}
