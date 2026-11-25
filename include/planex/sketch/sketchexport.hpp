// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "planex/sketch/sketch.hpp"

namespace planex2d {

    template <class Store>
    BBox BasicSketch<Store>::Box() const {
        const std::unordered_map<size_t, Properties> vertexProps = VertexProps();
        Wire w(axes);
        for (size_t e = sketch::ownElements; e < s.elements.size(); ++e)
            if (IsDrawn(e))
                AddEdges(e, w, vertexProps);
        return w.Box();
    }

    template <class Store>
    Wire BasicSketch<Store>::ElementWire(const std::string &id) {
        Flush();
        if (const std::vector<std::string> ids = Expanded({id}); ids.size() != 1 || ids[0] != id)
            return ElementsWire(ids);
        const sketch::Ref r = RefOf(id);
        if (r.part != sketch::Part::Whole || r.element < sketch::ownElements || !s.elements[r.element].IsEdge())
            throw std::invalid_argument("'" + id + "' is no line, arc, circle or spline");
        Wire w(axes);
        AddEdges(r.element, w, VertexProps());
        return w;
    }

    template <class Store>
    Wire BasicSketch<Store>::ElementGeometry(const std::string &id) {
        Flush();
        Wire w(axes);
        AddEdges(RefOf(id).element, w, {});
        return w;
    }

    template <class Store>
    Wire BasicSketch<Store>::ElementsWire(const std::vector<std::string> &ids) {
        Flush();
        const std::unordered_map<size_t, Properties> vertexProps = VertexProps();
        Wire w(axes);
        for (const std::string &id : ids)
            AddEdges(RefOf(id).element, w, vertexProps);
        return w;
    }

    template <class Store>
    Wire BasicSketch<Store>::NamedEdges(const std::string &name) {
        Flush();
        const std::unordered_map<size_t, Properties> vertexProps = VertexProps();
        Wire w(axes);
        for (size_t e = sketch::ownElements; e < s.elements.size(); ++e)
            if (props[e] && props[e]->name == name && s.elements[e].kind != sketch::ElementKind::Point && !IsHidden(e))
                AddEdges(e, w, vertexProps);
        return w;
    }

    template <class Store>
    Wire BasicSketch<Store>::Edges(bool constructionOnes) {
        Flush();
        const std::unordered_map<size_t, Properties> vertexProps = VertexProps();
        Wire w(axes);
        for (size_t e = sketch::ownElements; e < s.elements.size(); ++e)
            if (!IsDrawn(e) == constructionOnes && !IsHidden(e))
                AddEdges(e, w, vertexProps);
        return w;
    }

    // gap: none for 1e-3 of their size
    template <class Store>
    sketch::Faults BasicSketch<Store>::Diagnose(std::optional<double> gap) {
        Flush();
        std::vector<char> looked(s.elements.size(), 0);
        for (size_t e = sketch::ownElements; e < s.elements.size(); ++e)
            looked[e] = IsDrawn(e) && s.elements[e].IsEdge();
        const std::unordered_map<size_t, Properties> noVertexProps;
        return sketch::Diagnosis(s, std::move(looked), gap).Find([&](size_t e) {
            Wire w(axes);
            AddEdges(e, w, noVertexProps);
            return w.Edges();
        });
    }

    template <class Store>
    WireList BasicSketch<Store>::Wires(bool unify, const std::string &call) {
        Flush();
        const std::unordered_map<size_t, Properties> vertexProps = VertexProps();
        Wire c(axes);
        std::vector<size_t> source;
        for (size_t e = sketch::ownElements; e < s.elements.size(); ++e)
            if (IsDrawn(e)) {
                AddEdges(e, c, vertexProps);
                source.resize(c.Size(), e);
            }
        if (unify) {
            Wire all(axes);
            for (const Wire &w : WiresOf(c.Edges(), axes, std::nullopt))
                for (const Edge &e : w)
                    all.Add(e);
            all.MergeOnCarriers();
            return all.OrderedChains();
        }
        c.RemoveSmallGaps(c.Edges().JoinTol(std::nullopt));
        WireList out;
        for (const Loop &loop : c.Loops()) {
            Wire w(axes);
            std::vector<size_t> from;
            for (const std::pair<size_t, bool> &step : loop.edges) {
                w.Add(c[step.first]);
                from.push_back(source[step.first]);
            }
            if (const std::optional<std::pair<size_t, size_t>> hit = w.SelfContact()) {
                const std::string &a = book.elementIds[from[hit->first]], &b = book.elementIds[from[hit->second]];
                throw std::invalid_argument(call + ": " + (a == b ? "'" + a + "' crosses or touches itself" : "'" + a + "' and '" + b + "' cross, touch or overlap") +
                                            " in one chain; a wire never crosses itself: unify=True cuts the drawn edges into clean wires");
            }
            out.push_back(OrderChain(w.Edges(), axes, w.Edges().JoinTol(std::nullopt), call));
        }
        return out;
    }

    template <class Store>
    Wire BasicSketch<Store>::Region(const char *call) {
        Flush();
        std::vector<char> openGroup(s.elements.size(), 0);
        for (const std::vector<size_t> &g : Groups()) {
            std::unordered_map<size_t, size_t> uses;
            for (size_t e : g)
                for (size_t p : s.elements[e].Ends(s))
                    if (p != sketch::none)
                        ++uses[s.coordsOf[p]];
            const bool open = std::any_of(uses.begin(), uses.end(), [](const auto &u) { return u.second % 2 == 1; });
            for (size_t e : g)
                openGroup[e] = open;
        }
        const std::unordered_map<size_t, Properties> vertexProps = VertexProps();
        Wire w(axes);
        std::vector<bool> open;
        for (size_t e = sketch::ownElements; e < s.elements.size(); ++e)
            if (IsDrawn(e)) {
                // a curve crossing itself split there, as Wires(unify=True) does: InitialClean cuts edges against
                // each other, not one against itself
                Wire drawn(axes);
                AddEdges(e, drawn, vertexProps);
                for (const Edge &x : drawn)
                    AddUncrossed(w, x);
                open.resize(w.Size(), openGroup[e]);
            }
        return InitialClean(w, open, call);
    }

    template <class Store>
    std::unordered_map<size_t, Properties> BasicSketch<Store>::VertexProps() const {
        std::unordered_map<size_t, Properties> out;
        std::vector<std::pair<Vertex, Properties>> given;
        for (size_t k = sketch::ownElements; k < s.elements.size(); ++k)
            if (s.elements[k].kind == sketch::ElementKind::Point && props[k]) {
                out.emplace(s.coordsOf[s.elements[k].Point()], props[k]);
                given.push_back({Coords(s.elements[k].Point()), props[k]});
            }
        if (given.empty())
            return out;
        const double tol = Tol(s.Size());
        for (size_t k = sketch::ownElements; k < s.elements.size(); ++k) {
            if (s.elements[k].kind != sketch::ElementKind::Point || out.contains(s.coordsOf[s.elements[k].Point()]))
                continue;
            const Vertex p = Coords(s.elements[k].Point());
            for (const auto &[q, give] : given)
                if (p.IsClose(q, tol)) {
                    out.emplace(s.coordsOf[s.elements[k].Point()], give);
                    break;
                }
        }
        return out;
    }

    template <class Store>
    void BasicSketch<Store>::AddEdges(size_t e, Wire &w, const std::unordered_map<size_t, Properties> &vertexProps) const {
        const sketch::Element &el = s.elements[e];
        const auto at = [&](size_t point) -> Properties {
            const auto it = vertexProps.find(s.coordsOf[point]);
            return it == vertexProps.end() ? nullptr : it->second;
        };
        const std::array<size_t, 2> ends = el.Ends(s);
        const Ends vertices = ends[0] != sketch::none ? MakeEnds(at(ends[0]), at(ends[1])) : nullptr;
        EdgeList edges = el.Edges(s, book.elementIds[e]);
        for (size_t k = 0; k < edges.size(); ++k)
            std::visit([&](auto &x) {
                x.props = props[e];
                if (vertices)
                    x.ends = MakeEnds(k == 0 ? vertices->start : nullptr, k + 1 == edges.size() ? vertices->end : nullptr);
                w.Add(std::move(x));
            }, edges[k]);
    }

    template <class Store>
    std::vector<std::vector<size_t>> BasicSketch<Store>::GroupsOf(bool all) const {
        const size_t n = s.elements.size();
        DisjointSets joined(n);
        std::unordered_map<size_t, size_t> at;
        std::vector<size_t> drawn;
        for (size_t e = sketch::ownElements; e < n; ++e) {
            const sketch::Element &el = s.elements[e];
            if ((!IsDrawn(e) && !all) || IsHidden(e) || el.kind == sketch::ElementKind::Point || el.kind == sketch::ElementKind::Axis)
                continue;
            drawn.push_back(e);
            if (el.kind == sketch::ElementKind::Circle)
                continue;
            for (size_t p : el.Ends(s)) {
                const auto [it, fresh] = at.emplace(s.coordsOf[p], e);
                if (!fresh)
                    joined.Join(e, it->second);
            }
        }
        std::vector<std::vector<size_t>> out;
        std::unordered_map<size_t, size_t> index;
        for (size_t e : drawn) {
            const auto [it, fresh] = index.emplace(joined.Find(e), out.size());
            if (fresh)
                out.emplace_back();
            out[it->second].push_back(e);
        }
        return out;
    }

    template <class Store>
    Wire BasicSketch<Store>::PropagatedWire(const std::string &seed, Continuity c) {
        std::vector<std::string> ids = Expanded({seed});
        const size_t seeds = ids.size();
        const bool construction = IsConstruction(ids.front());
        for (const auto &[kind, of] : Elements())
            if (kind == "Line" || kind == "Arc" || kind == "Circle" || kind == "Spline" || kind == "BSpline" || kind == "Curve")
                for (const std::string &id : of)
                    if (IsConstruction(id) == construction &&
                        std::find(ids.begin(), ids.begin() + (std::ptrdiff_t)seeds, id) == ids.begin() + (std::ptrdiff_t)seeds)
                        ids.push_back(id);
        std::vector<EdgeList> chains;
        for (const std::string &id : ids)
            chains.push_back(ElementWire(id).Edges());
        std::vector<size_t> from(seeds);
        std::iota(from.begin(), from.end(), 0);
        std::vector<std::string> reached;
        for (size_t k : Propagated(chains, from, c))
            reached.push_back(ids[k]);
        return ElementsWire(reached);
    }

    template <class Store>
    std::optional<std::string> BasicSketch<Store>::ElementOfEdge(const std::string &id, size_t i) {
        for (const std::string &e : Expanded({id})) {
            const size_t n = ElementWire(e).Size();
            if (i < n)
                return e;
            i -= n;
        }
        return std::nullopt;
    }

}
