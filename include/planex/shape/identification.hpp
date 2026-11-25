// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "planex/curves/edge.hpp"
#include "planex/curves/edgelist.hpp"

namespace planex2d {

    // x -> m x + t in the plane, m row by row
    struct Affine2 {
        std::array<double, 4> m{1.0, 0.0, 0.0, 1.0};
        Vertex t{};

        Vertex operator()(const Vertex &p) const { return {m[0] * p.x + m[1] * p.y + t.x, m[2] * p.x + m[3] * p.y + t.y}; }
        double Det() const { return m[0] * m[3] - m[1] * m[2]; }
        // this after b
        Affine2 operator*(const Affine2 &b) const {
            return {{m[0] * b.m[0] + m[1] * b.m[2], m[0] * b.m[1] + m[1] * b.m[3], m[2] * b.m[0] + m[3] * b.m[2], m[2] * b.m[1] + m[3] * b.m[3]},
                    (*this)(b.t)};
        }
        Affine2 Inverse() const {
            const double d = Det();
            if (!(std::abs(d) > 0.0))
                throw std::invalid_argument("the transformation is singular");
            const Affine2 r{{m[3] / d, -m[1] / d, -m[2] / d, m[0] / d}, {}};
            const Vertex u = r(t);
            return {r.m, {-u.x, -u.y}};
        }
        // the angle (radians) its rotational part turns by
        double Turn() const { return std::atan2(m[2] - m[1], m[0] + m[3]); }

        static Affine2 Translation(const Vertex &d) { return {{1.0, 0.0, 0.0, 1.0}, d}; }
        static Affine2 Rotation(const Vertex &c, double degrees) {
            const double a = Radians(degrees), cs = std::cos(a), sn = std::sin(a);
            return {{cs, -sn, sn, cs}, {c.x - cs * c.x + sn * c.y, c.y - sn * c.x - cs * c.y}};
        }
        static Affine2 Reflection(const Vertex &a, const Vertex &d) {
            const Vertex u = Normalized(d);
            const Affine2 r{{2.0 * u.x * u.x - 1.0, 2.0 * u.x * u.y, 2.0 * u.x * u.y, 2.0 * u.y * u.y - 1.0}, {}};
            const Vertex ra = r(a);
            return {r.m, {a.x - ra.x, a.y - ra.y}};
        }
        static Affine2 Scaling(const Vertex &c, double s) { return {{s, 0.0, 0.0, s}, {c.x - s * c.x, c.y - s * c.y}}; }
    };

    // trafo maps from onto to; none: found from the geometry at every export (GetTransformation); type: netgen's
    // Identifications::ID_TYPE
    struct Identification {
        std::string name, from, to;
        std::optional<Affine2> trafo;
        int type = 2;
    };
    using Identifications = std::vector<Identification>;

    inline void MergeIdentifications(Identifications &a, const Identifications &b) {
        for (const Identification &i : b)
            if (std::none_of(a.begin(), a.end(), [&](const Identification &j) { return j.name == i.name; }))
                a.push_back(i);
    }
    inline std::vector<std::string> SidesOf(const Identifications &ids) {
        std::vector<std::string> sides;
        for (const Identification &i : ids) {
            sides.push_back(i.from);
            sides.push_back(i.to);
        }
        return sides;
    }
    inline void MoveIdentifications(Identifications &ids, const Affine2 &motion) {
        for (Identification &i : ids)
            if (i.trafo)
                i.trafo = motion * *i.trafo * motion.Inverse();
    }

    namespace identify {
        // a closed chain lists its start once
        struct Chain {
            std::vector<Vertex> points;
            EdgeList edges;
            bool closed = false;
        };
        Chain ChainOf(const EdgeList &given, const std::string &what, double tol);

        std::optional<Affine2> AffineFit(const std::vector<Vertex> &p, const std::vector<Vertex> &q);

        std::optional<Affine2> Similarity(const Vertex &p0, const Vertex &p1, const Vertex &q0, const Vertex &q1, bool mirrored);
    }

    Affine2 GetTransformation(const EdgeList &a, const EdgeList &b, bool reverse = false,
                              const std::string &aName = "a", const std::string &bName = "b");

}
