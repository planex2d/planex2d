// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/integrations/netgenocc/section.hpp"

#include <optional>

namespace planex2d {

    Wire SectionEdges(const TopoDS_Shape &shape, const AxisSystem &axes) {
        Wire out(axes);
        BRepAlgoAPI_Section section(shape, gp_Pln(ToOCC(axes)), false);
        section.Approximation(true);
        section.Build();
        if (!section.IsDone())
            throw std::runtime_error("Intersect3d: OCC's section failed");
        TopTools_IndexedDataMapOfShapeListOfShape faces;
        TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, faces);
        for (TopExp_Explorer x(section.Shape(), TopAbs_EDGE); x.More(); x.Next()) {
            const TopoDS_Edge &edge = TopoDS::Edge(x.Current());
            if (BRep_Tool::Degenerated(edge))
                continue;
            Properties props;
            if (faces.Contains(edge))
                for (TopTools_ListOfShape::Iterator f(faces.FindFromKey(edge)); f.More() && !props; f.Next())
                    if (!InPlane(TopoDS::Face(f.Value()), axes))
                        props = PropertiesOf(f.Value());
            if (TopoDS_Shape f; !props && section.HasAncestorFaceOn1(edge, f))
                props = PropertiesOf(f);
            for (Edge &e : ProjectEdge(edge, axes, props).edges)
                out.Add(std::move(e));
        }
        return out;
    }

    Wire CrossSection(Wire w, const TopoDS_Shape &solid, const AxisSystem &axes) {
        std::vector<CutState> state(w.Size());
        Grid grid = w.EdgeGrid();
        CutAll(w, grid, state, true);
        std::vector<bool> removed(w.Size());
        for (size_t k = 0; k < w.Size(); ++k)
            removed[k] = state[k].excluded || state[k].drop;
        w.Erase(removed);
        Snap(w);
        for (std::vector<std::pair<size_t, bool>> open = w.OpenEnds(); !open.empty(); open = w.OpenEnds()) {
            std::vector<bool> loose(w.Size(), false);
            for (const auto &[i, atEnd] : open)
                loose[i] = true;
            w.Erase(loose);
        }
        const std::vector<HalfEdges> cells = ArrangementFaces(w);
        const gp_XYZ origin = ToOCC(axes.Origin()), ux = ToOCC(axes.XDirection()), uy = ToOCC(axes.YDirection());
        BRepClass3d_SolidClassifier classify(solid);
        std::vector<bool> inside(cells.size() + 1, false);
        for (size_t k = 0; k < cells.size(); ++k)
            if (const std::optional<Vertex> p = EdgesOf(w, cells[k]).InteriorPoint()) {
                classify.Perform(gp_Pnt(origin + ux * p->x + uy * p->y), Precision::Confusion());
                inside[k] = classify.State() == TopAbs_IN || classify.State() == TopAbs_ON;
            }
        const std::vector<size_t> cellOf = SideFaces(cells, w.Size());
        Wire out(axes);
        for (size_t i = 0; i < w.Size(); ++i)
            if (inside[cellOf[2 * i]] != inside[cellOf[2 * i + 1]])
                out.Add(w[i]);
        out.MergeOnCarriers();
        return out;
    }

    Projection FreeSection(const TopoDS_Shape &shape, const AxisSystem &axes) {
        Projection out{Wire(axes), {}};
        TopTools_IndexedDataMapOfShapeListOfShape faces, wires;
        TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, faces);
        TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_WIRE, wires);
        for (int i = 1; i <= faces.Extent(); ++i) {
            const TopoDS_Edge &edge = TopoDS::Edge(faces.FindKey(i));
            if (!faces(i).IsEmpty() || BRep_Tool::Degenerated(edge))
                continue;
            Properties props = PropertiesOf(edge);
            if ((!props || !props->name) && wires.Contains(edge))
                for (TopTools_ListOfShape::Iterator w(wires.FindFromKey(edge)); w.More() && (!props || !props->name); w.Next())
                    if (const Properties named = PropertiesOf(w.Value()); named && named->name)
                        props = ChangedProperties(props, [&](ShapeProperties &x) { x.name = named->name; });
            const TopoDS_Shape cut = PlaneCut(edge, axes);
            for (TopExp_Explorer x(cut, TopAbs_EDGE); x.More(); x.Next())
                if (!BRep_Tool::Degenerated(TopoDS::Edge(x.Current())))
                    for (Edge &e : ProjectEdge(TopoDS::Edge(x.Current()), axes, props).edges)
                        out.wire.Add(std::move(e));
            for (TopExp_Explorer v(cut, TopAbs_VERTEX); v.More(); v.Next())
                out.points.push_back({axes.ToHV(Vector3Of(BRep_Tool::Pnt(TopoDS::Vertex(v.Current())).XYZ())), NameOf(props)});
        }
        const gp_Pln plane(ToOCC(axes));
        for (TopExp_Explorer v(shape, TopAbs_VERTEX, TopAbs_EDGE); v.More(); v.Next())
            if (const gp_Pnt p = BRep_Tool::Pnt(TopoDS::Vertex(v.Current())); plane.Distance(p) <= Precision::Confusion())
                out.points.push_back({axes.ToHV(Vector3Of(p.XYZ())), NameOf(PropertiesOf(v.Current()))});
        out.points = PointsOffEdges(std::move(out.points), out.wire, Precision::Confusion());
        return out;
    }

    PlaneSection SectionOf(const AxisSystem &axes, const TopoDS_Shape &shape) {
        ShapeList parts;
        for (TopExp_Explorer x(shape, TopAbs_SOLID); x.More(); x.Next())
            if (Wire w = SectionEdges(x.Current(), axes); !w.Empty())
                if (Wire region = CrossSection(std::move(w), x.Current(), axes); !region.Empty())
                    parts.emplace_back(std::move(region), PropertiesOf(x.Current()));
        TopTools_IndexedDataMapOfShapeListOfShape solids;
        TopExp::MapShapesAndAncestors(shape, TopAbs_FACE, TopAbs_SOLID, solids);
        for (int i = 1; i <= solids.Extent(); ++i) {
            if (!solids(i).IsEmpty())
                continue;
            const TopoDS_Face &face = TopoDS::Face(solids.FindKey(i));
            if (InPlane(face, axes)) {
                if (Wire region = InitialClean(ProjectedFaceEdges(face, axes)); !region.Empty())
                    parts.emplace_back(std::move(region), PropertiesOf(face));
            } else if (Wire w = SectionEdges(face, axes); !w.Empty())
                parts.emplace_back(axes, FaceList{}, WireList{w});
        }
        Projection free = FreeSection(shape, axes);
        if (!free.wire.Empty())
            parts.emplace_back(axes, FaceList{}, WireList{std::move(free.wire)});
        if (parts.empty())
            return {Shape(axes), std::move(free.points)};
        return {parts.size() == 1 ? parts.front() : Glue(parts), std::move(free.points)};
    }

    Shape FromOCC(const TopoDS_Shape &shape) {
        const std::string advice = "; px.Intersect3d(plane, shape) gives a section, px.Project(shape, plane) a projection";
        AxisSystem axes;
        try {
            axes = SketchAxes(shape, std::nullopt, std::nullopt, "FromOCC");
        } catch (const std::invalid_argument &e) {
            throw std::invalid_argument(e.what() + advice);
        }
        ShapeList parts;
        for (TopExp_Explorer f(shape, TopAbs_FACE); f.More(); f.Next()) {
            const TopoDS_Face &face = TopoDS::Face(f.Current());
            if (!InPlane(face, axes))
                throw std::invalid_argument("FromOCC: a face is curved or not in the plane of the rest" + advice);
            if (Wire region = InitialClean(ProjectedFaceEdges(face, axes)); !region.Empty())
                parts.emplace_back(std::move(region), PropertiesOf(face));
        }
        TopTools_IndexedDataMapOfShapeListOfShape faces;
        TopExp::MapShapesAndAncestors(shape, TopAbs_EDGE, TopAbs_FACE, faces);
        Wire free(axes);
        for (int i = 1; i <= faces.Extent(); ++i)
            if (const TopoDS_Edge &e = TopoDS::Edge(faces.FindKey(i)); faces(i).IsEmpty() && !BRep_Tool::Degenerated(e))
                for (Edge &x : ProjectEdge(e, axes, PropertiesOf(e)).edges)
                    free.Add(std::move(x));
        if (!free.Empty())
            parts.emplace_back(axes, FaceList{}, WireList{free});
        if (parts.empty())
            throw std::invalid_argument("FromOCC: the shape has no edges");
        return parts.size() == 1 ? parts.front() : Glue(parts);
    }

}
