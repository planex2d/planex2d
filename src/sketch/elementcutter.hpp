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

#include "boolean/boolean.hpp"
#include "core/properties.hpp"
#include "core/vertex.hpp"
#include "sketch/curveeval.hpp"
#include "sketch/cut.hpp"
#include "sketch/geometry.hpp"
#include "sketch/options.hpp"
#include "wire/wire.hpp"

namespace planex2d::sketch {

    // Trim (trim): the piece of the element nearest near (reverse: the next nearest) becomes a construction element, the
    // rest stays; Split: every piece stays. The cuts are where the element meets the cutters, each cut point constrained
    // onto its cutter so the cut follows later changes; pieces of a line stay on its line, pieces of an arc or circle on
    // its circle, pieces of a spline run between positions along it
    template <class Sketch>
    class ElementCutter {
    public:
        // a: the element to cut, id as the call names it
        ElementCutter(Sketch &sk, size_t a, const std::string &id, const std::string &call)
            : sk(sk), a(a), el(sk.s.elements[a]), id(id), call(call) {}

        // anyCutter: the cutters are every other element; Trim without cutters and with nothing to cut at makes all of it
        // construction
        void Cut(const std::vector<size_t> &cutters, bool anyCutter, const Vertex &near, bool reverse, bool trim, bool tied) {
            if (el.kind == ElementKind::Spline) {
                CutSpline(cutters, anyCutter, near, reverse, trim);
                return;
            }
            const bool circle = el.kind == ElementKind::Circle;
            std::vector<CutPoint> cuts;
            const std::unordered_map<size_t, Properties> vertexProps = sk.VertexProps();
            Wire mine(sk.axes);
            sk.AddEdges(a, mine, vertexProps);
            constexpr double same = CutPoint::same;
            for (size_t c : cutters) {
                for (Vertex x : Intersections(mine, CutterWire(c, vertexProps))) {
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
                removed = NearestPiece(PieceRanges(cuts, circle, 0.0, 1.0, 1.0), near, reverse, [&](double t) { return el.At(sk.s, t - std::floor(t)); });
                cuts = BoundingCuts(cuts, removed, circle);
            }
            CutAt(cuts, removed, trim, tied);
        }

        // a line, arc or circle cut at cuts, the piece numbered removed (trim) a construction element
        CutMade CutAt(const std::vector<CutPoint> &cuts, size_t removed, bool trim, bool tied) {
            const bool line = el.kind == ElementKind::Line, circle = el.kind == ElementKind::Circle;
            const size_t pieces = circle ? cuts.size() : cuts.size() + 1;
            constexpr double same = CutPoint::same;
            size_t keep = 0;
            // the cut points, and the points along the element: start, cuts, end (a circle: the cuts round)
            std::unordered_map<size_t, size_t> pointElement;
            for (size_t e = 0; e < sk.s.elements.size(); ++e)
                if (sk.s.elements[e].kind == ElementKind::Point)
                    pointElement[sk.s.elements[e].Point()] = e;
            std::vector<size_t> cutElement;
            for (const CutPoint &c : cuts) {
                sk.AddElement(sk.s.AddPoint(sk.s.NewPoint(c.p)), "Point", {std::nullopt, {}, !sk.IsDrawn(a)});
                cutElement.push_back(sk.s.elements.size() - 1);
                pointElement[sk.s.elements.back().Point()] = cutElement.back();
            }
            std::vector<size_t> along;
            if (!circle)
                along.push_back(el.Start());
            for (size_t k : cutElement)
                along.push_back(sk.s.elements[k].Point());
            if (!circle)
                along.push_back(el.End());
            else
                along.push_back(along.front());
            while (keep == removed)
                ++keep;
            const bool spanning = (trim ? pieces - 1 : pieces) > 1;
            if (spanning)
                keep = pieces;
            // a circle's pieces count from the one at 0 degrees: the one round past 0, or the one from a cut there
            const bool rotated = circle && cuts.front().t > same;
            const std::vector<std::string> names = PieceIds(pieces, rotated, keep, removed);
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
                        sk.Relate(Kind::Coincident, {{pointElement.at(point)}, {a}});
                }
            return {cutElement, pieceElement};
        }

        // the spline element cut at cuts, the piece numbered removed (trim) a construction element
        CutMade SplineCutAt(const SplineSpan &span, const std::vector<CutPoint> &cuts, size_t removed, bool trim) {
            const SplinePiece &piece = span.piece;
            const bool round = span.round;
            const double from = span.from, to = span.to, end = span.end;
            CutMade made;
            // the pieces' bounds: (position unknown, point), none for the spline's own start and end
            struct Bound {
                size_t unknown, point;
                double t;
            };
            std::vector<Bound> bounds;
            if (!round)
                bounds.push_back({piece.from, piece.fromPoint, from});
            for (const CutPoint &c : cuts) {
                sk.AddElement(sk.s.AddPoint(sk.s.NewPoint(c.p)), "Point", {std::nullopt, {}, !sk.IsDrawn(a)});
                made.cuts.push_back(sk.s.elements.size() - 1);
                bounds.push_back({sk.s.NewValue(span.Wrap(c.t)), sk.s.elements.back().Point(), c.t});
            }
            if (round)
                bounds.push_back({bounds.front().unknown, bounds.front().point, bounds.front().t + end});
            else
                bounds.push_back({piece.to, piece.toPoint, to});
            const size_t pieces = bounds.size() - 1;
            if (!trim)
                removed = pieces;
            size_t keep = 0;
            while (keep == removed)
                ++keep;
            const bool spanning = (trim ? pieces - 1 : pieces) > 1;
            if (spanning)
                keep = pieces;
            const std::vector<std::string> names = PieceIds(pieces, false, keep, removed);
            std::vector<size_t> pieceElement(pieces);
            for (size_t i = 0; i < pieces; ++i) {
                const SplinePiece range{bounds[i].unknown, bounds[i + 1].unknown, bounds[i].point, bounds[i + 1].point};
                if (i == keep) {
                    sk.s.pieces.push_back(range);
                    sk.s.elements[a].SetPieceIndex(sk.s.pieces.size());
                    pieceElement[i] = a;
                    continue;
                }
                sk.AddElement(sk.s.AddSplinePiece(el.SplineIndex(), range), "Spline", {names[i], sk.props[a], !sk.IsDrawn(a) || i == removed});
                pieceElement[i] = sk.s.elements.size() - 1;
            }
            Pieced(pieceElement, keep, removed, false);
            if (!spanning)
                sk.PiecesCut(a, el, pieceElement, sk.elementIds[a], call);
            for (size_t i = 0; i < cuts.size(); ++i) {
                const Bound &b = bounds[round ? i : i + 1];
                const size_t cut = std::find_if(sk.s.elements.begin(), sk.s.elements.end(), [&](const Element &x) {
                                       return x.kind == ElementKind::Point && x.Point() == b.point;
                                   }) - sk.s.elements.begin();
                sk.Relate(Kind::Coincident, {{cut}, {a}}, {b.unknown});
                if (cuts[i].by != none)
                    sk.Relate(Kind::Coincident, {{cut}, OnCutter(cuts[i])});
            }
            made.pieces = pieceElement;
            return made;
        }

    private:
        // what a cut point is held on: its cutter, or the cutter's end where it lies (one point with it then: at a tangent
        // contact a point held on the line could slide along it to about sqrt(eps) after a solve)
        Ref OnCutter(const CutPoint &c) const {
            const Element &k = sk.s.elements[c.by];
            if (k.kind == ElementKind::Line || k.kind == ElementKind::Arc) {
                const double near = 1e-9 * std::max(sk.s.Size(), 1.0);
                for (const Part end : {Part::Start, Part::End})
                    if (Norm(sk.Coords(end == Part::Start ? k.Start() : k.End()) - c.p) <= near)
                        return {c.by, end};
            }
            return {c.by};
        }

        Sketch &sk;
        const size_t a;
        const Element el;
        const std::string &id, &call;

        // Trim / Split of a spline: the spline stays as it is, its pieces between the cuts are elements that run between
        // positions along it; each cut is a point on the spline (its position the unknown both pieces there end at) and
        // on its cutter
        void CutSpline(const std::vector<size_t> &cutters, bool anyCutter, const Vertex &near, bool reverse, bool trim) {
            const SplineSpan span = el.Span(sk.s);
            const bool round = span.round;
            const double from = span.from, to = span.to, end = span.end, same = 1e-9 * end;
            std::vector<CutPoint> cuts;
            const std::unordered_map<size_t, Properties> vertexProps = sk.VertexProps();
            Wire mine(sk.axes);
            sk.AddEdges(a, mine, vertexProps);
            for (size_t c : cutters)
                for (const Vertex &x : Intersections(mine, CutterWire(c, vertexProps)))
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
                removed = NearestPiece(PieceRanges(cuts, round, from, to, end), near, reverse, [&](double u) { return span.At(u, 0); });
                cuts = BoundingCuts(cuts, removed, round);
            }
            SplineCutAt(span, cuts, removed, trim);
        }

        // an element as edges to cut with: an axis as a long line
        Wire CutterWire(size_t c, const std::unordered_map<size_t, Properties> &vertexProps) const {
            Wire theirs(sk.axes);
            if (sk.s.elements[c].kind == ElementKind::Axis) {
                const double reach = 10.0 * std::max(1.0, sk.s.Size());
                const auto [o, u] = sk.s.elements[c].Carrier(sk.s);
                theirs.Add(planex2d::Line(o - u * reach, o + u * reach));
            } else
                sk.AddEdges(c, theirs, vertexProps);
            return theirs;
        }
        // where the spline or offset curve c meets the line, arc or circle cut, from x near it: Newton steps along the
        // curve on the distance from the cut element's carrier, from the foot of x on its spline
        Vertex OnSpline(size_t c, const Vertex &x) const {
            const CurveEval g = sk.s.elements[c].Evaluator(sk.s);
            double w = g.f.Project(x);
            const bool line = el.kind == ElementKind::Line;
            const Vertex o = sk.Coords(line ? el.Start() : el.Center()), u = line ? sk.Coords(el.End()) - o : Vertex{};
            for (int i = 0; i < 30; ++i) {
                const Vertex p = g.At(w, 0), d = g.At(w, 1);
                const double dist = Norm(p - o), value = line ? Cross(u, p - o) : dist - sk.s.values[el.Radius()];
                const double slope = line ? Cross(u, d) : Dot(p - o, d) / dist;
                if (slope == 0.0)
                    break;
                w -= value / slope;
                if (std::abs(value / slope) <= 1e-15 * g.f.params.back())
                    break;
            }
            return g.At(w, 0);
        }
        // the position where the spline meets the cutter c exactly, from one near it (the intersection of the edges,
        // within their approximation): Newton steps on the distance from the cutter, for a spline cutter on both positions
        double Meet(const SplineSpan &span, double t, size_t c) const {
            const Element &k = sk.s.elements[c];
            const bool flat = k.kind == ElementKind::Line || k.kind == ElementKind::Axis;
            Vertex o, u;
            if (k.kind == ElementKind::Line) {
                o = sk.Coords(k.Start());
                u = sk.Coords(k.End()) - o;
            } else if (k.kind == ElementKind::Axis) {
                const double angle = sk.s.values[k.AxisAngle()];
                o = Vertex{std::cos(angle), std::sin(angle)} * sk.s.values[k.AxisDistance()];
                u = {-std::sin(angle), std::cos(angle)};
            } else if (k.kind == ElementKind::Circle || k.kind == ElementKind::Arc)
                o = sk.Coords(k.Center());
            std::optional<CurveEval> g;
            double w = 0.0;
            if (k.kind == ElementKind::Spline || k.kind == ElementKind::OffsetCurve) {
                g = k.Evaluator(sk.s);
                const double gend = g->f.params.back();
                const size_t n = 64 * g->f.params.size();
                for (size_t i = 0; i <= n; ++i)
                    if (const double v = gend * (double)i / (double)n; Norm(g->At(v, 0) - span.At(t, 0)) < Norm(g->At(w, 0) - span.At(t, 0)))
                        w = v;
            }
            for (int i = 0; i < 30; ++i) {
                const Vertex p = span.At(t, 0), d = span.At(t, 1);
                double step;
                if (g) {
                    const Vertex q = g->At(w, 0), e = g->At(w, 1), r = p - q;
                    const double det = Cross(d, e);
                    if (det == 0.0)
                        break;
                    step = Cross(r, e) / det;
                    w += Cross(d, r) / det;
                } else if (flat) {
                    if (Cross(u, d) == 0.0)
                        break;
                    step = Cross(u, p - o) / Cross(u, d);
                } else {
                    const double dist = Norm(p - o), slope = Dot(p - o, d) / dist;
                    if (slope == 0.0)
                        break;
                    step = (dist - sk.s.values[k.Radius()]) / slope;
                }
                t -= step;
                if (std::abs(step) <= 1e-15 * span.end)
                    break;
            }
            return t;
        }

        // the ids of the pieces, by piece: the one kept keeps the element's id (keep, a trim leaving one drawn piece), the
        // trimmed one id.trimmed.k, the others id.1, id.2, ... along it (a circle's from the piece at 0 degrees,
        // counterclockwise)
        std::vector<std::string> PieceIds(size_t pieces, bool round, size_t keep, size_t removed) const {
            const std::string &own = sk.elementIds[a];
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
                if (const std::string t = sk.elementIds[a] + ".trimmed." + std::to_string(k); !sk.byId.contains(t) && !sk.wires.contains(t))
                    return t;
        }
        // the pieces in the wires: pieces[keep] is the element, or with keep none (pieces.size()) it stays as their span:
        // hidden, the carrier and the ends of all of them, and its id the wire of the pieces in their order; the other
        // wires it was in take the drawn pieces in its place (the trimmed one, removed, is drawn no more)
        void Pieced(const std::vector<size_t> &pieces, size_t keep, size_t removed, bool round) {
            sk.BlendsFollow(a, pieces);
            const bool span = keep == pieces.size();
            for (auto &[name, ids] : sk.wires) {
                const auto at = std::find(ids.begin(), ids.end(), sk.elementIds[a]);
                if (at == ids.end())
                    continue;
                std::vector<std::string> before, after;
                for (size_t i = 0; i < pieces.size(); ++i)
                    if (i != keep && i != removed)
                        (span || i < keep ? before : after).push_back(sk.elementIds[pieces[i]]);
                const auto next = ids.insert(at, before.begin(), before.end()) + (std::ptrdiff_t)before.size();
                if (span)
                    ids.erase(next);
                else
                    ids.insert(next + 1, after.begin(), after.end());
            }
            if (!span)
                return;
            sk.roles[a] = Role::Span;
            std::vector<std::string> &w = sk.wires[sk.elementIds[a]];
            for (size_t j = 0; j < pieces.size(); ++j)
                w.push_back(sk.elementIds[pieces[round ? (j + pieces.size() - 1) % pieces.size() : j]]);
            sk.wireLog.push_back({sk.elementIds[a], std::string()});
        }
    };

}
