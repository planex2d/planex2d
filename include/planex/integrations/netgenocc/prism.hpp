// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once


#include <cmath>
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
#include <TopoDS_Face.hxx>
#include <TopoDS_Shape.hxx>
#include <TopoDS_Solid.hxx>
#include <gp_Ax2.hxx>
#include <gp_Dir.hxx>
#include <gp_Pln.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#include "planex/core/vector3.hpp"
#include "planex/integrations/netgenocc/compound.hpp"
#include "planex/integrations/netgenocc/convert.hpp"
#include "planex/integrations/netgenocc/occprops.hpp"
#include "planex/core/until.hpp"

namespace planex2d {

    struct PrismEnd {
        Until reach = Until::No;
        std::optional<gp_Pln> plane;

        bool IsGiven() const { return reach != Until::No || plane; }
    };

    struct Profile {
        TopoDS_Face face;
        netgen::ShapeProperties props;
    };

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

        void Check() const;
    };

    gp_Dir PrismDirection(const Vector3 &normal, const std::optional<Vector3> &dir, bool reverse, bool pocket, const std::string &call);

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

    TopoDS_Solid Swept(const Profile &profile, const gp_Vec &back, const gp_Vec &along);

    // a box on the far side of the plane, as wide as the prism's reach: it takes the prism's far end off
    struct FarSide {
        TopoDS_Shape box;
        TopoDS_Shape bottom;
    };

    FarSide BeyondPlane(const TopoDS_Shape &prism, const gp_Pln &plane, const gp_Dir &beyond);

    TopoDS_Solid CutAtPlane(const TopoDS_Solid &prism, const gp_Pln &plane, const Profile &profile, const PrismCall &c);

    double TravelTo(const gp_Pln &plane, const Profile &profile, const PrismCall &c);

    inline double ReachLength(const Profile &profile, const PrismCall &c) {
        Bnd_Box all = OCCBox(profile.face);
        all.Add(OCCBox(c.target));
        return 2.0 * all.CornerMin().Distance(all.CornerMax()) / std::abs(c.dir.Dot(c.normal));
    }

    inline std::string Describe(int index) { return "face " + std::to_string(index) + " of the target"; }

    bool RunsAlong(const TopoDS_Face &face, const gp_Pnt &p, const gp_Dir &dir);

    int ReachedFace(const TopoDS_Solid &prism, const Profile &profile, const PrismCall &c, TopTools_IndexedMapOfShape &faces);

    TopoDS_Solid EndedAt(const gp_Pln &plane, const Profile &profile, const PrismCall &c);

    inline TopoDS_Solid EndedAtPlane(const Profile &profile, const PrismCall &c) {
        return EndedAt(*c.until.plane, profile, c);
    }

    TopoDS_Solid EndedAtReachedFace(const Profile &profile, const PrismCall &c);

    TopoDS_Solid PrismOf(const TopoDS_Face &face, const PrismCall &c);

    inline std::vector<TopoDS_Solid> PrismsOf(const TopoDS_Shape &profile, const PrismCall &c) {
        std::vector<TopoDS_Solid> solids;
        for (TopExp_Explorer f(profile, TopAbs_FACE); f.More(); f.Next())
            solids.push_back(PrismOf(TopoDS::Face(f.Current()), c));
        if (solids.empty())
            throw std::invalid_argument(c.call + "(profile): the profile has no face; a profile is a Face, a Shape with faces or a "
                                                 "sketch with a closed region");
        return solids;
    }

    inline TopoDS_Shape PadSolid(const TopoDS_Shape &profile, const PrismCall &c) {
        c.Check();
        return OneOrCompound(PrismsOf(profile, c));
    }

    TopoDS_Shape PocketSolid(const TopoDS_Shape &profile, const PrismCall &c);

}
