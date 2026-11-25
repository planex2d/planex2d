// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "planex/core/config.hpp"
#include "planex/core/properties.hpp"
#include "planex/core/vertex.hpp"
#include "planex/shape/fillet.hpp"
#include "planex/sketch/cornerparts.hpp"
#include "planex/sketch/cut.hpp"
#include "planex/sketch/elementcutter.hpp"
#include "planex/sketch/geometry.hpp"
#include "planex/sketch/options.hpp"
#include "planex/wire/wire.hpp"

namespace planex2d::sketch {

    template <class Sketch, class Rule>
    class CornerMaker {
    public:
        CornerMaker(Sketch &sk, const Rule &rule, const std::string &call) : sk(sk), rule(rule), call(call) {}

        void Make(const std::vector<Vertex> &at, const std::vector<std::string> &ids, const std::vector<Properties> &given) {
            std::vector<std::string> failed;
            size_t done = 0;
            for (size_t k = 0; k < at.size(); ++k) {
                if (std::any_of(at.begin(), at.begin() + (std::ptrdiff_t)k, [&](const Vertex &q) { return q.IsClose(at[k], Tol(sk.s.Size())); }))
                    continue;
                const std::optional<std::string> id = ids.empty() ? std::nullopt : std::optional<std::string>(ids[k]);
                const Properties own = given.empty() ? nullptr : given[given.size() == 1 ? 0 : k];
                if (std::string why = CutCorner(at[k], id, own); why.empty())
                    ++done;
                else
                    failed.push_back(std::move(why));
            }
            ReportCorners(call, done, failed);
        }

    private:
        Sketch &sk;
        const Rule &rule;
        const std::string &call;

        std::vector<std::pair<size_t, bool>> EndsAt(const Vertex &p) const {
            std::vector<std::pair<size_t, bool>> out;
            for (size_t e = ownElements; e < sk.s.elements.size(); ++e) {
                if (!sk.IsDrawn(e) || !sk.s.elements[e].HasEnds())
                    continue;
                const std::array<size_t, 2> ends = sk.s.elements[e].Ends(sk.s);
                for (size_t k = 0; k < 2; ++k)
                    if (sk.Coords(ends[k]).IsClose(p, Tol(sk.s.Size())))
                        out.push_back({e, k == 1});
            }
            return out;
        }
        Arm ArmAt(const std::pair<size_t, bool> &end) const {
            Wire w(sk.axes);
            sk.AddEdges(end.first, w, sk.VertexProps());
            return {w[end.second ? w.Size() - 1 : 0], end.second};
        }
        ArmCut CutBack(size_t e, const Arm &arm, const Vertex &x) {
            const Element el = sk.s.elements[e];
            const Part corner = arm.atEnd ? Part::End : Part::Start;
            if (arm.IsUsedUpBy(x)) {
                sk.roles[e] = Role::Construction;
                return {el.Ends(sk.s)[arm.atEnd ? 0 : 1], {e, arm.atEnd ? Part::Start : Part::End}, {e, corner}};
            }
            const size_t removed = arm.atEnd ? 1 : 0;
            CutMade made;
            sk.cornering = true;
            const std::string id = sk.book.elementIds[e];
            ElementCutter<Sketch> cutter(sk, e, id, call);
            // a spline's or offset curve's pieces come in their order along the spline, which a reversed offset curve runs against
            size_t piece = removed;
            if (el.kind == ElementKind::Spline || el.kind == ElementKind::OffsetCurve) {
                if (el.kind == ElementKind::OffsetCurve && sk.s.offsetCurves[el.OffsetCurveIndex()].reversed)
                    piece = 1 - removed;
                const SplineSpan span = el.Span(sk.s);
                const double t = span.Along(x);
                made = cutter.SplineCutAt(span, {CutPoint{t, span.At(t, 0), none}}, piece, true);
            } else
                made = cutter.CutAt({CutPoint{el.Param(sk.s, x), x, none}}, removed, true, true);
            sk.cornering = false;
            return {sk.s.elements[made.cuts[0]].Point(), {made.cuts[0]}, {made.pieces[piece], corner}};
        }
        std::string CutCorner(const Vertex &p, const std::optional<std::string> &id, const Properties &given) {
            const std::string where = "the corner at " + PointText(p);
            std::vector<std::pair<size_t, bool>> ends = EndsAt(p);
            if (ends.empty())
                return OffVertex(p, {}, "");
            if (std::string why = CornerEndsProblem(where, ends.size()); !why.empty())
                return why;
            if (ends[0].first == ends[1].first)
                return where + " is where one edge closes on itself";
            if (rule.reverse)
                std::swap(ends[0], ends[1]);
            const Arm armOne = ArmAt(ends[0]), armTwo = ArmAt(ends[1]);
            CornerCut cut;
            if (std::string why = CornerCutOf(where, armOne, armTwo, rule, cut); !why.empty())
                return why;
            const Properties merged = Overridden(MergedWithoutPartition(sk.props[ends[0].first], sk.props[ends[1].first]), given);
            const ArmCut from = CutBack(ends[0].first, armOne, cut.first), to = CutBack(ends[1].first, armTwo, cut.second);
            const Options opt{id, merged, false};
            const size_t made = cut.center ? sk.AddElement(sk.s.AddArc(sk.s.NewPoint(*cut.center), rule.Size(), from.point, to.point, cut.IsCounterclockwise()), Rule::kind, opt)
                                           : sk.AddElement(sk.s.AddLine(from.point, to.point), Rule::kind, opt);
            sk.book.JoinWires(ends[0].first, ends[1].first, made);
            for (const auto &end : ends)
                if (!sk.IsDrawn(end.first))
                    for (auto &[name, ids] : sk.book.wires)
                        std::erase(ids, sk.book.elementIds[end.first]);
            sk.CornerMade(rule, {made, {ends[0].first, ends[1].first}, {from, to}});
            return {};
        }
    };

}
