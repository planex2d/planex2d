// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <vector>

#include "planex/core/disjointsets.hpp"
#include "planex/core/objectid.hpp"
#include "planex/core/properties.hpp"
#include "planex/core/vertex.hpp"
#include "planex/curves/edge.hpp"
#include "planex/curves/interpolate.hpp"
#include "planex/curves/spline.hpp"
#include "planex/offset/offsetpieces.hpp"
#include "planex/shape/fillet.hpp"
#include "planex/shape/shape.hpp"
#include "planex/sketch/arcconstruction.hpp"
#include "planex/sketch/circleconstruction.hpp"
#include "planex/sketch/copies.hpp"
#include "planex/sketch/copymaker.hpp"
#include "planex/sketch/cornermaker.hpp"
#include "planex/sketch/cornerparts.hpp"
#include "planex/sketch/curveeval.hpp"
#include "planex/sketch/cut.hpp"
#include "planex/sketch/diagnostics.hpp"
#include "planex/sketch/elementcutter.hpp"
#include "planex/sketch/geometry.hpp"
#include "planex/sketch/guards.hpp"
#include "planex/sketch/helperview.hpp"
#include "planex/sketch/idbook.hpp"
#include "planex/sketch/lineconstruction.hpp"
#include "planex/sketch/offsetmaker.hpp"
#include "planex/sketch/offsetsides.hpp"
#include "planex/sketch/options.hpp"
#include "planex/sketch/pen.hpp"
#include "planex/sketch/projection.hpp"
#include "planex/sketch/projectionmaker.hpp"
#include "planex/wire/chain.hpp"
#include "planex/wire/propagation.hpp"

namespace planex2d {

    // Store: sketch::Geometry for the plain sketch, cs::System for the constraint sketch
    template <class Store>
    class BasicSketch {
        ObjectId objectId{"Sketch"};

    public:
        using Options = sketch::Options;

        struct Undo {
            typename Store::Mark mark;
            std::unordered_map<std::string, size_t> counters;
            size_t elements;
            sketch::PenState pen;
            size_t chain, ended, wireLog;
            std::optional<sketch::PenState> parked;
            size_t groups;
        };

        template <class Other>
        friend class BasicSketch;
        template <class>
        friend class sketch::OffsetMaker;
        template <class>
        friend class sketch::ElementCutter;
        template <class>
        friend class sketch::ProjectionMaker;
        template <class, class>
        friend class sketch::CopyMaker;
        template <class, class>
        friend class sketch::CornerMaker;

        explicit BasicSketch(const AxisSystem &a = AxisSystem()) : axes(a) {
            // directed along +x and +y: a positive distance is to the left of a line's direction, above H, left of V
            AddElement(s.AddAxis(-std::numbers::pi / 2.0, 0.0, true), "Axis", {"H", {}, true});
            AddElement(s.AddAxis(0.0, 0.0, true), "Axis", {"V", {}, true});
            AddElement(s.AddPoint(s.NewPoint({0.0, 0.0}, true)), "Point", {"origin", {}, true});
        }
        template <class Other>
        explicit BasicSketch(const BasicSketch<Other> &o)
            : s(o.s), axes(o.axes), book(o.book), props(o.props), roles(o.roles), pointElementOf(o.pointElementOf), pen(o.pen), parked(o.parked),
              normalPen(o.normalPen) {
            if constexpr (std::is_same_v<Store, sketch::Geometry>)
                WithoutConstraints();
        }
        BasicSketch(const BasicSketch &) = default;
        BasicSketch(BasicSketch &&) = default;
        BasicSketch &operator=(const BasicSketch &) = default;
        BasicSketch &operator=(BasicSketch &&) = default;
        virtual ~BasicSketch() = default;

        // the sketch's id: Sketch.N, one counter for both kinds of sketch
        const std::string &Id() { return objectId.Get(); }
        const std::string &AssignedId() const { return objectId.id; }
        void SetId(std::string id) { objectId.id = std::move(id); }
        const std::string &IdKind() const { return objectId.kind; }
        void SetIdKind(std::string kind) { objectId.kind = std::move(kind); }
        const AxisSystem &Axes() const { return axes; }
        const Store &Geometry() const { return s; }
        const sketch::IdBook &Book() const { return book; }
        Vertex Position() const { return pen.pos; }
        Vertex StartPosition() const { return pen.start; }
        Vertex Heading() const { return pen.dir; }
        virtual void RequireFull(const std::string &) {}
        Vertex Coords(size_t point) const { return s.Coords(point); }
        size_t ElementCount() const { return s.elements.size(); }
        bool IsConstruction(const std::string &id) const { return !IsDrawn(RefOf(id).element); }
        double Size() const { return s.Size(); }

        // sketchdrawing.hpp
        BasicSketch &MoveTo(const Vertex &p, const std::vector<sketch::Kind> &flags);
        BasicSketch &MoveTo(const std::string &ref, const std::vector<sketch::Kind> &flags);
        BasicSketch &Line(const Vertex &end, const std::vector<sketch::Kind> &flags, const Options &opt);
        BasicSketch &Line(double l, const std::vector<sketch::Kind> &flags, const Options &opt);
        BasicSketch &Line(double dx, double dy, const std::vector<sketch::Kind> &flags, const Options &opt);
        BasicSketch &Arc(double r, double ang, const std::vector<sketch::Kind> &flags, const Options &opt);
        BasicSketch &FullTurn(const Vertex &center, bool ccw, const std::vector<sketch::Kind> &flags, const Options &opt);
        BasicSketch &Arc(const Vertex &end, const Vertex &t, const std::vector<sketch::Kind> &flags, const Options &opt);
        BasicSketch &Spline(const std::vector<Vertex> &points, bool closed, const std::map<size_t, Vertex> &tangents, bool fromPen,
                            const std::vector<sketch::Kind> &flags, const Options &opt, SplineStyle style = SplineStyle::Averaged,
                            const std::map<size_t, TangentSetting<double>> &settings = {});
        BasicSketch &BSpline(const std::vector<Vertex> &poles, const std::optional<std::vector<double>> &knots, size_t degree, bool periodic,
                             const std::vector<sketch::Kind> &flags, const Options &opt);
        BasicSketch &Curve(const planex2d::Curve<double, Vertex> &shape, std::optional<sketch::CurveFormula> formula, const std::vector<sketch::Kind> &flags,
                           const Options &opt);
        BasicSketch &Circle(const Vertex &center, double r, const std::vector<sketch::Kind> &flags, const Options &opt);
        BasicSketch &Rectangle(double l, double w, const Options &opt);
        BasicSketch &Rectangle(const std::vector<Vertex> &p, const std::vector<std::optional<std::string>> &at, const Options &opt);
        BasicSketch &RectangleC(const std::optional<Vertex> &center, const std::optional<std::string> &at, double l, double w, const Options &opt);
        BasicSketch &Direction(double dx, double dy);
        BasicSketch &Turn(double angle);
        BasicSketch &Close(const std::vector<sketch::Kind> &flags, const Options &opt, const std::optional<std::string> &idWire = std::nullopt);
        BasicSketch &Finish(const std::optional<std::string> &idWire = std::nullopt);
        std::string GlobalOrigin();
        BasicSketch &AddProjection(const Projection &p, bool asConstruction, const std::optional<std::string> &name, const std::string &kind,
                                   const std::optional<std::string> &id, bool hold = true);
        std::string ProjectedReference(const Projection &p, const std::string &call);
        BasicSketch &NamedPoint(const std::string &name, const Vertex &p);
        std::string AddAxisLine(const Vertex &p, const Vertex &along, const std::string &prefix, const std::optional<std::string> &id);
        std::string AddCentreBisector(const std::string &a, const std::string &b, const std::optional<std::string> &id);
        template <class F>
        BasicSketch &Slot(const std::string &kind, double w, std::optional<double> hole, const Options &opt, const std::string &call, F spine);
        template <class F>
        BasicSketch &Polygon(const std::string &kind, size_t n, std::optional<double> flats, const Options &opt, const std::string &call, F circle);

        // sketchconstruct.hpp
        BasicSketch &ConstructLine(const sketch::LineRefs &r, const std::vector<sketch::Kind> &flags, const Options &opt);
        BasicSketch &ConstructArc(const sketch::ArcRefs &r, const std::vector<sketch::Kind> &flags, const Options &opt);
        BasicSketch &ConstructCircle(const sketch::CircleRefs &r, const std::vector<sketch::Kind> &flags, const Options &opt);

        // sketchundo.hpp
        template <class F>
        BasicSketch &Atomic(F f);
        template <class F>
        BasicSketch &Drawing(const Options &opt, F f);
        sketch::PenState Swapped(sketch::PenState in) { return std::exchange(pen, std::move(in)); }

        // sketchplane.hpp
        virtual BasicSketch &SetOrigin(const Vertex &p);
        BasicSketch &Rotate(const Vertex &c, double angle);
        BasicSketch &Rotate(const Axis &axis, double angle);
        BasicSketch &Translate(const Vertex &d);
        BasicSketch &Translate(const Vector3 &v);
        BasicSketch &Scale(const Vertex &c, double f, bool scaleMaxh = false, bool scaleMinh = false);
        BasicSketch &Scale(const Vector3 &p, double f, bool scaleMaxh = false, bool scaleMinh = false);
        BasicSketch &Mirror(const std::string &axis);
        Wire InSketchAxes(Wire w, const std::string &call) const;

        // sketchqueries.hpp
        std::vector<std::string> Spans() const;
        virtual std::vector<std::string> Expanded(const std::vector<std::string> &ids) const;
        std::vector<std::pair<std::string, std::vector<std::string>>> Elements() const;
        std::optional<std::string> KindById(const std::string &id) const;
        std::vector<std::pair<std::string, std::string>> Chosen(const std::string &id) const;
        std::vector<std::pair<std::string, Vertex>> Locations();
        Vertex Location(const std::string &ref);
        sketch::ElementValues ElementOf(const std::string &id);
        std::optional<std::string> NameOf(const std::string &id) const;
        std::vector<std::pair<std::string, Vertex>> NamedPoints();
        std::optional<std::string> PointElementAt(const std::string &ref) const;
        void RenamePoint(const std::string &old, const std::string &id);
        template <class F>
        BasicSketch &ChangeProperties(const std::vector<std::string> &ids, bool points, const std::string &call, F change);
        BasicSketch &SetConstruction(const std::vector<std::string> &ids, bool on, const std::string &call);
        std::vector<std::pair<Properties, bool>> ElementProperties(const std::vector<std::string> &ids, bool points, const std::string &call) const;
        std::string OffsetCurveSource(const sketch::Element &e) const;
        std::string RefKind(const std::string &ref) const;
        bool IsPoint(const std::string &ref) const;
        bool HasLocation(const std::string &ref) const;
        std::vector<std::string> MembersOf(const std::string &id) const;
        std::vector<std::string> Through(const Vertex &p, const std::string &except) const;

        // sketchcopies.hpp
        BasicSketch &Mirror(const std::vector<std::string> &ids, const std::string &axis);
        BasicSketch &Mirrored(const std::vector<std::string> &ids, const std::string &axis, bool tie, bool internal, const std::optional<std::string> &id);
        BasicSketch &AddPattern(const std::vector<std::string> &ids, const std::string &ref, size_t count, double step, bool linear, bool keep, bool internal,
                                const std::optional<std::string> &id, bool = false, const std::optional<sketch::PatternDir2> &second = std::nullopt,
                                bool reverse = false);

        // sketchoffset.hpp
        BasicSketch &Offset(const std::vector<std::string> &ids, double d, const std::optional<Vertex> &point, const std::optional<bool> &bothSides,
                            const OffsetForms &forms, const Options &opt, const std::optional<bool> &constructionGiven);
        sketch::OffsetChain ChainOf(const std::vector<std::string> &ids, const std::string &call) const;
        std::optional<std::string> Crossing(const std::vector<PrimitiveEdge> &arcs, const std::string &source) const;

        // sketchcut.hpp
        virtual BasicSketch &Cut(const std::string &id, const std::vector<std::string> &cutterIds, std::optional<Vertex> nearPoint, bool reverse, bool trim,
                                 bool tied = true);
        virtual BasicSketch &Extend(const std::string &id, const std::vector<std::string> &targets, std::optional<double> length);
        BasicSketch &TrimChain(const std::string &wire, const std::vector<std::string> &cutterIds, const Vertex &nearPoint);
        virtual BasicSketch &Fillet(const std::vector<Vertex> &at, double r, bool curvature, const std::vector<std::string> &ids, const std::vector<Properties> &given);
        virtual BasicSketch &Chamfer(const std::vector<Vertex> &at, const ChamferRule &rule, const std::vector<std::string> &ids, const std::vector<Properties> &given);
        virtual BasicSketch &Connect(const std::string &a, const std::string &b, sketch::Kind continuity, bool reverse, const sketch::Options &opt);

        // sketchremove.hpp
        BasicSketch &Remove(const std::string &id);
        std::vector<std::string> LegsGoingWith(size_t k) const;

        // sketchexport.hpp
        BBox Box() const;
        Wire ElementWire(const std::string &id);
        Wire ElementGeometry(const std::string &id);
        Wire ElementsWire(const std::vector<std::string> &ids);
        Wire NamedEdges(const std::string &name);
        Wire PropagatedWire(const std::string &seed, Continuity c);
        std::optional<std::string> ElementOfEdge(const std::string &id, size_t i);
        Wire Edges(bool constructionOnes = false);
        sketch::Faults Diagnose(std::optional<double> gap);
        WireList Wires(bool unify, const std::string &call);
        Wire Region(const char *call = nullptr);
        planex2d::Face Face() { return Region("Face()").SingleFace("Face()"); }
        planex2d::Shape Shape() { return planex2d::Shape(Region(), nullptr); }

    protected:
        // met: the elements the cutters meet other than at their ends; nearest: the element nearest the point
        struct CutChain {
            std::vector<char> met;
            size_t nearest;
        };

        virtual void RequireConstraints(std::string_view call) const {
            throw std::invalid_argument(std::string(call) + ": a QuickSketch has no constraints; px.Sketch(...) has them");
        }
        // the hooks of the constraint sketch, which adds the constraints there; in the plain sketch they do nothing
        // a relation between elements, a flag or what a drawing call implies (params: positions along a spline)
        virtual void Relate(sketch::Kind, std::vector<sketch::Ref>, std::vector<size_t> = {}) {}
        virtual void RelateGiven(sketch::Kind, std::vector<sketch::Ref>) {}
        virtual void Chained(size_t, size_t) {}
        // dimension changes not applied yet go in first
        virtual void Flush() {}
        virtual void CallStarts() {}
        virtual void CallEnds() {}
        virtual void CallUndone() {}
        virtual void SplineAdded(size_t) {}
        virtual void ElementGoes(size_t, const std::string &) {}
        virtual bool IsReferenced(size_t) const { return false; }
        virtual void PiecesCut(size_t, const sketch::Element &, const std::vector<size_t> &, const std::string &, const std::string &) {}
        virtual void MirrorTied(const sketch::Copies &, const sketch::Ref &) {}
        virtual void MirrorMade(const std::string &, const std::vector<std::string> &, const std::string &, bool, bool) {}
        virtual void ConstraintsCopied(const std::string &, bool) {}
        virtual void Dimension(sketch::Kind, std::vector<sketch::Ref>, double, const std::string &, bool) {}
        virtual bool TiesOffsets() const { return false; }
        virtual size_t OffsetElement(double, const Options &, bool) { return 0; }
        virtual void TieOffset(sketch::OffsetTie, std::vector<sketch::Ref>, double, const std::string &) {}
        virtual void OffsetMade(const std::string &, const std::vector<std::string> &, const std::vector<std::string> &, const sketch::OffsetChain &,
                                const EdgeList &, double, const std::vector<int> &, const OffsetForms &,
                                const sketch::OffsetSides &,
                                const std::vector<std::pair<std::string, std::string>> &, const std::string &) {}
        bool Unused(size_t point) const { return s.IsPointUnused(point) && !IsReferenced(point); }
        bool IsSpan(size_t e) const { return roles[e] == sketch::Role::Span; }
        bool IsHidden(size_t e) const { return roles[e] == sketch::Role::Span || roles[e] == sketch::Role::Helper; }
        bool IsDrawn(size_t e) const { return roles[e] == sketch::Role::Drawn; }

        // sketchdrawing.hpp
        void LineTo(const Vertex &end, const std::vector<sketch::Kind> &flags, const Options &opt, bool inChain = true);
        void RenameChain(const std::string &id, const std::string &call);
        void PolygonDimension(const std::string &name, size_t ring, const std::vector<size_t> &sides, const std::optional<double> &flats);
        std::vector<size_t> PolygonSides(const std::string &name, size_t ring, size_t n, const Vertex &along, const Options &inner);
        void PolygonTies(size_t ring, const std::vector<size_t> &corners, const std::vector<size_t> &sides);
        std::optional<std::string> HoleProblem(double w, double r, std::optional<double> length) const;
        void CheckSpine(const std::string &id, double w, std::optional<double> hole, const std::string &call) const;
        void PenAtSpineEnd(size_t chains);
        void AddLegs(size_t e);
        size_t AddElement(size_t e, const std::string &prefix, const Options &opt);
        void CheckFlags(const std::vector<sketch::Kind> &flags) const;
        void Flags(size_t e, const std::vector<sketch::Kind> &flags);
        void ElementDrawn(size_t e, const std::vector<sketch::Kind> &flags);
        void AddToChain(size_t e);
        void Sync();
        bool ValidMove(const Vertex &end) const;
        void CentreTie(size_t middle, size_t diagonal) { Relate(sketch::Kind::Coincident, {{middle}, {diagonal, sketch::Part::Mid}}); }
        void RectangleTies(const std::vector<size_t> &sides);
        std::vector<size_t> RectangleSides(const std::string &name, const std::string &group, double l, double w, const Options &opt);
        void TurnTies(size_t home, size_t e, bool onCircle, const std::optional<size_t> &before);
        size_t PenPoint(const Options &opt);
        size_t EndPoint(const Vertex &end, const Options &opt);
        BasicSketch &ArcThrough(const Vertex &center, const Vertex &end, bool ccw, const Vertex &heading, const std::vector<sketch::Kind> &flags, const Options &opt);

        // sketchconstruct.hpp
        sketch::Pen HelperPen() const;
        EdgeList UntilEdges(Until until, const std::optional<sketch::Target> &target);

        // sketchundo.hpp
        template <class F>
        bool AtomicIf(F f);
        void Restore(const Undo &u);

        // sketchplane.hpp
        virtual void ScaleInside(double f, bool scaleMaxh, bool scaleMinh);

        // sketchqueries.hpp
        std::vector<size_t> PropertyElements(const std::vector<std::string> &ids, bool points, const std::string &call) const;
        std::pair<Vertex, Vertex> LineOf(const std::string &axis, const std::string &call) const;
        sketch::Ref SplinePartRef(const std::string &text, size_t dot, const std::pair<sketch::Part, size_t> &k) const;
        sketch::Ref RefOf(const std::string &text) const;
        std::optional<sketch::Ref> FindRef(const std::string &text) const;
        std::string RefText(const sketch::Ref &r) const;
        const std::string &ShownId(size_t e) const;
        std::string RefName(const sketch::Ref &r) const { return ShownId(r.element) + r.Suffix(); }

        // sketchcopies.hpp
        std::vector<std::pair<size_t, Vertex>> MirrorElements(const std::vector<size_t> &elements, const std::string &axis);
        std::vector<std::string> WithSlotSpines(std::vector<std::string> ids) const;
        static void CheckPattern(const std::string &call, size_t count, double step, const std::optional<sketch::PatternDir2> &second = std::nullopt);
        static std::vector<std::pair<size_t, size_t>> PatternSteps(size_t count, size_t count2);
        static std::string StepPrefix(const std::string &scope, size_t k, size_t j, bool two);
        sketch::Ref PatternRef(const std::string &call, const std::string &ref, bool linear) const;
        sketch::Copies PlaceCopy(const std::vector<size_t> &originals, const sketch::Ref &r, bool linear, double step, size_t k, const std::string &prefix,
                                 const std::optional<sketch::Ref> &r2 = std::nullopt, double step2 = 0.0, size_t j = 0);
        std::vector<size_t> Originals(const std::vector<std::string> &ids, const std::string &call) const;
        template <class F>
        sketch::Copies CopyElements(const std::vector<size_t> &originals, F f, bool flip, const std::string &prefix);

        // sketchoffset.hpp
        void MakeOffset(const sketch::OffsetChain &source, double d, const std::optional<Vertex> &point, const std::optional<bool> &bothSides, const Options &opt,
                        const std::optional<bool> &constructionGiven, const std::string &call, const std::string &slot = "", std::optional<double> hole = std::nullopt,
                        const OffsetForms &forms = {});
        sketch::OffsetSides SidesOf(const sketch::OffsetChain &source, const EdgeList &edges, double ad, const std::vector<int> &towards, const OffsetForms &forms,
                                    const std::string &call) const;
        size_t FitCurve(const sketch::CurveEval &g, double ta, double tb, std::array<size_t, 2> at, const Options &o);
        std::optional<std::string> CurveCrossing(const std::string &curve, const std::string &source) const;
        std::optional<std::string> CurveCusp(const std::string &curve, const std::string &source, double d) const;
        std::vector<size_t> OffsetSources(const std::vector<std::string> &ids, const std::string &call) const;

        // sketchcut.hpp
        std::pair<size_t, Vertex> ExtendedEnd(const std::string &id, const std::vector<std::string> &targets, std::optional<double> length, const std::string &call);
        Wire AsEdges(size_t e, const std::unordered_map<size_t, Properties> &vertexProps) const;
        Wire ExtendTargets(size_t e, const std::vector<std::string> &targets, const std::string &call) const;
        Vertex LineExtended(const Vertex &from, const Vertex &other, const Wire &to, std::optional<double> length, const std::string &what,
                            const std::string &call) const;
        Vertex ArcExtended(const sketch::Element &el, bool atEnd, const Wire &to, std::optional<double> length, const std::string &what, const std::string &call) const;
        CutChain MetByCutters(const std::vector<std::string> &ids, const std::vector<std::string> &cutterIds, const Vertex &nearPoint);
        bool IsClosedChain(const std::vector<std::string> &ids) const;
        void CutPieces(const std::string &id, const std::vector<std::string> &cutterIds, std::optional<Vertex> nearPoint, bool reverse, bool trim, bool tied,
                       const std::string &call);
        virtual void CornerMade(const FilletRule &, const sketch::CornerParts &) {}
        virtual void CornerMade(const ChamferRule &, const sketch::CornerParts &) {}
        virtual void CornerMade(const CurvatureFilletRule &, const sketch::CornerParts &c);
        std::array<std::pair<size_t, size_t>, 2> ConnectedEnds(const std::string &a, const std::string &b, bool reverse, const std::string &call) const;
        template <class Rule>
        BasicSketch &CutCorners(const std::vector<Vertex> &at, const Rule &rule, const std::vector<std::string> &ids, const std::vector<Properties> &given,
                                const std::string &call);

        // sketchremove.hpp
        virtual void RemoveOne(const std::string &id);
        void RemoveWire(const std::string &id);
        std::vector<std::string> EndsToRemove(size_t k, const std::string &id) const;
        void FreezeOffsetCurve(size_t e);
        void DropSolverElements();
        void WithoutConstraints();
        void EraseElementData(size_t k);
        virtual void Reindex();

        // sketchexport.hpp
        std::unordered_map<size_t, Properties> VertexProps() const;
        void AddEdges(size_t e, Wire &w, const std::unordered_map<size_t, Properties> &vertexProps) const;
        std::vector<std::vector<size_t>> Groups() const { return GroupsOf(false); }
        std::vector<std::vector<size_t>> GroupsOf(bool all) const;

        Store s;
        AxisSystem axes;
        sketch::IdBook book;
        std::vector<Properties> props;
        std::vector<sketch::Role> roles;
        size_t atomic = 0;
        std::vector<std::vector<size_t>> ended;
        // per point (its own index, not merged) the first point element on it
        std::unordered_map<size_t, size_t> pointElementOf;
        sketch::PenState pen;
        std::optional<sketch::PenState> parked, normalPen;
        // set while an arm is cut, for the constraint sketch: the constraints of the element cut stay on its far end and
        // the corner
        bool cornering = false;
    };

    using Sketch = BasicSketch<sketch::Geometry>;

}

#include "planex/sketch/sketchdrawing.hpp"
#include "planex/sketch/sketchconstruct.hpp"
#include "planex/sketch/sketchundo.hpp"
#include "planex/sketch/sketchplane.hpp"
#include "planex/sketch/sketchqueries.hpp"
#include "planex/sketch/sketchcopies.hpp"
#include "planex/sketch/sketchoffset.hpp"
#include "planex/sketch/sketchcut.hpp"
#include "planex/sketch/sketchremove.hpp"
#include "planex/sketch/sketchexport.hpp"
