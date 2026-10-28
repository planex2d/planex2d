// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "core/config.hpp"
#include "core/properties.hpp"
#include "core/vertex.hpp"
#include "curves/edgelist.hpp"
#include "offset/offsetpieces.hpp"
#include "sketch/curveeval.hpp"
#include "sketch/geometry.hpp"
#include "sketch/guards.hpp"
#include "sketch/offsetsides.hpp"
#include "sketch/options.hpp"

namespace planex2d::sketch {

    // the offset's elements (constraints on: also its element with the distance, the dimension and the ties). Every
    // point the offset runs through gets two rows, its arcs share the centres of the source's arcs and corners and get
    // their radii at their first ends: an offset point after a line (or at the start of a side) p = q + d n (q its
    // source point, n the normal there), after an arc on the normal through q (the arc holds it on its circle); where two
    // pieces cross, the point on each one's offset line or circle (the arc before it holds it on its own). A loop of arcs
    // only, all touching, would take its radii round in a circle: its widest arc gets a centre of its own, on the bisector
    // of its chord where the source's centre is
    template <class Sketch>
    class OffsetMaker {
    public:
        OffsetMaker(Sketch &sk, const OffsetChain &source, double d, const std::optional<Vertex> &point, const std::optional<bool> &bothSides,
                    const Options &opt, const std::optional<bool> &constructionGiven, const std::string &call, const std::string &slot,
                    std::optional<double> hole, const OffsetForms &forms)
            : sk(sk), source(source), d(d), point(point), opt(opt), constructionGiven(constructionGiven), call(call), slot(slot), hole(hole), forms(forms),
              n(source.elements.size()), tied(sk.TiesOffsets()), ad(std::abs(d)), circle(sk.s.elements[Element(0)].kind == ElementKind::Circle),
              edges(sk.SourceEdges(source)), tol(Tol(OffsetScale(edges, ad))), both(!point && bothSides.value_or(!source.closed)) {}

        void Make() {
            Sides();
            Labels();
            Name();
            Scoped scope{sk.idScope, sk.idScope};
            if (circle)
                CircleSides();
            if (sk.ClosedSpline(source))
                ClosedSplineSides();
            for (const auto &[fixed, loops] : sides)
                for (const OffsetLoop &loop : loops)
                    Loop(fixed, loop);
            SideWires();
            if (tied)
                sk.OffsetMade(name, made, tieIds, source, edges, ad, towards, forms, asked, curveIds, call);
            else
                sk.NameWire(name, drawn, call + ": ");
        }

    private:
        Sketch &sk;
        const OffsetChain &source;
        const double d;
        const std::optional<Vertex> point;
        const Options &opt;
        const std::optional<bool> constructionGiven;
        const std::string &call, &slot;
        const std::optional<double> hole;
        const OffsetForms forms;

        const size_t n;
        const bool tied;
        const double ad;
        const bool circle;
        const EdgeList edges;
        const double tol;
        const bool both;

        std::vector<int> towards;
        // what the offset is made as, for the checks later; a keyhole's outline is drawn with its hole opened
        OffsetSides sides, asked;
        std::set<std::string> labels;
        std::optional<size_t> param;
        std::string name;
        std::vector<std::string> made, tieIds;
        std::vector<std::pair<std::string, std::string>> curveIds;
        std::map<std::string, std::vector<std::pair<size_t, size_t>>> wireOf;
        std::vector<size_t> drawn;

        size_t Element(size_t k) const { return source.elements[k]; }
        const std::string &IdOf(size_t e) const { return sk.elementIds[e]; }

        // the point given lies inside the closed source
        bool IsInside() const {
            if (!circle)
                return InsideChain(edges, sk.axes, *point, tol, call);
            const sketch::Element &c = sk.s.elements[Element(0)];
            const double radius = sk.s.values[c.Radius()], away = Norm(*point - sk.Coords(c.Center()));
            if (std::abs(away - radius) <= Tol(radius + NormInf(*point)))
                throw std::invalid_argument(call + ": the point lies on the source and picks no side");
            return away < radius;
        }
        void Sides() {
            if (source.closed)
                towards = both ? std::vector<int>{1, -1} : std::vector<int>{(point ? !IsInside() : d > 0.0) ? 1 : -1};
            else
                towards = {both ? 0 : point ? SideOfChain(edges, *point, tol, call) : d > 0.0 ? 1 : -1};
            try {
                sides = sk.SidesOf(source, edges, ad, towards, forms, call);
            } catch (const std::exception &) {
                // near a cusp the offsets of a spline's arcs fall apart: say why
                if (const std::optional<std::string> cusp = sk.ChainCusp(source, edges, ad, towards))
                    throw std::invalid_argument(call + ": " + *cusp);
                throw;
            }
            asked = sides;
            if (hole)
                OpenHole(sides, *hole, ad);
        }

        // the side word in the ids: a two-sided offset's left and right (a loop runs counterclockwise: its inside is
        // left), none for one side
        std::string Word(const std::string &fixed, int side) const {
            if (!both)
                return "";
            if (!fixed.empty())
                return fixed == "outer" ? "right" : "left";
            return side > 0 ? "left" : side < 0 ? "right" : "";
        }
        std::string Label(const std::string &fixed, const OffsetPiece &p) const { return Word(fixed, p.side); }
        std::string Under(const std::string &side) const { return side.empty() ? name : name + "." + side; }
        void Labels() {
            for (const auto &[fixed, loops] : sides) {
                if ((circle || sk.ClosedSpline(source)) && both)
                    labels.insert(Word(fixed, 0));
                std::set<std::tuple<OffsetPiece::Kind, size_t, int>> seen;
                for (const OffsetLoop &loop : loops)
                    for (const OffsetPiece &p : loop.pieces) {
                        if (!seen.insert({p.kind, p.k, p.side}).second)
                            throw std::invalid_argument(call + ": the offset of " +
                                                        (IsAround(p.kind) ? "the corner where " + IdOf(Element(p.k % n)) + " starts"
                                                                                             : IdOf(Element(p.k))) +
                                                        " falls into pieces, which an offset cannot hold yet");
                        if (const std::string side = Label(fixed, p); !side.empty())
                            labels.insert(side);
                    }
            }
        }
        void Name() {
            if (tied) {
                param = sk.OffsetElement(ad, opt, !slot.empty());
                name = IdOf(*param);
            } else
                name = sk.NewId("Offset", opt.id);
            for (const std::string &side : labels)
                sk.CheckId(name + "." + side);
            sk.groups.push_back({slot.empty() ? "Offset" : slot, name});
        }

        // along the chain: the corner at vertex v, the spur before edge v, edge v, the spur after it
        size_t OrderOf(const OffsetPiece &p) const {
            const size_t vertex = source.closed && p.k == 0 ? n : p.k;
            if (IsAround(p.kind))
                return 4 * vertex;
            return 4 * p.k + (p.kind == OffsetPiece::Kind::SpurBefore ? 1 : p.kind == OffsetPiece::Kind::SpurAfter ? 3 : 2);
        }
        // a piece around or across a vertex: a corner or a cap
        static bool IsAround(OffsetPiece::Kind k) {
            return k == OffsetPiece::Kind::Vertex || k == OffsetPiece::Kind::CornerLine || k == OffsetPiece::Kind::CapLine;
        }
        void Tie(OffsetTie how, std::vector<Ref> refs, double sign) {
            refs.push_back({*param});
            tieIds.push_back(name + ".tie." + std::to_string(tieIds.size() + 1));
            sk.TieOffset(how, std::move(refs), sign, tieIds.back());
        }
        bool IsConstructed(size_t e) const { return constructionGiven ? *constructionGiven : opt.construction || (slot.empty() && !sk.IsDrawn(e)); }
        Options OptionsOf(size_t e, const std::string &id) const { return {id, Overridden(WithoutPartition(sk.props[e]), opt.props), IsConstructed(e)}; }
        // a piece's side relative to its element's own direction
        double SignOf(size_t k, int side) const { return (double)(source.forward[k] ? side : -side); }
        // vertex v (where chain element v starts, v = n where the last ends): its point, and that point as a reference
        // through chain element k (v its start or its end)
        size_t VertexPoint(size_t v) const {
            const bool atEnd = v == n;
            const size_t k = atEnd ? n - 1 : v;
            return sk.s.elements[Element(k)].Ends(sk.s)[bool(source.forward[k]) != atEnd ? 0 : 1];
        }
        Ref VertexRef(size_t k, size_t v) const { return Ref{Element(k), (v == k) == bool(source.forward[k]) ? Part::Start : Part::End}; }
        Ref VertexAt(size_t v) const { return v < n ? VertexRef(v, v) : VertexRef(n - 1, n); }
        // the element a corner or cap takes its properties from: the one before it, at the start the first
        size_t Before(size_t v) const { return Element(v == 0 ? (source.closed ? n - 1 : 0) : v - 1); }

        void CircleSides() {
            for (const auto &[fixed, loops] : sides) {
                const size_t c0 = Element(0);
                const size_t centre = sk.s.elements[c0].Center();
                sk.AddElement(sk.s.AddCircle(centre, sk.s.values[sk.s.elements[c0].Radius()] + (fixed == "outer" ? ad : -ad)), "Circle",
                              OptionsOf(c0, Under(Word(fixed, 0)) + "." + IdOf(c0)));
                drawn.push_back(sk.s.elements.size() - 1);
                if (both)
                    wireOf[Word(fixed, 0)].push_back({0, drawn.back()});
                made.push_back(sk.elementIds.back());
                if (tied)
                    Tie(OffsetTie::Radius, {{drawn.back()}, {c0}}, fixed == "outer" ? 1.0 : -1.0);
            }
        }
        void ClosedSplineSides() {
            for (const auto &[fixed, loops] : sides) {
                const size_t e0 = Element(0);
                const double turn = (fixed == "outer" ? 1.0 : -1.0) * (ChainArea(edges) > 0.0 ? -1.0 : 1.0);
                const CurveEval g{sk.s.elements[e0].Curve(sk.s), turn, ad};
                const double length = g.f.params.back();
                if (const std::optional<std::string> cusp = Cusp(g.f, turn, 0.0, length, ad, IdOf(e0)))
                    throw std::invalid_argument(call + ": " + *cusp);
                sk.idScope = Under(Word(fixed, 0));
                const size_t madeBefore = sk.elementIds.size();
                sk.AddElement(sk.s.AddPoint(sk.s.NewPoint(g.At(0.0, 0))), "Point", {std::nullopt, nullptr, IsConstructed(e0)});
                const size_t j = sk.s.elements.size() - 1, at = sk.s.elements[j].Point();
                const Options o = OptionsOf(e0, Under(Word(fixed, 0)) + "." + IdOf(e0));
                if (tied) {
                    sk.AddElement(sk.s.AddOffsetCurve({sk.s.elements[e0].SplineIndex(), 0, sk.s.elements[*param].Distance(), turn, {none, none, at, at}, false}),
                                  "Spline", o);
                    curveIds.push_back({sk.elementIds.back(), IdOf(e0)});
                } else {
                    sk.AddElement(sk.FitCurve(g, 0.0, length, {at, at}, o), "Spline", o);
                    if (const std::optional<std::string> crossing = sk.FrozenCrossing(sk.s.elements.size() - 1, IdOf(e0)))
                        throw std::invalid_argument(call + ": " + *crossing);
                }
                drawn.push_back(sk.s.elements.size() - 1);
                if (both)
                    wireOf[Word(fixed, 0)].push_back({0, drawn.back()});
                made.insert(made.end(), sk.elementIds.begin() + (std::ptrdiff_t)madeBefore, sk.elementIds.end());
                if (tied)
                    Tie(OffsetTie::Point, {{j}, {e0}, {e0, Part::Start}}, turn);
            }
        }

        static size_t JointCount(const OffsetLoop &loop) { return loop.closed ? loop.pieces.size() : loop.pieces.size() + 1; }
        static const OffsetPiece *PieceAt(const OffsetLoop &loop, size_t i) {
            const size_t m = loop.pieces.size();
            return loop.closed || i < m ? &loop.pieces[i % m] : nullptr;
        }
        static const OffsetPiece *PieceBefore(const OffsetLoop &loop, size_t i) {
            const size_t m = loop.pieces.size();
            return loop.closed || i > 0 ? &loop.pieces[(i + m - 1) % m] : nullptr;
        }
        void Loop(const std::string &fixed, const OffsetLoop &loop) {
            const std::vector<size_t> joints = Joints(fixed, loop);
            const std::optional<size_t> freeArc = FreeArc(loop);
            const std::vector<size_t> pieces = Pieces(fixed, loop, joints, freeArc);
            if (tied)
                Ties(loop, joints, pieces, freeArc);
        }
        // the point elements where the loop's pieces meet, and its ends
        std::vector<size_t> Joints(const std::string &fixed, const OffsetLoop &loop) {
            const size_t count = JointCount(loop);
            std::vector<size_t> joints(count);
            for (size_t i = 0; i < count; ++i) {
                const OffsetPiece *a = PieceBefore(loop, i), *b = PieceAt(loop, i);
                const std::string sa = a ? Label(fixed, *a) : "", sb = b ? Label(fixed, *b) : "";
                sk.idScope = name + (!sa.empty() ? "." + sa : !sb.empty() ? "." + sb : std::string());
                const OffsetPiece &next = b ? *b : *a;
                const size_t from = IsAround(next.kind) ? Before(next.k) : Element(next.k);
                sk.AddElement(sk.s.AddPoint(sk.s.NewPoint(b ? b->start : a->end)), "Point", {std::nullopt, nullptr, IsConstructed(from)});
                joints[i] = sk.s.elements.size() - 1;
                made.push_back(sk.elementIds.back());
            }
            return joints;
        }
        // of a tied closed loop of touching arcs only, the widest arc: it gets a centre of its own
        std::optional<size_t> FreeArc(const OffsetLoop &loop) const {
            if (!tied || !loop.closed || !std::all_of(loop.joints.begin(), loop.joints.end(), [](const OffsetJoint &j) { return j.tangent; }) ||
                std::any_of(loop.pieces.begin(), loop.pieces.end(), [](const OffsetPiece &p) {
                    return p.kind != OffsetPiece::Kind::Arc && p.kind != OffsetPiece::Kind::Vertex;
                }))
                return std::nullopt;
            return (size_t)(std::max_element(loop.pieces.begin(), loop.pieces.end(),
                                             [](const OffsetPiece &x, const OffsetPiece &y) { return PieceSweep(x) < PieceSweep(y); }) -
                            loop.pieces.begin());
        }
        std::vector<size_t> Pieces(const std::string &fixed, const OffsetLoop &loop, const std::vector<size_t> &joints, const std::optional<size_t> &freeArc) {
            const size_t m = loop.pieces.size(), count = JointCount(loop);
            std::vector<size_t> pieces(m);
            for (size_t i = 0; i < m; ++i) {
                const OffsetPiece &p = loop.pieces[i];
                const bool corner = IsAround(p.kind);
                const std::string side = Label(fixed, p);
                size_t a = sk.s.elements[joints[i]].Point(), b = sk.s.elements[joints[(i + 1) % count]].Point();
                if (!p.along)
                    std::swap(a, b);
                // a corner (round or chamfered) is named after the element whose end it goes around; a spur (a sharp
                // corner's or a square cap's straight piece along a curve) after the element it runs on
                // from, before or after its offset
                const std::string spur = p.kind == OffsetPiece::Kind::SpurBefore ? ".before" : p.kind == OffsetPiece::Kind::SpurAfter ? ".after" : "";
                const std::string id = !corner                         ? Under(side) + (slot.empty() ? "." + IdOf(Element(p.k)) : "") + spur
                                       : fixed.empty() && p.side == 0 ? name + (p.k == 0 ? (hole ? ".hole" : ".startcap") : ".endcap")
                                                                       : Under(side) + "." + IdOf(Before(p.k)) + ".corner";
                const Options o = OptionsOf(corner ? Before(p.k) : Element(p.k), id);
                const size_t madeBefore = sk.elementIds.size();
                sk.idScope = Under(side);
                if (p.kind != OffsetPiece::Kind::Arc && p.kind != OffsetPiece::Kind::Vertex && p.kind != OffsetPiece::Kind::Curve)
                    sk.AddElement(sk.s.AddLine(a, b), "Line", o);
                else if (p.kind == OffsetPiece::Kind::Curve) {
                    const size_t ja = p.along ? i : (i + 1) % count, jb = p.along ? (i + 1) % count : i;
                    const std::array<bool, 2> tangent{loop.joints[ja].tangent, loop.joints[jb].tangent};
                    if (tied) {
                        sk.AddElement(sk.s.AddOffsetCurve(sk.CurveData(source, p, *param, {a, b}, tangent)), "Spline", o);
                        curveIds.push_back({sk.elementIds.back(), IdOf(Element(p.k))});
                    } else
                        sk.FrozenCurve(source, p, ad, {a, b}, tangent, o, call);
                } else {
                    const size_t centre = tied && freeArc == i ? sk.s.NewPoint(p.center) : corner ? VertexPoint(p.k) : sk.s.elements[Element(p.k)].Center();
                    sk.AddElement(sk.s.AddArc(centre, p.r, a, b, p.ccw == p.along), "Arc", o);
                }
                pieces[i] = sk.s.elements.size() - 1;
                if (tied && hole && corner && p.k == 0)
                    sk.Dimension(Kind::Radius, {{pieces[i]}}, *hole, name + ".r", true);
                drawn.push_back(pieces[i]);
                if (!side.empty() && slot.empty())
                    wireOf[side].push_back({OrderOf(p), pieces[i]});
                made.insert(made.end(), sk.elementIds.begin() + (std::ptrdiff_t)madeBefore, sk.elementIds.end());
            }
            return pieces;
        }
        // the point on piece p's line, circle or curve (index: its place in the loop); a spur's line is the tangent of its
        // element at its end, offset; a square cap's line d beyond the chain's end
        void TieToCarrier(const Ref &at, const OffsetPiece &p, size_t piece) {
            if (p.kind == OffsetPiece::Kind::Vertex)
                Tie(OffsetTie::Carrier, {at, VertexAt(p.k)}, 1.0);
            else if (p.kind == OffsetPiece::Kind::Curve)
                Tie(OffsetTie::Along, {at, {piece}}, 1.0);
            else if (p.kind == OffsetPiece::Kind::SpurBefore || p.kind == OffsetPiece::Kind::SpurAfter) {
                const size_t v = p.kind == OffsetPiece::Kind::SpurBefore ? p.k : p.k + 1;
                Tie(OffsetTie::Spur, {at, {Element(p.k)}, VertexRef(p.k, source.closed ? v % n : v)}, SignOf(p.k, p.side));
            } else if (p.kind == OffsetPiece::Kind::CapLine) {
                // out of the chain: against the element's way at the start, along it at the end
                const size_t k = p.k == 0 ? 0 : n - 1;
                Tie(OffsetTie::Beyond, {at, {Element(k)}, VertexAt(p.k)}, (p.k == 0) == bool(source.forward[k]) ? -1.0 : 1.0);
            } else
                Tie(OffsetTie::Carrier, {at, {Element(p.k)}}, SignOf(p.k, p.side));
        }
        void Ties(const OffsetLoop &loop, const std::vector<size_t> &joints, const std::vector<size_t> &pieces, const std::optional<size_t> &freeArc) {
            const size_t m = loop.pieces.size(), count = JointCount(loop);
            for (size_t i = 0; i < count; ++i) {
                const OffsetPiece *a = PieceBefore(loop, i), *b = PieceAt(loop, i);
                const bool held = a && (a->kind == OffsetPiece::Kind::Arc || a->kind == OffsetPiece::Kind::Vertex) && freeArc != (i + m - 1) % m;
                const Ref at{joints[i]};
                if (loop.joints[i].tangent) {
                    // a curve's tie puts the point where the curve ends; else the element's before it, or after (a
                    // piece on an edge's offset)
                    const OffsetPiece *on = a && a->kind == OffsetPiece::Kind::Curve   ? a
                                            : b && b->kind == OffsetPiece::Kind::Curve ? b
                                            : a && IsEdgePiece(a->kind)                ? a
                                                                                       : b;
                    Tie(held ? OffsetTie::Foot : OffsetTie::Point, {at, {Element(on->k)}, VertexRef(on->k, loop.joints[i].vertex)}, SignOf(on->k, on->side));
                } else {
                    if (!held)
                        TieToCarrier(at, *a, pieces[(i + m - 1) % m]);
                    if (!(hole && b->kind == OffsetPiece::Kind::Vertex))
                        TieToCarrier(at, *b, pieces[i % m]);
                }
            }
            if (freeArc) {
                const OffsetPiece &p = loop.pieces[*freeArc];
                Tie(OffsetTie::Center, {{pieces[*freeArc]}, p.kind == OffsetPiece::Kind::Vertex ? VertexAt(p.k) : Ref{Element(p.k), Part::Center}}, 1.0);
            }
        }
        void SideWires() {
            for (auto &[side, list] : wireOf) {
                std::stable_sort(list.begin(), list.end(), [](const auto &x, const auto &y) { return x.first < y.first; });
                std::vector<size_t> elements;
                for (const auto &[key, e] : list)
                    elements.push_back(e);
                sk.NameWire(name + "." + side, elements, call + ": ");
            }
        }
    };

}
