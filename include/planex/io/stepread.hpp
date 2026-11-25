// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "planex/boolean/snap.hpp"
#include "planex/curves/ellipse.hpp"
#include "planex/io/partplane.hpp"
#include "planex/io/steptext.hpp"
#include "planex/shape/shape.hpp"
#include "planex/shape/shapeops.hpp"

namespace planex2d {

    // lengths come in millimetres, as OCC reads them (MillimetresPerUnit is the file's unit). A part is what planex's writer
    // marked as one (its planex_part entities), else each top-level item of a shape representation
    class StepReader {
    public:
        struct Part {
            std::string kind, path;
            Shape shape;
        };

        explicit StepReader(std::string_view text);

        double MillimetresPerUnit() const { return scale; }
        std::vector<Part> Parts();

    private:
        StepFile file;
        double scale = 1.0;
        std::unordered_map<size_t, ShapeProperties> props;
        AxisSystem axes;
        std::unordered_map<size_t, EdgeList> edgesOf;
        // map: the 3d matrix row by row, then the shift
        struct Linked {
            std::string name;
            bool isFrom;
            size_t other;
            std::array<double, 12> map;
            int type;
        };
        std::unordered_map<size_t, std::vector<Linked>> linked;

        void NetgenIdentifications(const StepRecord &r);
        Identifications IdentificationsOfPart(const std::vector<size_t> &items) const;

        [[noreturn]] static void Fail(const std::string &what) { throw std::invalid_argument("FromStep: " + what); }
        static size_t Ref(const StepValue &v) {
            if (v.kind != StepValue::Kind::Ref)
                Fail("expected a reference to an entity");
            return v.ref;
        }
        static const std::vector<StepValue> &List(const StepValue &v) {
            if (v.kind != StepValue::Kind::List)
                Fail("expected a list");
            return v.list;
        }
        static std::string Text(const StepValue &v) { return v.kind == StepValue::Kind::String ? v.text : ""; }
        static double Number(const StepValue &v) {
            if (v.kind == StepValue::Kind::Typed && v.list.size() == 1)
                return Number(v.list.front());
            if (v.kind != StepValue::Kind::Number)
                Fail("expected a number");
            return v.number;
        }
        static bool Flag(const StepValue &v) { return v.kind == StepValue::Kind::Enum && v.text == "T"; }
        const StepRecord &Only(size_t id) const { return file.At(id).records.front(); }

        // millimetres per length unit of the file: the length unit its context assigns (a converted unit such as the
        // inch refers to a millimetre unit of its own)
        double LengthScale() const {
            for (const auto &[id, e] : file.Entities())
                if (const StepRecord *g = e.Record("GLOBAL_UNIT_ASSIGNED_CONTEXT"); g && !g->params.empty() && g->params[0].kind == StepValue::Kind::List)
                    for (const StepValue &u : g->params[0].list)
                        if (u.kind == StepValue::Kind::Ref && file.At(u.ref).Is("LENGTH_UNIT"))
                            return UnitScale(file.At(u.ref));
            return 1.0;
        }
        double UnitScale(const StepEntity &e) const;

        void Netgen(const StepRecord &r);
        // the colour of a styled face: sRGB in the file, linear in planex as OCC reads it
        static double Linear(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }
        void Styled(const StepRecord &r);
        // an entity's name: its first parameter, of a complex entity its REPRESENTATION_ITEM's
        std::string NameOf(size_t id) const {
            const StepEntity &e = file.At(id);
            const StepRecord *r = e.records.size() == 1 ? &e.records.front() : e.Record("REPRESENTATION_ITEM");
            return r && !r->params.empty() ? Text(r->params[0]) : "";
        }
        Properties PropsOfItem(size_t id) const {
            const std::string name = NameOf(id);
            const auto it = props.find(id);
            if (name.empty() && it == props.end())
                return nullptr;
            ShapeProperties p = it == props.end() ? ShapeProperties{} : it->second;
            if (!name.empty())
                p.name = name;
            return std::make_shared<const ShapeProperties>(std::move(p));
        }

        Vector3 PointOf(size_t id) const {
            const std::vector<StepValue> &c = List(Only(id).params[1]);
            if (c.size() != 3)
                Fail("a point of the shape has " + std::to_string(c.size()) + " coordinates, not 3");
            return Vector3{Number(c[0]), Number(c[1]), Number(c[2])} * scale;
        }
        Vector3 DirectionOf(const StepValue &v, const Vector3 &otherwise) const {
            if (v.kind != StepValue::Kind::Ref)
                return otherwise;
            const std::vector<StepValue> &c = List(Only(v.ref).params[1]);
            return Normalized(Vector3{Number(c[0]), Number(c[1]), Number(c[2])});
        }
        // origin, axis and x direction of an AXIS2_PLACEMENT_3D
        std::tuple<Vector3, Vector3, Vector3> Placement(size_t id) const {
            const StepRecord &p = Only(id);
            const Vector3 axis = DirectionOf(p.params[2], {0.0, 0.0, 1.0});
            return {PointOf(Ref(p.params[1])), axis, DirectionOf(p.params[3], std::abs(axis.x) < 0.9 ? Vector3{1.0, 0.0, 0.0} : Vector3{0.0, 1.0, 0.0})};
        }
        AxisSystem PlaneOf(size_t id) const {
            const StepRecord &plane = Only(id);
            if (plane.type != "PLANE")
                Fail("a face lies on a " + plane.type + ", not a plane" + readsCurvedShapes);
            const auto [o, n, x] = Placement(Ref(plane.params[1]));
            return AxisSystem(o, n, x);
        }
        Vertex InPlane(const Vector3 &p) const {
            const double off = Dot(p - axes.Origin(), axes.Normal());
            if (std::abs(off) > PlaneSlack(axes, p))
                Fail("a point lies " + NumberText(std::abs(off)) + " off the plane of its part" + readsCurvedShapes);
            return axes.ToHV(p);
        }

        EdgeList EllipseEdges(const StepRecord &r, const Vertex &p, const Vertex &q, bool same) const;
        BSplineData BSplineOf(const StepEntity &e, const StepRecord &bs, const StepRecord *shape) const;
        EdgeList CurveEdges(size_t id, const Vector3 &a, const Vector3 &b, bool same);
        static EdgeList Pieces(const Curve<double, Vertex> &c, const Vertex &a, const Vertex &b, bool closed);

        const EdgeList &EdgeOf(size_t id);

        void Collect(size_t id, std::vector<size_t> &faces, std::vector<size_t> &curves) const;

        EdgeList FreeCurve(size_t id);

        AxisSystem CurvesPlane(const std::vector<size_t> &curves) const;

        Shape ShapeOf(const std::vector<size_t> &items, const std::optional<AxisSystem> &plane);
    };

}
