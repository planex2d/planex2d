// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <Eigen/Core>
#include <Eigen/QR>

#include "planex/constraint_sketch/reports.hpp"
#include "planex/constraint_sketch/system.hpp"
#include "planex/core/config.hpp"
#include "planex/core/disjointsets.hpp"
#include "planex/core/vertex.hpp"


namespace planex2d::cs {

    class FreeMotionFinder {
    public:
        FreeMotionFinder(System &s, std::vector<std::pair<std::string, std::string>> labels, const std::vector<std::string> &ids)
            : s(s), labels(std::move(labels)), ids(ids) {}

        std::vector<FreeMotion> Find() {
            std::vector<FreeMotion> out;
            for (const Motion &m : Ordered())
                out.push_back(Readable(m));
            return out;
        }

        std::vector<std::pair<size_t, std::string>> Removing(const std::vector<Constraint> &candidates);

    private:
        enum class Role { Other, X, Y, Turn };
        struct Motion {
            size_t lead;
            Eigen::VectorXd m;
        };
        struct MotionPart {
            std::vector<size_t> values, points;
            std::vector<Eigen::Index> columns;
        };

        System &s;
        std::vector<std::pair<std::string, std::string>> labels;
        const std::vector<std::string> &ids;
        Unknowns x;
        Eigen::MatrixXd N, W;
        // null vectors are accurate to about eps / rank_tol: a value moved by less, relative to the motion, stays
        double zero = 0.0;
        std::vector<Role> role;
        std::vector<char> onElement, frame;
        std::vector<size_t> order;
        std::vector<MotionPart> parts;

        std::vector<Motion> Ordered();

        void Roles();

        static int DragRank(const std::string &coordinate) { return coordinate == "x" || coordinate == "y" ? 0 : coordinate == "r" ? 1 : 2; }
        void DragOrder();

        void Parts();

        Eigen::VectorXd Rigid(const MotionPart &part, Vertex shift, double turn, Vertex centre) const;

        void RigidMotions(const MotionPart &part, std::vector<Motion> &out);

        std::optional<Eigen::VectorXd> Eliminate(size_t u);

        std::string Name(size_t u) const {
            const auto &[reference, coordinate] = labels[u];
            return reference + (role[u] == Role::X ? ".h" : role[u] == Role::Y ? ".v" : "." + coordinate);
        }

        FreeMotion Readable(const Motion &motion) const;
    };

}
