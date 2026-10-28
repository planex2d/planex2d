// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "boolean/snap.hpp"
#include "curves/ellipse.hpp"
#include "shape/shape.hpp"
#include "shape/shapeops.hpp"

namespace planex2d {

    // the planar faces and edges of a file in OCC's BRep text format (BRepTools) as planex objects, without OCC: the
    // planes of the faces and the 3d curves of the edges (lines, circles, ellipses, b-splines, trimmed curves), their
    // ends from the edge's vertices; locations applied; the sections a planar shape does not need (2d curves, polygons,
    // triangulations) read past. A compound of compounds is planex's Compound (WriteBrep writes its parts so); else each
    // shape in the root compound is a part. Each part lies in one plane, its faces cleaned one by one and glued
    class BrepReader {
    public:
        struct Part {
            std::string path;
            Shape shape;
        };

        explicit BrepReader(std::string_view text) : s(text) {
            const size_t start = s.find("CASCADE Topology V");
            if (start == std::string_view::npos)
                Fail("not a BRep file (no 'CASCADE Topology')");
            // "CASCADE Topology V3, (c) Open Cascade": the format's version, then the sections
            version = start + 18 < s.size() && std::isdigit((unsigned char)s[start + 18]) ? s[start + 18] - '0' : 1;
            i = s.find("Locations", start);
            if (i == std::string_view::npos)
                Fail("no 'Locations' section");
            Skip("Locations");
            const size_t nl = Count();
            for (size_t k = 0; k < nl; ++k)
                locations.push_back(LocationRecord());
            Skip("Curve2ds");
            const size_t n2 = Count();
            for (size_t k = 0; k < n2; ++k)
                SkipCurve(2);
            Skip("Curves");
            const size_t n3 = Count();
            for (size_t k = 0; k < n3; ++k)
                curves.push_back(CurveRecord());
            Skip("Polygon3D");
            const size_t np = Count();
            for (size_t k = 0; k < np; ++k) {
                const size_t nodes = Count();
                const bool params = Count() != 0;
                Number();
                SkipNumbers(nodes * 3 + (params ? nodes : 0));
            }
            Skip("PolygonOnTriangulations");
            const size_t npt = Count();
            for (size_t k = 0; k < npt; ++k) {
                const size_t nodes = Count();
                SkipNumbers(nodes);
                // "p deflection parameters" or the deflection alone
                if (Token() == "p")
                    SkipNumbers(1 + nodes);
            }
            Skip("Surfaces");
            const size_t ns = Count();
            for (size_t k = 0; k < ns; ++k)
                surfaces.push_back(SurfaceRecord());
            Skip("Triangulations");
            const size_t nt = Count();
            for (size_t k = 0; k < nt; ++k) {
                const size_t nodes = Count(), triangles = Count();
                const bool uv = Count() != 0;
                const bool normals = version >= 3 && Count() != 0;
                Number();
                SkipNumbers(nodes * 3 + (uv ? nodes * 2 : 0) + triangles * 3 + (normals ? nodes * 3 : 0));
            }
            Skip("TShapes");
            const size_t n = Count();
            shapes.resize(n);
            for (size_t k = 0; k < n; ++k)
                shapes[n - 1 - k] = ShapeRecord();
            root = ChildRecord();
            if (!root)
                Fail("no root shape");
        }

        std::vector<Part> Parts() {
            std::vector<Part> parts;
            const TShape &r = shapes.at(root->shape);
            const bool nested = r.type == "Co" && !r.children.empty() &&
                                std::all_of(r.children.begin(), r.children.end(), [&](const Child &c) { return shapes.at(c.shape).type == "Co"; });
            if (nested && r.children.size() == 1 && IsLeaf(r.children.front()))
                parts.push_back({"", PartShape({Located(*root, r.children.front())})});
            else if (nested)
                Planex(*root, "", parts);
            else if (r.type == "Co")
                for (const Child &c : r.children)
                    parts.push_back({std::to_string(parts.size()), PartShape({Located(*root, c)})});
            else
                parts.push_back({"", PartShape({*root})});
            return parts;
        }

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
            Trsf operator*(const Trsf &b) const {
                Trsf r;
                for (int row = 0; row < 3; ++row) {
                    for (int col = 0; col < 4; ++col) {
                        double v = col == 3 ? m[row * 4 + 3] : 0.0;
                        for (int k = 0; k < 3; ++k)
                            v += m[row * 4 + k] * b.m[k * 4 + col];
                        r.m[row * 4 + col] = v;
                    }
                }
                return r;
            }
            Trsf Inverse() const {
                // a rigid or similarity map: the inverse of the 3x3 part by its adjugate
                const auto &a = m;
                const double det = a[0] * (a[5] * a[10] - a[6] * a[9]) - a[1] * (a[4] * a[10] - a[6] * a[8]) + a[2] * (a[4] * a[9] - a[5] * a[8]);
                Trsf r;
                r.m = {(a[5] * a[10] - a[6] * a[9]) / det, (a[2] * a[9] - a[1] * a[10]) / det, (a[1] * a[6] - a[2] * a[5]) / det, 0.0,
                       (a[6] * a[8] - a[4] * a[10]) / det, (a[0] * a[10] - a[2] * a[8]) / det, (a[2] * a[4] - a[0] * a[6]) / det, 0.0,
                       (a[4] * a[9] - a[5] * a[8]) / det, (a[1] * a[8] - a[0] * a[9]) / det, (a[0] * a[5] - a[1] * a[4]) / det, 0.0};
                const Vector3 t = r.Direction({a[3], a[7], a[11]});
                r.m[3] = -t.x;
                r.m[7] = -t.y;
                r.m[11] = -t.z;
                return r;
            }
        };
        struct CurveData {
            int type = 0;
            std::vector<double> numbers;
            // a b-spline (a Bezier curve too)
            int degree = 0;
            bool periodic = false;
            std::vector<Vector3> poles;
            std::vector<double> weights, knots;
            // a trimmed curve's basis
            std::vector<CurveData> basis;
        };
        struct SurfaceData {
            int type = 0;
            std::vector<double> numbers;
            // a trimmed or offset surface's basis
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
        // what reads curved and 3d shapes (FromOCC reads planar ones only)
        static constexpr const char *inNetgen = "; netgen.occ reads the file (OCCGeometry(path).shape), px.Intersect3d(plane, shape) cuts a section from it, px.Project(shape, plane) projects it";
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

        Trsf LocationRecord() {
            const size_t type = Count();
            if (type == 1) {
                Trsf t;
                for (double &v : t.m)
                    v = Number();
                return t;
            }
            Trsf t;
            while (true) {
                const size_t index = Count();
                if (index == 0)
                    return t;
                const long power = (long)Number();
                Trsf l = locations.at(index - 1);
                if (power < 0)
                    l = l.Inverse();
                for (long k = 0; k < std::abs(power); ++k)
                    t = t * l;
            }
        }
        Trsf LocationAt(size_t index) const { return index == 0 ? Trsf{} : locations.at(index - 1); }

        // reads past a curve record of dimension dim (2 or 3)
        void SkipCurve(int dim) {
            const size_t type = Count();
            switch (type) {
                case 1: SkipNumbers(2 * dim); break;
                case 2: SkipNumbers(dim == 2 ? 7 : 13); break;
                case 3: case 5: SkipNumbers(dim == 2 ? 8 : 14); break;
                case 4: SkipNumbers(dim == 2 ? 7 : 13); break;
                case 6: {
                    const bool rational = Count() != 0;
                    const size_t degree = Count();
                    SkipNumbers((degree + 1) * (dim + (rational ? 1 : 0)));
                    break;
                }
                case 7: {
                    const bool rational = Count() != 0;
                    Count();
                    Count();
                    const size_t poles = Count(), knots = Count();
                    SkipNumbers(poles * (dim + (rational ? 1 : 0)) + knots * 2);
                    break;
                }
                case 8: SkipNumbers(2); SkipCurve(dim); break;
                case 9: SkipNumbers(dim == 2 ? 1 : 4); SkipCurve(dim); break;
                default: Fail("a curve of type " + std::to_string(type));
            }
        }
        CurveData CurveRecord() {
            CurveData c;
            c.type = (int)Count();
            const auto pole = [&](bool rational) {
                c.poles.push_back(Xyz());
                if (rational)
                    c.weights.push_back(Number());
            };
            switch (c.type) {
                case 1: c.numbers = Numbers(6); break;
                case 2: case 4: c.numbers = Numbers(13); break;
                case 3: case 5: c.numbers = Numbers(14); break;
                case 6: {
                    const bool rational = Count() != 0;
                    c.degree = (int)Count();
                    for (int k = 0; k <= c.degree; ++k)
                        pole(rational);
                    c.knots.assign((size_t)c.degree + 1, 0.0);
                    c.knots.resize(2 * ((size_t)c.degree + 1), 1.0);
                    c.type = 7;
                    break;
                }
                case 7: {
                    const bool rational = Count() != 0;
                    c.periodic = Count() != 0;
                    c.degree = (int)Count();
                    const size_t poles = Count(), knots = Count();
                    for (size_t k = 0; k < poles; ++k)
                        pole(rational);
                    for (size_t k = 0; k < knots; ++k) {
                        const double u = Number();
                        c.knots.insert(c.knots.end(), Count(), u);
                    }
                    break;
                }
                case 8: c.numbers = Numbers(2); c.basis.push_back(CurveRecord()); break;
                default: Fail("a 3d curve of type " + std::to_string(c.type) + " (offset or unknown) is not read; FromOCC reads it");
            }
            return c;
        }
        std::vector<double> Numbers(size_t n) {
            std::vector<double> v(n);
            for (double &x : v)
                x = Number();
            return v;
        }
        SurfaceData SurfaceRecord() {
            SurfaceData f;
            f.type = (int)Count();
            switch (f.type) {
                case 1: f.numbers = Numbers(12); break;
                case 2: case 4: SkipNumbers(13); break;
                case 3: case 5: SkipNumbers(14); break;
                case 6: SkipNumbers(3); SkipCurve(3); break;
                case 7: SkipNumbers(6); SkipCurve(3); break;
                case 8: {
                    const bool ur = Count() != 0, vr = Count() != 0;
                    const size_t ud = Count(), vd = Count();
                    SkipNumbers((ud + 1) * (vd + 1) * (3 + (ur || vr ? 1 : 0)));
                    break;
                }
                case 9: {
                    const bool ur = Count() != 0, vr = Count() != 0;
                    Count();
                    Count();
                    Count();
                    Count();
                    const size_t nu = Count(), nv = Count(), ku = Count(), kv = Count();
                    SkipNumbers(nu * nv * (3 + (ur || vr ? 1 : 0)) + 2 * (ku + kv));
                    break;
                }
                case 10: SkipNumbers(4); f.basis.push_back(SurfaceRecord()); break;
                case 11: f.numbers = Numbers(1); f.basis.push_back(SurfaceRecord()); break;
                default: Fail("a surface of type " + std::to_string(f.type));
            }
            return f;
        }

        std::optional<Child> ChildRecord() {
            const std::string_view t = Token();
            if (t == "*")
                return std::nullopt;
            if (t.size() < 2 || (t[0] != '+' && t[0] != '-' && t[0] != 'i' && t[0] != 'e'))
                Fail("expected a sub-shape '+n loc'");
            const std::optional<double> n = NumberOf(t.substr(1));
            if (!n || *n < 1.0)
                Fail("a bad sub-shape '" + std::string(t) + "'");
            return Child{t[0], (size_t)*n - 1, LocationAt(Count())};
        }
        TShape ShapeRecord() {
            TShape t;
            t.type = std::string(Token());
            if (t.type == "Ve") {
                Number();
                t.point = Xyz();
                while (true) {
                    const size_t rep = Count();
                    if (rep == 0) {
                        Count();
                        break;
                    }
                    if (rep == 1)
                        SkipNumbers(3);
                    else if (rep == 2)
                        SkipNumbers(4);
                    else if (rep == 3)
                        SkipNumbers(4);
                    else
                        Fail("a vertex representation " + std::to_string(rep));
                }
            } else if (t.type == "Ed") {
                SkipNumbers(4);
                while (true) {
                    const size_t rep = Count();
                    if (rep == 0)
                        break;
                    if (rep == 1) {
                        t.curve = Count();
                        t.curveLocation = LocationAt(Count());
                        t.first = Number();
                        t.last = Number();
                    } else if (rep == 2 || rep == 3) {
                        // a curve on a surface: pcurve, surface, location, first, last; on a closed surface (3) a second
                        // pcurve and the continuity before the surface (OCC writes them as one word, "4CN"); the uv
                        // points of the ends in format 2
                        Count();
                        if (rep == 3 && NumberOf(Token()))
                            Token();
                        SkipNumbers(4);
                        if (version == 2)
                            SkipNumbers(rep == 3 ? 8 : 4);
                    } else if (rep == 4) {
                        Token();
                        SkipNumbers(4);
                    } else if (rep == 5 || rep == 6 || rep == 7) {
                        SkipNumbers(rep - 3);
                    } else {
                        Fail("an edge representation " + std::to_string(rep) + " (polygons) is not read; FromOCC reads the file");
                    }
                }
            } else if (t.type == "Fa") {
                Number();
                Number();
                t.surface = Count();
                t.surfaceLocation = LocationAt(Count());
            } else if (t.type != "Wi" && t.type != "Sh" && t.type != "So" && t.type != "CS" && t.type != "Co") {
                Fail("a shape of type '" + t.type + "'");
            }
            std::string_view flags = Token();
            if (t.type == "Fa" && flags == "2") {
                Count();
                flags = Token();
            }
            if (flags.size() != 7)
                Fail("expected the flags of a shape");
            while (std::optional<Child> c = ChildRecord())
                t.children.push_back(*c);
            return t;
        }

        static Child Located(const Child &parent, const Child &c) { return {c.orientation, c.shape, parent.location * c.location}; }

        // planex's Compound: compounds of compounds, the innermost ones (of faces and edges) its parts
        bool IsLeaf(const Child &c) const {
            const TShape &t = shapes.at(c.shape);
            return std::none_of(t.children.begin(), t.children.end(), [&](const Child &x) { return shapes.at(x.shape).type == "Co"; });
        }
        void Planex(const Child &node, const std::string &path, std::vector<Part> &parts) {
            const TShape &t = shapes.at(node.shape);
            for (size_t k = 0; k < t.children.size(); ++k) {
                const Child c = Located(node, t.children[k]);
                const std::string p = path.empty() ? std::to_string(k) : path + "." + std::to_string(k);
                if (IsLeaf(c))
                    parts.push_back({p, PartShape({c})});
                else
                    Planex(c, p, parts);
            }
        }

        // the faces and free edges under the shapes, with the locations they are under
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

        AxisSystem PlaneOf(const Child &face) const {
            const TShape &t = shapes.at(face.shape);
            const SurfaceData *f = &surfaces.at(t.surface - 1);
            double offset = 0.0;
            for (; f->type == 10 || f->type == 11; f = &f->basis.front())
                if (f->type == 11)
                    offset += f->numbers.front();
            if (f->type != 1)
                Fail(std::string("a face lies on a curved surface") + inNetgen);
            const Trsf l = face.location * t.surfaceLocation;
            const std::vector<double> &n = f->numbers;
            const Vector3 normal{n[3], n[4], n[5]};
            return AxisSystem(l.Point(Vector3{n[0], n[1], n[2]} + normal * offset), l.Direction(normal), l.Direction({n[6], n[7], n[8]}));
        }
        Vertex InPlane(const Vector3 &p) const {
            const double off = Dot(p - axes.Origin(), axes.Normal());
            if (std::abs(off) > config::state->axes_distance + 1e-10 * Norm(p - axes.Origin()))
                Fail("a point lies " + NumberText(std::abs(off)) + " off the plane of its part" + inNetgen);
            return axes.ToHV(p);
        }
        bool SamePlane(const AxisSystem &a) const {
            return std::abs(std::abs(Dot(a.Normal(), axes.Normal())) - 1.0) <= 1e-10 &&
                   std::abs(Dot(a.Origin() - axes.Origin(), axes.Normal())) <= config::state->axes_distance + 1e-10 * Norm(a.Origin() - axes.Origin());
        }

        // an edge between its vertices along its 3d curve over [first, last]
        EdgeList EdgeOf(const Child &c) {
            if (const auto it = edgesOf.find(c.shape); it != edgesOf.end())
                return it->second;
            const TShape &t = shapes.at(c.shape);
            if (t.curve == 0)
                return {};
            std::optional<Vector3> a, b;
            for (const Child &v : t.children) {
                const Vector3 p = (c.location * v.location).Point(shapes.at(v.shape).point);
                (v.orientation == '+' ? a : b) = p;
            }
            if (!a || !b)
                Fail("an edge without its two vertices");
            const Trsf l = c.location * t.curveLocation;
            return edgesOf[c.shape] = CurveEdges(curves.at(t.curve - 1), l, InPlane(*a), InPlane(*b), t.first, t.last);
        }
        EdgeList CurveEdges(const CurveData &c, const Trsf &l, const Vertex &p, const Vertex &q, double first, double last) {
            EdgeList out;
            const std::vector<double> &n = c.numbers;
            if (c.type == 1) {
                if (p != q)
                    out.push_back(Line(p, q));
                return out;
            }
            if (c.type == 2 || c.type == 3) {
                const Vector3 center = l.Point({n[0], n[1], n[2]}), axis = l.Direction({n[3], n[4], n[5]});
                const Vector3 x = l.Direction({n[6], n[7], n[8]}), y = l.Direction({n[9], n[10], n[11]});
                const Vertex o = InPlane(center);
                if (c.type == 2) {
                    const bool ccw = Dot(axis, axes.Normal()) > 0.0;
                    if (p == q)
                        out.push_back(Circle(o, p, ccw));
                    else
                        out.push_back(Arc(p, q, o, ccw));
                    return out;
                }
                const Vertex A = InPlane(center + x * n[12]) - o, B = InPlane(center + y * n[13]) - o;
                for (Spline &s : Spline::FromCurve(BSplineCurve(EllipseData(o, A, B, first, last), false)))
                    out.push_back(std::move(s));
                Ends(out, p, q);
                return out;
            }
            if (c.type == 8)
                return CurveEdges(c.basis.front(), l, p, q, first, last);
            if (c.type == 7) {
                BSplineData d{{}, c.knots, c.weights, c.degree};
                for (const Vector3 &pole : c.poles)
                    d.poles.push_back(InPlane(l.Point(pole)));
                std::vector<Spline> pieces = Spline::FromCurve(BSplineCurve(d, c.periodic));
                const bool whole = first <= d.knots.front() && last >= d.knots.back();
                if (!whole) {
                    if (pieces.size() != 1)
                        Fail("an edge on a part of a spline of several pieces is not read yet; FromOCC reads the file");
                    pieces.front() = pieces.front().Part(p, q);
                }
                for (Spline &s : pieces)
                    out.push_back(std::move(s));
                Ends(out, p, q);
                return out;
            }
            Fail("a curve of type " + std::to_string(c.type) + " is not read; FromOCC reads the file");
        }
        // the ends exactly on the vertices, so that neighbours meet
        static void Ends(EdgeList &edges, const Vertex &p, const Vertex &q) {
            if (edges.empty() || p == q)
                return;
            std::get<Spline>(edges.front()).Trim(p, std::get<Spline>(edges.front()).End());
            std::get<Spline>(edges.back()).Trim(std::get<Spline>(edges.back()).Start(), q);
        }

        Shape PartShape(const std::vector<Child> &items) {
            std::vector<Child> faces, edges;
            for (const Child &c : items)
                Collect(c, faces, edges);
            if (faces.empty() && edges.empty())
                Fail("a part without faces or edges");
            axes = faces.empty() ? EdgesPlane(edges) : PlaneOf(faces.front());
            edgesOf.clear();
            ShapeList parts;
            for (const Child &f : faces) {
                if (!SamePlane(PlaneOf(f)))
                    Fail(std::string("the faces of a part lie on several planes") + inNetgen);
                std::vector<Child> none, faceEdges;
                for (const Child &w : shapes.at(f.shape).children)
                    Collect(Located(f, w), none, faceEdges);
                Wire w(axes);
                for (const Child &e : faceEdges)
                    for (const Edge &x : EdgeOf(e))
                        w.Add(x);
                if (Wire region = InitialClean(w); !region.Empty())
                    parts.emplace_back(std::move(region), nullptr);
            }
            Wire free(axes);
            for (const Child &e : edges)
                for (const Edge &x : EdgeOf(e))
                    free.Add(x);
            if (!free.Empty())
                parts.emplace_back(axes, FaceList{}, WireList{free});
            return parts.size() == 1 ? parts.front() : Glue(parts);
        }
        // the plane of edges without faces: of a circle among them, else of three of their vertices not on a line
        AxisSystem EdgesPlane(const std::vector<Child> &edges) const {
            std::vector<Vector3> pts;
            for (const Child &c : edges) {
                const TShape &t = shapes.at(c.shape);
                if (t.curve) {
                    const CurveData &cd = curves.at(t.curve - 1);
                    if (cd.type == 2 || cd.type == 3) {
                        const Trsf l = c.location * t.curveLocation;
                        const std::vector<double> &n = cd.numbers;
                        return AxisSystem(l.Point({n[0], n[1], n[2]}), l.Direction({n[3], n[4], n[5]}), l.Direction({n[6], n[7], n[8]}));
                    }
                }
                for (const Child &v : t.children)
                    pts.push_back((c.location * v.location).Point(shapes.at(v.shape).point));
            }
            for (size_t a = 1; a < pts.size(); ++a)
                for (size_t b = a + 1; b < pts.size(); ++b)
                    if (const Vector3 n = Cross(pts[a] - pts[0], pts[b] - pts[0]); Norm(n) > 1e-9 * Dot(pts[a] - pts[0], pts[a] - pts[0]))
                        return AxisSystem(pts[0], n, pts[a] - pts[0]);
            return pts.size() > 1 ? AxesAlong(pts[0], std::abs(Normalized(pts[1] - pts[0]).z) < 0.9 ? Vector3{0.0, 0.0, 1.0} : Vector3{1.0, 0.0, 0.0})
                                  : AxisSystem();
        }
    };

}
