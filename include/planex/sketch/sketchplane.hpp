// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "planex/sketch/sketch.hpp"

namespace planex2d {

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::SetOrigin(const Vertex &p) {
        Flush();
        axes.Translate(axes.XDirection() * p.x + axes.YDirection() * p.y);
        s.MoveOrigin(p);
        pen.Translate(-p);
        if (parked)
            parked->Translate(-p);
        return *this;
    }

    // angle in degrees; the plane moves, what is drawn stays where it is in it
    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Rotate(const Vertex &c, double angle) {
        axes.Rotate(Axis{axes.ToXYZ(c), axes.Normal()}, angle);
        return *this;
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Rotate(const Axis &axis, double angle) {
        axes.Rotate(axis, angle);
        return *this;
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Translate(const Vertex &d) {
        axes.Translate(axes.XDirection() * d.x + axes.YDirection() * d.y);
        return *this;
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Translate(const Vector3 &v) {
        axes.Translate(v);
        return *this;
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Scale(const Vertex &c, double f, bool scaleMaxh, bool scaleMinh) {
        ScaleInside(f, scaleMaxh, scaleMinh);
        return Translate(c * (1.0 - f));
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Scale(const Vector3 &p, double f, bool scaleMaxh, bool scaleMinh) {
        ScaleInside(f, scaleMaxh, scaleMinh);
        axes.Scale(p, f);
        return *this;
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Mirror(const std::string &axis) {
        Flush();
        const auto [a0, u] = LineOf(axis, "Mirror");
        const Vector3 along = axes.XDirection() * u.x + axes.YDirection() * u.y;
        axes.MirrorKeepingCoordinates(AxesAlong(axes.ToXYZ(a0), Cross(along, axes.Normal())));
        return *this;
    }

    template <class Store>
    void BasicSketch<Store>::ScaleInside(double f, bool scaleMaxh, bool scaleMinh) {
        CheckScale(f);
        Flush();
        s.Scale(f);
        for (Properties &p : props)
            p = ScaledSizes(p, f, scaleMaxh, scaleMinh);
        pen.Scale(f);
        if (parked)
            parked->Scale(f);
    }

    template <class Store>
    Wire BasicSketch<Store>::InSketchAxes(Wire w, const std::string &call) const {
        const AxisSystem &a = w.Axes();
        if (a.IsClose(axes))
            return w;
        const Vector3 &n = axes.Normal();
        const double angle = config::state->axes_angle;
        const bool same = AngleBetween(a.Normal(), n) <= angle, flipped = AngleBetween(a.Normal(), n * -1.0) <= angle;
        if (!(same || flipped) || std::abs(Dot(a.Origin() - axes.Origin(), n)) > config::state->axes_distance)
            throw std::invalid_argument(call + ": the geometry lies on another plane than the sketch");
        const Vertex o = axes.ToHV(a.Origin()), x = axes.ToHV(a.Origin() + a.XDirection()) - o;
        if (flipped)
            w.Mirror(Vertex{0.0, 0.0}, Vertex{1.0, 0.0});
        w.Rotate(Vertex{0.0, 0.0}, Degrees(std::atan2(x.y, x.x)));
        w.Translate(o);
        w.SetAxes(axes);
        return w;
    }

}
