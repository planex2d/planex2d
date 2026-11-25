// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "planex/sketch/sketch.hpp"

namespace planex2d {

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Offset(const std::vector<std::string> &ids, double d, const std::optional<Vertex> &point,
                                                   const std::optional<bool> &bothSides, const OffsetForms &forms, const Options &opt,
                                                   const std::optional<bool> &constructionGiven) {
        std::string shown;
        for (const std::string &id : ids)
            shown += (shown.empty() ? "'" : ", '") + id + "'";
        const std::string call = "Offset(" + (ids.size() == 1 ? shown : "[" + shown + "]") + ")";
        IsFinite(call.c_str(), {d});
        if (point)
            IsFinite(call.c_str(), {point->x, point->y});
        if (d == 0.0)
            throw std::invalid_argument(call + ": the distance d must not be 0");
        if (point && bothSides.value_or(false))
            throw std::invalid_argument(call + ": a point picks one side, both_sides=True asks for both");
        if (d < 0.0 && (point || bothSides.value_or(false)))
            throw std::invalid_argument(call + ": d is the distance to " + (point ? "the point's side" : "both sides") + ", it must be positive");
        return Atomic([&] { MakeOffset(ChainOf(ids, call), d, point, bothSides, opt, constructionGiven, call, "", std::nullopt, forms); });
    }

    template <class Store>
    sketch::OffsetChain BasicSketch<Store>::ChainOf(const std::vector<std::string> &ids, const std::string &call) const {
        return sketch::OffsetChain::Of(s, OffsetSources(ids, call), book.elementIds, call);
    }

    template <class Store>
    void BasicSketch<Store>::MakeOffset(const sketch::OffsetChain &source, double d, const std::optional<Vertex> &point, const std::optional<bool> &bothSides,
                                        const Options &opt, const std::optional<bool> &constructionGiven, const std::string &call, const std::string &slot,
                                        std::optional<double> hole, const OffsetForms &forms) {
        sketch::OffsetMaker<BasicSketch>(*this, source, d, point, bothSides, opt, constructionGiven, call, slot, hole, forms).Make();
    }

    template <class Store>
    sketch::OffsetSides BasicSketch<Store>::SidesOf(const sketch::OffsetChain &source, const EdgeList &edges, double ad, const std::vector<int> &towards,
                                                    const OffsetForms &forms, const std::string &call) const {
        sketch::OffsetSides out;
        const sketch::Element &c = s.elements[source.elements.front()];
        const bool circle = c.kind == sketch::ElementKind::Circle, alone = circle || source.IsClosedSpline(s);
        for (int t : towards) {
            if (source.closed) {
                std::vector<OffsetLoop> loops = alone ? std::vector<OffsetLoop>{} : ClosedOffsetLoops(edges, t * ad, forms.corners, axes, call);
                if (circle ? t < 0 && ad >= s.values[c.Radius()] : !alone && loops.empty())
                    throw std::invalid_argument(call + ": nothing is left inside at d = " + NumberText(ad));
                out.push_back({t > 0 ? "outer" : "inner", std::move(loops)});
                continue;
            }
            std::vector<OffsetLoop> band = BandLoops(edges, ad, t == 0 ? forms.ends : OffsetEnd::Round, forms.corners, axes, call);
            if (t != 0) {
                band = BandSide(band, t, source.elements.size(), call);
                if (band.empty())
                    throw std::invalid_argument(call + ": nothing is left on the " + (t > 0 ? "left" : "right") + " at d = " + NumberText(ad));
            }
            out.push_back({"", std::move(band)});
        }
        return out;
    }

    template <class Store>
    size_t BasicSketch<Store>::FitCurve(const sketch::CurveEval &g, double ta, double tb, std::array<size_t, 2> at, const Options &o) {
        const bool closed = at[0] == at[1];
        const std::vector<double> t = g.FitParams(ta, tb, Coords(at[0]), Coords(at[1]), closed, SplineStyle::Averaged);
        const std::array<Vertex, 2> ends = g.EndDirections(ta, tb);
        std::vector<size_t> ids{at[0]};
        for (size_t i = 1; i + 1 < t.size(); ++i) {
            AddElement(s.AddPoint(s.NewPoint(g.At(t[i], 0))), "Point", {std::nullopt, {}, o.construction});
            ids.push_back(s.elements.back().Point());
        }
        if (!closed)
            ids.push_back(at[1]);
        const std::vector<double> angles = closed ? std::vector<double>{} : std::vector<double>{std::atan2(ends[0].y, ends[0].x), std::atan2(ends[1].y, ends[1].x)};
        return s.AddSpline(ids, closed ? std::vector<size_t>{} : std::vector<size_t>{0, ids.size() - 1}, angles, closed, SplineStyle::Averaged);
    }

    template <class Store>
    std::optional<std::string> BasicSketch<Store>::Crossing(const std::vector<PrimitiveEdge> &arcs, const std::string &source) const {
        Wire w(axes);
        for (const PrimitiveEdge &a : arcs)
            std::visit([&](const auto &x) { w.Add(x); }, a);
        if (WireList{w}.Region().Faces().size() == 1)
            return std::nullopt;
        return "the offset of " + source + " would cross itself";
    }

    template <class Store>
    std::optional<std::string> BasicSketch<Store>::CurveCrossing(const std::string &curve, const std::string &source) const {
        const size_t e = book.byId.at(curve).index;
        if (!s.elements[e].IsClosed(s))
            return std::nullopt;
        return Crossing(s.elements[e].OffsetCurveArcs(s), source);
    }

    template <class Store>
    std::optional<std::string> BasicSketch<Store>::CurveCusp(const std::string &curve, const std::string &source, double d) const {
        const sketch::OffsetCurveData &c = s.offsetCurves[s.elements[book.byId.at(curve).index].OffsetCurveIndex()];
        const planex2d::Curve<double, Vertex> f = s.splines[c.spline].Interpolated(s);
        const std::array<double, 2> range = c.Range(s, f);
        const double end = f.params.back();
        if (range[1] <= end)
            return sketch::Cusp(f, c.side, range[0], range[1], d, source);
        if (std::optional<std::string> cusp = sketch::Cusp(f, c.side, range[0], end, d, source))
            return cusp;
        return sketch::Cusp(f, c.side, 0.0, range[1] - end, d, source);
    }

    template <class Store>
    std::vector<size_t> BasicSketch<Store>::OffsetSources(const std::vector<std::string> &ids, const std::string &call) const {
        std::vector<size_t> given;
        for (const std::string &id : Expanded(ids)) {
            const sketch::Ref r = RefOf(id);
            const sketch::ElementKind k = s.elements[r.element].kind;
            if (r.part == sketch::Part::Whole && k == sketch::ElementKind::Point)
                continue;
            if (r.part != sketch::Part::Whole || r.element < sketch::ownElements ||
                !(k == sketch::ElementKind::Line || k == sketch::ElementKind::Arc || k == sketch::ElementKind::Circle || k == sketch::ElementKind::Spline))
                throw std::invalid_argument(call + ": '" + id + "' is no line, arc, circle or spline" +
                                            (k == sketch::ElementKind::OffsetCurve ? " (an offset of an offset curve comes later)" : ""));
            if (k == sketch::ElementKind::Spline && s.elements[r.element].IsPiece(s)) {
                const planex2d::Curve<double, Vertex> f = s.elements[r.element].Curve(s);
                if (s.elements[r.element].Range(s, f)[1] > f.params.back())
                    throw std::invalid_argument(call + ": '" + id + "' runs over the seam of its closed spline, which an offset cannot take yet");
            }
            if (std::find(given.begin(), given.end(), r.element) == given.end())
                given.push_back(r.element);
        }
        if (given.empty())
            throw std::invalid_argument(call + ": no line, arc, circle or spline to offset");
        return given;
    }

}
