// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "planex/boolean/boolean.hpp"
#include "planex/core/properties.hpp"
#include "planex/core/vertex.hpp"
#include "planex/sketch/curveeval.hpp"
#include "planex/sketch/cut.hpp"
#include "planex/sketch/geometry.hpp"
#include "planex/sketch/options.hpp"
#include "planex/wire/wire.hpp"

namespace planex2d::sketch {

    template <class Sketch>
    class ElementCutter {
    public:
        ElementCutter(Sketch &sk, size_t a, const std::string &id, const std::string &call)
            : sk(sk), a(a), el(sk.s.elements[a]), id(id), call(call) {}

        void Cut(const std::vector<size_t> &cutters, bool anyCutter, const Vertex &nearPoint, bool reverse, bool trim, bool tied) {
            if (el.kind == ElementKind::Spline || el.kind == ElementKind::OffsetCurve) {
                CutSpline(cutters, anyCutter, nearPoint, reverse, trim);
                return;
            }
            const bool circle = el.kind == ElementKind::Circle;
            std::vector<CutPoint> cuts;
            const std::unordered_map<size_t, Properties> vertexProps = sk.VertexProps();
            Wire mine(sk.axes);
            sk.AddEdges(a, mine, vertexProps);
            constexpr double same = CutPoint::same;
            for (size_t c : cutters) {
                for (Vertex x : Intersections(mine, sk.AsEdges(c, vertexProps))) {
                    if (sk.s.elements[c].kind == ElementKind::Spline || sk.s.elements[c].kind == ElementKind::OffsetCurve)
                        x = OnSpline(c, x);
                    const double t = el.Param(sk.s, x);
                    if (circle || (t > same && t < 1.0 - same))
                        cuts.push_back({t, x, c});
                }
            }
            SortCuts(cuts, same);
            if (cuts.empty()) {
                if (trim && anyCutter) {
                    sk.roles[a] = Role::Construction;
                    return;
                }
                throw std::invalid_argument(call + (anyCutter ? ": it meets nothing" : ": it does not meet the cutter"));
            }
            if (circle && cuts.size() < 2)
                throw std::invalid_argument(call + ": a circle needs two cuts");
            size_t removed = circle ? cuts.size() : cuts.size() + 1;
            if (trim) {
                removed = NearestPiece(PieceRanges(cuts, circle, 0.0, 1.0, 1.0), nearPoint, reverse, [&](double t) { return el.At(sk.s, t - std::floor(t)); });
                cuts = BoundingCuts(cuts, removed, circle);
            }
            CutAt(cuts, removed, trim, tied);
        }

        CutMade CutAt(const std::vector<CutPoint> &cuts, size_t removed, bool trim, bool tied) {
            const bool line = el.kind == ElementKind::Line, circle = el.kind == ElementKind::Circle;
            const size_t pieces = circle ? cuts.size() : cuts.size() + 1;
            constexpr double same = CutPoint::same;
            const std::vector<size_t> cutElement = CutPointElements(cuts), along = PointsAlong(cutElement);
            size_t keep = 0;
            while (keep == removed)
                ++keep;
            const bool spanning = (trim ? pieces - 1 : pieces) > 1;
            if (spanning)
                keep = pieces;
            // a circle's pieces count from the one at 0 degrees: the one round past 0, or the one from a cut there
            const bool rotated = circle && cuts.front().t > same;
            const std::vector<std::string> names = PieceIds(pieces, rotated, keep, removed);
            const std::vector<size_t> pieceElement = PieceElements(along, names, keep, removed, tied);
            Pieced(pieceElement, keep, removed, rotated);
            if (!spanning)
                sk.PiecesCut(a, el, pieceElement, id, call);
            for (size_t i = 0; i < cuts.size(); ++i)
                if (cuts[i].by != none)
                    sk.Relate(Kind::Coincident, {{cutElement[i]}, OnCutter(cuts[i])});
            if (line && tied)
                for (size_t point : along) {
                    const Element &e = sk.s.elements[a];
                    if (point != e.Start() && point != e.End())
                        sk.Relate(Kind::Coincident, {{sk.pointElementOf.at(point)}, {a}});
                }
            return {cutElement, pieceElement};
        }

        CutMade SplineCutAt(const SplineSpan &span, const std::vector<CutPoint> &cuts, size_t removed, bool trim) {
            const bool round = span.round;
            CutMade made;
            const std::vector<Bound> bounds = CutBounds(span, cuts, made.cuts);
            const size_t pieces = bounds.size() - 1;
            if (!trim)
                removed = pieces;
            size_t keep = 0;
            while (keep == removed)
                ++keep;
            const bool spanning = (trim ? pieces - 1 : pieces) > 1;
            if (spanning)
                keep = pieces;
            // a reversed offset curve runs against its spline: its pieces are named and chained in its own direction
            const bool against = el.kind == ElementKind::OffsetCurve && sk.s.offsetCurves[el.OffsetCurveIndex()].reversed;
            const auto along = [&](size_t i) { return against && i < pieces ? pieces - 1 - i : i; };
            const std::vector<std::string> names = PieceIds(pieces, false, along(keep), along(removed));
            std::vector<size_t> pieceElement(pieces), ordered(pieces);
            for (size_t i = 0; i < pieces; ++i) {
                const SplinePiece range{bounds[i].unknown, bounds[i + 1].unknown, bounds[i].point, bounds[i + 1].point};
                if (i == keep) {
                    KeepPiece(range);
                    pieceElement[i] = a;
                } else {
                    sk.AddElement(PieceOf(range), "Spline", {names[along(i)], sk.props[a], !sk.IsDrawn(a) || i == removed});
                    pieceElement[i] = sk.s.elements.size() - 1;
                }
                ordered[along(i)] = pieceElement[i];
            }
            Pieced(ordered, along(keep), along(removed), false);
            if (!spanning)
                sk.PiecesCut(a, el, pieceElement, sk.book.elementIds[a], call);
            for (size_t i = 0; i < cuts.size(); ++i) {
                const Bound &b = bounds[round ? i : i + 1];
                const size_t cut = sk.pointElementOf.at(b.point);
                sk.Relate(Kind::Coincident, {{cut}, {a}}, {b.unknown});
                if (cuts[i].by != none)
                    sk.Relate(Kind::Coincident, {{cut}, OnCutter(cuts[i])});
            }
            made.pieces = pieceElement;
            return made;
        }

    private:
        Sketch &sk;
        const size_t a;
        const Element el;
        const std::string &id, &call;

        // what a cut point is held on: its cutter, or the cutter's end where it lies (one point with it then: at a tangent
        // contact a point held on the line could slide along it to about sqrt(eps) after a solve)
        Ref OnCutter(const CutPoint &c) const {
            const Element &k = sk.s.elements[c.by];
            if (k.kind == ElementKind::Line || k.kind == ElementKind::Arc) {
                const double nearTol = 1e-9 * std::max(sk.s.Size(), 1.0);
                for (const Part end : {Part::Start, Part::End})
                    if (Norm(sk.Coords(end == Part::Start ? k.Start() : k.End()) - c.p) <= nearTol)
                        return {c.by, end};
            }
            return {c.by};
        }

        std::vector<size_t> CutPointElements(const std::vector<CutPoint> &cuts) {
            std::vector<size_t> cutElement;
            for (const CutPoint &c : cuts) {
                sk.AddElement(sk.s.AddPoint(sk.s.NewPoint(c.p)), "Point", {std::nullopt, {}, !sk.IsDrawn(a)});
                cutElement.push_back(sk.s.elements.size() - 1);
            }
            return cutElement;
        }
        std::vector<size_t> PointsAlong(const std::vector<size_t> &cutElement) const {
            const bool circle = el.kind == ElementKind::Circle;
            std::vector<size_t> along;
            if (!circle)
                along.push_back(el.Start());
            for (size_t k : cutElement)
                along.push_back(sk.s.elements[k].Point());
            if (!circle)
                along.push_back(el.End());
            else
                along.push_back(along.front());
            return along;
        }
        std::vector<size_t> PieceElements(const std::vector<size_t> &along, const std::vector<std::string> &names, size_t keep, size_t removed, bool tied) {
            const bool line = el.kind == ElementKind::Line, circle = el.kind == ElementKind::Circle;
            const size_t pieces = names.size();
            std::vector<size_t> pieceElement(pieces);
            for (size_t i = 0; i < pieces; ++i) {
                const size_t p = along[i], q = along[i + 1];
                if (i == keep) {
                    Element &e = sk.s.elements[a];
                    if (!line) {
                        e.kind = ElementKind::Arc;
                        e.ccw = circle || el.ccw;
                    }
                    e.SetEnds(p, q);
                    pieceElement[i] = a;
                    continue;
                }
                const Options opt{names[i], sk.props[a], !sk.IsDrawn(a) || i == removed};
                if (line)
                    sk.AddElement(sk.s.AddLine(p, q), "Line", opt);
                else if (tied)
                    sk.AddElement(sk.s.AddArcWithRadius(el.Center(), el.Radius(), p, q, circle || el.ccw), "Arc", opt);
                else
                    sk.AddElement(sk.s.AddArc(sk.s.NewPoint(sk.Coords(el.Center())), sk.s.values[el.Radius()], p, q, circle || el.ccw), "Arc", opt);
                pieceElement[i] = sk.s.elements.size() - 1;
            }
            return pieceElement;
        }
        // unknown: none for the spline's own start and end
        struct Bound {
            size_t unknown, point;
            double t;
        };
        std::vector<Bound> CutBounds(const SplineSpan &span, const std::vector<CutPoint> &cuts, std::vector<size_t> &cutElements) {
            const SplinePiece &piece = span.piece;
            const bool round = span.round;
            const double from = span.from, to = span.to, end = span.end;
            std::vector<Bound> bounds;
            if (!round)
                bounds.push_back({piece.from, piece.fromPoint, from});
            for (const CutPoint &c : cuts) {
                sk.AddElement(sk.s.AddPoint(sk.s.NewPoint(c.p)), "Point", {std::nullopt, {}, !sk.IsDrawn(a)});
                cutElements.push_back(sk.s.elements.size() - 1);
                bounds.push_back({sk.s.NewValue(span.Wrap(c.t)), sk.s.elements.back().Point(), c.t});
            }
            if (round)
                bounds.push_back({bounds.front().unknown, bounds.front().point, bounds.front().t + end});
            else
                bounds.push_back({piece.to, piece.toPoint, to});
            return bounds;
        }

        // element a keeps the piece range of its spline or offset curve, as new data: the element as it was (el) keeps its
        // ends for the constraints on them to follow
        void KeepPiece(const SplinePiece &range) {
            if (el.kind == ElementKind::OffsetCurve) {
                sk.s.offsetCurves.push_back(CurveOver(range));
                sk.s.elements[a].SetOffsetCurveIndex(sk.s.offsetCurves.size() - 1);
                return;
            }
            sk.s.pieces.push_back(range);
            sk.s.elements[a].SetPieceIndex(sk.s.pieces.size());
        }
        size_t PieceOf(const SplinePiece &range) {
            if (el.kind != ElementKind::OffsetCurve)
                return sk.s.AddSplinePiece(el.SplineIndex(), range);
            return sk.s.AddOffsetCurve(CurveOver(range));
        }
        OffsetCurveData CurveOver(const SplinePiece &range) const {
            OffsetCurveData c = sk.s.offsetCurves[el.OffsetCurveIndex()];
            c.range = range;
            return c;
        }

        void CutSpline(const std::vector<size_t> &cutters, bool anyCutter, const Vertex &nearPoint, bool reverse, bool trim) {
            const SplineSpan span = el.Span(sk.s);
            const bool round = span.round;
            const double from = span.from, to = span.to, end = span.end, same = samePlace * end;
            std::vector<CutPoint> cuts;
            const std::unordered_map<size_t, Properties> vertexProps = sk.VertexProps();
            Wire mine(sk.axes);
            sk.AddEdges(a, mine, vertexProps);
            for (size_t c : cutters)
                for (const Vertex &x : Intersections(mine, sk.AsEdges(c, vertexProps)))
                    if (const double t = Meet(span, span.Along(x), c); round || (t > from + same && t < to - same))
                        cuts.push_back({t, span.At(t, 0), c});
            SortCuts(cuts, same);
            if (round && cuts.size() > 1 && cuts.back().t - cuts.front().t >= end - same)
                cuts.pop_back();
            if (cuts.empty()) {
                if (trim && anyCutter) {
                    sk.roles[a] = Role::Construction;
                    return;
                }
                throw std::invalid_argument(call + (anyCutter ? ": it meets nothing" : ": it does not meet the cutter"));
            }
            if (round && cuts.size() < 2)
                throw std::invalid_argument(call + ": a closed spline needs two cuts");
            size_t removed = 0;
            if (trim) {
                removed = NearestPiece(PieceRanges(cuts, round, from, to, end), nearPoint, reverse, [&](double u) { return span.At(u, 0); });
                cuts = BoundingCuts(cuts, removed, round);
            }
            SplineCutAt(span, cuts, removed, trim);
        }

        Vertex OnSpline(size_t c, const Vertex &x) const {
            const CurveEval g = sk.s.elements[c].Evaluator(sk.s);
            const double w = g.f.Project(x);
            if (el.kind == ElementKind::Line) {
                const Vertex o = sk.Coords(el.Start());
                return g.At(g.MeetLine(w, o, sk.Coords(el.End()) - o), 0);
            }
            return g.At(g.MeetCircle(w, sk.Coords(el.Center()), sk.s.values[el.Radius()]), 0);
        }
        // t: an intersection of the edges, which is only within the splines' approximation
        double Meet(const SplineSpan &span, double t, size_t c) const {
            const Element &k = sk.s.elements[c];
            if (k.kind == ElementKind::Line) {
                const Vertex o = sk.Coords(k.Start());
                return span.MeetLine(t, o, sk.Coords(k.End()) - o);
            }
            if (k.kind == ElementKind::Axis) {
                const auto [o, u] = k.Carrier(sk.s);
                return span.MeetLine(t, o, u);
            }
            if (k.IsRound())
                return span.MeetCircle(t, sk.Coords(k.Center()), sk.s.values[k.Radius()]);
            return span.MeetCurve(t, k.Evaluator(sk.s));
        }

        // the ids of the pieces, by piece: the one kept keeps the element's id (keep, a trim leaving one drawn piece), the
        // trimmed one id.trimmed.k, the others id.1, id.2, ... along it (a circle's from the piece at 0 degrees,
        // counterclockwise)
        std::vector<std::string> PieceIds(size_t pieces, bool round, size_t keep, size_t removed) const {
            const std::string &own = sk.book.elementIds[a];
            std::vector<std::string> out(pieces);
            size_t n = 0;
            for (size_t j = 0; j < pieces; ++j) {
                const size_t i = round ? (j + pieces - 1) % pieces : j;
                out[i] = i == keep ? own : i == removed ? TrimmedId() : own + "." + std::to_string(++n);
            }
            return out;
        }
        std::string TrimmedId() const {
            for (size_t k = 1;; ++k)
                if (const std::string t = sk.book.elementIds[a] + ".trimmed." + std::to_string(k); !sk.book.byId.contains(t) && !sk.book.wires.contains(t))
                    return t;
        }
        // the pieces in the wires: pieces[keep] is the element, or with keep none (pieces.size()) it stays as their span:
        // hidden, the carrier and the ends of all of them, and its id the wire of the pieces in their order; the other
        // wires it was in take the drawn pieces in its place (the trimmed one, removed, is drawn no more)
        void Pieced(const std::vector<size_t> &pieces, size_t keep, size_t removed, bool round) {
            sk.s.BlendsFollow(a, pieces);
            const bool span = keep == pieces.size();
            for (auto &[name, ids] : sk.book.wires) {
                const auto at = std::find(ids.begin(), ids.end(), sk.book.elementIds[a]);
                if (at == ids.end())
                    continue;
                std::vector<std::string> before, after;
                for (size_t i = 0; i < pieces.size(); ++i)
                    if (i != keep && i != removed)
                        (span || i < keep ? before : after).push_back(sk.book.elementIds[pieces[i]]);
                const auto next = ids.insert(at, before.begin(), before.end()) + (std::ptrdiff_t)before.size();
                if (span)
                    ids.erase(next);
                else
                    ids.insert(next + 1, after.begin(), after.end());
            }
            if (!span)
                return;
            sk.roles[a] = Role::Span;
            std::vector<std::string> &w = sk.book.wires[sk.book.elementIds[a]];
            for (size_t j = 0; j < pieces.size(); ++j)
                w.push_back(sk.book.elementIds[pieces[round ? (j + pieces.size() - 1) % pieces.size() : j]]);
            sk.book.wireLog.push_back({sk.book.elementIds[a], std::string()});
        }
    };

}
