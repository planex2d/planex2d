// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

struct SketchSetup {
    py::object autoConstraint;
    std::optional<bool> errorOnBranchChange;
    std::string mode;
    std::optional<std::string> id;
    py::object dir;
};

py::object SketchToCopy(const std::string &call, const py::tuple &args, const SketchSetup &setup);

struct CopiedGeometry {
    Projection projection;
    bool hold;
};

void AddCopiedGeometry(const py::handle &h, const AxisSystem &axes, std::vector<CopiedGeometry> &out);

inline Sketch QuickSketchOfObjects(const py::object &objects, const std::string &call) {
    Sketch s(AxesArg(objects, call));
    std::vector<CopiedGeometry> geometry;
    AddCopiedGeometry(objects, s.Axes(), geometry);
    if (geometry.empty())
        Warning() << call << "(" << py::repr(objects).cast<std::string>() << ", copy=True): nothing to copy, an empty sketch";
    for (const CopiedGeometry &g : geometry)
        s.AddProjection(g.projection, false, std::nullopt, "Projection", std::nullopt, g.hold);
    return s;
}

Wire OneGeometry(const py::object &geometry, const std::string &call);

inline py::object (*quickOfConstraintSketch)(const py::handle &h) = nullptr;

template <class S>
inline S NewSketch(const std::string &call, const py::tuple &args, const py::object &axes, const SketchSetup &setup) {
    const auto &[autoConstraint, errorOnBranchChange, mode, id, dir] = setup;
    const std::optional<Vertex> start = args.empty() ? std::nullopt : StartArg(args[0], call);
    if (!args.empty() && !start && !axes.is_none())
        throw py::type_error(call + ": the plane is given twice, as the first argument and as plane=");
    const CallArgs rest = Split(args.empty() ? py::tuple() : py::tuple(args[py::slice(1, (py::ssize_t)args.size(), 1)]), call);
    if (!rest.values.empty() || (!start && !rest.flags.empty()))
        throw py::type_error(call + "((h, v), flags..., dir=(dh, dv)) starts at (h, v), " + call + "(plane) draws on the plane");
    if (rest.autoFlag)
        throw py::type_error(call + "(px.Auto): a new sketch draws nothing yet; auto_constraint= sets it for the sketch, px.Auto goes "
                                    "with a drawing call");
    if (!dir.is_none() && !start)
        throw py::type_error(call + "(dir=...): dir= goes with a start point, " + call + "((h, v), dir=(dh, dv))");
    S s(AxesArg(start || args.empty() ? axes : py::object(args[0]), call));
    if constexpr (HasConstraints<S>) {
        s.autoConstraint = AutoConstraintOf(autoConstraint, call);
        s.SetMode(mode);
        if (errorOnBranchChange)
            s.errorOnBranchChange = *errorOnBranchChange;
    } else if (AsksAutoConstraint(autoConstraint) || mode == "strict")
        throw py::value_error(call + (AsksAutoConstraint(autoConstraint) ? "(auto_constraint=True)" : "(mode='strict')") +
                              ": a QuickSketch has no constraints; px.Sketch(...) has them");
    GiveId(s, id);
    if (start) {
        s.MoveTo(*start, rest.flags);
        if (!dir.is_none()) {
            const Vertex d = PointArg(dir, call + "(dir=)");
            if (!(Norm(d) > 0.0))
                throw py::value_error(call + "(dir=): the direction has no length");
            s.Direction(d.x, d.y);
        }
    }
    return s;
}

inline std::vector<bool (*)(py::handle, const std::string &)> sketchOwnsId;

// px.Move's px.Sketch, set by BindConstraintSketch; a build without the constraints has none
inline py::object (*constraintSketchAt)(const py::tuple &start, const py::object &axes, const SketchSetup &setup) = nullptr;
