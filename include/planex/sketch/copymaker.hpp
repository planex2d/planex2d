// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "planex/core/vertex.hpp"
#include "planex/curves/interpolate.hpp"
#include "planex/sketch/copies.hpp"
#include "planex/sketch/geometry.hpp"
#include "planex/sketch/guards.hpp"
#include "planex/sketch/options.hpp"

namespace planex2d::sketch {

    // f: where a point of the originals goes; flip: f turns the plane over (a mirror)
    template <class Sketch, class F>
    class CopyMaker {
    public:
        CopyMaker(Sketch &sk, F f, bool flip, const std::string &prefix) : sk(sk), f(f), flip(flip), prefix(prefix) {}

        Copies Make(const std::vector<size_t> &originals) {
            Scoped scope{sk.book.idScope, std::exchange(sk.book.idScope, prefix)};
            std::set<size_t> centres, splines;
            for (size_t e : originals) {
                if (sk.s.elements[e].IsRound())
                    centres.insert(sk.s.elements[e].Center());
                if (sk.s.elements[e].kind == ElementKind::Spline)
                    splines.insert(sk.s.elements[e].SplineIndex());
            }
            for (size_t e : originals) {
                const Element el = sk.s.elements[e];
                // a copied b-spline makes its own legs
                if (splines.contains(sk.s.LegOf(el)))
                    continue;
                const Options opt{CopyId(e), sk.props[e], !sk.IsDrawn(e)};
                if (el.kind == ElementKind::Point) {
                    if (!centres.contains(el.Point()))
                        CopyPoint(el.Point());
                    continue;
                }
                if (el.kind == ElementKind::Spline) {
                    CopySpline(e, opt);
                    continue;
                }
                if (el.kind == ElementKind::Line) {
                    const size_t a = CopyPoint(el.Start()), b = CopyPoint(el.End());
                    sk.AddElement(sk.s.AddLine(a, b), "Line", opt);
                } else
                    CopyRound(e, opt);
                made.elements.push_back({e, sk.book.elementIds.size() - 1});
            }
            BlendCopiesJoinCopies();
            TiePositions();
            std::erase_if(sk.book.givenIds, [&](const std::string &g) { return g.starts_with(prefix + "."); });
            return made;
        }

    private:
        Sketch &sk;
        const F f;
        const bool flip;
        const std::string &prefix;
        Copies made;
        std::unordered_map<size_t, size_t> copiedCentres;

        std::optional<std::string> CopyId(size_t e) const { return prefix + "." + sk.book.elementIds[e]; }
        size_t CopyPoint(size_t point) {
            const size_t pe = sk.pointElementOf.at(point);
            for (const auto &[o, c] : made.points)
                if (o == pe)
                    return sk.s.elements[c].Point();
            sk.AddElement(sk.s.AddPoint(sk.s.NewPoint(f(sk.Coords(point)))), "Point", {CopyId(pe), sk.props[pe], !sk.IsDrawn(pe)});
            made.points.push_back({pe, sk.book.elementIds.size() - 1});
            return sk.s.elements.back().Point();
        }
        void TiePositions() {
            for (const Copies::Position &x : made.positions) {
                size_t point = none;
                for (const SplinePiece &piece : sk.s.pieces)
                    for (const auto &[u, p] : {std::pair{piece.from, piece.fromPoint}, std::pair{piece.to, piece.toPoint}})
                        if (u == x.copy)
                            point = p;
                sk.Relate(Kind::Coincident, {{sk.pointElementOf.at(point)}, {x.copyElement}}, {x.copy});
            }
        }
        void BlendCopiesJoinCopies() {
            const auto copyOf = [&](size_t e) -> std::optional<size_t> {
                for (const auto &[o, c] : made.elements)
                    if (o == e)
                        return c;
                return std::nullopt;
            };
            for (const auto &[original, copy] : made.elements) {
                if (!sk.s.elements[copy].IsBlend(sk.s))
                    continue;
                BlendShape &b = *sk.s.splines[sk.s.elements[copy].SplineIndex()].blend;
                for (size_t &arm : b.arms) {
                    const std::optional<size_t> c = copyOf(arm);
                    if (!c)
                        throw std::invalid_argument(prefix + ": '" + sk.book.elementIds[original] + "' is computed from '" + sk.book.elementIds[arm] +
                                                    "'; copy that with it");
                    arm = *c;
                }
                if (b.helper == none)
                    continue;
                b.helper = *copyOf(b.helper);
                sk.roles[b.helper] = Role::Helper;
                sk.roles[sk.pointElementOf.at(sk.s.elements[b.helper].Center())] = Role::Helper;
            }
        }
        void CopySpline(size_t e, const Options &opt) {
            const Element el = sk.s.elements[e];
            const auto copied = std::find_if(made.splines.begin(), made.splines.end(),
                                             [&](const auto &x) { return sk.s.elements[x.first].SplineIndex() == el.SplineIndex(); });
            const bool fresh = copied == made.splines.end();
            const SplinePiece *piece = el.Piece(sk.s);
            const size_t copy = fresh ? CopyWholeSpline(e, opt) : copied->second;
            size_t element = copy;
            if (piece) {
                const auto position = [&](size_t u, size_t point) {
                    if (u == none)
                        return std::pair{u, point};
                    made.placed.insert(sk.pointElementOf.at(point));
                    const size_t at = CopyPoint(point);
                    for (const Copies::Position &x : made.positions)
                        if (x.original == u)
                            return std::pair{x.copy, at};
                    made.positions.push_back({u, sk.s.NewValue(sk.s.values[u]), e, copy});
                    return std::pair{made.positions.back().copy, at};
                };
                const auto [from, fromPoint] = position(piece->from, piece->fromPoint);
                const auto [to, toPoint] = position(piece->to, piece->toPoint);
                const SplinePiece range{from, to, fromPoint, toPoint};
                if (fresh) {
                    sk.s.pieces.push_back(range);
                    sk.s.elements[copy].SetPieceIndex(sk.s.pieces.size());
                } else {
                    sk.AddElement(sk.s.AddSplinePiece(sk.s.elements[copy].SplineIndex(), range), "Spline", opt);
                    element = sk.s.elements.size() - 1;
                }
            }
            made.elements.push_back({e, element});
        }
        size_t CopyWholeSpline(size_t e, const Options &opt) {
            const Element el = sk.s.elements[e];
            if (el.IsFitted(sk.s))
                return CopyFitted(e, opt);
            const SplineData d = sk.s.splines[el.SplineIndex()];
            if (el.IsBlend(sk.s)) {
                // joining the originals' elements until CopyElements gives it their copies
                sk.AddElement(sk.s.AddBlend(CopyPoint(d.points[0]), CopyPoint(d.points[1]), *d.blend), "Spline", opt);
                made.splines.push_back({e, sk.s.elements.size() - 1});
                return sk.s.elements.size() - 1;
            }
            std::vector<size_t> points;
            for (size_t p : d.points)
                points.push_back(CopyPoint(p));
            std::vector<double> angles;
            std::map<size_t, TangentSetting<double>> settings;
            for (size_t t = 0; t < d.tangent.size(); ++t) {
                const Vertex p = sk.Coords(d.points[d.tangentAt[t]]), u{std::cos(sk.s.values[d.tangent[t]]), std::sin(sk.s.values[d.tangent[t]])};
                const Vertex w = f(p + u) - f(p);
                angles.push_back(std::atan2(w.y, w.x));
                const size_t k = d.tangentAt[t], c = d.CurvatureAt(k);
                settings[k] = {d.TensionAt(k), c == none ? std::nullopt : std::optional<double>(flip ? -sk.s.values[c] : sk.s.values[c])};
            }
            size_t copy;
            if (d.poled) {
                sk.AddElement(sk.s.AddPoledSpline(points, d.poled->knots, d.poled->degree, d.closed), "BSpline", opt);
                copy = sk.s.elements.size() - 1;
                sk.AddLegs(copy);
            } else {
                sk.AddElement(sk.s.AddSpline(points, d.tangentAt, angles, d.closed, d.style, settings), "Spline", opt);
                copy = sk.s.elements.size() - 1;
            }
            made.splines.push_back({e, copy});
            return copy;
        }
        size_t CopyFitted(size_t e, const Options &opt) {
            const SplineData d = sk.s.splines[sk.s.elements[e].SplineIndex()];
            const FittedShape &source = *d.fitted;
            std::vector<size_t> ends;
            for (size_t p : d.points) {
                made.placed.insert(sk.pointElementOf.at(p));
                ends.push_back(CopyPoint(p));
            }
            const Vertex o = sk.Coords(source.origin), w = f(o + Vertex{std::cos(sk.s.values[source.angle]), std::sin(sk.s.values[source.angle])}) - f(o);
            FittedShape copy{source.shape};
            copy.mirrored = source.mirrored != flip;
            copy.scale = source.scale;
            copy.copyOf = sk.s.elements[e].SplineIndex();
            sk.AddElement(sk.s.AddFittedSpline(ends.front(), ends.back(), std::move(copy), f(o), std::atan2(w.y, w.x)), "Curve", opt);
            made.splines.push_back({e, sk.s.elements.size() - 1});
            return sk.s.elements.size() - 1;
        }
        void CopyRound(size_t e, const Options &opt) {
            const Element el = sk.s.elements[e];
            const size_t origin = el.kind == ElementKind::Circle && el.OriginPoint() != none ? CopyPoint(el.OriginPoint()) : none;
            const auto shared = copiedCentres.find(el.Center());
            const size_t centre = shared != copiedCentres.end() ? shared->second : sk.s.NewPoint(f(sk.Coords(el.Center())));
            const size_t before = sk.book.elementIds.size();
            const size_t round = el.kind == ElementKind::Arc
                                     ? sk.AddElement(sk.s.AddArc(centre, sk.s.values[el.Radius()], CopyPoint(el.Start()), CopyPoint(el.End()), flip ? !el.ccw : el.ccw), "Arc", opt)
                                     : sk.AddElement(sk.s.AddCircle(centre, sk.s.values[el.Radius()], OriginAngleCopied(el), flip ? !el.ccw : el.ccw, origin), "Circle", opt);
            if (shared != copiedCentres.end())
                return;
            copiedCentres[el.Center()] = centre;
            if (round > before && sk.pointElementOf.contains(el.Center()))
                sk.book.Rename(round - 1, prefix + "." + sk.book.elementIds[sk.pointElementOf.at(el.Center())]);
        }
        double OriginAngleCopied(const Element &c) const {
            const double a = sk.s.values[c.OriginAngle()];
            const Vertex centre = sk.Coords(c.Center()), w = f(centre + Vertex{std::cos(a), std::sin(a)}) - f(centre);
            return std::atan2(w.y, w.x);
        }
    };

}
