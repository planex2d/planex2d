// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <array>
#include <cmath>
#include <cstddef>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "planex/io/faceloops.hpp"
#include "planex/io/steptext.hpp"
#include "planex/shape/identification.hpp"
#include "planex/shape/shape.hpp"

namespace planex2d {

    // a length unit a STEP file names: metre with an SI prefix, or a unit converted from the millimetre (inch, foot, mil)
    struct StepLengthUnit {
        // the SI prefix (".MILLI.", "$" for the metre itself), or the name of a converted unit
        std::string prefix, name;
        double mm = 1.0;
    };
    std::optional<StepLengthUnit> StepLengthUnitOf(double metres);

    // AP214, as OCC's own export writes it, so OCC and netgen read it. Which items make one planex part a marker entity
    // says: COMPOUND_REPRESENTATION_ITEM('planex_part <kind> <path>', (items)), kind Face, Shape or Wire, path the indices
    // of the part in the nested Compounds (empty for an object on its own); netgen and OCC skip it. netgen reads every
    // file in millimetres: its maxh and the shifts of its identifications are written in millimetres
    class StepWriter {
    public:
        explicit StepWriter(const StepLengthUnit &unit = {".MILLI.", "", 1.0});

        void Part(const Shape &shape, const std::string &kind, const std::string &path, const ArcFit &arcs, const SplineFit &fit,
                  const Cleanup &cleanup);

        std::string Text();

    private:
        std::vector<std::string> entities;
        std::vector<size_t> all;
        std::vector<size_t> styled;
        std::map<std::pair<double, double>, size_t> vertices;
        std::vector<size_t> edgeCurves;
        size_t app = 0, context = 0, productContext = 0, definitionContext = 0;
        // millimetres per unit of the file: netgen's lengths are in millimetres
        double mmPerUnit = 1.0;

        size_t Add(std::string record) {
            entities.push_back(std::move(record));
            return entities.size();
        }
        static std::string Ref(size_t id) { return "#" + std::to_string(id); }
        static std::string Refs(const std::vector<size_t> &ids) {
            std::string out;
            for (size_t k = 0; k < ids.size(); ++k)
                out += (k > 0 ? "," : "") + Ref(ids[k]);
            return out;
        }
        static std::string Name(const Properties &p) { return StepString(p && p->name ? *p->name : ""); }
        static std::string Triple(const Vector3 &v) { return "(" + StepReal(v.x) + "," + StepReal(v.y) + "," + StepReal(v.z) + ")"; }
        size_t Point(const Vector3 &p) { return Add("CARTESIAN_POINT(''," + Triple(p) + ")"); }
        size_t Direction(const Vector3 &d) { return Add("DIRECTION(''," + Triple(d) + ")"); }
        size_t Placement(const Vector3 &origin, const Vector3 &axis, const Vector3 &x) {
            const size_t o = Point(origin), a = Direction(axis), r = Direction(x);
            return Add("AXIS2_PLACEMENT_3D(''," + Ref(o) + "," + Ref(a) + "," + Ref(r) + ")");
        }

        void Product();

        void NetgenProperties(size_t item, const Properties &p);
        // OCC writes a colour as sRGB and reads it back as linear RGB, as planex keeps it
        static double SRGB(double c) { return c <= 0.0031308 ? 12.92 * c : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055; }
        void Colour(size_t item, const Properties &p);

        size_t VertexAt(const Vertex &uv, const AxisSystem &axes, const Properties &p) {
            const auto [it, added] = vertices.try_emplace({uv.x, uv.y}, 0);
            if (added) {
                it->second = Add("VERTEX_POINT(" + Name(p) + "," + Ref(Point(axes.ToXYZ(uv))) + ")");
                NetgenProperties(it->second, p);
            }
            return it->second;
        }

        size_t CurveOf(const Edge &e, const AxisSystem &axes);
        size_t BSplineOf(const Spline &s, const AxisSystem &axes);
        size_t CircleOf(const AxisSystem &axes, const Vertex &center, const Vertex &start, double radius, bool ccw) {
            const Vector3 c = axes.ToXYZ(center);
            const Vector3 x = Normalized(axes.ToXYZ(start) - c);
            return Add("CIRCLE(''," + Ref(Placement(c, ccw ? axes.Normal() : -axes.Normal(), x)) + "," + StepReal(radius) + ")");
        }

        size_t EdgeCurveOf(const Wire &c, size_t i);

        size_t FaceOf(const Wire &c, const std::vector<size_t> &faceEdges, size_t plane, const Properties &props);

        size_t PairRecord(const Identification &id, const std::array<Vector3, 3> &columns, const Vector3 &shift, size_t other);
        void IdentificationsOf(const Wire &c, const Identifications &ids, const std::vector<size_t> &itemOf);

        size_t Trimmed(const Edge &e, const AxisSystem &axes) {
            const size_t curve = CurveOf(e, axes);
            const std::string a = Ref(Point(axes.ToXYZ(StartOf(e)))), b = Ref(Point(axes.ToXYZ(EndOf(e))));
            return Add("TRIMMED_CURVE(" + Name(PropsOf(e)) + "," + Ref(curve) + ",(" + a + "),(" + b + "),.T.,.CARTESIAN.)");
        }
    };

}
