// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/geometry.hpp"

#include <limits>
#include <numbers>

#include "planex/core/config.hpp"

namespace planex2d::sketch {

    size_t Geometry::AddSpline(const std::vector<size_t> &points, const std::vector<size_t> &tangentAt, const std::vector<double> &angles,
                               bool closed, SplineStyle style, const std::map<size_t, TangentSetting<double>> &settings) {
        SplineData d{points, tangentAt, {}, closed, style};
        for (double a : angles)
            d.tangent.push_back(NewValue(a, true));
        for (const auto &[k, setting] : settings) {
            if (setting.tension != 1.0) {
                d.tension.resize(points.size(), 1.0);
                d.tension[k] = setting.tension;
            }
            if (setting.curvature) {
                d.curvature.resize(points.size(), none);
                d.curvature[k] = NewValue(*setting.curvature);
            }
        }
        splines.push_back(std::move(d));
        // raises for points and tangents that give no spline
        splines.back().Interpolated(*this);
        return Added({ElementKind::Spline, {splines.size() - 1}});
    }

    // its knots start at 0, as the elements' ranges take them; raises for ones that give no b-spline
    size_t Geometry::AddPoledSpline(const std::vector<size_t> &points, std::vector<double> knots, size_t degree, bool closed) {
        std::vector<Vertex> q;
        for (size_t p : points)
            q.push_back(Coords(p));
        BSplineCurve({q, knots, {}, (int)degree}, closed);
        const double first = knots.front();
        for (double &k : knots)
            k -= first;
        SplineData d{points, {}, {}, closed};
        d.poled = PoleShape{std::move(knots), degree};
        splines.push_back(std::move(d));
        return Added({ElementKind::Spline, {splines.size() - 1}});
    }

    size_t Geometry::LegOf(const Element &e) const {
        if (e.kind != ElementKind::Line)
            return none;
        for (size_t i = 0; i < splines.size(); ++i) {
            const SplineData &d = splines[i];
            if (!d.poled)
                continue;
            const size_t n = d.points.size();
            for (size_t k = 0; k + 1 < n + (d.closed ? 1 : 0); ++k) {
                const size_t a = d.points[k], b = d.points[(k + 1) % n];
                if ((e.Start() == a && e.End() == b) || (e.Start() == b && e.End() == a))
                    return i;
            }
        }
        return none;
    }

    void Geometry::Truncate(const Mark &m) {
        const auto cut = [](auto &v, size_t n) { v.erase(v.begin() + (std::ptrdiff_t)n, v.end()); };
        cut(values, m.values);
        cut(isAngle, m.values);
        cut(held, m.values);
        cut(ownCoordsOf, m.points);
        cut(coordsOf, m.points);
        cut(elements, m.elements);
        cut(splines, m.splines);
        cut(pieces, m.pieces);
        cut(offsetCurves, m.offsetCurves);
        for (SplineData &d : splines)
            while (!d.tangent.empty() && d.tangent.back() >= m.values) {
                d.tangent.pop_back();
                d.tangentAt.pop_back();
            }
    }

    bool Geometry::IsCentreShared(size_t point, size_t except) const {
        for (size_t k = 0; k < elements.size(); ++k) {
            if (k == except)
                continue;
            const Element &o = elements[k];
            if (o.IsRound() && o.Center() == point)
                return true;
            const std::vector<size_t> through = o.kind == ElementKind::Point ? std::vector<size_t>{o.Point()} : o.PointsOn(*this);
            if (std::find(through.begin(), through.end(), point) != through.end())
                return true;
        }
        return false;
    }

    void Geometry::RemoveElement(size_t element) {
        switch (elements[element].kind) {
        case ElementKind::Point: HoldOwnValues<ElementKind::Point>(element); break;
        case ElementKind::Line: HoldOwnValues<ElementKind::Line>(element); break;
        case ElementKind::Circle: HoldOwnValues<ElementKind::Circle>(element); break;
        case ElementKind::Arc: HoldOwnValues<ElementKind::Arc>(element); break;
        case ElementKind::Axis: HoldOwnValues<ElementKind::Axis>(element); break;
        case ElementKind::Pattern: HoldOwnValues<ElementKind::Pattern>(element); break;
        case ElementKind::Spline: HoldOwnValues<ElementKind::Spline>(element); break;
        case ElementKind::Offset: HoldOwnValues<ElementKind::Offset>(element); break;
        case ElementKind::OffsetCurve: HoldOwnValues<ElementKind::OffsetCurve>(element); break;
        }
        elements.erase(elements.begin() + (std::ptrdiff_t)element);
        for (SplineData &d : splines)
            if (d.blend)
                for (size_t *k : {&d.blend->arms[0], &d.blend->arms[1], &d.blend->helper})
                    if (*k != none && *k > element)
                        --*k;
    }

    std::vector<std::pair<size_t, Vertex>> Geometry::Mirror(const std::vector<size_t> &which, const Vertex &a, const Vertex &u) {
        const double turn = 2.0 * std::atan2(u.y, u.x);
        std::vector<size_t> points;
        std::set<size_t> seen;
        for (size_t e : which) {
            Element &el = elements[e];
            switch (el.kind) {
            case ElementKind::Point:
                points.push_back(el.Point());
                break;
            case ElementKind::Circle:
                points.push_back(el.Center());
                if (el.OriginPoint() != none)
                    points.push_back(el.OriginPoint());
                values[el.OriginAngle()] = turn - values[el.OriginAngle()];
                el.ccw = !el.ccw;
                break;
            case ElementKind::Line:
                points.insert(points.end(), {el.Start(), el.End()});
                break;
            case ElementKind::Arc:
                points.insert(points.end(), {el.Center(), el.Start(), el.End()});
                el.ccw = !el.ccw;
                break;
            default:
                if (seen.insert(el.SplineIndex()).second) {
                    SplineData &d = splines[el.SplineIndex()];
                    points.insert(points.end(), d.points.begin(), d.points.end());
                    for (size_t t : d.tangent)
                        values[t] = turn - values[t];
                    if (d.fitted) {
                        points.push_back(d.fitted->origin);
                        values[d.fitted->angle] = turn - values[d.fitted->angle];
                        d.fitted->mirrored = !d.fitted->mirrored;
                    }
                }
            }
        }
        std::set<size_t> slots;
        for (size_t q : points)
            slots.insert({coordsOf[q], ownCoordsOf[q]});
        for (size_t k : slots) {
            const Vertex m = planex2d::Mirrored(Vertex{values[k], values[k + 1]}, a, u);
            values[k] = m.x;
            values[k + 1] = m.y;
        }
        std::vector<std::pair<size_t, Vertex>> out;
        for (size_t q : points)
            out.push_back({q, Coords(q)});
        return out;
    }

    void Geometry::Scale(double f) {
        // a curvature is 1 / length
        std::vector<char> inverse(values.size(), 0);
        for (const SplineData &d : splines)
            for (size_t c : d.curvature)
                if (c != none)
                    inverse[c] = 1;
        for (size_t u = 0; u < values.size(); ++u)
            if (!isAngle[u])
                values[u] = inverse[u] ? values[u] / f : values[u] * f;
        for (SplineData &d : splines)
            if (d.fitted) {
                d.fitted->scale *= f;
                d.fitted->Rescale(f);
            }
    }

    void Geometry::MoveOrigin(const Vertex &p) {
        const size_t origin = elements[2].Point();
        for (size_t q = 0; q < ownCoordsOf.size(); ++q)
            if (q != origin) {
                values[ownCoordsOf[q]] -= p.x;
                values[ownCoordsOf[q] + 1] -= p.y;
            }
        for (size_t e = ownElements; e < elements.size(); ++e)
            if (elements[e].kind == ElementKind::Axis)
                values[elements[e].AxisDistance()] -= elements[e].AxisShift(*this, p);
    }

    bool Geometry::IsPointUnused(size_t pointElement) const {
        const size_t p = elements[pointElement].Point();
        for (size_t k = 0; k < elements.size(); ++k) {
            const std::vector<size_t> through = elements[k].PointsAndCentre(*this);
            if (k != pointElement && std::find(through.begin(), through.end(), p) != through.end())
                return false;
        }
        return true;
    }

    std::set<size_t> Geometry::CentrePoints() const {
        std::set<size_t> ends, centres;
        for (const Element &e : elements) {
            const std::vector<size_t> on = e.PointsOn(*this);
            ends.insert(on.begin(), on.end());
            if (e.IsRound())
                centres.insert(e.Center());
        }
        std::set<size_t> out;
        for (size_t e = ownElements; e < elements.size(); ++e)
            if (elements[e].kind == ElementKind::Point && centres.contains(elements[e].Point()) && !ends.contains(elements[e].Point()))
                out.insert(e);
        return out;
    }

    std::vector<size_t> Geometry::LegsGoingWith(size_t k) const {
        const Element &e = elements[k];
        if (!e.IsPoled(*this))
            return {};
        std::vector<size_t> legs;
        for (size_t j = 0; j < elements.size(); ++j) {
            const Element &other = elements[j];
            if (j != k && other.kind == ElementKind::Spline && other.SplineIndex() == e.SplineIndex())
                return {};
            if (LegOf(other) == e.SplineIndex())
                legs.push_back(j);
        }
        return legs;
    }

    void Geometry::BlendsFollow(size_t a, const std::vector<size_t> &into) {
        for (SplineData &d : splines)
            if (d.blend)
                for (size_t i = 0; i < 2; ++i)
                    if (d.blend->arms[i] == a)
                        for (size_t p : into)
                            for (size_t end : elements[p].Ends(*this))
                                if (coordsOf[end] == coordsOf[d.points[i]])
                                    d.blend->arms[i] = p;
    }

    std::optional<size_t> Geometry::BlendOn(size_t e) const {
        for (size_t k = ownElements; k < elements.size(); ++k)
            if (elements[k].kind == ElementKind::Spline)
                if (const std::optional<BlendShape> &b = splines[elements[k].SplineIndex()].blend; b && (b->arms[0] == e || b->arms[1] == e))
                    return k;
        return std::nullopt;
    }

    std::optional<Ref> Geometry::PieceEndAt(const std::vector<size_t> &pieces, size_t point) const {
        for (size_t e : pieces) {
            const std::array<size_t, 2> ends = elements[e].Ends(*this);
            if (ends[0] == point || ends[1] == point)
                return Ref{e, ends[0] == point ? Part::Start : Part::End};
        }
        return std::nullopt;
    }

    std::vector<Vertex> Geometry::PointPlaces() const {
        std::vector<Vertex> out;
        for (size_t p = 0; p < coordsOf.size(); ++p)
            out.push_back(Coords(p));
        return out;
    }

    // what a solve must not turn round, per element: a line's or axis's direction, an arc's sweep (radians); NaN for
    // the rest and for lines too short to have a direction
    std::vector<double> Geometry::Headings() const {
        const auto at = [&](size_t p) { return Vertex{values[coordsOf[p]], values[coordsOf[p] + 1]}; };
        const double tiny = config::state->branch_check_min_length * Size();
        std::vector<double> h(elements.size(), std::numeric_limits<double>::quiet_NaN());
        for (size_t e = ownElements; e < elements.size(); ++e) {
            const Element &el = elements[e];
            if (el.kind == ElementKind::Line) {
                if (const Vertex d = at(el.End()) - at(el.Start()); Norm(d) > tiny)
                    h[e] = std::atan2(d.y, d.x);
            } else if (el.kind == ElementKind::Axis)
                h[e] = values[el.AxisAngle()];
            else if (el.kind == ElementKind::Arc) {
                const Vertex c = at(el.Center()), a = at(el.Start()) - c, b = at(el.End()) - c;
                const double w = std::atan2(Cross(a, b), Dot(a, b)) * (el.ccw ? 1.0 : -1.0);
                h[e] = w <= 0.0 ? w + 2.0 * std::numbers::pi : w;
            }
        }
        return h;
    }

    std::pair<double, size_t> Geometry::MostTurned(const std::vector<double> &before, const std::vector<double> &after) const {
        double most = 0.0;
        size_t which = 0;
        for (size_t e = 0; e < before.size(); ++e) {
            const double d = after[e] - before[e];
            if (std::isnan(d))
                continue;
            const double turn = elements[e].kind == ElementKind::Arc ? std::abs(d) : std::abs(std::atan2(std::sin(d), std::cos(d)));
            if (turn > most) {
                most = turn;
                which = e;
            }
        }
        return {most, which};
    }

    // how closely sketch curves are drawn: 2e-4 of the size of the box around the elements' points, as the plots draw
    // projected outlines
    double Geometry::DrawingTol() const {
        BBox box;
        for (const Element &e : elements)
            for (size_t p : e.kind == ElementKind::Point ? std::vector<size_t>{e.Point()} : e.PointsAndCentre(*this))
                box.Grow(Coords(p));
        return 2e-4 * NormInf(box.Size());
    }

}
