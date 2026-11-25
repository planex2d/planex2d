// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <optional>
#include <utility>
#include <string>
#include <vector>

#include "planex/shape/shape.hpp"
#include "planex/core/disjointsets.hpp"

namespace planex2d {

    inline std::string LengthText(double d) { return NumberText(d, 3); }

    inline size_t ChainCount(const std::vector<size_t> &id, size_t nv, size_t edges) {
        DisjointSets chain(nv);
        for (size_t i = 0; i < edges; ++i)
            chain.Join(id[2 * i], id[2 * i + 1]);
        size_t count = 0;
        for (size_t v = 0; v < nv; ++v)
            count += chain.IsRoot(v);
        return count;
    }

    inline std::string BranchError(const std::string &call, size_t ends, const Vertex &p) {
        return call + ": " + std::to_string(ends) + " edge ends meet at " + PointText(p) +
               "; a wire is one chain without branches (an 8 is two wires): px.Wires(edges) returns the chains as a list";
    }

    // each edge's index and whether the chain runs along its direction (enters it at its start)
    struct ChainWalk {
        std::vector<std::pair<size_t, bool>> edges;
        bool closed = false;
    };
    ChainWalk WalkChain(const EdgeList &edges, double tol, const std::string &call, const std::string &hint);

    Wire OrderChain(const EdgeList &edges, const AxisSystem &axes, double tol, const std::string &call,
                    const std::string &hint = "px.Wires(edges) returns them as a list, tol= joins ends further apart");

    inline std::string EdgeText(const Edge &e) {
        return "the edge from " + PointText(Wire::EndOf(e, false)) + " to " + PointText(Wire::EndOf(e, true));
    }

    inline Wire ChainOf(const EdgeList &edges, const AxisSystem &axes, std::optional<double> tol, const std::string &call) {
        Wire w = OrderChain(edges, axes, edges.JoinTol(tol), call);
        w.CheckChain(call);
        return w;
    }

    struct FreeEnd {
        Vertex point;
        size_t edge;
        bool atEnd;
    };
    struct FreeEnds {
        FreeEnd start, end;
        bool closed;
    };

    FreeEnds FreeEndsOf(const EdgeList &edges);

    std::vector<std::pair<size_t, Vertex>> SelfCrossings(const Spline &s);

    void AddUncrossed(Wire &c, const Edge &e);

    WireList WiresOf(const EdgeList &edges, const AxisSystem &axes, std::optional<double> tol);

}
