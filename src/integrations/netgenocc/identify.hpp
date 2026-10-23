// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <TopExp.hxx>
#include <TopTools_IndexedMapOfShape.hxx>
#include <gp_GTrsf.hxx>
#include <gp_Mat.hxx>

#include "integrations/netgenocc/convert.hpp"
#include "integrations/netgenocc/project.hpp"
#include "shape/identification.hpp"

namespace planex2d {

    // trafo, a map of the plane of axes, in 3d
    inline gp_GTrsf GlobalTrafo(const Affine2 &trafo, const AxisSystem &axes) {
        const gp_Mat r(ToOCC(axes.XDirection()), ToOCC(axes.YDirection()), ToOCC(axes.Normal()));
        const gp_Mat local(trafo.m[0], trafo.m[1], 0.0, trafo.m[2], trafo.m[3], 0.0, 0.0, 0.0, 1.0);
        const gp_Mat g = r * local * r.Transposed();
        const gp_XYZ o = ToOCC(axes.Origin());
        return gp_GTrsf(g, gp_XYZ(trafo.t.x, trafo.t.y, 0.0).Multiplied(r) + o - o.Multiplied(g));
    }

    // the identifications on shape, an export on the plane of axes: every edge named from is paired with one named to,
    // else an error says how many paired
    inline void ApplyIdentifications(const TopoDS_Shape &shape, const AxisSystem &axes, const Identifications &ids) {
        if (ids.empty())
            return;
        TopTools_IndexedMapOfShape edges;
        TopExp::MapShapes(shape, TopAbs_EDGE, edges);
        for (const Identification &id : ids) {
            netgen::ListOfShapes from, to;
            EdgeList a, b;
            bool ownSizes = false;
            for (int i = 1; i <= edges.Extent(); ++i) {
                const TopoDS_Edge &e = TopoDS::Edge(edges(i));
                if (!netgen::OCCGeometry::HaveProperties(e))
                    continue;
                const netgen::ShapeProperties &p = netgen::OCCGeometry::GetProperties(e);
                if (!p.name || (*p.name != id.from && *p.name != id.to))
                    continue;
                const bool isFrom = *p.name == id.from;
                (isFrom ? from : to).push_back(e);
                for (Edge &x : ProjectEdge(e, axes, nullptr).edges)
                    (isFrom ? a : b).push_back(std::move(x));
                // netgen's default maxh means none
                ownSizes = ownSizes || (!isFrom && (p.maxh < 1e99 || p.partition));
            }
            if (from.empty() || to.empty())
                throw std::runtime_error("identification '" + id.name + "': no exported edge is named '" + (from.empty() ? id.from : id.to) + "'");
            const Affine2 trafo = id.trafo ? *id.trafo : GetTransformation(a, b, false, "'" + id.from + "'", "'" + id.to + "'");
            const size_t pairs = netgen::Identify(from, to, id.name, (netgen::Identifications::ID_TYPE)id.type,
                                                  netgen::occ2ng(GlobalTrafo(trafo, axes)));
            if (pairs != from.size() || pairs != to.size())
                throw std::runtime_error("identification '" + id.name + "': " + std::to_string(pairs) + " pairs of the " +
                                         std::to_string(from.size()) + " exported edges named '" + id.from + "' and the " +
                                         std::to_string(to.size()) + " named '" + id.to + "'; the transformation must take every "
                                         "piece of one side onto one of the other");
            if (ownSizes)
                Warning() << "identification '" << id.name << "': the edges named '" << id.to << "' have their own maxh or "
                          << "partition, netgen meshes them as those named '" << id.from << "'";
        }
    }

}
