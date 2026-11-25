// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "planex/boolean/snap.hpp"
#include "planex/curves/ellipse.hpp"
#include "planex/io/partplane.hpp"
#include "planex/shape/shape.hpp"
#include "planex/shape/shapeops.hpp"

namespace planex2d {

    // a compound of compounds is planex's Compound (WriteBrep writes its parts so); else each shape in the root compound is a part
    class BrepReader {
    public:
        struct Part {
            std::string path;
            Shape shape;
        };

        explicit BrepReader(std::string_view text);

        std::vector<Part> Parts();

    private:
        struct Trsf {
            std::array<double, 12> m{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
            Vector3 Point(const Vector3 &p) const {
                return {m[0] * p.x + m[1] * p.y + m[2] * p.z + m[3], m[4] * p.x + m[5] * p.y + m[6] * p.z + m[7],
                        m[8] * p.x + m[9] * p.y + m[10] * p.z + m[11]};
            }
            Vector3 Direction(const Vector3 &d) const {
                return {m[0] * d.x + m[1] * d.y + m[2] * d.z, m[4] * d.x + m[5] * d.y + m[6] * d.z, m[8] * d.x + m[9] * d.y + m[10] * d.z};
            }
            Trsf operator*(const Trsf &b) const;
            Trsf Inverse() const;
        };
        struct CurveData {
            int type = 0;
            std::vector<double> numbers;
            // a b-spline (a Bezier curve too)
            int degree = 0;
            bool periodic = false;
            std::vector<Vector3> poles;
            std::vector<double> weights, knots;
            std::vector<CurveData> basis;
        };
        struct SurfaceData {
            int type = 0;
            std::vector<double> numbers;
            std::vector<SurfaceData> basis;
        };
        struct Child {
            char orientation;
            size_t shape;
            Trsf location;
        };
        struct TShape {
            std::string type;
            Vector3 point;
            size_t curve = 0, surface = 0;
            Trsf curveLocation, surfaceLocation;
            double first = 0.0, last = 0.0;
            std::vector<Child> children;
        };

        std::string_view s;
        size_t i = 0;
        int version = 1;
        std::vector<Trsf> locations;
        std::vector<CurveData> curves;
        std::vector<SurfaceData> surfaces;
        std::vector<TShape> shapes;
        std::optional<Child> root;
        AxisSystem axes;
        std::unordered_map<size_t, EdgeList> edgesOf;

        [[noreturn]] void Fail(const std::string &what) const {
            throw std::invalid_argument("FromBrep: " + what + " (at character " + std::to_string(i) + ")");
        }
        std::string_view Token() {
            while (i < s.size() && std::isspace((unsigned char)s[i]))
                ++i;
            const size_t start = i;
            while (i < s.size() && !std::isspace((unsigned char)s[i]))
                ++i;
            if (start == i)
                Fail("unexpected end of the file");
            return s.substr(start, i - start);
        }
        void Skip(std::string_view word) {
            if (Token() != word)
                Fail("expected '" + std::string(word) + "'");
        }
        double Number() {
            const std::string_view t = Token();
            const std::optional<double> x = NumberOf(t);
            if (!x)
                Fail("'" + std::string(t) + "' is no number");
            return *x;
        }
        size_t Count() {
            const double x = Number();
            if (!(x >= 0.0 && x == std::floor(x)))
                Fail("expected a count");
            return (size_t)x;
        }
        void SkipNumbers(size_t n) {
            for (size_t k = 0; k < n; ++k)
                Number();
        }
        Vector3 Xyz() {
            const double x = Number(), y = Number(), z = Number();
            return {x, y, z};
        }

        Trsf LocationRecord();
        Trsf LocationAt(size_t index) const { return index == 0 ? Trsf{} : locations.at(index - 1); }

        void SkipCurve(int dim);
        CurveData CurveRecord();
        std::vector<double> Numbers(size_t n) {
            std::vector<double> v(n);
            for (double &x : v)
                x = Number();
            return v;
        }
        SurfaceData SurfaceRecord();

        std::optional<Child> ChildRecord();
        void VertexRecord(TShape &t);
        void EdgeRecord(TShape &t);
        TShape ShapeRecord();

        static Child Located(const Child &parent, const Child &c) { return {c.orientation, c.shape, parent.location * c.location}; }

        bool IsLeaf(const Child &c) const {
            const TShape &t = shapes.at(c.shape);
            return std::none_of(t.children.begin(), t.children.end(), [&](const Child &x) { return shapes.at(x.shape).type == "Co"; });
        }
        void Planex(const Child &node, const std::string &path, std::vector<Part> &parts);

        void Collect(const Child &c, std::vector<Child> &faces, std::vector<Child> &edges) const {
            const TShape &t = shapes.at(c.shape);
            if (t.type == "Fa")
                faces.push_back(c);
            else if (t.type == "Ed")
                edges.push_back(c);
            else if (t.type != "Ve")
                for (const Child &x : t.children)
                    Collect(Located(c, x), faces, edges);
        }

        AxisSystem PlaneOf(const Child &face) const;
        Vertex InPlane(const Vector3 &p) const {
            const double off = Dot(p - axes.Origin(), axes.Normal());
            if (std::abs(off) > PlaneSlack(axes, p))
                Fail("a point lies " + NumberText(std::abs(off)) + " off the plane of its part" + readsCurvedShapes);
            return axes.ToHV(p);
        }

        EdgeList EdgeOf(const Child &c);
        EdgeList CurveEdges(const CurveData &c, const Trsf &l, const Vertex &p, const Vertex &q, double first, double last);
        // the ends exactly on the vertices, so that neighbours meet
        static void Ends(EdgeList &edges, const Vertex &p, const Vertex &q) {
            if (edges.empty() || p == q)
                return;
            std::get<Spline>(edges.front()).Trim(p, std::get<Spline>(edges.front()).End());
            std::get<Spline>(edges.back()).Trim(std::get<Spline>(edges.back()).Start(), q);
        }

        Shape PartShape(const std::vector<Child> &items);
        AxisSystem EdgesPlane(const std::vector<Child> &edges) const;
    };

}
