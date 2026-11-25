// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/io/brepread.hpp"

#include <utility>

namespace planex2d {

    BrepReader::BrepReader(std::string_view text): s(text) {
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

    std::vector<BrepReader::Part> BrepReader::Parts() {
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

    BrepReader::Trsf BrepReader::Trsf::operator*(const Trsf &b) const {
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

    BrepReader::Trsf BrepReader::Trsf::Inverse() const {
        // a rigid or similarity map
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

    BrepReader::Trsf BrepReader::LocationRecord() {
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

    void BrepReader::SkipCurve(int dim) {
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

    BrepReader::CurveData BrepReader::CurveRecord() {
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

    BrepReader::SurfaceData BrepReader::SurfaceRecord() {
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

    std::optional<BrepReader::Child> BrepReader::ChildRecord() {
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

    void BrepReader::VertexRecord(TShape &t) {
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
    }

    void BrepReader::EdgeRecord(TShape &t) {
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
    }

    BrepReader::TShape BrepReader::ShapeRecord() {
        TShape t;
        t.type = std::string(Token());
        if (t.type == "Ve") {
            VertexRecord(t);
        } else if (t.type == "Ed") {
            EdgeRecord(t);
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

    void BrepReader::Planex(const Child &node, const std::string &path, std::vector<Part> &parts) {
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

    AxisSystem BrepReader::PlaneOf(const Child &face) const {
        const TShape &t = shapes.at(face.shape);
        const SurfaceData *f = &surfaces.at(t.surface - 1);
        double offset = 0.0;
        for (; f->type == 10 || f->type == 11; f = &f->basis.front())
            if (f->type == 11)
                offset += f->numbers.front();
        if (f->type != 1)
            Fail(std::string("a face lies on a curved surface") + readsCurvedShapes);
        const Trsf l = face.location * t.surfaceLocation;
        const std::vector<double> &n = f->numbers;
        const Vector3 normal{n[3], n[4], n[5]};
        return AxisSystem(l.Point(Vector3{n[0], n[1], n[2]} + normal * offset), l.Direction(normal), l.Direction({n[6], n[7], n[8]}));
    }

    EdgeList BrepReader::EdgeOf(const Child &c) {
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

    EdgeList BrepReader::CurveEdges(const CurveData &c, const Trsf &l, const Vertex &p, const Vertex &q, double first, double last) {
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

    Shape BrepReader::PartShape(const std::vector<Child> &items) {
        std::vector<Child> faces, edges;
        for (const Child &c : items)
            Collect(c, faces, edges);
        if (faces.empty() && edges.empty())
            Fail("a part without faces or edges");
        axes = faces.empty() ? EdgesPlane(edges) : PlaneOf(faces.front());
        edgesOf.clear();
        ShapeList parts;
        for (const Child &f : faces) {
            if (!IsSamePlane(PlaneOf(f), axes))
                Fail(std::string("the faces of a part lie on several planes") + readsCurvedShapes);
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

    AxisSystem BrepReader::EdgesPlane(const std::vector<Child> &edges) const {
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
        if (const std::optional<AxisSystem> plane = PlaneThrough(pts))
            return *plane;
        return pts.size() > 1 ? AxesAlong(pts[0], std::abs(Normalized(pts[1] - pts[0]).z) < 0.9 ? Vector3{0.0, 0.0, 1.0} : Vector3{1.0, 0.0, 0.0})
                              : AxisSystem();
    }

}
