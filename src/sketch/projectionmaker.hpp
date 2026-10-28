// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "core/config.hpp"
#include "core/properties.hpp"
#include "core/vertex.hpp"
#include "curves/edge.hpp"
#include "curves/interpolate.hpp"
#include "sketch/options.hpp"
#include "sketch/projection.hpp"

namespace planex2d::sketch {

    // projected geometry, fixed where it is, under the call's scope (kind.N, or id): lines, arcs, splines (ends that meet
    // shared) and points. An edge or point the projection gave an id (Edge.5, Vertex.2) has it under the scope, the pieces
    // of one edge numbered along it (Edge.5.1, Edge.5.2); the others get ids as drawn. A projected curve becomes a spline
    // of ours through samples of it: its knots, and halfway between two samples where ours strays from it by more than
    // the spline tolerance; tangent to it at its ends. name names the edges, construction makes them references only;
    // hold false leaves the geometry free to move, as drawn
    template <class Sketch>
    class ProjectionMaker {
    public:
        ProjectionMaker(Sketch &sk, const Projection &p, bool asConstruction, const std::optional<std::string> &name, const std::string &kind,
                        const std::string &scope, bool hold)
            : sk(sk), p(p), asConstruction(asConstruction), kind(kind), scope(scope), hold(hold),
              named(name ? ChangedProperties(nullptr, [&](ShapeProperties &q) { q.name = *name; }) : Properties{}) {}

        void Make() {
            Join();
            const std::vector<size_t> made = Elements();
            for (const ProjectedPoint &q : p.points)
                PointOf(q.p, named || q.name.empty() ? named : ChangedProperties(nullptr, [&](ShapeProperties &x) { x.name = q.name; }), q.id);
            if (!made.empty())
                sk.NameWire(scope, made, kind + ": ");
            sk.groups.push_back({kind, scope});
            std::erase_if(sk.givenIds, [&](const std::string &g) { return g.starts_with(scope + "."); });
        }

    private:
        Sketch &sk;
        const Projection &p;
        const bool asConstruction;
        const std::string &kind, &scope;
        const bool hold;
        const Properties named;
        // the projected edges, the pieces of one curve joined, with their properties; the arcs that are a whole circle
        std::vector<std::pair<Edge, Properties>> edges;
        std::set<size_t> circles;
        // the points made, where they are
        std::vector<std::pair<size_t, Vertex>> known;

        static std::optional<std::string> IdOf(const Properties &q) { return q ? q->id : std::nullopt; }
        std::optional<std::string> UnderScope(const std::optional<std::string> &x) const {
            return x && !sk.byId.contains(scope + "." + *x) ? std::optional(scope + "." + *x) : std::nullopt;
        }
        size_t PointOf(const Vertex &v, const Properties &q, const std::optional<std::string> &x) {
            const double tol = Tol(std::max(NormInf(v), 1.0));
            for (const auto &[k, w] : known)
                if (w.IsClose(v, tol))
                    return k;
            sk.AddElement(sk.s.AddPoint(sk.s.NewPoint(v, hold)), "Point", {UnderScope(x), q, asConstruction});
            known.push_back({sk.s.elements.back().Point(), v});
            return known.back().first;
        }
        // drawn ones keep the properties of the shape's vertices
        template <class C>
        size_t EndPoint(const C &c, const Vertex &v, bool atEnd) {
            const Properties q = PropsAtEnd(c.ends, atEnd);
            return PointOf(v, asConstruction ? Properties{} : WithoutId(q), IdOf(q));
        }
        void Join() {
            const EdgeList projected = p.wire.Edges().Expanded();
            for (size_t i = 0; i < projected.size(); ++i) {
                Edge e = projected[i];
                const Properties q = PropsOf(e);
                // the pieces of one curve
                if (planex2d::Spline *curve = std::get_if<planex2d::Spline>(&e))
                    while (i + 1 < projected.size() && std::holds_alternative<planex2d::Spline>(projected[i + 1]) && IdOf(PropsOf(projected[i + 1])) == IdOf(q)) {
                        const std::optional<planex2d::Spline> joined = curve->Joined(std::get<planex2d::Spline>(projected[i + 1]));
                        if (!joined)
                            break;
                        *curve = *joined;
                        ++i;
                    }
                edges.push_back({std::move(e), q});
                if (const planex2d::Arc *a = std::get_if<planex2d::Arc>(&edges.back().first); a && i + 1 < projected.size())
                    if (const planex2d::Arc *b = std::get_if<planex2d::Arc>(&projected[i + 1]); b && a->CompletesCircle(*b, Tol(std::max(a->LengthScale(), 1.0)))) {
                        circles.insert(edges.size() - 1);
                        ++i;
                    }
            }
        }
        std::vector<size_t> Elements() {
            std::map<std::string, size_t> pieces, seen;
            for (const auto &[e, q] : edges)
                if (const std::optional<std::string> x = IdOf(q))
                    ++pieces[*x];
            std::vector<size_t> made;
            for (size_t n = 0; n < edges.size(); ++n) {
                const auto &[e, q] = edges[n];
                std::optional<std::string> x = IdOf(q);
                if (x && pieces[*x] > 1)
                    *x += "." + std::to_string(++seen[*x]);
                const Options opt{UnderScope(x), named ? named : WithoutId(q), asConstruction};
                if (const planex2d::Line *l = std::get_if<planex2d::Line>(&e))
                    made.push_back(sk.AddElement(sk.s.AddLine(EndPoint(*l, l->Start(), false), EndPoint(*l, l->End(), true)), "Line", opt));
                else if (const planex2d::Arc *a = std::get_if<planex2d::Arc>(&e); a && circles.contains(n)) {
                    made.push_back(sk.AddElement(sk.s.AddCircle(sk.s.NewPoint(a->Center(), hold), a->Radius(),
                                                                std::atan2(a->Start().y - a->Center().y, a->Start().x - a->Center().x), a->CCW()),
                                                 "Circle", opt));
                    sk.s.held[sk.s.elements.back().Radius()] = hold;
                } else if (a) {
                    const size_t from = EndPoint(*a, a->Start(), false), stop = EndPoint(*a, a->End(), true);
                    made.push_back(sk.AddElement(sk.s.AddArc(sk.s.NewPoint(a->Center(), hold), a->Radius(), from, stop, a->CCW()), "Arc", opt));
                    sk.s.held[sk.s.elements.back().Radius()] = hold;
                } else {
                    AddSpline(std::get<planex2d::Spline>(e), opt);
                    made.push_back(sk.elementIds.size() - 1);
                }
            }
            return made;
        }
        void AddSpline(const planex2d::Spline &curve, const Options &opt) {
            const Curve<double, Vertex> c = curve.Segment();
            const double u0 = c.First(), u1 = c.Last();
            const BBox &box = curve.Box();
            const double tol = config::state->spline_tol * Norm(box.Size());
            const bool closed = curve.Start().IsClose(curve.End(), Tol(std::max(NormInf(curve.Start()), 1.0)));
            const auto at = [&](double u) { return c.At(u); };
            const auto direction = [&](double u) {
                const Vertex v = c.Derivative(u);
                return std::atan2(v.y, v.x);
            };
            std::vector<double> u{u0};
            for (double k : c.knots)
                if (k > u.back() && k < u1)
                    u.push_back(k);
            u.push_back(u1);
            while (u.size() < (closed ? 4 : 3))
                for (size_t k = u.size() - 1; k > 0; --k)
                    u.insert(u.begin() + (std::ptrdiff_t)k, (u[k - 1] + u[k]) / 2.0);
            // a bound on the work: each round interpolates all samples densely
            constexpr size_t most = 200;
            std::vector<size_t> tangentAt;
            if (!closed)
                tangentAt = {0, u.size() - 1};
            for (bool finer = true; finer && u.size() < most;) {
                std::vector<Vertex> q, t;
                for (size_t k = 0; k + (closed ? 1 : 0) < u.size(); ++k)
                    q.push_back(at(u[k]));
                if (!closed) {
                    tangentAt = {0, q.size() - 1};
                    t = {{std::cos(direction(u0)), std::sin(direction(u0))}, {std::cos(direction(u1)), std::sin(direction(u1))}};
                }
                const Curve<double, Vertex> f = Interpolate(q, tangentAt, t, closed);
                std::vector<double> more;
                for (size_t k = 0; k + 1 < u.size(); ++k) {
                    const double half = (u[k] + u[k + 1]) / 2.0;
                    const Vertex x = at(half);
                    if (Norm(f.At(f.Project(x, (f.params[k] + f.params[k + 1]) / 2.0), 0) - x) > tol)
                        more.push_back(half);
                }
                finer = !more.empty();
                u.insert(u.end(), more.begin(), more.end());
                std::sort(u.begin(), u.end());
            }
            std::vector<size_t> ids;
            for (size_t k = 0; k + (closed ? 1 : 0) < u.size(); ++k) {
                if (k == 0 || k + 1 == u.size())
                    ids.push_back(EndPoint(curve, at(u[k]), k != 0));
                else {
                    sk.AddElement(sk.s.AddPoint(sk.s.NewPoint(at(u[k]), hold)), "Point", {std::nullopt, {}, opt.construction});
                    ids.push_back(sk.s.elements.back().Point());
                }
            }
            std::vector<double> angles;
            if (!closed) {
                tangentAt = {0, ids.size() - 1};
                angles = {direction(u0), direction(u1)};
            } else
                tangentAt.clear();
            sk.AddElement(sk.s.AddSpline(ids, tangentAt, angles, closed), "Spline", opt);
            for (size_t a : sk.s.splines.back().tangent)
                sk.s.held[a] = hold;
        }
    };

}
