// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "planex/sketch/sketch.hpp"

namespace planex2d {

    template <class Store>
    std::vector<std::string> BasicSketch<Store>::Spans() const {
        std::vector<std::string> out;
        for (size_t e = sketch::ownElements; e < s.elements.size(); ++e)
            if (IsSpan(e))
                out.push_back(book.elementIds[e]);
        return out;
    }

    template <class Store>
    std::vector<std::string> BasicSketch<Store>::Expanded(const std::vector<std::string> &ids) const {
        std::vector<std::string> out;
        for (const std::string &id : ids)
            if (const auto it = book.wires.find(id); it != book.wires.end())
                out.insert(out.end(), it->second.begin(), it->second.end());
            else
                out.push_back(id);
        return out;
    }

    template <class Store>
    std::vector<std::pair<std::string, std::vector<std::string>>> BasicSketch<Store>::Elements() const {
        std::vector<std::pair<std::string, std::vector<std::string>>> out;
        for (const char *kind : {"Point", "Line", "Arc", "Circle", "Spline", "BSpline", "Curve", "Axis", "Pattern", "Offset"}) {
            std::vector<std::string> ids;
            for (size_t e = 0; e < s.elements.size(); ++e)
                if (s.elements[e].KindName(s) == kind && !IsHidden(e))
                    ids.push_back(book.elementIds[e]);
            if (!ids.empty())
                out.push_back({kind, ids});
        }
        return out;
    }

    template <class Store>
    std::optional<std::string> BasicSketch<Store>::KindById(const std::string &id) const {
        const auto at = book.byId.find(id);
        if (at == book.byId.end() || !at->second.isElement)
            return std::nullopt;
        const size_t e = at->second.index;
        if (book.elementIds[e] != id || IsHidden(e))
            return std::nullopt;
        return s.elements[e].KindName(s);
    }

    template <class Store>
    std::vector<std::pair<std::string, std::string>> BasicSketch<Store>::Chosen(const std::string &id) const {
        constexpr std::array<const char *, 7> kinds = {"Point", "Line", "Arc", "Circle", "Spline", "BSpline", "Curve"};
        std::vector<std::pair<std::pair<size_t, size_t>, std::string>> hits;
        const auto take = [&](const std::string &x) {
            const auto at = book.byId.find(x);
            if (at == book.byId.end() || !at->second.isElement || x == "origin" || x == "px.origin")
                return false;
            const size_t e = at->second.index;
            const auto rank = std::find_if(kinds.begin(), kinds.end(), [&](const char *k) { return k == s.elements[e].KindName(s); });
            if (book.elementIds[e] != x || IsHidden(e) || rank == kinds.end())
                return false;
            hits.push_back({{(size_t)(rank - kinds.begin()), e}, x});
            return true;
        };
        if (!take(id))
            return {};
        if (const auto under = book.idsUnder.find(id); under != book.idsUnder.end())
            for (const std::string &x : under->second)
                take(x);
        std::sort(hits.begin(), hits.end());
        std::vector<std::pair<std::string, std::string>> out;
        for (const auto &[at, x] : hits)
            out.push_back({kinds[at.first], x});
        return out;
    }

    template <class Store>
    std::vector<std::pair<std::string, Vertex>> BasicSketch<Store>::Locations() {
        Flush();
        std::vector<std::pair<std::string, Vertex>> out;
        const auto add = [&](const std::string &ref) { out.push_back({ref, Location(ref)}); };
        const std::set<size_t> centres = s.CentrePoints();
        for (size_t e = 2; e < s.elements.size(); ++e) {
            const sketch::Element &el = s.elements[e];
            const std::string &id = book.elementIds[e];
            if (IsHidden(e))
                continue;
            if (el.kind == sketch::ElementKind::Point) {
                if (!centres.contains(e))
                    add(id);
                continue;
            }
            if (el.IsRound())
                add(id + ".center");
            if (el.HasEnds())
                for (const char *part : {".start", ".end", ".mid"})
                    add(id + part);
            if (el.kind == sketch::ElementKind::Spline && !el.IsFitted(s))
                for (size_t k = 0; k < s.splines[el.SplineIndex()].points.size(); ++k)
                    add(id + ".p" + std::to_string(k));
        }
        return out;
    }

    template <class Store>
    Vertex BasicSketch<Store>::Location(const std::string &ref) {
        Flush();
        const sketch::Ref r = RefOf(ref);
        if (!r.IsPoint(s) && r.part != sketch::Part::Tangent)
            throw std::invalid_argument("'" + ref + "' is no point");
        return r.part == sketch::Part::Mid ? r.PointValue(s) : Coords(r.PointAt(s));
    }

    template <class Store>
    sketch::ElementValues BasicSketch<Store>::ElementOf(const std::string &id) {
        Flush();
        const sketch::Element &e = s.elements[RefOf(id).element];
        const bool round = e.IsRound(), axis = e.kind == sketch::ElementKind::Axis;
        return {e, round ? s.values[e.Radius()] : 0.0, axis ? Degrees(s.values[e.AxisAngle()]) : 0.0, axis ? s.values[e.AxisDistance()] : 0.0};
    }

    template <class Store>
    std::optional<std::string> BasicSketch<Store>::NameOf(const std::string &id) const {
        const auto it = book.byId.find(id);
        if (it == book.byId.end() || !it->second.isElement || !props[it->second.index])
            return std::nullopt;
        return props[it->second.index]->name;
    }

    template <class Store>
    std::vector<std::pair<std::string, Vertex>> BasicSketch<Store>::NamedPoints() {
        Flush();
        std::vector<std::pair<std::string, Vertex>> out;
        for (size_t e = sketch::ownElements; e < s.elements.size(); ++e)
            if (s.elements[e].kind == sketch::ElementKind::Point && book.givenIds.contains(book.elementIds[e]) && book.elementIds[e] != "px.origin")
                out.push_back({book.elementIds[e], Coords(s.elements[e].Point())});
        return out;
    }

    template <class Store>
    std::vector<size_t> BasicSketch<Store>::PropertyElements(const std::vector<std::string> &ids, bool points, const std::string &call) const {
        std::vector<size_t> out;
        for (const std::string &id : Expanded(ids)) {
            const sketch::Ref r = RefOf(id);
            size_t e = r.element;
            if (r.part == sketch::Part::Start || r.part == sketch::Part::End || r.part == sketch::Part::Center) {
                const auto at = pointElementOf.find(r.PointAt(s));
                if (at == pointElementOf.end())
                    throw std::invalid_argument(call + ": no point element at '" + id + "'");
                e = at->second;
            } else if (r.part != sketch::Part::Whole)
                throw std::invalid_argument(call + ": '" + id + "' is no element");
            const sketch::ElementKind k = s.elements[e].kind;
            if (e < sketch::ownElements || k == sketch::ElementKind::Axis || k == sketch::ElementKind::Pattern || k == sketch::ElementKind::Offset)
                throw std::invalid_argument(call + ": '" + id + "' has no properties to set");
            if ((points || k != sketch::ElementKind::Point) && std::find(out.begin(), out.end(), e) == out.end())
                out.push_back(e);
        }
        return out;
    }

    template <class Store>
    std::optional<std::string> BasicSketch<Store>::PointElementAt(const std::string &ref) const {
        const sketch::Ref r = RefOf(ref);
        if (r.part == sketch::Part::Whole)
            return s.elements[r.element].kind == sketch::ElementKind::Point ? std::optional(book.elementIds[r.element]) : std::nullopt;
        if (r.part != sketch::Part::Start && r.part != sketch::Part::End && r.part != sketch::Part::Center)
            return std::nullopt;
        const auto at = pointElementOf.find(r.PointAt(s));
        return at == pointElementOf.end() ? std::nullopt : std::optional(book.elementIds[at->second]);
    }

    template <class Store>
    void BasicSketch<Store>::RenamePoint(const std::string &old, const std::string &id) {
        const sketch::Ref r = RefOf(old);
        const size_t e = r.element;
        if (r.part != sketch::Part::Whole || s.elements[e].kind != sketch::ElementKind::Point)
            throw std::invalid_argument("id = '" + id + "': " + old + " is no point element");
        book.RenamePoint(e, id);
    }

    template <class Store>
    template <class F>
    BasicSketch<Store> &BasicSketch<Store>::ChangeProperties(const std::vector<std::string> &ids, bool points, const std::string &call, F change) {
        const std::vector<size_t> elements = PropertyElements(ids, points, call);
        if (elements.empty())
            throw std::invalid_argument(call + (ids.empty() ? ": selects nothing" : ": a point takes name, maxh and hpref"));
        for (size_t e : elements)
            props[e] = ChangedProperties(props[e], change);
        return *this;
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::SetConstruction(const std::vector<std::string> &ids, bool on, const std::string &call) {
        const std::vector<size_t> elements = PropertyElements(ids, true, call);
        if (elements.empty())
            throw std::invalid_argument(call + ": selects nothing");
        for (size_t e : elements)
            roles[e] = on ? sketch::Role::Construction : sketch::Role::Drawn;
        return *this;
    }

    template <class Store>
    std::vector<std::pair<Properties, bool>> BasicSketch<Store>::ElementProperties(const std::vector<std::string> &ids, bool points, const std::string &call) const {
        std::vector<std::pair<Properties, bool>> out;
        for (size_t e : PropertyElements(ids, points, call))
            out.push_back({props[e], !IsDrawn(e)});
        return out;
    }

    template <class Store>
    std::string BasicSketch<Store>::OffsetCurveSource(const sketch::Element &e) const {
        const sketch::OffsetCurveData &c = s.offsetCurves[e.OffsetCurveIndex()];
        for (size_t k = 0; k < s.elements.size(); ++k)
            if (s.elements[k].kind == sketch::ElementKind::Spline && s.elements[k].SplineIndex() == c.spline && s.elements[k].PieceIndex() == c.piece)
                return book.elementIds[k];
        return "";
    }

    template <class Store>
    std::string BasicSketch<Store>::RefKind(const std::string &ref) const {
        const sketch::Ref r = RefOf(ref);
        return r.IsPoint(s) ? "point" : r.IsSegment(s) ? "line" : r.IsLine(s) ? "axis" : "round";
    }

    template <class Store>
    std::pair<Vertex, Vertex> BasicSketch<Store>::LineOf(const std::string &axis, const std::string &call) const {
        const sketch::Ref ar = RefOf(axis);
        if (!ar.IsLine(s))
            throw std::invalid_argument(call + ": the axis '" + axis + "' is no line");
        return s.elements[ar.element].Carrier(s);
    }

    template <class Store>
    std::vector<std::string> BasicSketch<Store>::MembersOf(const std::string &id) const {
        if (const auto it = book.byId.find(id); it != book.byId.end() && it->second.isElement)
            return {};
        if (const auto w = book.wires.find(id); w != book.wires.end())
            return w->second;
        std::vector<std::string> members;
        if (const auto under = book.idsUnder.find(id); under != book.idsUnder.end())
            for (const std::string &x : under->second)
                if (const auto at = book.byId.find(x); at != book.byId.end() && at->second.isElement && book.elementIds[at->second.index] == x &&
                    s.elements[at->second.index].kind != sketch::ElementKind::Point)
                    members.push_back(x);
        return members;
    }

    template <class Store>
    std::vector<std::string> BasicSketch<Store>::Through(const Vertex &p, const std::string &except) const {
        const double tol = Tol(std::max(s.Size(), NormInf(p)));
        std::vector<std::string> out, axesThrough;
        for (size_t k = 0; k < s.elements.size(); ++k) {
            const sketch::Element &e = s.elements[k];
            if (!IsHidden(k) && e.PassesThrough(s, p, tol) && book.elementIds[k] != except)
                (e.kind == sketch::ElementKind::Axis ? axesThrough : out).push_back(book.elementIds[k]);
        }
        out.insert(out.end(), axesThrough.begin(), axesThrough.end());
        return out;
    }

    template <class Store>
    sketch::Ref BasicSketch<Store>::SplinePartRef(const std::string &text, size_t dot, const std::pair<sketch::Part, size_t> &k) const {
        const std::string part = text.substr(dot + 1);
        const auto it = book.byId.find(text.substr(0, dot));
        if (it == book.byId.end() || !it->second.isElement || s.elements[it->second.index].kind != sketch::ElementKind::Spline)
            throw std::invalid_argument("no element '" + text + "' (." + part + ": a point or tangent of a spline)");
        if (s.elements[it->second.index].IsFitted(s))
            throw std::invalid_argument("no element '" + text + "': a curve has no points of its own but its ends, '" + text.substr(0, dot) +
                                        ".start' and '" + text.substr(0, dot) + ".end'");
        const sketch::SplineData &d = s.splines[s.elements[it->second.index].SplineIndex()];
        const size_t n = d.points.size();
        if (k.second >= n)
            throw std::invalid_argument("no element '" + text + "': the spline has the points p0 to p" + std::to_string(n - 1));
        if (d.poled && k.first == sketch::Part::Tangent && d.closed)
            throw std::invalid_argument("no element '" + text + "': a periodic BSpline runs through none of its poles; its legs '" +
                                        text.substr(0, dot) + ".leg.0' to '" + text.substr(0, dot) + ".leg." + std::to_string(n - 1) +
                                        "' shape it");
        if (d.poled && k.first == sketch::Part::Tangent)
            throw std::invalid_argument("no element '" + text + "': a BSpline runs through no pole but its ends; its tangents there are "
                                        "its first and last legs, '" + text.substr(0, dot) + ".leg.0' and '" + text.substr(0, dot) +
                                        ".leg." + std::to_string(n - 2) + "'");
        return {it->second.index, k.first, k.second};
    }

    template <class Store>
    sketch::Ref BasicSketch<Store>::RefOf(const std::string &text) const {
        if (const auto it = book.byId.find(text); it != book.byId.end()) {
            if (!it->second.isElement)
                throw std::invalid_argument("'" + text + "' is a constraint, not an element");
            return {it->second.index};
        }
        const size_t dot = text.rfind('.');
        if (dot != std::string::npos) {
            const std::string part = text.substr(dot + 1);
            if (const std::optional<std::pair<sketch::Part, size_t>> k = sketch::SplinePart(part))
                return SplinePartRef(text, dot, *k);
            const std::optional<sketch::Part> p = sketch::PartOf(part);
            if (const auto it = book.byId.find(text.substr(0, dot)); p && it != book.byId.end() && it->second.isElement) {
                if (s.elements[it->second.index].kind == sketch::ElementKind::Circle && *p != sketch::Part::Center)
                    throw std::invalid_argument("no element '" + text + "': a circle has no start, end or mid point; its point is '" +
                                                text.substr(0, dot) + ".center'");
                if (s.elements[it->second.index].IsPeriodicBSpline(s) && (*p == sketch::Part::Start || *p == sketch::Part::End))
                    throw std::invalid_argument("no element '" + text + "': a periodic BSpline has no start or end and runs through none of its "
                                                "poles, '" + text.substr(0, dot) + ".p0', ...");
                const sketch::Ref r{it->second.index, *p};
                if (!r.HasPart(s))
                    throw std::invalid_argument("no element '" + text + "': '" + text.substr(0, dot) + "' has no ." + part + " point");
                return r;
            }
        }
        book.NoElement(text);
    }

    template <class Store>
    std::string BasicSketch<Store>::RefText(const sketch::Ref &r) const {
        const std::string &id = book.elementIds[r.element];
        if (r.element < 2)
            return "px." + id;
        if (id == "px.origin" && r.part == sketch::Part::Whole)
            return id;
        return "\"" + RefName(r) + "\"";
    }

    template <class Store>
    const std::string &BasicSketch<Store>::ShownId(size_t e) const {
        if (roles[e] == sketch::Role::Helper)
            for (size_t k = sketch::ownElements; k < s.elements.size(); ++k)
                if (s.elements[k].kind == sketch::ElementKind::Spline) {
                    const std::optional<sketch::BlendShape> &b = s.splines[s.elements[k].SplineIndex()].blend;
                    if (b && b->helper == e)
                        return book.elementIds[k];
                }
        return book.elementIds[e];
    }

    template <class Store>
    std::optional<sketch::Ref> BasicSketch<Store>::FindRef(const std::string &text) const {
        try {
            return RefOf(text);
        } catch (const std::invalid_argument &) {
            return std::nullopt;
        }
    }

    template <class Store>
    bool BasicSketch<Store>::IsPoint(const std::string &ref) const {
        const std::optional<sketch::Ref> r = FindRef(ref);
        return r && r->IsPoint(s);
    }

    template <class Store>
    bool BasicSketch<Store>::HasLocation(const std::string &ref) const {
        const std::optional<sketch::Ref> r = FindRef(ref);
        return r && (r->IsPoint(s) || r->part == sketch::Part::Tangent);
    }

}
