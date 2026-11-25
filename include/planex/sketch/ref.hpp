// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>

#include "planex/core/vertex.hpp"

namespace planex2d::sketch {

    class Geometry;

    enum class Part { Whole, Start, End, Center, Mid, Point, Tangent };

    struct Ref {
        size_t element = 0;
        Part part = Part::Whole;
        size_t index = 0;

        bool IsSpline(const Geometry &g) const;
        bool IsCurve(const Geometry &g) const;
        bool HasPart(const Geometry &g) const;
        bool IsPoint(const Geometry &g) const;
        bool IsRealPoint(const Geometry &g) const;
        bool IsLine(const Geometry &g) const;
        bool IsSegment(const Geometry &g) const;
        bool IsRound(const Geometry &g) const;
        bool IsArc(const Geometry &g) const;
        bool IsPattern(const Geometry &g) const;
        bool IsOffset(const Geometry &g) const;
        size_t PointAt(const Geometry &g) const;
        Vertex PointValue(const Geometry &g) const;
        std::string Suffix() const;
    };

    std::optional<Part> PartOf(const std::string &word);
    // "pK" / "tK": a spline's point K, its tangent line there
    std::optional<std::pair<Part, size_t>> SplinePart(const std::string &part);

}
