// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <functional>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

#include "planex/core/axissystem.hpp"
#include "planex/core/disjointsets.hpp"
#include "planex/core/timer.hpp"
#include "planex/shape/shape.hpp"

namespace planex2d {

    Shape WireUnion(const ShapeList &shapes);

    struct GluePart {
        Wire edges;
        bool face;
        Properties props;
    };

    Shape Glue(const ShapeList &shapes);

    Shape WiresIn(const Shape &w, const Wire &region, bool inside);
    Shape SharingFaces(const AxisSystem &axes, const FaceList &faces, const WireList &wires = {});

    template <class F>
    Shape Result(const AxisSystem &axes, Wire region, const ShapeList &operands, F assign) {
        region.SetAxes(axes);
        if (std::optional<Properties> props = operands.CommonFaceProps())
            return Shape(std::move(region), *props);
        FaceList regions = region.Faces();
        std::invoke(assign, regions, operands.FaceLists());
        return Shape(axes, regions);
    }

    double SharedLength(const Face &a, const Face &b);

    inline Shape Unify(const ShapeList &shapes, bool multithreaded = false) {
        shapes.CheckParts(true);
        const AxisSystem axes = shapes.CommonAxes();
        for (const Shape &s : shapes)
            if (s.WireCount() > 0)
                return WireUnion(shapes);
        return Result(axes, BalancedBoolean(shapes.Outlines(), BooleanOp::Union, multithreaded), shapes, &FaceList::MergeCovered);
    }

    template <class F>
    Shape FaceByFace(const AxisSystem &axes, const ShapeList &shapes, const Wire &rest, BooleanOp op, F assign) {
        FaceList out;
        const std::vector<FaceList> others = shapes.Rest().FaceLists();
        for (const Face &f : shapes.front().Faces()) {
            Wire region = Boolean(f.Edges(), rest, op);
            Snap(region);
            FaceList regions = region.Faces();
            std::vector<FaceList> operands{{f}};
            operands.insert(operands.end(), others.begin(), others.end());
            std::invoke(assign, regions, operands);
            out.insert(out.end(), regions.begin(), regions.end());
        }
        return SharingFaces(axes, out);
    }

    Shape Intersect(const ShapeList &shapes, bool multithreaded = false);

    Shape Subtract(const ShapeList &shapes, bool multithreaded = false);

    Shape SymmetricDifference(const ShapeList &shapes, bool multithreaded = false);

    inline Shape Boolean(const Shape &a, const Shape &b, BooleanOp op) {
        switch (op) {
            case BooleanOp::Union: return Unify({a, b});
            case BooleanOp::Intersection: return Intersect({a, b});
            case BooleanOp::Difference: return Subtract({a, b});
        }
        return Shape();
    }

    inline Shape RoundOffset(const Shape &s, double d) {
        Shape band(s.FaceEdges().Offset(std::abs(d), OffsetEnd::Round, OffsetCorner::Round), nullptr);
        return d > 0.0 ? Unify({s, band}) : Subtract({s, band});
    }

    inline Shape Fresh(Shape s) {
        s.Edges().SetIdKind("Shape");
        s.Edges().AssignIds(true);
        s.Edges().ClearVertexIds();
        return s;
    }

    Shape OffsetFaces(const Shape &s, double d, OffsetCorner corners);

    std::pair<double, double> RangeInBox(const Line &l, const BBox &b);

    std::vector<Vertex> ThinSpotsOf(const WireList &loops, double width);

    std::vector<BBox> SpotWindows(const std::vector<Vertex> &points, double width);

    inline Shape BoxShape(const BBox &b, const AxisSystem &axes) {
        Wire w(axes);
        const Vertex corners[4] = {{b.x0, b.y0}, {b.x1, b.y0}, {b.x1, b.y1}, {b.x0, b.y1}};
        for (size_t k = 0; k < 4; ++k)
            w.Add(Line(corners[k], corners[(k + 1) % 4]));
        return Shape(w, nullptr);
    }

    // per window: of the shape cut to the window grown by 2 width, the pieces lying in the window
    template <class Residue>
    FaceList ResidueAtSpots(const Shape &s, const std::vector<Vertex> &spots, double width, Residue residue) {
        static Timer t("planex ResidueAtSpots");
        Region reg(t);
        FaceList pieces;
        for (const BBox &window : SpotWindows(spots, width)) {
            const Shape clipped = Intersect({s, BoxShape(window.Grown(2.0 * width), s.Axes())});
            for (Face &piece : residue(clipped).Faces()) {
                const BBox b = piece.Box();
                if (window.IsInside({b.x0, b.y0}) && window.IsInside({b.x1, b.y1}))
                    pieces.push_back(std::move(piece));
            }
        }
        return pieces;
    }

    bool IsSliver(const Face &piece, const Properties &mark, double width);

    struct FaceShare {
        uint32_t face;
        double length;
    };

    struct FaceShares : std::vector<FaceShare> {
        void Add(uint32_t face, double length) {
            for (FaceShare &s : *this)
                if (s.face == face) {
                    s.length += length;
                    return;
                }
            push_back({face, length});
        }
        bool IsMixed(const FaceList &faces) const {
            for (const FaceShare &s : *this)
                if (!SameProperties(faces[s.face].Props(), faces[front().face].Props()))
                    return true;
            return false;
        }
        uint32_t FaceSharingMost() const {
            FaceShare most = front();
            for (const FaceShare &s : *this)
                if (s.length > most.length || (s.length == most.length && s.face < most.face))
                    most = s;
            return most.face;
        }
    };

    class FaceEdgeIndex {
        private:
            struct FaceEdge {
                uint32_t face, edge;
            };
            const FaceList &faces;
            std::vector<FaceEdge> at;
            Grid grid;
            std::vector<uint32_t> nearby;

            static Grid GridOf(const FaceList &faces, std::vector<FaceEdge> &at) {
                std::vector<BBox> boxes;
                for (uint32_t j = 0; j < faces.size(); ++j)
                    for (uint32_t i = 0; i < faces[j].Edges().Size(); ++i) {
                        boxes.push_back(BoxOf(faces[j].Edges()[i]));
                        at.push_back({j, i});
                    }
                return Grid::OfBoxes(boxes);
            }

        public:
            explicit FaceEdgeIndex(const FaceList &f) : faces(f), grid(GridOf(f, at)) {}
            FaceShares SharedLengths(const Face &piece, const Properties &mark);
    };

    inline Shape Unmarked(Face piece, const Properties &mark) {
        for (Edge &e : piece.Edges())
            if (PropsOf(e) == mark)
                SetProps(e, nullptr);
        return Shape(piece);
    }

    inline Shape OpeningResidue(const Shape &f, double r, const Properties &mark) {
        const Shape eroded = RoundOffset(f, -r);
        if (!eroded.HasFaces())
            return f;
        Shape opened = RoundOffset(eroded, r);
        for (Edge &e : opened.Edges())
            SetProps(e, mark);
        return opened.HasFaces() ? Subtract({f, opened}) : f;
    }

    inline Shape ClosingResidue(const Shape &u, double r, const Properties &mark) {
        Shape closed = RoundOffset(RoundOffset(u, r), -r);
        for (Edge &e : closed.Edges())
            SetProps(e, mark);
        return Subtract({closed, u});
    }

    FaceList Reassigned(const FaceList &faces, const std::vector<ShapeList> &taken, const std::vector<ShapeList> &given);

}
