// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

#include "planex/boolean/boolean.hpp"

namespace planex2d {

    // outer loops run counterclockwise and holes clockwise, so the material is on the left of every segment and the
    // winding number tells inside
    class DeformableShape {
        public:
            struct Ring {
                std::vector<Vertex> ref, cur;   // the first point is not repeated at the end
                std::vector<size_t> starts;     // first point of every original edge
                std::vector<Properties> props;  // of every original edge
            };

        private:
            std::vector<Ring> rings;
            AxisSystem axes;
            double h, minDistance;

            static void Sample(const PrimitiveEdge &piece, double h, std::vector<Vertex> &out);
            static void Sample(const Edge &e, bool forward, double h, std::vector<Vertex> &out);

            static double SignedArea(const std::vector<Vertex> &pts) {
                double a = 0.0;
                for (size_t i = 0; i < pts.size(); ++i)
                    a += Cross(pts[i], pts[(i + 1) % pts.size()]);
                return 0.5 * a;
            }

            static void Reverse(Ring &r) {
                size_t n = r.cur.size();
                std::reverse(r.cur.begin() + 1, r.cur.end());
                std::reverse(r.ref.begin() + 1, r.ref.end());
                std::reverse(r.props.begin(), r.props.end());
                std::vector<size_t> starts{0};
                for (size_t k = r.starts.size(); k-- > 1;)
                    starts.push_back(n - r.starts[k]);
                r.starts = std::move(starts);
            }

            Wire Lines(std::vector<Properties> *props) const;

            // signed crossing of the ray from p: +1 upward, -1 downward (vertical ray: the frame is turned so the
            // ray points to +x, up is then -x)
            template <bool vertical>
            static int Crossing(const Line &l, const Vertex &p) {
                auto along = [](const Vertex &v) { return vertical ? v.y : v.x; };
                auto across = [](const Vertex &v) { return vertical ? -v.x : v.y; };
                const Vertex &a = l.Start(), &b = l.End();
                bool aAbove = across(a) > across(p), bAbove = across(b) > across(p);
                if (aAbove == bAbove)
                    return 0;
                double t = (across(p) - across(a)) / (across(b) - across(a));
                if (along(a) + t * (along(b) - along(a)) <= along(p))
                    return 0;
                return bAbove ? 1 : -1;
            }
            template <bool vertical>
            static int Winding(const Wire &w, Grid &grid, const Vertex &p, size_t skip) {
                int total = 0;
                grid.ForRay<vertical>(p, [&](uint32_t k) {
                    if (k != skip)
                        total += Crossing<vertical>(std::get<Line>(w[k]), p);
                });
                return total;
            }
            // material is winding >= 1 on the left of piece k; the piece itself adds one on its left
            static bool Keep(const Wire &w, Grid &grid, size_t k) {
                const Line &l = std::get<Line>(w[k]);
                Vertex d = l.End() - l.Start();
                Vertex m = (l.Start() + l.End()) * 0.5;
                if (std::abs(d.y) >= std::abs(d.x))
                    return Winding<false>(w, grid, m, k) == (d.y > 0.0 ? 0 : 1);
                return Winding<true>(w, grid, m, k) == (d.x < 0.0 ? 0 : 1);
            }

            static std::vector<Ring> Trace(const Wire &w, const std::vector<bool> &keep, const std::vector<Properties> &props);

            void Orient();

            bool Resolve();

            struct Marks {
                Wire w;                            // vertex k starts line k
                std::vector<Properties> props;     // of the original edges, w's owners
                std::vector<size_t> ring, offset;  // the ring of every vertex, the rings' vertex ranges
                std::vector<bool> marked;          // closer than the minimum distance to another part of the boundary
                size_t Next(size_t v) const { return v + 1 < offset[ring[v] + 1] ? v + 1 : offset[ring[v]]; }
                size_t Prev(size_t v) const { return v > offset[ring[v]] ? v - 1 : offset[ring[v] + 1] - 1; }
                const Vertex &At(size_t v) const { return std::get<Line>(w[v]).Start(); }
            };

            Marks Mark() const;

            void Resample(const Marks &a);

            struct Path {
                size_t first, last;
            };
            std::vector<Path> UnmarkedPaths(const Marks &a, std::vector<Ring> &whole);
            static std::vector<size_t> EndPartners(const Marks &a, const std::vector<Path> &paths);
            static void JoinPaths(const Marks &a, const std::vector<Path> &paths, const std::vector<size_t> &partner, std::vector<Ring> &out);

        public:
            DeformableShape(const Wire &w, double h, double minDistance, bool clean = false);

            double H() const { return h; }
            double MinDistance() const { return minDistance; }
            const AxisSystem &Axes() const { return axes; }
            const std::vector<Ring> &Rings() const { return rings; }
            size_t Size() const {
                size_t n = 0;
                for (const Ring &r : rings)
                    n += r.cur.size();
                return n;
            }
            std::vector<size_t> RingSizes() const {
                std::vector<size_t> n;
                for (const Ring &r : rings)
                    n.push_back(r.cur.size());
                return n;
            }
            std::vector<std::string> VertexNames() const;
            std::vector<Vertex> Vertices() const {
                std::vector<Vertex> v;
                for (const Ring &r : rings)
                    v.insert(v.end(), r.cur.begin(), r.cur.end());
                return v;
            }
            std::vector<Vertex> ReferenceVertices() const {
                std::vector<Vertex> v;
                for (const Ring &r : rings)
                    v.insert(v.end(), r.ref.begin(), r.ref.end());
                return v;
            }
            double Area() const {
                double a = 0.0;
                for (const Ring &r : rings)
                    a += SignedArea(r.cur);
                return a;
            }
            BBox Box() const {
                BBox b;
                for (const Ring &r : rings)
                    for (const Vertex &v : r.cur)
                        b.Grow(v);
                return b;
            }

            void Refine();

            void Commit() {
                for (Ring &r : rings)
                    r.ref = r.cur;
            }

            bool SetDeformation(const std::vector<Vertex> &d) {
                if (d.size() != Size())
                    throw std::invalid_argument("one displacement per vertex expected");
                size_t k = 0;
                for (Ring &r : rings)
                    for (size_t i = 0; i < r.cur.size(); ++i)
                        r.cur[i] = r.ref[i] + d[k++];
                Refine();
                return Clean();
            }

            bool IsValid() const {
                Wire w = Lines(nullptr);
                size_t before = w.Size();
                std::vector<CutState> state(before);
                Grid grid = w.EdgeGrid();
                CutAll(w, grid, state, false);
                return w.Size() == before && std::none_of(state.begin(), state.end(), [](const CutState &s) { return s.excluded; });
            }

            bool Clean();

            bool Defeature();

            Wire Lines() const { return Lines(nullptr); }

            Wire Edges() const;
    };

}
