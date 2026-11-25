// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/io/brepwrite.hpp"

#include <algorithm>
#include <numbers>

namespace planex2d {

    // path: "" the object itself, "1.0" part 0 of part 1 of a Compound
    void BrepWriter::Part(const Shape &shape, const std::string &path, const ArcFit &arcs, const SplineFit &fit, const Cleanup &cleanup) {
        const Shape::ExportPlan plan = shape.Export(arcs, fit, cleanup);
        const Wire &c = plan.edges;
        const AxisSystem &axes = c.Axes();
        vertices.clear();
        edgeOf.assign(c.Size(), npos);
        std::vector<Child> children;
        const size_t surface = Surface(axes);
        for (size_t k = 0; k < plan.groupProps.size(); ++k)
            for (const std::vector<size_t> &faceEdges : plan.facesOfGroup[k]) {
                std::vector<Child> wires;
                for (const FaceLoop &loop : FaceLoops(c, faceEdges)) {
                    std::vector<Child> edges;
                    for (const auto &[i, forward] : loop.edges)
                        edges.push_back({forward ? '+' : '-', EdgeOf(c, i)});
                    wires.push_back({'+', Add("Wi", "", "0101100", edges)});
                }
                children.push_back({'+', Add("Fa", "0  1e-07 " + std::to_string(surface) + " 0\n", "1101000", wires)});
            }
        for (size_t i = 0; i < c.Size(); ++i)
            if (!plan.IsOnGroup(i))
                children.push_back({'+', EdgeOf(c, i)});
        parts.emplace_back(path, Add("Co", "", "1100000", children));
    }

    std::string BrepWriter::Text() {
        // the parts under their Compounds by their paths ("1.0" under "1" under the root), the root last
        std::map<std::string, std::vector<std::pair<std::string, size_t>>> under;
        const auto parent = [](const std::string &path) {
            const size_t dot = path.rfind('.');
            return dot == std::string::npos ? std::string() : path.substr(0, dot);
        };
        for (const auto &[path, shape] : parts) {
            under[parent(path)].emplace_back(path, shape);
            for (std::string q = parent(path); !q.empty(); q = parent(q)) {
                std::vector<std::pair<std::string, size_t>> &siblings = under[parent(q)];
                if (std::none_of(siblings.begin(), siblings.end(), [&](const auto &x) { return x.first == q; }))
                    siblings.emplace_back(q, npos);
            }
        }
        const size_t root = Nested("", under);
        std::string out = "DBRep_DrawableShape\n\nCASCADE Topology V3, (c) Open Cascade\nLocations 0\nCurve2ds 0\nCurves " +
                          std::to_string(curves.size()) + "\n";
        for (const std::string &c : curves)
            out += c + "\n";
        out += "Polygon3D 0\nPolygonOnTriangulations 0\nSurfaces " + std::to_string(surfaces.size()) + "\n";
        for (const std::string &s : surfaces)
            out += s + "\n";
        out += "Triangulations 0\n\nTShapes " + std::to_string(shapes.size()) + "\n";
        for (const Record &r : shapes) {
            out += r.type + "\n" + r.geometry + "\n" + r.flags + "\n";
            for (const Child &c : r.children)
                out += c.orientation + std::to_string(Number(c.shape)) + " 0 ";
            out += "*\n";
        }
        return out + "\n+" + std::to_string(Number(root)) + " 0\n";
    }

    std::pair<size_t, std::pair<double, double>> BrepWriter::CurveOf(const Edge &e, const AxisSystem &axes) {
        if (const Line *l = std::get_if<Line>(&e)) {
            const Vector3 a = axes.ToXYZ(l->Start()), b = axes.ToXYZ(l->End());
            curves.push_back("1 " + Xyz(a) + " " + Xyz(Normalized(b - a)));
            return {curves.size(), {0.0, Norm(b - a)}};
        }
        const auto circle = [&](const Vertex &center, const Vertex &start, double r, bool ccw, double sweep) {
            const Vector3 c = axes.ToXYZ(center), n = ccw ? axes.Normal() : -axes.Normal();
            const Vector3 x = Normalized(axes.ToXYZ(start) - c);
            curves.push_back("2 " + Xyz(c) + " " + Xyz(n) + " " + Xyz(x) + " " + Xyz(Cross(n, x)) + " " + N(r));
            return std::pair<size_t, std::pair<double, double>>{curves.size(), {0.0, sweep}};
        };
        if (const Arc *a = std::get_if<Arc>(&e))
            return circle(a->Center(), a->Start(), a->Radius(), a->CCW(), a->Sweep());
        if (const Circle *c = std::get_if<Circle>(&e))
            return circle(c->Center(), c->Origin(), c->Radius(), c->CCW(), 2.0 * std::numbers::pi);
        const BSplineData d = std::get<Spline>(e).BSpline();
        std::vector<std::pair<double, int>> knots;
        for (double k : d.knots)
            if (!knots.empty() && knots.back().first == k)
                ++knots.back().second;
            else
                knots.emplace_back(k, 1);
        std::string text = "7 " + std::string(d.weights.empty() ? "0" : "1") + " 0 " + std::to_string(d.degree) + " " +
                           std::to_string(d.poles.size()) + " " + std::to_string(knots.size()) + " ";
        for (size_t k = 0; k < d.poles.size(); ++k)
            text += " " + Xyz(axes.ToXYZ(d.poles[k])) + (d.weights.empty() ? "" : " " + N(d.weights[k])) + " ";
        for (const auto &[u, m] : knots)
            text += " " + N(u) + " " + std::to_string(m) + " ";
        curves.push_back(text);
        return {curves.size(), {d.knots.front(), d.knots.back()}};
    }

}
