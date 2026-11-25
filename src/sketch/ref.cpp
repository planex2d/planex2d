// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/geometry.hpp"

#include <charconv>
#include <stdexcept>

namespace planex2d::sketch {

    size_t Ref::PointAt(const Geometry &g) const {
        const Element &e = g.elements[element];
        if (e.kind == ElementKind::Spline)
            return part == Part::Point || part == Part::Tangent ? g.splines[e.SplineIndex()].points[index] : e.Ends(g)[part == Part::End];
        if (e.kind == ElementKind::Point)
            return e.Point();
        if (part == Part::Center)
            return e.Center();
        if (e.kind == ElementKind::OffsetCurve)
            return e.Ends(g)[part == Part::End];
        if (e.kind == ElementKind::Line)
            return part == Part::End ? e.End() : e.Start();
        return part == Part::Start ? e.Start() : e.End();
    }

    Vertex Ref::PointValue(const Geometry &g) const {
        if (!IsPoint(g) && part != Part::Tangent)
            throw std::invalid_argument("not a point");
        if (part != Part::Mid)
            return g.Coords((part == Part::Tangent ? Ref{element, Part::Point, index} : *this).PointAt(g));
        const Element &e = g.elements[element];
        if (e.kind == ElementKind::OffsetCurve) {
            const OffsetCurveData &c = g.offsetCurves[e.OffsetCurveIndex()];
            const Curve<double, Vertex> f = e.Curve(g);
            const std::array<double, 2> t = c.Range(g, f);
            return c.Point(g, f, 0.5 * (t[0] + t[1]));
        }
        if (e.kind == ElementKind::Spline) {
            const Curve<double, Vertex> f = e.Curve(g);
            const auto [from, to] = e.Range(g, f);
            const double mid = 0.5 * (from + to), end = f.params.back();
            return f.At(mid > end ? mid - end : mid, 0);
        }
        if (e.kind == ElementKind::Line)
            return (g.Coords(e.Start()) + g.Coords(e.End())) * 0.5;
        // on the arc's side of its chord, also for arcs over 180 degrees
        const Vertex chord = g.Coords(e.End()) - g.Coords(e.Start());
        const Vertex side = e.ccw ? Vertex{chord.y, -chord.x} : Vertex{-chord.y, chord.x};
        return g.Coords(e.Center()) + Normalized(side) * g.values[e.Radius()];
    }

    std::string Ref::Suffix() const {
        if (part == Part::Point || part == Part::Tangent)
            return (part == Part::Point ? ".p" : ".t") + std::to_string(index);
        constexpr const char *parts[] = {"", ".start", ".end", ".center", ".mid"};
        return parts[(size_t)part];
    }

    std::optional<Part> PartOf(const std::string &word) {
        return word == "start"    ? std::optional(Part::Start)
               : word == "end"    ? std::optional(Part::End)
               : word == "center" ? std::optional(Part::Center)
               : word == "mid"    ? std::optional(Part::Mid)
                                  : std::nullopt;
    }

    std::optional<std::pair<Part, size_t>> SplinePart(const std::string &part) {
        if (part.size() < 2 || (part[0] != 'p' && part[0] != 't'))
            return std::nullopt;
        size_t k = 0;
        const auto [end, error] = std::from_chars(part.data() + 1, part.data() + part.size(), k);
        if (error != std::errc() || end != part.data() + part.size())
            return std::nullopt;
        return std::pair{part[0] == 'p' ? Part::Point : Part::Tangent, k};
    }

}
