// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/diagnostics.hpp"

#include <cstdint>
#include <map>

namespace planex2d::sketch {

    bool Diagnosis::IsDegenerate(size_t k) {
        const Element &e = g.elements[k];
        std::optional<Vertex> at;
        if (e.kind == ElementKind::Line)
            at = DegenerateLine(e);
        else if (e.IsRound())
            at = DegenerateRound(e);
        else if (e.kind == ElementKind::Spline)
            at = DegenerateSpline(e);
        if (!at)
            return false;
        degenerate[k] = 1;
        box.Grow(*at);
        out.degenerate.push_back({{k}, *at});
        return true;
    }

    std::optional<Vertex> Diagnosis::DegeneratePoles(const SplineData &d) const {
        const Vertex a = g.Coords(d.points.front());
        double size = 0.0;
        for (size_t p : d.points)
            size = std::max(size, NormInf(g.Coords(p)));
        for (size_t p : d.points)
            if (!g.Coords(p).IsClose(a, Tol(size)))
                return std::nullopt;
        return a;
    }

    std::optional<Vertex> Diagnosis::DegenerateSpline(const Element &e) const {
        if (const SplineData &d = g.splines[e.SplineIndex()]; d.poled)
            return DegeneratePoles(d);
        std::vector<Vertex> q = e.SplinePoints(g);
        if (g.splines[e.SplineIndex()].closed)
            q.push_back(q.front());
        double size = 0.0;
        for (const Vertex &p : q)
            size = std::max(size, NormInf(p));
        bool repeated = false;
        for (size_t k = 1; k < q.size(); ++k)
            repeated = repeated || q[k].IsClose(q[k - 1], Tol(size));
        if (!repeated)
            return std::nullopt;
        return HalfwayAlong(q);
    }

    Vertex Diagnosis::HalfwayAlong(const std::vector<Vertex> &q) {
        double length = 0.0;
        for (size_t k = 1; k < q.size(); ++k)
            length += Norm(q[k] - q[k - 1]);
        double left = 0.5 * length;
        for (size_t k = 1; k < q.size(); ++k) {
            const double l = Norm(q[k] - q[k - 1]);
            if (l > 0.0 && left <= l)
                return q[k - 1] + (q[k] - q[k - 1]) * (left / l);
            left -= l;
        }
        return q.front();
    }

    // the ends sorted along h: an end is met by another element's end at its place (one unknown, or within the
    // rounding); ends of two elements closer than the gap and not one point are a missing coincident when one of
    // them is met by none
    void Diagnosis::Ends() {
        std::vector<End> ends;
        for (size_t k = 0; k < g.elements.size(); ++k)
            if (IsLookedEnd(k))
                for (size_t p : g.elements[k].Ends(g))
                    ends.push_back({k, p, g.Coords(p)});
        std::sort(ends.begin(), ends.end(), [](const End &a, const End &b) { return a.at.x < b.at.x; });
        const double reach = std::max(Gap(), tol);
        std::vector<char> met(ends.size(), 0);
        for (size_t i = 0; i < ends.size(); ++i)
            for (size_t j = i + 1; j < ends.size() && ends[j].at.x - ends[i].at.x <= reach; ++j)
                if (ends[i].element != ends[j].element && IsOnePoint(ends[i], ends[j]))
                    met[i] = met[j] = 1;
        for (size_t i = 0; i < ends.size(); ++i)
            for (size_t j = i + 1; j < ends.size() && ends[j].at.x - ends[i].at.x <= reach; ++j) {
                const End &a = ends[i], &b = ends[j];
                if (a.element == b.element || (met[i] && met[j]) || IsOnePoint(a, b) || Norm(b.at - a.at) >= Gap())
                    continue;
                const auto [first, second] = std::minmax(a.element, b.element);
                out.missingCoincident.push_back({{first, second}, (a.at + b.at) * 0.5});
            }
        for (size_t i = 0; i < ends.size(); ++i)
            if (!met[i])
                out.openEnds.push_back({{ends[i].element}, ends[i].at});
    }

    void Diagnosis::Contacts() {
        if (flat.size() < 2)
            return;
        std::vector<BBox> boxes;
        for (const PrimitiveEdge &p : flat)
            boxes.push_back(BoxOf(p));
        Grid grid = Grid::OfBoxes(boxes);
        std::map<std::pair<size_t, size_t>, Contact> contacts;
        std::vector<uint32_t> nearby;
        for (size_t i = 0; i < flat.size(); ++i) {
            nearby.clear();
            grid.InBox(boxes[i].Grown(nearTol), nearby);
            for (uint32_t j : nearby)
                if (j > i && source[j] != source[i])
                    CutPair(i, j, contacts[{source[i], source[j]}]);
        }
        for (auto &[pair, contact] : contacts) {
            for (const Vertex &m : StretchMiddles(contact.shared))
                out.overlaps.push_back({{pair.first, pair.second}, m});
            std::vector<Vertex> seen;
            for (const Vertex &p : contact.points) {
                if ((IsEndOf(pair.first, p) && IsEndOf(pair.second, p)) || IsOn(contact.shared, p) || IsOn(seen, p))
                    continue;
                seen.push_back(p);
                out.crossings.push_back({{pair.first, pair.second}, p});
            }
        }
    }

    void Diagnosis::CutPair(size_t i, size_t j, Contact &c) const {
        PrimitiveEdge a = flat[i], b = flat[j];
        CutPieces pieces;
        CutRecord rec;
        const CutResult r = std::visit([&](auto &x, auto &y) { return Cut(x, y, pieces, rec, tol); }, a, b);
        for (const PairEvent &e : rec.events)
            if (e.kind != PairKind::Overlap)
                c.points.push_back(e.at);
        if (r != CutResult::Overlap && r != CutResult::Congruent)
            return;
        if (pieces.bShared) {
            c.shared.push_back(b);
            return;
        }
        for (size_t k = 0; k < pieces.a.size() + 1; ++k)
            if (pieces.AShared(k))
                c.shared.push_back(k == 0 ? a : PrimitiveOf(pieces.a[k - 1]));
    }

    std::vector<Vertex> Diagnosis::StretchMiddles(const std::vector<PrimitiveEdge> &shared) const {
        std::vector<Vertex> middles;
        std::vector<char> used(shared.size(), 0);
        const auto meets = [&](const Vertex &p, size_t except) {
            for (size_t k = 0; k < shared.size(); ++k)
                if (k != except && !used[k] && (shared[k].Start().IsClose(p, nearTol) || shared[k].End().IsClose(p, nearTol)))
                    return k;
            return none;
        };
        for (size_t first = 0; first < shared.size(); ++first) {
            if (used[first])
                continue;
            size_t start = first;
            bool reversed = false;
            for (size_t k = first; k < shared.size(); ++k)
                if (!used[k] && (meets(shared[k].Start(), k) == none || meets(shared[k].End(), k) == none)) {
                    start = k;
                    reversed = meets(shared[k].Start(), k) != none;
                    break;
                }
            std::vector<std::pair<size_t, bool>> run{{start, reversed}};
            used[start] = 1;
            Vertex tip = reversed ? shared[start].Start() : shared[start].End();
            for (size_t next = meets(tip, start); next != none; next = meets(tip, next)) {
                const bool flip = !shared[next].Start().IsClose(tip, nearTol);
                run.push_back({next, flip});
                used[next] = 1;
                tip = flip ? shared[next].Start() : shared[next].End();
            }
            middles.push_back(HalfwayAlong(shared, run));
        }
        return middles;
    }

    Vertex Diagnosis::HalfwayAlong(const std::vector<PrimitiveEdge> &shared, const std::vector<std::pair<size_t, bool>> &run) {
        double length = 0.0;
        for (const auto &[k, flip] : run)
            length += shared[k].Length();
        double left = 0.5 * length;
        for (const auto &[k, flip] : run) {
            const double l = shared[k].Length();
            if (left <= l) {
                const double t = l > 0.0 ? left / l : 0.5;
                return shared[k].PointAtFraction(flip ? 1.0 - t : t);
            }
            left -= l;
        }
        return shared[run.back().first].MidPoint();
    }

}
