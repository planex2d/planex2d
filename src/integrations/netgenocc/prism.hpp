// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once


#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <BRepAdaptor_Surface.hxx>
#include <BRepAlgoAPI_Common.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepBndLib.hxx>
#include <BRepGProp.hxx>
#include <BRepLib.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Builder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <GeomAPI_ProjectPointOnSurf.hxx>
#include <Precision.hxx>
#include <TopExp.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Compound.hxx>
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include "core/vector3.hpp"
#include "integrations/netgenocc/convert.hpp"
#include "integrations/netgenocc/occprops.hpp"
#include "core/until.hpp"

namespace planex2d {

    // where a prism ends instead of after a length: the first or last face of the target it crosses, or a plane
    struct PrismEnd {
        Until reach = Until::No;
        std::optional<gp_Pln> plane;

        bool IsGiven() const { return reach != Until::No || plane; }
    };

    // a profile face with the properties it had before the prism shared its shape as a cap
    struct Profile {
        TopoDS_Face face;
        netgen::ShapeProperties props;
    };

    // normal: of the profile's plane; target: the solid a Pocket cuts, null for a Pad
    struct PrismCall {
        std::string call;
        gp_Dir normal, dir;
        std::optional<double> length;
        double length2 = 0.0;
        PrismEnd until;
        // with until: the end this far from the plane along its normal, past it if positive
        double offset = 0.0;
        TopoDS_Shape target;

        gp_Vec Along(double d) const { return gp_Vec(dir).Multiplied(d); }

        void Check() const {
            if (length && until.IsGiven())
                throw std::invalid_argument(call + ": give a length or until=, not both");
            const double first = length.value_or(0.0);
            if (!std::isfinite(first) || !std::isfinite(length2) || first < 0.0 || length2 < 0.0)
                throw std::invalid_argument(call + "(length, length2): lengths are finite and not negative, got " + std::to_string(first) +
                                            " and " + std::to_string(length2));
            if (!until.IsGiven() && first + length2 <= 0.0)
                throw std::invalid_argument(call + ": give a length or until=, e.g. " + call + "(..., 3)");
            if (!std::isfinite(offset))
                throw std::invalid_argument(call + "(offset=): the offset is finite, got " + std::to_string(offset));
            if (offset != 0.0 && !until.IsGiven())
                throw std::invalid_argument(call + "(offset=): offset= moves the end given by until=; with a length, give the "
                                                   "length it adds up to");
            if (target.IsNull() && until.reach != Until::No)
                throw std::invalid_argument(call + "(until=px." + (until.reach == Until::First ? "first" : "last") +
                                            "): a Pad has no target to reach; until= is a px.Plane, a flat face or a planex object "
                                            "(px.Pocket takes px.first and px.last)");
        }
    };

    // the direction of a prism: the normal (a Pocket: against it) unless dir is given, turned around by reverse
    inline gp_Dir PrismDirection(const Vector3 &normal, const std::optional<Vector3> &dir, bool reverse, bool pocket, const std::string &call) {
        const gp_Vec n(ToOCC(normal));
        const gp_Vec v = dir ? gp_Vec(ToOCC(*dir)) : n.Multiplied(pocket ? -1.0 : 1.0);
        if (v.Magnitude() <= Precision::Confusion())
            throw std::invalid_argument(call + "(dir): the direction is zero; give a direction that leaves the profile's plane, "
                                               "e.g. dir=(0, 0, 1)");
        gp_Dir d(v);
        if (reverse)
            d.Reverse();
        if (std::abs(d.Dot(gp_Dir(n))) <= 1e-9)
            throw std::invalid_argument(call + "(dir): the direction runs parallel to the profile's plane; give one that leaves it, "
                                               "e.g. dir=(0, 0, 1)");
        return d;
    }

    inline netgen::ShapeProperties NetgenPropertiesOf(const TopoDS_Shape &s) {
        return netgen::OCCGeometry::HaveProperties(s) ? netgen::OCCGeometry::GetProperties(s) : netgen::ShapeProperties();
    }

    // the caps carry the face's colour and maximal element size, not its name
    inline netgen::ShapeProperties CapProperties(const netgen::ShapeProperties &face) {
        netgen::ShapeProperties cap;
        cap.col = face.col;
        cap.maxh = face.maxh;
        return cap;
    }

    inline netgen::ShapeProperties SolidProperties(const netgen::ShapeProperties &face) {
        netgen::ShapeProperties solid;
        solid.name = face.name;
        solid.maxh = face.maxh;
        solid.col = face.col;
        return solid;
    }

    inline void MergeProperties(const TopoDS_Shape &from, const TopoDS_Shape &to) {
        if (netgen::OCCGeometry::HaveProperties(from))
            netgen::OCCGeometry::GetProperties(to).Merge(netgen::OCCGeometry::GetProperties(from));
    }

    // netgen keys properties by shape and location: the moved base's parts take those of the profile's parts
    inline void MergeGenerated(BRepPrimAPI_MakePrism &prism, const TopoDS_Shape &moved, const TopoDS_Shape &source) {
        for (const TopoDS_Shape &made : prism.Generated(moved))
            MergeProperties(source, made);
    }

    inline Bnd_Box OCCBox(const TopoDS_Shape &s) {
        Bnd_Box box;
        BRepBndLib::AddOptimal(s, box, false, false);
        return box;
    }

    inline double VolumeOf(const TopoDS_Shape &s) {
        GProp_GProps props;
        BRepGProp::VolumeProperties(s, props);
        return props.Mass();
    }

    inline TopoDS_Solid OneSolid(const TopoDS_Shape &shape, const std::string &call) {
        TopoDS_Solid solid;
        int n = 0;
        for (TopExp_Explorer e(shape, TopAbs_SOLID); e.More(); e.Next(), ++n)
            solid = TopoDS::Solid(e.Current());
        if (n != 1)
            throw std::runtime_error(call + ": OCC gave " + std::to_string(n) + " solids for one prism");
        return solid;
    }

    // the profile face swept along `along` from its place moved back by `back`; the walls take the properties of the
    // edges, the edges along the sweep those of the vertices, the caps those of the face without its name
    inline TopoDS_Solid Swept(const Profile &profile, const gp_Vec &back, const gp_Vec &along) {
        gp_Trsf shift;
        shift.SetTranslation(back.Reversed());
        const TopoDS_Shape base = profile.face.Moved(TopLoc_Location(shift));
        BRepPrimAPI_MakePrism prism(base, along.Added(back));
        TopTools_IndexedMapOfShape edges, vertices, profileEdges, profileVertices;
        TopExp::MapShapes(base, TopAbs_EDGE, edges);
        TopExp::MapShapes(base, TopAbs_VERTEX, vertices);
        TopExp::MapShapes(profile.face, TopAbs_EDGE, profileEdges);
        TopExp::MapShapes(profile.face, TopAbs_VERTEX, profileVertices);
        for (int k = 1; k <= edges.Extent(); ++k)
            MergeGenerated(prism, edges(k), profileEdges(k));
        for (int k = 1; k <= vertices.Extent(); ++k)
            MergeGenerated(prism, vertices(k), profileVertices(k));
        netgen::OCCGeometry::GetProperties(prism.FirstShape()) = CapProperties(profile.props);
        netgen::OCCGeometry::GetProperties(prism.LastShape()) = CapProperties(profile.props);
        TopoDS_Solid solid = TopoDS::Solid(prism.Shape());
        BRepLib::OrientClosedSolid(solid);
        return solid;
    }

    // a box on the far side of the plane, as wide as the prism's reach: it takes the prism's far end off
    struct FarSide {
        TopoDS_Shape box;
        TopoDS_Shape bottom;
    };

    inline FarSide BeyondPlane(const TopoDS_Shape &prism, const gp_Pln &plane, const gp_Dir &beyond) {
        const Bnd_Box reach = OCCBox(prism);
        const double size = 4.0 * reach.CornerMin().Distance(reach.CornerMax());
        const gp_Pnt middle(reach.CornerMin().XYZ().Added(reach.CornerMax().XYZ()).Multiplied(0.5));
        const gp_Dir n = plane.Axis().Direction();
        const gp_Pnt onPlane = middle.Translated(gp_Vec(n).Multiplied(gp_Vec(middle, plane.Location()).Dot(gp_Vec(n))));
        const gp_Ax2 axes(onPlane, beyond);
        const gp_Vec corner = gp_Vec(axes.XDirection()).Added(gp_Vec(axes.YDirection())).Multiplied(-0.5 * size);
        // BottomFace is the box's face at z = 0, the plane
        BRepPrimAPI_MakeBox box(gp_Ax2(onPlane.Translated(corner), beyond, axes.XDirection()), size, size, size);
        return {box.Shape(), box.BottomFace()};
    }

    // the prism cut at the plane; its end there is a cap
    inline TopoDS_Solid CutAtPlane(const TopoDS_Solid &prism, const gp_Pln &plane, const Profile &profile, const PrismCall &c) {
        const gp_Dir n = plane.Axis().Direction();
        const FarSide far = BeyondPlane(prism, plane, c.dir.Dot(n) > 0.0 ? n : n.Reversed());
        BRepAlgoAPI_Cut cut(prism, far.box);
        if (!cut.IsDone() || cut.HasErrors())
            throw std::runtime_error(c.call + "(until=): OCC could not cut the prism at the plane");
        PropagateProperties(cut, prism);
        const TopTools_ListOfShape &end = cut.Modified(far.bottom);
        if (end.IsEmpty())
            throw std::runtime_error(c.call + "(until=): OCC gave no end face at the plane");
        for (const TopoDS_Shape &s : end)
            netgen::OCCGeometry::GetProperties(s) = CapProperties(profile.props);
        return OneSolid(cut.Shape(), c.call);
    }

    // how far along dir the plane lies from the profile's farthest corner of its bounding box; all corners must lie before it
    inline double TravelTo(const gp_Pln &plane, const Profile &profile, const PrismCall &c) {
        const gp_Dir n = plane.Axis().Direction();
        const double slope = c.dir.Dot(n);
        if (std::abs(slope) <= 1e-9)
            throw std::invalid_argument(c.call + "(until=): dir runs parallel to the plane, which it never reaches; give a dir that crosses it");
        const Bnd_Box box = OCCBox(profile.face);
        const gp_Pnt lo = box.CornerMin(), hi = box.CornerMax();
        double nearest = std::numeric_limits<double>::infinity(), farthest = -nearest;
        for (int corner = 0; corner < 8; ++corner) {
            const gp_Pnt p((corner & 1) ? hi.X() : lo.X(), (corner & 2) ? hi.Y() : lo.Y(), (corner & 4) ? hi.Z() : lo.Z());
            const double t = gp_Vec(p, plane.Location()).Dot(gp_Vec(n)) / slope;
            nearest = std::min(nearest, t);
            farthest = std::max(farthest, t);
        }
        if (nearest <= Precision::Confusion())
            throw std::invalid_argument(c.call + "(until=): the plane does not lie ahead of the whole profile along dir (it meets the "
                                                 "profile's bounding box); move the plane or turn dir around with reverse=True");
        return farthest;
    }

    inline double ReachLength(const Profile &profile, const PrismCall &c) {
        Bnd_Box all = OCCBox(profile.face);
        all.Add(OCCBox(c.target));
        return 2.0 * all.CornerMin().Distance(all.CornerMax()) / std::abs(c.dir.Dot(c.normal));
    }

    inline std::string Describe(int index) { return "face " + std::to_string(index) + " of the target"; }

    // a face along dir (a side wall, a bore) is no end, the prism runs past it: its normal at the point nearest p is square
    // to dir
    inline bool RunsAlong(const TopoDS_Face &face, const gp_Pnt &p, const gp_Dir &dir) {
        GeomAPI_ProjectPointOnSurf nearest(p, BRep_Tool::Surface(face));
        if (nearest.NbPoints() == 0)
            return false;
        double u, v;
        nearest.LowerDistanceParameters(u, v);
        gp_Pnt at;
        gp_Vec du, dv;
        BRepAdaptor_Surface(face).D1(u, v, at, du, dv);
        const gp_Vec n = du.Crossed(dv);
        return n.Magnitude() > 0.0 && std::abs(n.Normalized().Dot(gp_Vec(dir))) <= 1e-9;
    }

    // the face of the target the long prism crosses first or last ahead of the profile, by its height over the profile's plane
    inline int ReachedFace(const TopoDS_Solid &prism, const Profile &profile, const PrismCall &c, TopTools_IndexedMapOfShape &faces) {
        TopExp::MapShapes(c.target, TopAbs_FACE, faces);
        GProp_GProps from;
        BRepGProp::SurfaceProperties(profile.face, from);
        const double sign = c.dir.Dot(c.normal) > 0.0 ? 1.0 : -1.0;
        const bool first = c.until.reach == Until::First;
        int found = 0;
        double best = 0.0;
        for (int k = 1; k <= faces.Extent(); ++k) {
            BRepAlgoAPI_Common piece(faces(k), prism);
            GProp_GProps props;
            BRepGProp::SurfaceProperties(piece.Shape(), props);
            if (props.Mass() <= 1e-9 * from.Mass() || RunsAlong(TopoDS::Face(faces(k)), props.CentreOfMass(), c.dir))
                continue;
            const double height = sign * gp_Vec(from.CentreOfMass(), props.CentreOfMass()).Dot(gp_Vec(c.normal));
            if (height <= Precision::Confusion() || (found && (first ? height >= best : height <= best)))
                continue;
            found = k;
            best = height;
        }
        if (!found)
            throw std::invalid_argument(c.call + "(until=px." + (first ? "first" : "last") + "): the prism meets no face of the target "
                                                 "ahead of the profile; check dir and the profile's place");
        return found;
    }

    // the prism up to the plane moved by c.offset along its normal, away from the profile if positive
    inline TopoDS_Solid EndedAt(const gp_Pln &plane, const Profile &profile, const PrismCall &c) {
        gp_Dir beyond = plane.Axis().Direction();
        if (c.dir.Dot(beyond) < 0.0)
            beyond.Reverse();
        const gp_Pln end = plane.Translated(gp_Vec(beyond).Multiplied(c.offset));
        double travel;
        try {
            travel = TravelTo(end, profile, c);
        } catch (const std::invalid_argument &) {
            if (c.offset == 0.0)
                throw;
            throw std::invalid_argument(c.call + "(until=, offset=" + std::to_string(c.offset) + "): the end offset from the "
                                                 "plane does not lie ahead of the whole profile along dir; give a smaller offset");
        }
        const TopoDS_Solid prism = Swept(profile, c.Along(c.length2), c.Along(2.0 * travel));
        return CutAtPlane(prism, end, profile, c);
    }

    inline TopoDS_Solid EndedAtPlane(const Profile &profile, const PrismCall &c) {
        return EndedAt(*c.until.plane, profile, c);
    }

    inline TopoDS_Solid EndedAtReachedFace(const Profile &profile, const PrismCall &c) {
        const TopoDS_Solid prism = Swept(profile, c.Along(c.length2), c.Along(ReachLength(profile, c)));
        TopTools_IndexedMapOfShape faces;
        const int k = ReachedFace(prism, profile, c, faces);
        const TopoDS_Face hit = TopoDS::Face(faces(k));
        const BRepAdaptor_Surface surface(hit);
        if (surface.GetType() != GeomAbs_Plane)
            throw std::invalid_argument(c.call + "(until=px." + (c.until.reach == Until::First ? "first" : "last") + "): the " +
                                        Describe(k) + " it reaches is not flat; until= ends on flat faces only, give a px.Plane or "
                                        "a flat face instead");
        const gp_Pln plane = surface.Plane();
        TravelTo(plane, profile, c);
        return EndedAt(plane, profile, c);
    }

    inline TopoDS_Solid PrismOf(const TopoDS_Face &face, const PrismCall &c) {
        const Profile profile{face, NetgenPropertiesOf(face)};
        TopoDS_Solid solid;
        if (c.until.plane)
            solid = EndedAtPlane(profile, c);
        else if (c.until.reach != Until::No)
            solid = EndedAtReachedFace(profile, c);
        else
            solid = Swept(profile, c.Along(c.length2), c.Along(c.length.value_or(0.0)));
        netgen::OCCGeometry::GetProperties(solid) = SolidProperties(profile.props);
        return solid;
    }

    inline std::vector<TopoDS_Solid> PrismsOf(const TopoDS_Shape &profile, const PrismCall &c) {
        std::vector<TopoDS_Solid> solids;
        for (TopExp_Explorer f(profile, TopAbs_FACE); f.More(); f.Next())
            solids.push_back(PrismOf(TopoDS::Face(f.Current()), c));
        if (solids.empty())
            throw std::invalid_argument(c.call + "(profile): the profile has no face; a profile is a Face, a Shape with faces or a "
                                                 "sketch with a closed region");
        return solids;
    }

    inline TopoDS_Shape SolidOrCompound(const std::vector<TopoDS_Solid> &solids) {
        if (solids.size() == 1)
            return solids.front();
        BRep_Builder builder;
        TopoDS_Compound all;
        builder.MakeCompound(all);
        for (const TopoDS_Solid &s : solids)
            builder.Add(all, s);
        return all;
    }

    // the solid of the profile swept along dir (length), or up to an end (until)
    inline TopoDS_Shape PadSolid(const TopoDS_Shape &profile, const PrismCall &c) {
        c.Check();
        return SolidOrCompound(PrismsOf(profile, c));
    }

    // the target minus the profile's prism; the target's faces keep their properties, the prism's walls, floor and edges bring theirs
    inline TopoDS_Shape PocketSolid(const TopoDS_Shape &profile, const PrismCall &c) {
        c.Check();
        if (!TopExp_Explorer(c.target, TopAbs_SOLID).More())
            throw std::invalid_argument(c.call + "(target): the target has no solid; a Pocket subtracts from a solid");
        const std::vector<TopoDS_Solid> cutters = PrismsOf(profile, c);
        TopTools_ListOfShape from, tools;
        from.Append(c.target);
        for (const TopoDS_Solid &s : cutters)
            tools.Append(s);
        BRepAlgoAPI_Cut cut;
        cut.SetArguments(from);
        cut.SetTools(tools);
        cut.Build();
        if (!cut.IsDone() || cut.HasErrors())
            throw std::runtime_error(c.call + ": OCC could not cut the prism from the target");
        PropagateProperties(cut, c.target);
        for (TopExp_Explorer s(c.target, TopAbs_SOLID); s.More(); s.Next())
            for (const TopoDS_Shape &m : cut.Modified(s.Current()))
                MergeProperties(s.Current(), m);
        for (const TopoDS_Solid &s : cutters)
            PropagateProperties(cut, s);
        std::vector<TopoDS_Solid> solids;
        for (TopExp_Explorer s(cut.Shape(), TopAbs_SOLID); s.More(); s.Next())
            solids.push_back(TopoDS::Solid(s.Current()));
        if (solids.empty())
            throw std::invalid_argument(c.call + "(profile): the prism removes the whole target; use a smaller profile or length");
        if (std::abs(VolumeOf(cut.Shape()) - VolumeOf(c.target)) <= 1e-9 * VolumeOf(c.target))
            throw std::invalid_argument(c.call + "(profile): the prism removes nothing from the target; check the profile's place, "
                                                 "dir and reverse");
        return SolidOrCompound(solids);
    }

}
