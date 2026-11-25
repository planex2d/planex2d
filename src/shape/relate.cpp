// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/shape/relate.hpp"

namespace planex2d {

    std::vector<Vertex> VerticesOf(const Arrangement &r, unsigned char m) {
        std::vector<Vertex> out;
        for (size_t k = 0; k < r.c.Size(); ++k)
            if (!r.state[k].excluded && (r.state[k].mask & m) != 0) {
                out.push_back(StartOf(r.c[k]));
                out.push_back(EndOf(r.c[k]));
            }
        std::sort(out.begin(), out.end(), VertexLess);
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    }

    // a or b empty: the other's interior and boundary lie in its exterior
    RelateMatrix RelateEmpty(const RelateOperand &other, bool otherIsA) {
        RelateMatrix m;
        m.Raise(2, 2, 2);
        if (!other.Empty()) {
            m.Raise(2, 0, (signed char)other.dim);
            if (other.dim == 2)
                m.Raise(2, 1, 1);
            else if (other.dim == 1 && !OddEnds(other.edges, [](size_t) { return true; }).empty())
                m.Raise(2, 1, 0);
        }
        return otherIsA ? m.Transposed() : m;
    }

    RelateMatrix RelateRegions(const RelateOperand &a, const RelateOperand &b) {
        RelateMatrix m;
        const BBox boxa = a.edges.Box(), boxb = b.edges.Box();
        const Arrangement r = Arranged(a.edges, b.edges, boxa, boxb, Tol(std::max(boxa.NormInf(), boxb.NormInf())), true);
        bool shared = false;
        for (size_t k = 0; k < r.c.Size(); ++k) {
            if (r.state[k].excluded)
                continue;
            const PieceSide side = r.side[k];
            switch (r.state[k].mask) {
                case 3:
                    shared = true;
                    m.Raise(1, 1, 1);
                    if (side == PieceSide::SharedSame) {
                        m.Raise(0, 0, 2);
                    } else {
                        m.Raise(0, 2, 2);
                        m.Raise(2, 0, 2);
                    }
                    break;
                case 1:
                    if (side == PieceSide::Inside) {
                        m.Raise(0, 0, 2);
                        m.Raise(1, 0, 1);
                        m.Raise(2, 0, 2);
                    } else {
                        m.Raise(0, 2, 2);
                        m.Raise(1, 2, 1);
                    }
                    break;
                default:
                    if (side == PieceSide::Inside) {
                        m.Raise(0, 0, 2);
                        m.Raise(0, 1, 1);
                        m.Raise(0, 2, 2);
                    } else {
                        m.Raise(2, 0, 2);
                        m.Raise(2, 1, 1);
                    }
            }
        }
        if (!shared) {
            const std::vector<Vertex> va = VerticesOf(r, 1);
            for (const Vertex &v : VerticesOf(r, 2))
                if (HasVertex(va, v)) {
                    m.Raise(1, 1, 0);
                    break;
                }
        }
        return m;
    }

    // a a region, b lines
    RelateMatrix RelateRegionLines(const RelateOperand &a, const RelateOperand &b) {
        RelateMatrix m;
        m.Raise(0, 2, 2);
        const BBox boxa = a.edges.Box(), boxb = b.edges.Box();
        const Arrangement r = Arranged(b.edges, a.edges, boxb, boxa, Tol(std::max(boxa.NormInf(), boxb.NormInf())), true);
        for (size_t k = 0; k < r.c.Size(); ++k) {
            if (r.state[k].excluded)
                continue;
            switch (r.state[k].mask) {
                case 3: m.Raise(1, 0, 1); break;
                case 1: m.Raise(r.side[k] == PieceSide::Inside ? 0 : 2, 0, 1); break;
                default: m.Raise(1, 2, 1);
            }
        }
        const std::vector<Vertex> ends = OddEnds(r.c, [&](size_t k) { return !r.state[k].excluded && (r.state[k].mask & 1) != 0; });
        const std::vector<Vertex> onRegion = VerticesOf(r, 2);
        for (const Vertex &e : ends) {
            if (HasVertex(onRegion, e)) {
                m.Raise(1, 1, 0);
                continue;
            }
            for (size_t k = 0; k < r.c.Size(); ++k)
                if (!r.state[k].excluded && r.state[k].mask == 1 && (StartOf(r.c[k]) == e || EndOf(r.c[k]) == e)) {
                    m.Raise(r.side[k] == PieceSide::Inside ? 0 : 2, 1, 0);
                    break;
                }
        }
        for (const Vertex &v : VerticesOf(r, 1))
            if (HasVertex(onRegion, v) && !HasVertex(ends, v))
                m.Raise(1, 0, 0);
        return m;
    }

    RelateMatrix RelateLines(const RelateOperand &a, const RelateOperand &b) {
        RelateMatrix m;
        const BBox boxa = a.edges.Box(), boxb = b.edges.Box();
        const Arrangement r = Arranged(a.edges, b.edges, boxa, boxb, Tol(std::max(boxa.NormInf(), boxb.NormInf())), false);
        for (size_t k = 0; k < r.c.Size(); ++k)
            if (!r.state[k].excluded)
                switch (r.state[k].mask) {
                    case 3: m.Raise(0, 0, 1); break;
                    case 1: m.Raise(0, 2, 1); break;
                    default: m.Raise(2, 0, 1);
                }
        auto ends = [&](unsigned char mask) {
            return OddEnds(r.c, [&](size_t k) { return !r.state[k].excluded && (r.state[k].mask & mask) != 0; });
        };
        const std::vector<Vertex> endsA = ends(1), endsB = ends(2);
        const std::vector<Vertex> va = VerticesOf(r, 1), vb = VerticesOf(r, 2);
        for (const Vertex &v : va)
            if (HasVertex(vb, v))
                m.Raise(HasVertex(endsA, v) ? 1 : 0, HasVertex(endsB, v) ? 1 : 0, 0);
        for (const Vertex &e : endsA)
            if (!HasVertex(vb, e))
                m.Raise(1, 2, 0);
        for (const Vertex &e : endsB)
            if (!HasVertex(va, e))
                m.Raise(2, 1, 0);
        return m;
    }

    // a: a region or lines
    RelateMatrix RelatePoint(const RelateOperand &a, const Vertex &p) {
        RelateMatrix m;
        m.Raise(0, 2, (signed char)a.dim);
        const std::vector<Vertex> ends = a.dim == 1 ? OddEnds(a.edges, [](size_t) { return true; }) : std::vector<Vertex>{};
        if (a.dim == 2)
            m.Raise(1, 2, 1);
        const Planar s = a.dim == 2 ? Planar{a.edges, Wire(a.edges.Axes())} : Planar{Wire(a.edges.Axes()), a.edges};
        const double tol = std::max(s.Tolerance(), Tol(NormInf(p)));
        size_t atEnd = ends.size();
        for (size_t i = 0; i < ends.size(); ++i)
            if (ends[i].IsClose(p, tol))
                atEnd = i;
        if (ends.size() > (atEnd < ends.size() ? 1u : 0u))
            m.Raise(1, 2, 0);
        const Where w = Locate(s, p);
        if (a.dim == 2)
            m.Raise(w == Where::Inside ? 0 : w == Where::Boundary ? 1 : 2, 0, 0);
        else
            m.Raise(atEnd < ends.size() ? 1 : w == Where::Boundary ? 0 : 2, 0, 0);
        return m;
    }

    RelateMatrix Relate(const RelateOperand &a, const RelateOperand &b) {
        if (a.Empty())
            return RelateEmpty(b, false);
        if (b.Empty())
            return RelateEmpty(a, true);
        if (a.dim < b.dim)
            return Relate(b, a).Transposed();
        RelateMatrix m;
        m.Raise(2, 2, 2);
        RelateMatrix r;
        if (b.dim == 0 && a.dim == 0) {
            const double tol = Tol(std::max(NormInf(a.point), NormInf(b.point)));
            if (a.point.IsClose(b.point, tol)) {
                r.Raise(0, 0, 0);
            } else {
                r.Raise(0, 2, 0);
                r.Raise(2, 0, 0);
            }
        } else if (b.dim == 0) {
            r = RelatePoint(a, b.point);
        } else if (a.dim == 2 && b.dim == 2) {
            r = RelateRegions(a, b);
        } else if (a.dim == 2) {
            r = RelateRegionLines(a, b);
        } else {
            r = RelateLines(a, b);
        }
        for (size_t k = 0; k < 9; ++k)
            m.dims[k] = std::max(m.dims[k], r.dims[k]);
        return m;
    }

    RelateOperand ShapeOperand(const Shape &s, const std::string &call) {
        if (s.HasFaces() && s.WireCount() > 0)
            throw std::invalid_argument(call + ": a shape of faces and wires has no relation, as it has no boolean; compare "
                                         "shape.faces or shape.wires");
        return s.HasFaces() ? RelateOperand{2, s.Outline(), {}} : RelateOperand{1, s.WireEdges(), {}};
    }

}
