// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/integrations/netgenocc/silhouette.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>

namespace planex2d {

    // a spline cut at points of its exact curve, each piece fitted again between them so that it ends exactly there
    // (Split cuts the approximation, which can miss a point of the curve by the approximation's tolerance); points
    // within tol of each other are one
    std::vector<Spline> SplitExactly(const Spline &s, const std::vector<Vertex> &at, double tol) {
        const Curve<double, Vertex> c = s.Segment();
        std::vector<std::pair<double, Vertex>> cuts;
        for (const Vertex &p : at)
            cuts.push_back({s.ExactNearest(p).first, p});
        std::sort(cuts.begin(), cuts.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
        std::vector<std::pair<double, Vertex>> kept{{c.First(), s.Start()}};
        for (const auto &x : cuts)
            if (!x.second.IsClose(kept.back().second, tol) && !x.second.IsClose(s.End(), tol))
                kept.push_back(x);
        kept.push_back({c.Last(), s.End()});
        std::vector<Spline> pieces;
        for (size_t k = 0; k + 1 < kept.size(); ++k)
            for (Spline &piece : Spline::FromNodes(c, {kept[k].first, kept[k + 1].first}, {kept[k].second, kept[k + 1].second})) {
                piece.props = s.props;
                pieces.push_back(std::move(piece));
            }
        return pieces;
    }

    // a fold line ends on an edge of its face, touching that edge's projection: the edges cut where fold lines end on
    // them (the arrangement cuts at crossings, a touch it may miss)
    void CutAtFoldEnds(Wire &w, const Wire &folds, double tol) {
        std::vector<Vertex> ends;
        for (const Edge &f : folds)
            if (!IsClosedEdge(f)) {
                ends.push_back(StartOf(f));
                ends.push_back(EndOf(f));
            }
        const auto pointsOn = [&]() {
            std::vector<std::vector<Vertex>> on(w.Size());
            for (size_t i = 0; i < w.Size(); ++i)
                for (const Vertex &p : ends)
                    if (ExactDistance(p, w[i]) <= tol && (IsClosedEdge(w[i]) || (!p.IsClose(StartOf(w[i]), tol) && !p.IsClose(EndOf(w[i]), tol))))
                        on[i].push_back(p);
            return on;
        };
        w.SplitCircles(pointsOn(), tol);
        const std::vector<std::vector<Vertex>> on = pointsOn();
        Wire cut(w.Axes());
        for (size_t i = 0; i < w.Size(); ++i)
            std::visit([&](const auto &e) {
                using E = std::decay_t<decltype(e)>;
                if constexpr (IsCircle<E>)
                    cut.Add(e);
                else if constexpr (std::is_same_v<E, Spline>) {
                    for (Spline &piece : on[i].empty() ? std::vector<Spline>{e} : SplitExactly(e, on[i], tol))
                        cut.Add(std::move(piece));
                } else {
                    SplitPoints pts;
                    for (const Vertex &p : on[i])
                        pts.Add(e.Param(p), p);
                    for (auto &piece : Split(e, pts, tol))
                        cut.Add(std::move(piece));
                }
            }, w[i]);
        w = std::move(cut);
    }

    std::vector<bool> ShadedCells(const Wire &w, const std::vector<HalfEdges> &cells, const std::vector<size_t> &cellOf,
                                  const TopoDS_Shape &shape, const AxisSystem &axes, double tol) {
        IntCurvesFace_ShapeIntersector hit;
        hit.Load(shape, tol);
        std::vector<bool> shaded(cells.size() + 1, false);
        for (size_t k = 0; k < cells.size(); ++k)
            if (const std::optional<Vertex> p = EdgesOf(w, cells[k]).InteriorPoint()) {
                hit.Perform(gp_Lin(gp_Pnt(ToOCC(axes.ToXYZ(*p))), gp_Dir(ToOCC(axes.Normal()))), -Precision::Infinite(), Precision::Infinite());
                shaded[k] = hit.IsDone() && hit.NbPnt() > 0;
            }
        for (size_t k = 0; k < cells.size(); ++k) {
            double length = 0.0, longest = 0.0;
            size_t across = cells.size();
            for (auto [i, forward] : cells[k]) {
                const double l = LengthOf(w[i]);
                length += l;
                if (l > longest) {
                    longest = l;
                    across = cellOf[2 * i + (forward ? 1 : 0)];
                }
            }
            if (std::abs(w.LoopArea(cells[k])) <= tol * length && across != k)
                shaded[k] = shaded[across];
        }
        return shaded;
    }

    void JoinCutPieces(Wire &w, double tol) {
        w.MergeOnCarriers();
        Wire joined(w.Axes());
        std::vector<bool> used(w.Size(), false);
        for (size_t i = 0; i < w.Size(); ++i) {
            if (used[i])
                continue;
            used[i] = true;
            const Arc *a = std::get_if<Arc>(&w[i]);
            size_t j = i + 1;
            while (a && j < w.Size() && (used[j] || !std::holds_alternative<Arc>(w[j]) || !a->CompletesCircle(std::get<Arc>(w[j]), tol)))
                ++j;
            if (!a || j == w.Size()) {
                joined.Add(w[i]);
                continue;
            }
            used[j] = true;
            Circle c(a->Center(), a->Start(), a->CCW());
            c.props = a->props;
            joined.Add(std::move(c));
        }
        w = std::move(joined);
    }

    Wire Shadow(const TopoDS_Shape &shape, const AxisSystem &axes, bool ids) {
        if (!TopExp_Explorer(shape, TopAbs_FACE).More())
            throw std::invalid_argument("Project(silhouette=True): the shape has no faces, so it casts no shadow");
        const double tol = PointTolerance(shape);
        const Wire folds = FoldLines(shape, axes, ids);
        Wire w = Project(shape, axes, ids).wire;
        for (const Edge &e : folds)
            w.Add(e);
        CutAtFoldEnds(w, folds, tol);
        ArrangeProjected(w, tol);
        const std::vector<HalfEdges> cells = ArrangementFaces(w);
        const std::vector<size_t> cellOf = SideFaces(cells, w.Size());
        const std::vector<bool> shaded = ShadedCells(w, cells, cellOf, shape, axes, tol);
        Wire outline(axes);
        for (size_t i = 0; i < w.Size(); ++i)
            if (shaded[cellOf[2 * i]] != shaded[cellOf[2 * i + 1]])
                outline.Add(w[i]);
        JoinCutPieces(outline, tol);
        return outline;
    }

}
