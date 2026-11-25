// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <optional>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

#include "planex/boolean/boolean.hpp"
#include "planex/core/until.hpp"
#include "planex/core/vertex.hpp"
#include "planex/curves/circletouch.hpp"
#include "planex/curves/edgelist.hpp"
#include "planex/sketch/choices.hpp"
#include "planex/wire/wire.hpp"

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
        double RadiusAround(const Vertex &c, const std::optional<Vertex> &nearPoint, double tol, Choices &choices, const std::string &call) const;
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

    struct Magnitude {
        double m = 1.0;
        void Add(const Vertex &p) { m = std::max(m, NormInf(p)); }
        void Add(const Edge &e) { m = std::max(m, std::visit([](const auto &x) { return x.LengthScale(); }, e)); }
        void Add(const sketch::Target &t) {
            for (const Edge &e : t.edges)
                Add(e);
            Add(t.point);
            Add(t.at);
        }
        void Add(const std::vector<sketch::Target> &targets) {
            for (const sketch::Target &t : targets)
                Add(t);
        }
        template <class T>
        void Add(const std::optional<T> &x) {
            if (x)
                Add(*x);
        }
    };

    struct HelperTargets {
        std::optional<std::string> from, to, parallel, perpendicular, coincident, through, center, concentric;
        std::vector<std::string> tangent;
        bool until = false;
    };

}
