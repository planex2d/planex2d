// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

#include "planex/shape/shape.hpp"
#include "planex/offset/offsetpath.hpp"

namespace planex2d {

    // k: the chain's edge k (from vertex k to vertex k + 1), or vertex k (Vertex, CornerLine, CapLine); side: +1 left of
    // the chain, -1 right of it, 0 a cap; at, heading: a point of a Curve piece and its direction there
    struct OffsetPiece {
        enum class Kind { Line, Arc, Vertex, Curve, CornerLine, CapLine, SpurBefore, SpurAfter };
        Kind kind;
        size_t k;
        Vertex start, end, center;
        double r = 0.0;
        bool ccw = true;
        int side = 0;
        bool along = true;
        Vertex at{}, heading{};
    };
    struct OffsetJoint {
        bool tangent = false;
        size_t vertex = 0;
    };
    // joint i is where piece i starts; an open run of m pieces has the joints 0 to m
    struct OffsetLoop {
        std::vector<OffsetPiece> pieces;
        std::vector<OffsetJoint> joints;
        bool closed = true;
    };

    inline double PieceSweep(const OffsetPiece &p) {
        const Vertex a = p.start - p.center, b = p.end - p.center;
        const double t = std::atan2(Cross(a, b), Dot(a, b)) * (p.ccw ? 1.0 : -1.0);
        return t <= 0.0 ? t + 2.0 * std::numbers::pi : t;
    }
    double OffsetScale(const EdgeList &chain, double d);
    Wire Tagged(const EdgeList &chain, const AxisSystem &axes);
    inline double ChainDistance(const EdgeList &chain, const Vertex &p) {
        double nearest = std::numeric_limits<double>::infinity();
        for (const Edge &e : chain)
            nearest = std::min(nearest, Norm(p - NearestPointOf(e, p)));
        return nearest;
    }
    inline bool InsideChain(const EdgeList &chain, const AxisSystem &axes, const Vertex &p, double tol, const std::string &call) {
        if (ChainDistance(chain, p) <= tol)
            throw std::invalid_argument(call + ": the point lies on the source and picks no side");
        return Tagged(chain, axes).Contains(p);
    }
    int SideOfChain(const EdgeList &chain, const Vertex &p, double tol, const std::string &call);

    inline size_t VertexAfter(size_t k, size_t n, bool closed) { return closed ? (k + 1) % n : k + 1; }
    inline Vertex ChainVertex(const EdgeList &chain, size_t v) { return v < chain.size() ? StartOf(chain[v]) : EndOf(chain.back()); }

    OffsetPiece PieceOf(const Edge &e, bool forward, int tag, const EdgeList &chain);
    inline bool IsStraightPiece(OffsetPiece::Kind k) { return k == OffsetPiece::Kind::CornerLine || k == OffsetPiece::Kind::CapLine || k == OffsetPiece::Kind::SpurBefore || k == OffsetPiece::Kind::SpurAfter; }
    inline bool IsEdgePiece(OffsetPiece::Kind k) { return k == OffsetPiece::Kind::Line || k == OffsetPiece::Kind::Arc || k == OffsetPiece::Kind::Curve; }

    std::vector<OffsetPiece> Merged(std::vector<OffsetPiece> pieces, bool reversed, double tol, const std::string &call);

    void SetSides(std::vector<OffsetPiece> &pieces, const EdgeList &chain, bool closed, double ad);

    std::vector<OffsetJoint> JointsOf(const std::vector<OffsetPiece> &pieces, const EdgeList &chain, bool closed,
                                      double ad, double tol);

    OffsetLoop LoopPieces(const Wire &loop, const EdgeList &chain, bool closed, double d, bool ccw, double tol,
                          const std::string &call);

    std::vector<OffsetLoop> ClosedOffsetLoops(const EdgeList &chain, double d, OffsetCorner corners, const AxisSystem &axes,
                                              const std::string &call);

    std::vector<OffsetLoop> BandLoops(const EdgeList &chain, double d, OffsetEnd ends, OffsetCorner corners, const AxisSystem &axes,
                                      const std::string &call);

    std::vector<OffsetLoop> BandSide(const std::vector<OffsetLoop> &band, int side, size_t n, const std::string &call);

}
