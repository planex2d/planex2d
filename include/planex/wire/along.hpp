// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "planex/curves/edgelist.hpp"
#include "planex/wire/chain.hpp"

namespace planex2d {

    // on the exact curves; the curvature a left turn positive
    struct ChainPlace {
        Vertex point, tangent;
        double curvature, arcLength;
        size_t edge;
    };

    class MeasuredChain {
        private:
            // a circle is its two halves
            EdgeList parts;
            std::vector<size_t> owners;
            ChainWalk walk;
            std::vector<double> lengths, before;
            double tol;

            static ChainPlace OnEdge(const Edge &e, double local);
            static std::pair<double, Vertex> NearestOn(const Edge &e, const Vertex &p);
            ChainPlace Place(size_t k, double local) const;

        public:
            MeasuredChain(const EdgeList &edges, const std::string &call);
            double Length() const { return before.empty() ? 0.0 : before.back() + lengths.back(); }
            ChainPlace AtLength(double s) const {
                if (!(s >= -tol && s <= Length() + tol))
                    throw std::invalid_argument("at=" + std::to_string(s) + ": an arc length from 0 to the length " +
                                                std::to_string(Length()));
                size_t k = 0;
                while (k + 1 < lengths.size() && s > before[k] + lengths[k])
                    ++k;
                return Place(k, std::clamp(s - before[k], 0.0, lengths[k]));
            }
            ChainPlace Nearest(const Vertex &p) const;
    };

}
