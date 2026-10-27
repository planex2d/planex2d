// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "curves/blend.hpp"
#include "curves/corner.hpp"
#include "wire/chain.hpp"

namespace planex2d {

    inline std::string SizeText(double x) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.6g", x);
        return buf;
    }

    inline void CheckLength(const std::string &call, const char *what, double x) {
        if (!(x > 0.0) || !std::isfinite(x))
            throw std::invalid_argument(call + ": " + what + " must be a positive, finite length, not " + SizeText(x) +
                                        "; give a length greater than 0");
    }

    // x to six significant digits: down, so that it still fits where x does, or to the nearest
    inline double DownRounded(double x) {
        const double unit = std::pow(10.0, std::floor(std::log10(x)) - 5.0);
        return std::floor(x / unit) * unit;
    }
    inline double NearRounded(double x) {
        const double unit = std::pow(10.0, std::floor(std::log10(x)) - 5.0);
        return std::round(x / unit) * unit;
    }

    // the rounding of corners by a circle of radius r
    struct FilletRule {
        static constexpr const char *kind = "Fillet";
        static constexpr bool reverse = false;
        double r;

        void Check(const std::string &call) const { CheckLength(call, "r", r); }
        double Size() const { return r; }
        std::string Text(double size) const { return "radius " + SizeText(size); }
        std::optional<CornerCut> Cutting(const Arm &a, const Arm &b, double size) const { return RoundingOf(a, b, size); }
        Edge Joining(const Arm &, const Arm &, const CornerCut &made) const { return made.Joining(); }
    };

    // the rounding of corners by a curvature-continuous blend: from where the circle of radius r touches the first edge
    // to where it touches the second, the quintic of G2Blend
    struct CurvatureFilletRule : FilletRule {
        Edge Joining(const Arm &first, const Arm &second, const CornerCut &made) const {
            const Curve<double, Vertex> c = G2Blend<double, Vertex>(made.first, first.TangentAt(made.first) * -1.0, -first.CurvatureAt(made.first),
                                                                    made.second, second.TangentAt(made.second), second.CurvatureAt(made.second));
            return Spline::FromCurve(c).front();
        }
    };

    // the cutting of corners by a line: d on the first edge, then d2 on the second or the angle (degrees) to the first
    struct ChamferRule {
        static constexpr const char *kind = "Chamfer";
        double d;
        std::optional<double> d2, angle;
        bool reverse = false;

        void Check(const std::string &call) const {
            CheckLength(call, "d", d);
            if (d2)
                CheckLength(call, "d2", *d2);
            if (d2 && angle)
                throw std::invalid_argument(call + ": d2 and angle both give the second side of the chamfer; give one of them");
            if (angle && !(*angle > 0.0 && *angle < 180.0))
                throw std::invalid_argument(call + ": angle must be between 0 and 180 degrees, not " + SizeText(*angle) +
                                            "; give the angle to the first edge");
        }
        double Size() const { return d; }
        std::string Text(double size) const {
            return "d " + SizeText(size) + (d2 ? ", d2 " + SizeText(*d2 * size / d) : std::string());
        }
        std::optional<CornerCut> Cutting(const Arm &a, const Arm &b, double size) const {
            return ChamferingOf(a, b, size, d2.value_or(d) * size / d, angle);
        }
        Edge Joining(const Arm &, const Arm &, const CornerCut &made) const { return made.Joining(); }
    };

    struct CornerRun {
        std::vector<char> forward;
        size_t number;
    };

    // the properties of a corner's two edges merged, the first one's winning, without a partition (which belongs to the
    // edge it was)
    inline Properties MergedWithoutPartition(const Properties &first, const Properties &second) {
        return WithoutPartition(MergedProperties(first, second));
    }

    // the properties of the new edge: those of the two edges merged, the first one's winning, but its id and
    // partition (which belong to the edge it was)
    inline Properties CornerProperties(const Arm &first, const Arm &second, const std::string &id) {
        const Properties merged = MergedWithoutPartition(PropsOf(first.edge), PropsOf(second.edge));
        return ChangedProperties(merged, [&](ShapeProperties &q) { q.id = id; });
    }

    // a corner point on no vertex with all its digits (rounded it may read as the vertex it missed), the nearest of the
    // vertices and how far, and the call that selects it
    inline std::string OffVertex(const Vertex &p, const std::vector<Vertex> &vertices, const std::string &select) {
        char at[96];
        std::snprintf(at, sizeof at, "(%.17g, %.17g)", p.x, p.y);
        std::string text = std::string("the corner at ") + at + " is on no vertex";
        if (vertices.empty())
            return text;
        Vertex nearest = vertices.front();
        for (const Vertex &v : vertices)
            if (Dot(v - p, v - p) < Dot(nearest - p, nearest - p))
                nearest = v;
        char away[32];
        std::snprintf(away, sizeof away, "%.3g", Norm(nearest - p));
        return text + " (the nearest, " + PointText(nearest) + ", is " + away + " away" + (select.empty() ? "" : "; " + select) + ")";
    }

    // why a point with this many edge ends is no corner, empty for exactly two
    inline std::string CornerEndsProblem(const std::string &where, size_t ends) {
        if (ends == 0)
            return where + " is on no vertex";
        if (ends != 2)
            return where + " has " + std::to_string(ends) + " edge end" + (ends > 1 ? "s" : "") + ", a corner needs exactly two";
        return {};
    }

    // why the corner of the arms cannot be cut, empty after finding the cut
    template <class Rule>
    std::string CornerCutOf(const std::string &where, const Arm &first, const Arm &second, const Rule &rule, CornerCut &cut) {
        if (first.IsTangentTo(second))
            return where + ": the edges meet tangentially or fold back onto each other, there is no corner to cut";
        const std::optional<CornerCut> found = rule.Cutting(first, second, rule.Size());
        if (!found) {
            auto fits = [&](double size) { return rule.Cutting(first, second, size).has_value(); };
            const double largest = LargestFit(fits, rule.Size());
            const double shown = fits(NearRounded(largest)) ? NearRounded(largest) : DownRounded(largest);
            return where + ": " + rule.Text(rule.Size()) + " does not fit" +
                   (largest > 0.0 ? ", the largest that fits is " + rule.Text(shown) : ", the edges leave no room for one");
        }
        cut = *found;
        return {};
    }

    // raises if no corner was cut, naming why each failed; else warns about the failed ones
    inline void ReportCorners(const std::string &call, size_t done, const std::vector<std::string> &failed) {
        if (done == 0) {
            std::string all;
            for (const std::string &why : failed)
                all += (all.empty() ? "" : "; ") + why;
            throw std::invalid_argument(call + ": no corner can be cut: " + all +
                                        "; give corners that are the end of exactly two edges, with a size that fits");
        }
        for (const std::string &why : failed)
            Warning() << call << ": " << why << ", left as it is";
    }

    // why the corner at p cannot be cut, empty after cutting it
    template <class Rule>
    std::string CutCorner(Wire &w, const Vertex &p, const Rule &rule, CornerRun &run) {
        const std::string where = "the corner at " + PointText(p);
        const std::vector<size_t> ends = w.EndpointsAt(p);
        if (ends.empty())
            return OffVertex(p, w.Endpoints(), "select it with vertices(near=...)");
        if (std::string why = CornerEndsProblem(where, ends.size()); !why.empty())
            return why;
        size_t i = ends[0] / 2, j = ends[1] / 2;
        if (i == j)
            return where + " is where one edge closes on itself";
        if (w.Owner(i) != w.Owner(j))
            return where + " joins edges of different parts";
        Arm first{w[i], ends[0] % 2 == 1}, second{w[j], ends[1] % 2 == 1};
        if (((bool)run.forward[i] == first.atEnd) == rule.reverse) {
            std::swap(first, second);
            std::swap(i, j);
        }
        CornerCut cut;
        if (std::string why = CornerCutOf(where, first, second, rule, cut); !why.empty())
            return why;
        // an edge the cut reaches the far end of is used up: it goes, the new edge ends at its far vertex
        const bool usedFirst = first.IsUsedUpBy(cut.first), usedSecond = second.IsUsedUpBy(cut.second);
        CornerCut made = cut;
        made.first = usedFirst ? first.Far() : first.Shortened(cut.first).Corner();
        made.second = usedSecond ? second.Far() : second.Shortened(cut.second).Corner();
        Edge added = rule.Joining(first, second, made);
        SetProps(added, CornerProperties(first, second, std::string(Rule::kind) + "." + std::to_string(++run.number)));
        const uint32_t owner = w.Owner(i);
        if (!usedFirst)
            w[i] = first.Shortened(cut.first).edge;
        if (!usedSecond)
            w[j] = second.Shortened(cut.second).edge;
        const size_t at = std::max(i, j) == std::min(i, j) + 1 ? std::max(i, j) : w.Size();
        w.Insert(at, std::move(added), owner);
        run.forward.insert(run.forward.begin() + (long)at, 1);
        std::vector<bool> used(w.Size(), false);
        used[i < at ? i : i + 1] = usedFirst;
        used[j < at ? j : j + 1] = usedSecond;
        w.Erase(used);
        Erase(run.forward, used);
        return {};
    }

    template <class Rule>
    void Wire::CutCorners(const std::vector<Vertex> &at, const Rule &rule, const std::string &call) {
        rule.Check(call);
        Wire cut = *this;
        cut.AssignIds();
        CornerRun run{cut.Traversal(), cut.LastNumber(std::string(Rule::kind) + ".")};
        std::vector<std::string> failed;
        size_t done = 0;
        for (size_t k = 0; k < at.size(); ++k) {
            if (std::any_of(at.begin(), at.begin() + (long)k, [&](const Vertex &q) { return q.IsClose(at[k], Tol()); }))
                continue;
            if (std::string why = CutCorner(cut, at[k], rule, run); why.empty())
                ++done;
            else
                failed.push_back(std::move(why));
        }
        ReportCorners(call, done, failed);
        *this = std::move(cut);
    }

}
