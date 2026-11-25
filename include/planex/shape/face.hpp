// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <utility>
#include <vector>

#include "planex/core/axissystem.hpp"
#include "planex/boolean/boolean.hpp"
#include "planex/shape/identification.hpp"

namespace planex2d {

    class Face {
        private:
            Wire wire;
            Properties props;
            Identifications ids;

            Wire LoopWire(const Loop &loop, bool ccw) const {
                const double a = wire.LoopArea(loop.edges);
                Wire w(wire.Axes());
                const bool along = (a > 0.0) == ccw;
                for (size_t k = 0; k < loop.edges.size(); ++k) {
                    const auto [i, forward] = loop.edges[along ? k : loop.edges.size() - 1 - k];
                    w.Add(forward == along ? wire[i] : ReversedOf(wire[i]));
                }
                return w;
            }

        public:
            Face() { wire.SetIdKind("Face"); }
            explicit Face(Wire w, Properties faceProps = nullptr) : wire(std::move(w)), props(std::move(faceProps)) { wire.SetIdKind("Face"); }
            Wire &IdWire() { return wire; }
            const Properties &Props() const { return props; }
            void SetProps(Properties p) { props = std::move(p); }
            const Identifications &Ids() const { return ids; }
            Identifications &Ids() { return ids; }
            std::vector<Line> Lines() const { return wire.All<Line>(); }
            std::vector<Arc> Arcs() const { return wire.All<Arc>(); }
            std::vector<Circle> Circles() const { return wire.All<Circle>(); }
            std::vector<Spline> Splines() const { return wire.All<Spline>(); }
            const Wire &Edges() const { return wire; }
            Wire &Edges() { return wire; }
            WireList Wires() const { return wire.Wires(); }
            // the outer loop counterclockwise and the holes clockwise, the edges in that order and each run head to tail
            Wire Outer() const {
                for (const NestedLoop &n : wire.Nesting())
                    if (n.depth == 0)
                        return LoopWire(n.loop, true);
                return Wire(wire.Axes());
            }
            WireList Holes() const {
                WireList holes;
                for (const NestedLoop &n : wire.Nesting())
                    if (n.depth == 1)
                        holes.push_back(LoopWire(n.loop, false));
                return holes;
            }
            WireList Loops() const {
                WireList loops = Holes();
                loops.insert(loops.begin(), Outer());
                return loops;
            }
            double Area() const { return wire.Area(); }
            bool Empty() const { return wire.Empty(); }
            BBox Box() const { return wire.Box(); }
            const AxisSystem &Axes() const { return wire.Axes(); }
            void SetAxes(const AxisSystem &a) { wire.SetAxes(a); }
            Face &Rotate(const Vertex &c, double angle) {
                wire.Rotate(c, angle);
                MoveIdentifications(ids, Affine2::Rotation(c, angle));
                return *this;
            }
            Face &Translate(const Vertex &d) {
                wire.Translate(d);
                MoveIdentifications(ids, Affine2::Translation(d));
                return *this;
            }
            Face &Mirror(const Vertex &a, const Vertex &d) {
                wire.Mirror(a, d);
                MoveIdentifications(ids, Affine2::Reflection(a, d));
                return *this;
            }
            Face &Scale(const Vertex &c, double s, bool scaleMaxh = false, bool scaleMinh = false) {
                wire.Scale(c, s, scaleMaxh, scaleMinh);
                props = ScaledSizes(props, s, scaleMaxh, scaleMinh);
                MoveIdentifications(ids, Affine2::Scaling(c, s));
                return *this;
            }
            // in 3d the plane moves; mirroring also mirrors the coordinates in it (v -> -v), scaling scales them
            Face &Mirror(const AxisSystem &plane) {
                wire.Mirror(plane);
                MoveIdentifications(ids, Affine2::Reflection({}, {1.0, 0.0}));
                return *this;
            }
            Face &Scale(const Vector3 &p, double s, bool scaleMaxh = false, bool scaleMinh = false) {
                wire.Scale(p, s, scaleMaxh, scaleMinh);
                props = ScaledSizes(props, s, scaleMaxh, scaleMinh);
                MoveIdentifications(ids, Affine2::Scaling({}, s));
                return *this;
            }
            Face &Rotate(const Axis &axis, double angle) {
                wire.Rotate(axis, angle);
                return *this;
            }
            Face &Translate(const Vector3 &v) {
                wire.Translate(v);
                return *this;
            }
            std::vector<std::pair<size_t, bool>> OpenEnds() const { return wire.OpenEnds(); }
            bool IsClosed() const { return wire.IsClosed(); }
            std::vector<size_t> SmallEdges(double size) const { return wire.SmallEdges(size); }
            Face &FitArcs(const ArcFit &fit) {
                wire.FitArcs(fit);
                return *this;
            }
            Face &MergeCollinearEdges(double tol) {
                wire.MergeCollinearEdges(tol);
                return *this;
            }
            Face &Simplify(const ArcFit &arcs = config::state->arc_fit, const SplineFit &fit = config::state->spline_fit,
                           const Cleanup &cleanup = config::state->cleanup) {
                wire = wire.Cleaned(arcs, fit, cleanup, SidesOf(ids));
                return *this;
            }
            Face &RemoveSmallGaps(double tol) {
                wire.RemoveSmallGaps(tol);
                return *this;
            }
            Face &RemoveSmallEdges(double tol) {
                wire.RemoveSmallEdges(tol);
                return *this;
            }
    };

    struct FaceList : std::vector<Face> {
        using vector::vector;

        std::vector<BBox> Boxes() const {
            std::vector<BBox> boxes;
            for (const Face &f : *this)
                boxes.push_back(f.Box());
            return boxes;
        }
        // size() if no face holds the point
        size_t Locate(const std::vector<BBox> &boxes, const Vertex &p) const {
            for (size_t k = 0; k < size(); ++k)
                if (boxes[k].IsInside(p) && (*this)[k].Edges().Contains(p))
                    return k;
            return size();
        }
        void MergeCovered(const std::vector<FaceList> &inputs);
        void InheritProps(const std::vector<FaceList> &operands);
        void InheritFirstOperandProps(const std::vector<FaceList> &operands);
    };

}
