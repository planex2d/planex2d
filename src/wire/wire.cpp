// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/wire/wire.hpp"

#include <map>

namespace planex2d {

    // the edges stay what they are, views taken before stay valid
    void Wire::AssignIds(bool renumber) {
        auto kindOf = [](const Edge &e) -> std::string {
            return std::holds_alternative<Line>(e) ? "Line" : std::holds_alternative<Arc>(e) ? "Arc" : std::holds_alternative<Circle>(e) ? "Circle" : "Spline";
        };
        std::map<std::string, size_t> last;
        if (!renumber)
            for (const Edge &e : edges)
                if (const Properties &p = PropsOf(e); p && p->id)
                    last[kindOf(e)] = std::max(last[kindOf(e)], NumberAfter(*p->id, kindOf(e) + "."));
        for (Edge &e : edges) {
            const Properties p = PropsOf(e);
            if (!renumber && p && p->id)
                continue;
            const std::string kind = kindOf(e);
            SetProps(e, ChangedProperties(p, [&](ShapeProperties &q) { q.id = kind + "." + std::to_string(++last[kind]); }));
        }
    }

    void Wire::AssignVertexIds(bool renumber) {
        const std::vector<std::vector<size_t>> vertices = VertexEndpoints();
        const std::string prefix = "Vertex.";
        size_t last = 0;
        if (!renumber)
            for (const std::vector<size_t> &at : vertices)
                if (const Properties p = VertexProps(at); p && p->id)
                    last = std::max(last, NumberAfter(*p->id, prefix));
        for (const std::vector<size_t> &at : vertices) {
            const Properties p = VertexProps(at);
            if (!renumber && p && p->id)
                continue;
            const Properties q = ChangedProperties(p, [&](ShapeProperties &x) { x.id = prefix + std::to_string(++last); });
            for (size_t k : at)
                SetEndpointProps(k, q);
        }
    }

    Wire &Wire::Scale(const Vertex &c, double s, bool scaleMaxh, bool scaleMinh) {
        CheckScale(s);
        IsFinite("Scale", {c.x, c.y});
        for (Edge &e : edges)
            std::visit([&](auto &x) { x.Scale(c, s); }, e);
        if (!scaleMaxh && !scaleMinh)
            return *this;
        std::map<Properties, Properties> scaled;
        auto sized = [&](const Properties &p) {
            auto [it, isNew] = scaled.try_emplace(p);
            if (isNew)
                it->second = ScaledSizes(p, s, scaleMaxh, scaleMinh);
            return it->second;
        };
        for (Edge &e : edges) {
            SetProps(e, sized(PropsOf(e)));
            if (const Ends &x = EndsOf(e))
                SetEnds(e, MakeEnds(sized(x->start), sized(x->end)));
        }
        return *this;
    }

    // returns per edge now the index it had before (empty: nothing changed)
    std::vector<size_t> Wire::SplitCircles(const std::vector<std::vector<Vertex>> &at, double tol) {
        if (std::all_of(at.begin(), at.end(), [](const std::vector<Vertex> &p) { return p.empty(); }))
            return {};
        EdgeList split;
        std::vector<uint32_t> splitOwners;
        std::vector<size_t> from;
        auto put = [&](Edge e, size_t i) {
            split.push_back(std::move(e));
            from.push_back(i);
            if (!owners.empty())
                splitOwners.push_back(owners[i]);
        };
        for (size_t i = 0; i < edges.size(); ++i) {
            const Circle *c = std::get_if<Circle>(&edges[i]);
            if (c && !at[i].empty())
                for (Arc &arc : c->Arcs(at[i], tol))
                    put(std::move(arc), i);
            else
                put(edges[i], i);
        }
        edges = std::move(split);
        owners = std::move(splitOwners);
        ++changed.n;
        return from;
    }

    void Wire::SplitAt(const std::vector<std::vector<Vertex>> &at, double tol) {
        EdgeList split;
        std::vector<uint32_t> splitOwners;
        for (size_t i = 0; i < edges.size(); ++i) {
            if (at[i].empty())
                split.push_back(edges[i]);
            else
                std::visit([&](const auto &e) {
                    using E = std::decay_t<decltype(e)>;
                    if constexpr (std::is_same_v<E, Spline>) {
                        split.push_back(e);
                    } else if constexpr (IsCircle<E>) {
                        for (Arc &arc : e.Arcs(at[i], tol))
                            split.push_back(std::move(arc));
                    } else {
                        SplitPoints pts;
                        for (const Vertex &p : at[i])
                            pts.Add(e.Param(p), p);
                        for (E &piece : Split(e, pts, tol))
                            split.push_back(std::move(piece));
                    }
                }, edges[i]);
            if (!owners.empty())
                splitOwners.resize(split.size(), owners[i]);
        }
        edges = std::move(split);
        owners = std::move(splitOwners);
        ++changed.n;
    }

    // returns per edge now the index it had before (empty: none halved)
    std::vector<size_t> Wire::HalveClosedSplines(const std::vector<bool> &which) {
        bool any = false;
        for (size_t i = 0; i < edges.size() && !any; ++i)
            any = which[i] && IsClosedEdge(edges[i]) && std::holds_alternative<Spline>(edges[i]);
        if (!any)
            return {};
        EdgeList split;
        std::vector<uint32_t> splitOwners;
        std::vector<size_t> from;
        for (size_t i = 0; i < edges.size(); ++i) {
            const Spline *s = std::get_if<Spline>(&edges[i]);
            const bool halve = which[i] && s && s->IsClosed();
            for (size_t k = 0; k < (halve ? 2u : 1u); ++k) {
                split.push_back(halve ? Edge(s->Halves()[k]) : edges[i]);
                from.push_back(i);
                if (!owners.empty())
                    splitOwners.push_back(owners[i]);
            }
        }
        edges = std::move(split);
        owners = std::move(splitOwners);
        ++changed.n;
        return from;
    }

    std::optional<Axis> Wire::StraightLine() const {
        std::vector<Vector3> ends;
        for (const Edge &e : edges) {
            const Line *l = std::get_if<Line>(&e);
            if (!l)
                return std::nullopt;
            ends.push_back(axes.ToXYZ(l->Start()));
            ends.push_back(axes.ToXYZ(l->End()));
        }
        if (ends.empty() || !(Norm(ends[1] - ends[0]) > 0.0))
            return std::nullopt;
        const Axis line{ends[0], Normalized(ends[1] - ends[0])};
        double size = 0.0;
        for (const Vector3 &q : ends)
            size = std::max({size, std::abs(q.x), std::abs(q.y), std::abs(q.z)});
        for (const Vector3 &q : ends)
            if (Norm(Cross(q - line.origin, line.direction)) > planex2d::Tol(size))
                return std::nullopt;
        return line;
    }

}
