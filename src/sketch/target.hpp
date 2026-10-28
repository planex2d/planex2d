// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "boolean/boolean.hpp"
#include "core/until.hpp"
#include "core/vertex.hpp"
#include "curves/circletouch.hpp"
#include "curves/edgelist.hpp"
#include "sketch/choices.hpp"
#include "wire/wire.hpp"

namespace planex2d::sketch {

    // point picks among the solutions on edges; at: an exact point an end passes through
    struct Target {
        EdgeList edges;
        std::optional<Vertex> point;
        std::string key;
        std::optional<std::string> name;
        std::optional<Vertex> at;

        bool IsSame(const Target &o) const { return !key.empty() && key == o.key; }
        double LengthScale() const { return at ? std::max(edges.LengthScale(), NormInf(*at)) : edges.LengthScale(); }
        std::string Text() const {
            if (name)
                return "'" + *name + "'";
            return at ? PointText(*at) : "<target>";
        }
        // not when the point lies on a line of the target
        bool PointPicks(double tol) const {
            if (!point)
                return false;
            for (const Piece &piece : PiecesOf(edges))
                if (const Line *l = std::get_if<Line>(&piece.e); l && l->OnCarrier(*point, tol))
                    return false;
            return true;
        }
        // a line where its foot from c lies, an arc where the line of the centers meets it; on the edge first, then nearest
        // to the point given (or to near), else the smaller
        double RadiusAround(const Vertex &c, const std::optional<Vertex> &nearPoint, double tol, Choices &choices, const std::string &call) const {
            struct Touching {
                Vertex p;
                double rho;
                bool onEdge;
            };
            std::vector<Touching> found;
            // a touch point found on two pieces (the halves of a circle) counts once
            const auto accept = [&](const Piece &piece, const Vertex &p) {
                const double rho = Norm(p - c);
                const bool on = piece.e.Locate(p, tol) != Location::Off;
                if (!(rho > tol && (on || piece.carrier)))
                    return;
                for (Touching &f : found)
                    if (f.p.IsClose(p, tol)) {
                        f.onEdge = f.onEdge || on;
                        return;
                    }
                found.push_back({p, rho, on});
            };
            for (const Piece &piece : PiecesOf(edges)) {
                if (const Line *l = std::get_if<Line>(&piece.e)) {
                    accept(piece, l->Foot(c));
                    continue;
                }
                const Arc &arc = std::get<Arc>(piece.e);
                if (Norm(c - arc.Center()) <= tol)
                    accept(piece, arc.MidPoint());
                else
                    for (const Vertex &p : arc.RadialPoints(c))
                        accept(piece, p);
            }
            if (found.empty())
                throw std::invalid_argument(call + ": no circle around the center touches it");
            const std::optional<Vertex> ref = point ? point : nearPoint;
            const std::vector<size_t> order = BestFirst(found.size(), [&](size_t x, size_t y) {
                if (found[x].onEdge != found[y].onEdge)
                    return found[x].onEdge;
                return ref ? Norm(found[x].p - *ref) < Norm(found[y].p - *ref) - tol : found[x].rho < found[y].rho - tol;
            });
            return found[choices.Take(order, !point)].rho;
        }
    };

    struct Refs {
        std::string call;
        std::vector<Target> tangent;
        bool reverse = false;
        std::optional<Vertex> nearPoint;

        double Scale(const Vertex &pen) const {
            double scale = NormInf(pen);
            for (const Target &t : tangent)
                scale = std::max(scale, t.edges.LengthScale());
            return scale;
        }
        [[noreturn]] void Fail(const std::string &why) const { throw std::invalid_argument(call + ": " + why); }
    };

    // from, to: the positional ends (none: the pen; one: to); untilTarget: until= names a target (until is First)
    struct EndRefs : Refs {
        std::optional<Target> from, to, untilTarget;
        Until until = Until::No;

        double Scale(const Vertex &pen) const {
            double scale = Refs::Scale(pen);
            if (from)
                scale = std::max(scale, from->LengthScale());
            if (to)
                scale = std::max(scale, to->LengthScale());
            return scale;
        }
    };

    inline std::vector<Vertex> CarrierHits(const Wire &carrier, const EdgeList &targets) {
        Wire across(carrier.Axes());
        for (const Edge &e : targets)
            across.Add(e);
        return Intersections(carrier, across);
    }

}
