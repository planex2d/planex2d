// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/shape/face.hpp"

#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

namespace planex2d {

    // every region of a union holds whole input faces: it takes their properties merged in the given order
    void FaceList::MergeCovered(const std::vector<FaceList> &inputs) {
        const std::vector<BBox> boxes = Boxes();
        std::vector<Properties> props(size());
        for (const FaceList &faces : inputs)
            for (const Face &f : faces)
                if (std::optional<Vertex> p = f.Edges().InteriorPoint())
                    if (size_t r = Locate(boxes, *p); r < size())
                        props[r] = MergedProperties(props[r], f.Props());
        for (size_t r = 0; r < size(); ++r)
            (*this)[r].SetProps(props[r]);
    }

    // every region of a difference or intersection lies inside one face of each operand: their properties merged
    // in the order of the operands
    void FaceList::InheritProps(const std::vector<FaceList> &operands) {
        std::vector<std::vector<BBox>> boxes;
        for (const FaceList &faces : operands)
            boxes.push_back(faces.Boxes());
        for (Face &r : *this) {
            std::optional<Vertex> p = r.Edges().InteriorPoint();
            Properties props;
            for (size_t o = 0; p && o < operands.size(); ++o)
                if (size_t k = operands[o].Locate(boxes[o], *p); k < operands[o].size())
                    props = MergedProperties(props, operands[o][k].Props());
            r.SetProps(props);
        }
    }

    void FaceList::InheritFirstOperandProps(const std::vector<FaceList> &operands) {
        std::vector<std::vector<BBox>> boxes;
        for (const FaceList &faces : operands)
            boxes.push_back(faces.Boxes());
        for (Face &r : *this) {
            std::optional<Vertex> p = r.Edges().InteriorPoint();
            Properties first;
            std::optional<double> maxh, minh;
            for (size_t o = 0; p && o < operands.size(); ++o)
                if (size_t k = operands[o].Locate(boxes[o], *p); k < operands[o].size()) {
                    const Properties &q = operands[o][k].Props();
                    if (o == 0)
                        first = q;
                    if (q) {
                        KeepBetter(maxh, q->maxh, std::less<>());
                        KeepBetter(minh, q->minh, std::less<>());
                    }
                }
            r.SetProps(ChangedProperties(first, [&](ShapeProperties &q) {
                q.maxh = maxh;
                q.minh = minh;
            }));
        }
    }

    FaceList Wire::Faces(const Properties &props) const {
        FaceList faces;
        for (const std::vector<size_t> &region : RegionFaces(*this, std::vector<bool>(edges.size(), false))) {
            Wire w(axes);
            for (size_t i : region)
                w.Add(edges[i]);
            faces.emplace_back(std::move(w), props);
        }
        return faces;
    }

    Face Wire::SingleFace(const std::string &call) const {
        FaceList faces = Faces();
        if (faces.size() > 1)
            throw std::invalid_argument(call + ": the edges bound " + std::to_string(faces.size()) +
                                        " regions; Shape() keeps them all");
        if (faces.empty()) {
            Face f;
            f.SetAxes(axes);
            return f;
        }
        return std::move(faces.front());
    }

    Wire WireList::Region(const char *call) const {
        if (empty())
            return Wire();
        Wire all(front().Axes());
        for (const Wire &w : *this) {
            if (!w.Axes().IsClose(all.Axes()))
                throw std::invalid_argument("the wires lie on different planes; move them onto one plane first");
            for (const Edge &e : w)
                all.Add(e);
        }
        const double tol = all.Tol();
        std::vector<bool> open;
        for (const Wire &w : *this)
            open.resize(open.size() + w.Size(), !w.HasEvenDegrees(tol));
        return InitialClean(all, open, call);
    }

}
