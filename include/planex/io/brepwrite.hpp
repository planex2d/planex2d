// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "planex/io/faceloops.hpp"
#include "planex/shape/shape.hpp"

namespace planex2d {

    // as OCC writes planar shapes (it finds the curves in a plane's parameters itself); the file has no names or other
    // properties. The root is a compound of the parts; a Compound's parts are compounds in it
    class BrepWriter {
    public:
        void Part(const Shape &shape, const std::string &path, const ArcFit &arcs, const SplineFit &fit, const Cleanup &cleanup);

        std::string Text();

    private:
        static constexpr size_t npos = (size_t)-1;
        struct Child {
            char orientation;
            size_t shape;
        };
        struct Record {
            std::string type, geometry, flags;
            std::vector<Child> children;
        };
        std::vector<std::string> curves, surfaces;
        std::vector<Record> shapes;
        std::vector<std::pair<std::string, size_t>> parts;
        std::map<std::pair<double, double>, size_t> vertices;
        std::vector<size_t> edgeOf;

        // the shapes are numbered from the end: the first written is the last number
        size_t Number(size_t written) const { return shapes.size() - written; }
        size_t Add(const char *type, std::string geometry, const char *flags, std::vector<Child> children) {
            shapes.push_back({type, std::move(geometry), flags, std::move(children)});
            return shapes.size() - 1;
        }
        static std::string N(double x) { return NumberText(x == 0.0 ? 0.0 : x); }
        static std::string Xyz(const Vector3 &v) { return N(v.x) + " " + N(v.y) + " " + N(v.z); }

        size_t Nested(const std::string &path, const std::map<std::string, std::vector<std::pair<std::string, size_t>>> &under) {
            std::vector<Child> children;
            const auto it = under.find(path);
            if (it != under.end())
                for (const auto &[child, shape] : it->second)
                    children.push_back({'+', shape == npos ? Nested(child, under) : shape});
            return Add("Co", "", "1100000", children);
        }

        size_t Surface(const AxisSystem &a) {
            surfaces.push_back("1 " + Xyz(a.Origin()) + " " + Xyz(a.Normal()) + " " + Xyz(a.XDirection()) + " " + Xyz(a.YDirection()));
            return surfaces.size();
        }
        size_t Vertex3(const Vertex &uv, const AxisSystem &axes) {
            const auto [it, added] = vertices.try_emplace({uv.x, uv.y}, 0);
            if (added)
                it->second = Add("Ve", "1e-07\n" + Xyz(axes.ToXYZ(uv)) + "\n0 0\n", "0101101", {});
            return it->second;
        }
        std::pair<size_t, std::pair<double, double>> CurveOf(const Edge &e, const AxisSystem &axes);
        size_t EdgeOf(const Wire &c, size_t i) {
            if (edgeOf[i] != npos)
                return edgeOf[i];
            const Edge &e = c[i];
            const size_t v0 = Vertex3(StartOf(e), c.Axes()), v1 = Vertex3(EndOf(e), c.Axes());
            const auto [curve, range] = CurveOf(e, c.Axes());
            const std::string geometry = " 1e-07 1 1 0\n1  " + std::to_string(curve) + " 0 " + N(range.first) + " " + N(range.second) + "\n0\n";
            return edgeOf[i] = Add("Ed", geometry, "0101000", {{'+', v0}, {'-', v1}});
        }
    };

}
