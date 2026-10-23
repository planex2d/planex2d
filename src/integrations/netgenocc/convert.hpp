// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <stdexcept>

#include <gp_Ax1.hxx>
#include <gp_Ax3.hxx>
#include <gp_Dir.hxx>
#include <gp_Pnt.hxx>
#include <gp_XYZ.hxx>

#include "core/axissystem.hpp"
#include "core/vector3.hpp"

namespace planex2d {

    inline gp_XYZ ToOCC(const Vector3 &v) { return {v.x, v.y, v.z}; }
    inline gp_Ax1 ToOCC(const Axis &a) { return {gp_Pnt(ToOCC(a.origin)), gp_Dir(ToOCC(a.direction))}; }
    inline gp_Ax3 ToOCC(const AxisSystem &a) { return {gp_Pnt(ToOCC(a.Origin())), gp_Dir(ToOCC(a.Normal())), gp_Dir(ToOCC(a.XDirection()))}; }

    inline Vector3 Vector3Of(const gp_XYZ &p) { return {p.X(), p.Y(), p.Z()}; }
    // planex's axis systems are right-handed: a left-handed one (a mirrored gp_Ax3) has no planex counterpart
    inline AxisSystem AxisSystemOf(const gp_Ax3 &a) {
        if (!a.Direct())
            throw std::invalid_argument("the axes are left-handed (mirrored); planex's axis systems are right-handed");
        return {Vector3Of(a.Location().XYZ()), Vector3Of(a.Direction().XYZ()), Vector3Of(a.XDirection().XYZ())};
    }

}
