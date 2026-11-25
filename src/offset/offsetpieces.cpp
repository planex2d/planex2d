// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/offset/offsetpieces.hpp"

#include <optional>
#include <variant>

namespace planex2d {

    double OffsetScale(const EdgeList &chain, double d) {
        double scale = std::abs(d);
        for (const Edge &e : chain) {
            scale = std::max({scale, NormInf(StartOf(e)) + std::abs(d), NormInf(EndOf(e)) + std::abs(d)});
            if (const Arc *a = std::get_if<Arc>(&e))
                scale = std::max(scale, NormInf(a->Center()) + a->Radius() + std::abs(d));
            if (const Spline *spline = std::get_if<Spline>(&e))
                scale = std::max(scale, spline->Box().NormInf() + std::abs(d));
        }
        return scale;
    }

    Wire Tagged(const EdgeList &chain, const AxisSystem &axes) {
        Wire w(axes);
        for (size_t k = 0; k < chain.size(); ++k) {
            Edge e = chain[k];
            std::visit([&](auto &x) {
                x.props = ChangedProperties(nullptr, [&](ShapeProperties &q) { q.layer = (int)k; });
                x.ends = nullptr;
            }, e);
            w.Add(std::move(e));
        }
        return w;
    }

    // +1 left of the chain, -1 right of it
    int SideOfChain(const EdgeList &chain, const Vertex &p, double tol, const std::string &call) {
        size_t best = 0;
        Vertex foot;
        double nearest = std::numeric_limits<double>::infinity();
        for (size_t k = 0; k < chain.size(); ++k)
            if (const Vertex q = NearestPointOf(chain[k], p); Norm(p - q) < nearest) {
                nearest = Norm(p - q);
                foot = q;
                best = k;
            }
        if (nearest <= tol)
            throw std::invalid_argument(call + ": the point lies on the source and picks no side");
        Vertex normal = LeftNormal(chain[best], foot);
        if (best > 0 && foot == StartOf(chain[best]))
            normal = normal + LeftNormal(chain[best - 1], foot);
        else if (best + 1 < chain.size() && foot == EndOf(chain[best]))
            normal = normal + LeftNormal(chain[best + 1], foot);
        const double s = Dot(p - foot, normal);
        if (std::abs(s) <= tol)
            throw std::invalid_argument(call + ": the point lies ahead of an end of the source and picks no side");
        return s > 0.0 ? 1 : -1;
    }

    OffsetPiece PieceOf(const Edge &e, bool forward, int tag, const EdgeList &chain) {
        const Vertex a = StartOf(e), b = EndOf(e);
        const size_t k = IndexOfTag(tag);
        OffsetPiece piece{OffsetPiece::Kind::Line, k, forward ? a : b, forward ? b : a, {}, 0.0, true};
        const Arc *arc = std::get_if<Arc>(&e);
        if (arc) {
            piece.center = arc->Center();
            piece.r = arc->Radius();
            piece.ccw = arc->CCW() == forward;
        }
        switch (SourceOfTag(tag)) {
        case OffsetSource::Edge:
            if (std::holds_alternative<Spline>(chain[k])) {
                piece.kind = OffsetPiece::Kind::Curve;
                piece.at = MidPointOf(e);
                const Vertex n = LeftNormal(e, piece.at);
                piece.heading = Vertex{n.y, -n.x} * (forward ? 1.0 : -1.0);
            } else
                piece.kind = arc ? OffsetPiece::Kind::Arc : OffsetPiece::Kind::Line;
            break;
        case OffsetSource::Corner: piece.kind = arc ? OffsetPiece::Kind::Vertex : OffsetPiece::Kind::CornerLine; break;
        case OffsetSource::Cap: piece.kind = arc ? OffsetPiece::Kind::Vertex : OffsetPiece::Kind::CapLine; break;
        case OffsetSource::SpurBefore: piece.kind = OffsetPiece::Kind::SpurBefore; break;
        case OffsetSource::SpurAfter: piece.kind = OffsetPiece::Kind::SpurAfter; break;
        }
        return piece;
    }

    std::vector<OffsetPiece> Merged(std::vector<OffsetPiece> pieces, bool reversed, double tol, const std::string &call) {
        if (reversed) {
            std::reverse(pieces.begin(), pieces.end());
            for (OffsetPiece &p : pieces) {
                std::swap(p.start, p.end);
                p.ccw = !p.ccw;
                p.heading = p.heading * -1.0;
            }
        }
        std::vector<OffsetPiece> merged;
        for (const OffsetPiece &p : pieces)
            if (!merged.empty() && merged.back().kind == p.kind && merged.back().k == p.k)
                merged.back().end = p.end;
            else
                merged.push_back(p);
        if (merged.size() > 1 && merged.front().kind == merged.back().kind && merged.front().k == merged.back().k) {
            merged.front().start = merged.back().start;
            merged.pop_back();
        }
        // a corner at a vertex the chain runs through smoothly (within rounding): the pieces beside it touch there
        for (size_t i = 0; i < merged.size() && merged.size() > 1;)
            if ((merged[i].kind == OffsetPiece::Kind::Vertex && merged[i].r * PieceSweep(merged[i]) <= tol) ||
                (IsStraightPiece(merged[i].kind) && Norm(merged[i].end - merged[i].start) <= tol)) {
                const Vertex end = merged[(i + merged.size() - 1) % merged.size()].end;
                merged.erase(merged.begin() + (std::ptrdiff_t)i);
                merged[i % merged.size()].start = end;
            } else
                ++i;
        if (merged.size() < 2)
            throw std::invalid_argument(call + ": a loop of the offset is one piece, which a sketch cannot draw");
        return merged;
    }

    void SetSides(std::vector<OffsetPiece> &pieces, const EdgeList &chain, bool closed, double ad) {
        const size_t n = chain.size();
        for (OffsetPiece &p : pieces) {
            if (p.kind == OffsetPiece::Kind::Curve) {
                // of a point on it: a curved piece's chord can pass the source on its other side
                const Vertex foot = NearestPointOf(chain[p.k], p.at), normal = LeftNormal(chain[p.k], foot);
                p.side = Dot(p.at - foot, normal) > 0.0 ? 1 : -1;
                p.along = Dot(p.heading, Vertex{normal.y, -normal.x}) > 0.0;
            } else if (IsEdgePiece(p.kind)) {
                const Edge &source = chain[p.k];
                if (const Arc *arc = std::get_if<Arc>(&source)) {
                    p.side = (p.r > arc->Radius()) == arc->CCW() ? -1 : 1;
                    p.along = p.ccw == arc->CCW();
                } else {
                    const Vertex u = EndOf(source) - StartOf(source);
                    p.side = Cross(u, (p.start + p.end) * 0.5 - StartOf(source)) > 0.0 ? 1 : -1;
                    p.along = Dot(p.end - p.start, u) > 0.0;
                }
            } else if (p.kind == OffsetPiece::Kind::SpurBefore || p.kind == OffsetPiece::Kind::SpurAfter) {
                const bool after = p.kind == OffsetPiece::Kind::SpurAfter;
                const Vertex v = after ? EndOf(chain[p.k]) : StartOf(chain[p.k]), normal = LeftNormal(chain[p.k], v);
                p.side = Dot((p.start + p.end) * 0.5 - v, normal) > 0.0 ? 1 : -1;
                p.along = Dot(p.end - p.start, Vertex{normal.y, -normal.x}) > 0.0;
            } else if (p.kind != OffsetPiece::Kind::CapLine && (closed || (p.k > 0 && p.k < n))) {
                const Vertex v = ChainVertex(chain, p.k), in = LeftNormal(chain[closed ? (p.k + n - 1) % n : p.k - 1], v);
                Vertex mid = (p.start + p.end) * 0.5;
                if (p.kind == OffsetPiece::Kind::Vertex) {
                    const double half = 0.5 * PieceSweep(p) * (p.ccw ? 1.0 : -1.0);
                    mid = Rotated(p.start, p.center, std::cos(half), std::sin(half));
                }
                p.side = Dot(mid - v, in + LeftNormal(chain[p.k % n], v)) > 0.0 ? 1 : -1;
                const Vertex from = v + in * (p.side * ad);
                p.along = Norm(p.start - from) <= Norm(p.end - from);
            }
        }
    }

    std::vector<OffsetJoint> JointsOf(const std::vector<OffsetPiece> &pieces, const EdgeList &chain, bool closed,
                                      double ad, double tol) {
        const size_t count = pieces.size(), n = chain.size();
        std::vector<OffsetJoint> joints;
        for (size_t i = 0; i < count; ++i) {
            const OffsetPiece &a = pieces[(i + count - 1) % count], &b = pieces[i];
            const bool av = a.kind == OffsetPiece::Kind::Vertex, bv = b.kind == OffsetPiece::Kind::Vertex;
            const auto endsAt = [&](const OffsetPiece &p, size_t v) { return p.k == v || VertexAfter(p.k, n, closed) == v; };
            OffsetJoint j;
            if (IsStraightPiece(a.kind) || IsStraightPiece(b.kind)) {
                const auto vertices = [&](const OffsetPiece &p) {
                    return IsEdgePiece(p.kind) || p.kind == OffsetPiece::Kind::SpurBefore || p.kind == OffsetPiece::Kind::SpurAfter
                               ? std::vector<size_t>{p.k, VertexAfter(p.k, n, closed)}
                               : std::vector<size_t>{p.k};
                };
                for (size_t v : vertices(a))
                    for (size_t w : vertices(b))
                        if (v == w && std::abs(Norm(b.start - ChainVertex(chain, v)) - ad) <= tol)
                            j = {true, v};
            } else if (av != bv) {
                const OffsetPiece &around = av ? a : b, &other = av ? b : a;
                if (endsAt(other, around.k))
                    j = {true, around.k};
            } else if (!av) {
                const std::optional<size_t> v = VertexAfter(a.k, n, closed) == b.k   ? std::optional<size_t>(b.k)
                                                : VertexAfter(b.k, n, closed) == a.k ? std::optional<size_t>(a.k)
                                                                                     : std::nullopt;
                if (v && a.k != b.k) {
                    const Vertex p = ChainVertex(chain, *v);
                    if (ad * Norm(ExactLeftNormal(chain[a.k], p) - ExactLeftNormal(chain[b.k], p)) <= tol)
                        j = {true, *v};
                }
            }
            joints.push_back(j);
        }
        return joints;
    }

    OffsetLoop LoopPieces(const Wire &loop, const EdgeList &chain, bool closed, double d, bool ccw, double tol,
                          const std::string &call) {
        const size_t n = chain.size(), m = loop.Size();
        const double ad = std::abs(d);
        const auto tagOf = [&](const Edge &e) {
            const Properties &p = PropsOf(e);
            if (!p || !p->layer || *p->layer < 0 || IndexOfTag(*p->layer) > n)
                throw std::logic_error(call + ": internal error, an edge of the exact offset lost its source");
            return *p->layer;
        };
        std::vector<OffsetPiece> pieces;
        double area = 0.0;
        Vertex at;
        for (size_t i = 0; i < m; ++i) {
            const Edge &e = loop[i];
            const Vertex a = StartOf(e), b = EndOf(e);
            bool forward;
            if (i > 0)
                forward = Norm(at - a) <= Norm(at - b);
            else if (m > 1) {
                const Vertex p = StartOf(loop[1]), q = EndOf(loop[1]);
                forward = std::min(Norm(b - p), Norm(b - q)) <= std::min(Norm(a - p), Norm(a - q));
            } else
                forward = true;
            at = forward ? b : a;
            pieces.push_back(PieceOf(e, forward, tagOf(e), chain));
            area += SignedArea(forward ? e : ReversedOf(e), StartOf(loop[0]));
        }
        std::vector<OffsetPiece> merged = Merged(std::move(pieces), (area > 0.0) != ccw, tol, call);
        SetSides(merged, chain, closed, ad);
        // along the chain: a corner or cap at vertex v, then the spur before edge v, edge v, the spur after it
        const auto order = [&](const OffsetPiece &p) {
            const bool around = p.kind == OffsetPiece::Kind::Vertex || p.kind == OffsetPiece::Kind::CornerLine || p.kind == OffsetPiece::Kind::CapLine;
            const size_t base = 4 * (around && closed && p.k == 0 ? n : p.k);
            return around ? base : base + (p.kind == OffsetPiece::Kind::SpurBefore ? 1 : p.kind == OffsetPiece::Kind::SpurAfter ? 3 : 2);
        };
        std::rotate(merged.begin(), std::min_element(merged.begin(), merged.end(), [&](const OffsetPiece &x, const OffsetPiece &y) {
                        return order(x) < order(y);
                    }), merged.end());
        OffsetLoop out;
        out.joints = JointsOf(merged, chain, closed, ad, tol);
        out.pieces = std::move(merged);
        return out;
    }

    // d > 0 outside, < 0 inside
    std::vector<OffsetLoop> ClosedOffsetLoops(const EdgeList &chain, double d, OffsetCorner corners, const AxisSystem &axes,
                                              const std::string &call) {
        const Wire w = Tagged(chain, axes);
        const FaceList faces = WireList{w}.Region().Faces();
        if (faces.size() != 1)
            throw std::invalid_argument(call + ": the chain crosses itself (it bounds " + std::to_string(faces.size()) + " regions)");
        const double tol = Tol(OffsetScale(chain, d));
        const Shape result(TaggedLoopOffset(chain, d, corners, axes, tol), nullptr);
        // the pieces run as their edges do: around a counterclockwise chain the result's outer loops counterclockwise
        const bool chainCcw = ChainArea(chain) > 0.0;
        std::vector<OffsetLoop> out;
        for (const Face &f : result.Faces()) {
            out.push_back(LoopPieces(f.Outer(), chain, true, d, chainCcw, tol, call));
            for (const Wire &hole : f.Holes())
                out.push_back(LoopPieces(hole, chain, true, d, !chainCcw, tol, call));
        }
        return out;
    }

    std::vector<OffsetLoop> BandLoops(const EdgeList &chain, double d, OffsetEnd ends, OffsetCorner corners, const AxisSystem &axes,
                                      const std::string &call) {
        const double tol = Tol(OffsetScale(chain, d));
        const Shape band(TaggedBand(chain, std::abs(d), ends, corners, axes, tol), nullptr);
        std::vector<OffsetLoop> out;
        for (const Face &f : band.Faces()) {
            out.push_back(LoopPieces(f.Outer(), chain, false, d, true, tol, call));
            for (const Wire &hole : f.Holes())
                out.push_back(LoopPieces(hole, chain, false, d, false, tol, call));
        }
        return out;
    }

    static OffsetLoop SideRun(const OffsetLoop &loop, size_t i, int side, size_t n, const std::string &call) {
        const size_t m = loop.pieces.size();
        OffsetLoop run;
        run.closed = false;
        run.joints.push_back(loop.joints[i]);
        size_t j = i;
        for (; loop.pieces[j % m].side == side; ++j) {
            run.pieces.push_back(loop.pieces[j % m]);
            run.joints.push_back(loop.joints[(j + 1) % m]);
        }
        const auto cap = [&](size_t at, size_t v) {
            const OffsetPiece &p = loop.pieces[at % m];
            return p.kind == OffsetPiece::Kind::Vertex && p.side == 0 && p.k == v;
        };
        const bool along = cap(i + m - 1, 0) && cap(j, n), against = cap(i + m - 1, n) && cap(j, 0);
        if (!(along || against) || !run.joints.front().tangent || !run.joints.back().tangent)
            throw std::invalid_argument(call + ": that side of the offset does not run from the chain's start to its end, which an "
                                               "offset cannot hold yet");
        if (against) {
            std::reverse(run.pieces.begin(), run.pieces.end());
            for (OffsetPiece &p : run.pieces) {
                std::swap(p.start, p.end);
                p.ccw = !p.ccw;
                p.along = !p.along;
            }
            std::reverse(run.joints.begin(), run.joints.end());
        }
        return run;
    }

    // side: +1 left of the chain, -1 right of it
    std::vector<OffsetLoop> BandSide(const std::vector<OffsetLoop> &band, int side, size_t n, const std::string &call) {
        std::vector<OffsetLoop> out;
        size_t runs = 0;
        for (const OffsetLoop &loop : band) {
            const size_t m = loop.pieces.size();
            const auto on = [&](size_t i) { return loop.pieces[i % m].side == side; };
            size_t count = 0;
            for (size_t i = 0; i < m; ++i)
                count += on(i);
            if (count == m)
                out.push_back(loop);
            if (count == 0 || count == m)
                continue;
            for (size_t i = 0; i < m; ++i)
                if (on(i) && !on(i + m - 1)) {
                    ++runs;
                    out.push_back(SideRun(loop, i, side, n, call));
                }
        }
        if (runs > 1)
            throw std::invalid_argument(call + ": that side of the offset falls into pieces, which a sketch's offset cannot hold; "
                                        "px.Offset(sketch.Wires(), d) offsets the chain as wires");
        return out;
    }

}
