// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <compare>
#include <initializer_list>
#include <type_traits>
#include <variant>
#include <vector>

#include "core/vertex.hpp"
#include "core/bbox.hpp"
#include "core/properties.hpp"

namespace planex2d {

    class Line;
    class Arc;
    class Circle;
    class Spline;
    using Edge = std::variant<Line, Arc, Circle, Spline>;

    struct PointOnCurve {
        double t = 0.0;
        Vertex p;

        auto operator<=>(const PointOnCurve &o) const { return t <=> o.t; }
    };

    class SplitPoints {
        private:
            std::vector<PointOnCurve> pts;

        public:
            SplitPoints() = default;
            SplitPoints(std::initializer_list<PointOnCurve> l) : pts(l) {}
            void Add(double t, const Vertex &p) { pts.push_back({t, p}); }
            template <class F>
            void RemoveIf(F f) { std::erase_if(pts, f); }
            void Sort() {
                std::sort(pts.begin(), pts.end());
                auto duplicates = std::ranges::unique(pts, {}, &PointOnCurve::p);
                pts.erase(duplicates.begin(), duplicates.end());
            }
            bool IsEmpty() const { return pts.empty(); }
            size_t Size() const { return pts.size(); }
            const PointOnCurve &operator[](size_t i) const { return pts[i]; }
            auto begin() const { return pts.begin(); }
            auto end() const { return pts.end(); }
    };

    enum class Location { Off, Inside, AtStart, AtEnd };

    template <class T>
    void Erase(std::vector<T> &v, const std::vector<bool> &removed) {
        size_t k = 0;
        for (size_t i = 0; i < v.size(); ++i) {
            if (removed[i])
                continue;
            if (k != i)
                v[k] = std::move(v[i]);
            ++k;
        }
        v.erase(v.begin() + static_cast<long>(k), v.end());
    }

    // tol: how close the caller found the points, only splines need it
    template <class E>
    std::vector<E> Split(const E &e, SplitPoints pts, double) {
        pts.Sort();
        std::vector<E> pieces;
        Vertex from = e.Start();
        for (const PointOnCurve &x : pts) {
            pieces.emplace_back(e).Trim(from, x.p);
            from = x.p;
        }
        pieces.emplace_back(e).Trim(from, e.End());
        KeepOuterEnds(pieces, e.ends);
        return pieces;
    }

    template <class E>
    inline constexpr bool IsCircle = std::is_same_v<std::decay_t<E>, Circle>;

    // a spline by the lines and arcs of its approximation, a circle by its halves
    template <class E, class F>
    void ForEachPart(const E &e, F &&f) {
        if constexpr (std::is_same_v<E, Spline>) {
            for (const auto &part : e.Approximation())
                std::visit(f, part);
        } else if constexpr (IsCircle<E>) {
            for (const auto &half : e.Halves())
                f(half);
        } else {
            f(e);
        }
    }

    template <class... T>
    BBox BoxOf(const std::variant<T...> &e) {
        return std::visit([](const auto &x) -> BBox { return x.Box(); }, e);
    }
    template <class... T>
    const Properties &PropsOf(const std::variant<T...> &e) {
        return std::visit([](const auto &x) -> const Properties & { return x.props; }, e);
    }
    template <class... T>
    void SetProps(std::variant<T...> &e, const Properties &p) {
        std::visit([&](auto &x) { x.props = p; }, e);
    }
    template <class... T>
    const Ends &EndsOf(const std::variant<T...> &e) {
        return std::visit([](const auto &x) -> const Ends & { return x.ends; }, e);
    }
    template <class... T>
    void SetEnds(std::variant<T...> &e, Ends ends) {
        std::visit([&](auto &x) { x.ends = std::move(ends); }, e);
    }
    template <class... T>
    Vertex StartOf(const std::variant<T...> &e) {
        return std::visit([](const auto &x) { return x.Start(); }, e);
    }
    template <class... T>
    Vertex EndOf(const std::variant<T...> &e) {
        return std::visit([](const auto &x) { return x.End(); }, e);
    }
    template <class... T>
    std::variant<T...> ReversedOf(const std::variant<T...> &e) {
        return std::visit([](const auto &x) -> std::variant<T...> { return x.Reversed(); }, e);
    }
    // a circle, or a spline that is the whole of a closed curve: its start and end are one point, no vertex there
    template <class... T>
    bool IsClosedEdge(const std::variant<T...> &e) {
        return std::visit([](const auto &x) {
            if constexpr (IsCircle<decltype(x)>)
                return true;
            else if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Spline>)
                return x.IsClosed();
            else
                return false;
        }, e);
    }
    template <class... T>
    double LengthOf(const std::variant<T...> &e) {
        return std::visit([](const auto &x) { return x.Length(); }, e);
    }
    template <class... T>
    double LengthScaleOf(const std::variant<T...> &e) {
        return std::visit([](const auto &x) { return x.LengthScale(); }, e);
    }
    template <class... T>
    Vertex MidPointOf(const std::variant<T...> &e) {
        return std::visit([](const auto &x) { return x.MidPoint(); }, e);
    }
    template <class... T>
    double SignedArea(const std::variant<T...> &e, const Vertex &o) {
        return std::visit([&](const auto &x) { return x.SignedArea(o); }, e);
    }
    // as measured: a spline's on its exact curve
    template <class... T>
    double ExactSignedArea(const std::variant<T...> &e, const Vertex &o) {
        return std::visit([&](const auto &x) {
            if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Spline>)
                return x.ExactSignedArea(o);
            else
                return x.SignedArea(o);
        }, e);
    }
    template <class... T>
    double ExactLengthOf(const std::variant<T...> &e) {
        return std::visit([](const auto &x) {
            if constexpr (std::is_same_v<std::decay_t<decltype(x)>, Spline>)
                return x.ExactLength();
            else
                return x.Length();
        }, e);
    }
    template <class... T>
    double LengthFraction(const std::variant<T...> &e, const Vertex &p) {
        return std::visit([&](const auto &x) { return x.LengthFraction(p); }, e);
    }
    template <class... T>
    Vertex PointAtFraction(const std::variant<T...> &e, double t) {
        return std::visit([&](const auto &x) { return x.PointAtFraction(t); }, e);
    }
    template <class... T>
    Vertex TangentAtFraction(const std::variant<T...> &e, double t) {
        return std::visit([&](const auto &x) { return x.TangentAtFraction(t); }, e);
    }
    template <class... T>
    Vertex NearestPointOf(const std::variant<T...> &e, const Vertex &p) {
        return std::visit([&](const auto &x) { return x.NearestPoint(p); }, e);
    }
    template <class... T>
    double Distance(const Vertex &q, const std::variant<T...> &e) {
        return std::visit([&](const auto &x) { return x.Distance(q); }, e);
    }
    template <class... T>
    bool HasPropsAt(const std::variant<T...> &e, const Vertex &v) {
        return std::visit([&](const auto &x) { return Marked(PropsAt(x, v)); }, e);
    }

}
