// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "shape/shape.hpp"

namespace planex2d {

    // a piece of the exact offset of a chain of lines, arcs and splines (edge k runs from vertex k to vertex k + 1, the
    // last edge of a closed chain back to vertex 0): the line or arc of edge k at the distance (Line, Arc), the offset of
    // spline k (Curve: the offsets of its arcs and lines), or the circle of that radius around vertex k (Vertex: a corner,
    // or at an open chain's end a cap), from start to end. side: +1 left of the chain, -1 right of it, 0 a cap; along: it
    // runs the way the chain does; at, heading: a point of a Curve and its direction there
    struct OffsetPiece {
        enum class Kind { Line, Arc, Vertex, Curve };
        Kind kind;
        size_t k;
        Vertex start, end, center;
        double r = 0.0;
        bool ccw = true;
        int side = 0;
        bool along = true;
        Vertex at{}, heading{};
    };
    // where a piece starts: the piece before and it touch at the foot of source vertex `vertex` (tangent), or cross
    struct OffsetJoint {
        bool tangent = false;
        size_t vertex = 0;
    };
    // pieces in order, joint i where piece i starts: a loop (closed), or a run of m pieces from joint 0 to joint m
    struct OffsetLoop {
        std::vector<OffsetPiece> pieces;
        std::vector<OffsetJoint> joints;
        bool closed = true;
    };

    // the unit normal to the left of a line, arc or spline at its point p
    inline Vertex LeftNormal(const Edge &e, const Vertex &p) {
        if (const Line *l = std::get_if<Line>(&e)) {
            const Vertex u = Normalized(l->End() - l->Start());
            return Vertex{-u.y, u.x};
        }
        if (const Spline *spline = std::get_if<Spline>(&e)) {
            const Vertex t = std::visit([&](const auto &x) { return x.template Tangent<false>(x.NearestPoint(p)); }, spline->NearestPiece(p));
            return Vertex{-t.y, t.x};
        }
        const Arc &a = std::get<Arc>(e);
        const Vertex out = Normalized(p - a.Center());
        return a.CCW() ? out * -1.0 : out;
    }
    // the area a closed chain encloses, positive counterclockwise
    inline double ChainArea(const EdgeList &chain) {
        const Vertex o = StartOf(chain.front());
        double a = 0.0;
        for (const Edge &e : chain)
            a += SignedArea(e, o);
        return a;
    }
    inline double PieceSweep(const OffsetPiece &p) {
        const Vertex a = p.start - p.center, b = p.end - p.center;
        const double t = std::atan2(Cross(a, b), Dot(a, b)) * (p.ccw ? 1.0 : -1.0);
        return t <= 0.0 ? t + 2.0 * std::numbers::pi : t;
    }
    // the largest coordinate the chain and its offset by d reach
    inline double OffsetScale(const EdgeList &chain, double d) {
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
    // the chain's edges, each with its index as its layer (the pieces of an offset keep it)
    inline Wire Tagged(const EdgeList &chain, const AxisSystem &axes) {
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
    // the distance from p to the chain
    inline double ChainDistance(const EdgeList &chain, const Vertex &p) {
        double nearest = std::numeric_limits<double>::infinity();
        for (const Edge &e : chain)
            nearest = std::min(nearest, Norm(p - NearestPointOf(e, p)));
        return nearest;
    }
    // whether p lies inside the closed chain
    inline bool InsideChain(const EdgeList &chain, const AxisSystem &axes, const Vertex &p, double tol, const std::string &call) {
        if (ChainDistance(chain, p) <= tol)
            throw std::invalid_argument(call + ": the point lies on the source and picks no side");
        return Tagged(chain, axes).Contains(p);
    }
    // the side of the open chain p lies on, +1 left of it, -1 right: of its nearest point there, at a vertex between two
    // edges by the sum of their normals
    inline int SideOfChain(const EdgeList &chain, const Vertex &p, double tol, const std::string &call) {
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

    inline size_t VertexAfter(size_t k, size_t n, bool closed) { return closed ? (k + 1) % n : k + 1; }
    inline Vertex ChainVertex(const EdgeList &chain, size_t v) { return v < chain.size() ? StartOf(chain[v]) : EndOf(chain.back()); }

    // the piece of a loop edge e of the offset, running from a to b if forward: on the line or arc of its source edge k,
    // or on a vertex circle (an edge of a spline's offset on neither belongs to its Curve)
    inline OffsetPiece PieceOf(const Edge &e, bool forward, size_t k, const EdgeList &chain, bool closed, double ad,
                               double tol, const std::string &call) {
        const Vertex a = StartOf(e), b = EndOf(e);
        const bool spline = std::holds_alternative<Spline>(chain[k]);
        OffsetPiece piece{OffsetPiece::Kind::Line, k, forward ? a : b, forward ? b : a, {}, 0.0, true};
        if (const Arc *arc = std::get_if<Arc>(&e)) {
            piece.center = arc->Center();
            piece.r = arc->Radius();
            piece.ccw = arc->CCW() == forward;
            struct Candidate {
                OffsetPiece::Kind kind;
                size_t k;
                Vertex c;
                double r;
            };
            std::vector<Candidate> candidates{{OffsetPiece::Kind::Vertex, k, StartOf(chain[k]), ad},
                                              {OffsetPiece::Kind::Vertex, VertexAfter(k, chain.size(), closed), EndOf(chain[k]), ad}};
            if (const Arc *source = std::get_if<Arc>(&chain[k])) {
                candidates.push_back({OffsetPiece::Kind::Arc, k, source->Center(), source->Radius() + ad});
                if (source->Radius() > ad)
                    candidates.push_back({OffsetPiece::Kind::Arc, k, source->Center(), source->Radius() - ad});
            }
            const auto off = [&](const Candidate &c) { return Norm(piece.center - c.c) + std::abs(piece.r - c.r); };
            const Candidate &best = *std::min_element(candidates.begin(), candidates.end(),
                                                      [&](const Candidate &x, const Candidate &y) { return off(x) < off(y); });
            // the Booleans snap ends by up to Tol, an arc's centre follows its ends
            if (spline && off(best) > 100.0 * tol)
                piece.kind = OffsetPiece::Kind::Curve;
            else if (off(best) > 100.0 * tol)
                throw std::logic_error(call + ": internal error, an arc of the exact offset lies on no offset circle");
            else {
                piece.kind = best.kind;
                piece.k = best.k;
            }
            if (piece.kind == OffsetPiece::Kind::Curve) {
                piece.at = arc->MidPoint();
                piece.heading = arc->Tangent<false>(piece.at) * (forward ? 1.0 : -1.0);
            }
        } else if (spline) {
            piece.kind = OffsetPiece::Kind::Curve;
            piece.at = (a + b) * 0.5;
            piece.heading = forward ? b - a : a - b;
        } else if (!std::holds_alternative<Line>(chain[k]))
            throw std::logic_error(call + ": internal error, a line of the exact offset belongs to an arc");
        return piece;
    }

    // the pieces turned to run counterclockwise if ccw, pieces on one line, arc or circle merged, a corner too short to
    // tell from rounding dropped
    inline std::vector<OffsetPiece> Merged(std::vector<OffsetPiece> pieces, const std::vector<char> &straight, bool ccw, double tol,
                                           const std::string &call) {
        double area = 0.0;
        for (size_t i = 0; i < pieces.size(); ++i) {
            const OffsetPiece &p = pieces[i];
            area += straight[i] ? Line(p.start, p.end).SignedArea(pieces.front().start)
                                : Arc(p.start, p.end, p.center, p.ccw).SignedArea(pieces.front().start);
        }
        if ((area > 0.0) != ccw) {
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
            if (merged[i].kind == OffsetPiece::Kind::Vertex && merged[i].r * PieceSweep(merged[i]) <= tol) {
                const Vertex end = merged[(i + merged.size() - 1) % merged.size()].end;
                merged.erase(merged.begin() + (std::ptrdiff_t)i);
                merged[i % merged.size()].start = end;
            } else
                ++i;
        if (merged.size() < 2)
            throw std::invalid_argument(call + ": a loop of the offset is one piece, which a sketch cannot draw");
        return merged;
    }

    // the side of each piece and its direction against the chain's
    inline void SetSides(std::vector<OffsetPiece> &pieces, const EdgeList &chain, bool closed, double ad) {
        const size_t n = chain.size();
        for (OffsetPiece &p : pieces) {
            if (p.kind == OffsetPiece::Kind::Curve) {
                // of a point on it: a curved piece's chord can pass the source on its other side
                const Vertex foot = NearestPointOf(chain[p.k], p.at), normal = LeftNormal(chain[p.k], foot);
                p.side = Dot(p.at - foot, normal) > 0.0 ? 1 : -1;
                p.along = Dot(p.heading, Vertex{normal.y, -normal.x}) > 0.0;
            } else if (p.kind != OffsetPiece::Kind::Vertex) {
                const Edge &source = chain[p.k];
                if (const Arc *arc = std::get_if<Arc>(&source)) {
                    p.side = (p.r > arc->Radius()) == arc->CCW() ? -1 : 1;
                    p.along = p.ccw == arc->CCW();
                } else {
                    const Vertex u = EndOf(source) - StartOf(source);
                    p.side = Cross(u, (p.start + p.end) * 0.5 - StartOf(source)) > 0.0 ? 1 : -1;
                    p.along = Dot(p.end - p.start, u) > 0.0;
                }
            } else if (closed || (p.k > 0 && p.k < n)) {
                const Vertex v = ChainVertex(chain, p.k), in = LeftNormal(chain[closed ? (p.k + n - 1) % n : p.k - 1], v);
                const double half = 0.5 * PieceSweep(p) * (p.ccw ? 1.0 : -1.0);
                const Vertex mid = Rotated(p.start, p.center, std::cos(half), std::sin(half));
                p.side = Dot(mid - v, in + LeftNormal(chain[p.k % n], v)) > 0.0 ? 1 : -1;
                const Vertex from = v + in * (p.side * ad);
                p.along = Norm(p.start - from) <= Norm(p.end - from);
            }
        }
    }

    // joint i where piece i starts: tangent where the pieces touch at the foot of a source vertex
    inline std::vector<OffsetJoint> JointsOf(const std::vector<OffsetPiece> &pieces, const EdgeList &chain, bool closed,
                                             double ad, double tol) {
        const size_t count = pieces.size(), n = chain.size();
        std::vector<OffsetJoint> joints;
        for (size_t i = 0; i < count; ++i) {
            const OffsetPiece &a = pieces[(i + count - 1) % count], &b = pieces[i];
            const bool av = a.kind == OffsetPiece::Kind::Vertex, bv = b.kind == OffsetPiece::Kind::Vertex;
            const auto endsAt = [&](const OffsetPiece &p, size_t v) { return p.k == v || VertexAfter(p.k, n, closed) == v; };
            OffsetJoint j;
            if (av != bv) {
                const OffsetPiece &around = av ? a : b, &other = av ? b : a;
                if (endsAt(other, around.k))
                    j = {true, around.k};
            } else if (!av) {
                // two edges' offsets touch where the chain runs on smoothly through the vertex between the edges
                const std::optional<size_t> v = VertexAfter(a.k, n, closed) == b.k   ? std::optional<size_t>(b.k)
                                                : VertexAfter(b.k, n, closed) == a.k ? std::optional<size_t>(a.k)
                                                                                     : std::nullopt;
                if (v && a.k != b.k) {
                    const Vertex p = ChainVertex(chain, *v);
                    if (ad * Norm(LeftNormal(chain[a.k], p) - LeftNormal(chain[b.k], p)) <= tol)
                        j = {true, *v};
                }
            }
            joints.push_back(j);
        }
        return joints;
    }

    // the pieces of one loop of an exact offset of the chain by d, in the loop's order, from the piece of the first edge
    // on: each edge mapped to the chain edge it comes from (its layer). ccw: the loop runs counterclockwise
    inline OffsetLoop LoopPieces(const Wire &loop, const EdgeList &chain, bool closed, double d, bool ccw, double tol,
                                 const std::string &call) {
        const size_t n = chain.size(), m = loop.Size();
        const double ad = std::abs(d);
        const auto sourceOf = [&](const Edge &e) {
            const Properties &p = PropsOf(e);
            if (!p || !p->layer || *p->layer < 0 || (size_t)*p->layer >= n)
                throw std::logic_error(call + ": internal error, an edge of the exact offset lost its source");
            return (size_t)*p->layer;
        };
        std::vector<OffsetPiece> pieces;
        std::vector<char> straight;
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
            pieces.push_back(PieceOf(e, forward, sourceOf(e), chain, closed, ad, tol, call));
            straight.push_back(std::holds_alternative<Line>(e));
        }
        std::vector<OffsetPiece> merged = Merged(std::move(pieces), straight, ccw, tol, call);
        SetSides(merged, chain, closed, ad);
        // each edge's piece before the corner at its end
        const auto order = [&](const OffsetPiece &p) {
            const bool around = p.kind == OffsetPiece::Kind::Vertex;
            return 2 * (around && closed && p.k == 0 ? n : p.k) + (around ? 0 : 1);
        };
        std::rotate(merged.begin(), std::min_element(merged.begin(), merged.end(), [&](const OffsetPiece &x, const OffsetPiece &y) {
                        return order(x) < order(y);
                    }), merged.end());
        OffsetLoop out;
        out.joints = JointsOf(merged, chain, closed, ad, tol);
        out.pieces = std::move(merged);
        return out;
    }

    // the loops of the exact offset of the closed chain (its edges in order, each starting where the one before ends) by
    // d, d > 0 outside, d < 0 inside, as Face.Offset: the face of the chain with its band added or taken away. Every piece
    // runs the way its edge does; nothing if nothing is left
    inline std::vector<OffsetLoop> ClosedOffsetLoops(const EdgeList &chain, double d, const AxisSystem &axes, const std::string &call) {
        const Wire w = Tagged(chain, axes);
        const FaceList faces = WireList{w}.Region().Faces();
        if (faces.size() != 1)
            throw std::invalid_argument(call + ": the chain crosses itself (it bounds " + std::to_string(faces.size()) + " regions)");
        const Shape region(faces.front()), band(w.Offset(std::abs(d)), nullptr);
        const Shape result = d > 0.0 ? Unify({region, band}) : Subtract({region, band});
        // the pieces run as their edges do: around a counterclockwise chain the result's outer loops counterclockwise
        const bool chainCcw = ChainArea(chain) > 0.0;
        const double tol = Tol(OffsetScale(chain, d));
        std::vector<OffsetLoop> out;
        for (const Face &f : result.Faces()) {
            out.push_back(LoopPieces(f.Outer(), chain, true, d, chainCcw, tol, call));
            for (const Wire &hole : f.Holes())
                out.push_back(LoopPieces(hole, chain, true, d, !chainCcw, tol, call));
        }
        return out;
    }

    // the loops of the exact band of width 2 |d| around the open chain (Wire.Offset): the offsets of its edges on both
    // sides, the corners, the caps around its two ends
    inline std::vector<OffsetLoop> BandLoops(const EdgeList &chain, double d, const AxisSystem &axes, const std::string &call) {
        const Shape band(Tagged(chain, axes).Offset(std::abs(d)), nullptr);
        const double tol = Tol(OffsetScale(chain, d));
        std::vector<OffsetLoop> out;
        for (const Face &f : band.Faces()) {
            out.push_back(LoopPieces(f.Outer(), chain, false, d, true, tol, call));
            for (const Wire &hole : f.Holes())
                out.push_back(LoopPieces(hole, chain, false, d, false, tol, call));
        }
        return out;
    }

    // the part of an open chain's band on one side (+1 left, -1 right): its loops all on that side, and the run from the
    // offset of the chain's start to that of its end (n its edges), running the way the chain does; nothing if nothing
    // is left there
    inline std::vector<OffsetLoop> BandSide(const std::vector<OffsetLoop> &band, int side, size_t n, const std::string &call) {
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
            for (size_t i = 0; i < m; ++i) {
                if (!on(i) || on(i + m - 1))
                    continue;
                ++runs;
                OffsetLoop run;
                run.closed = false;
                run.joints.push_back(loop.joints[i]);
                size_t j = i;
                for (; on(j); ++j) {
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
                out.push_back(std::move(run));
            }
        }
        if (runs > 1)
            throw std::invalid_argument(call + ": that side of the offset falls into pieces, which an offset cannot hold yet");
        return out;
    }

}
