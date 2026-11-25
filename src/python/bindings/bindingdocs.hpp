// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <string>
#include <vector>

#include "bindingbase.hpp"

const char *OffsetDoc(const std::string &cls, const std::string &returns, const std::string &raises, const std::string &x);

const char *FieldDoc(const std::string &field, const std::string &whose);

const char *PolylinesDoc(const std::string &x);

struct MotionDoc {
    std::string call, copyCall, verb, past, how, more, args, raises;
    std::vector<std::string> forms, examples;

    const char *Of(const std::string &cls, bool copy) const {
        const std::string c = copy ? copyCall : call, x = VariableOf(cls);
        std::string text;
        for (const std::string &f : forms)
            text += c + "(" + f + ") -> " + cls + "\n";
        text += copy ? "\nA copy " + past + " " + how + "; this " + cls + " is not changed.\n\n" : "\n" + verb + " it in place " + how + ".\n\n";
        text += more + "Args:\n" + args + "\n" + (copy ? "" : "Returns:\n    this " + cls + ".\n\n") + "Raises:\n" + raises + "\nExamples:";
        for (const std::string &e : examples)
            text += "\n    " + x + "." + c + e;
        return Doc(text);
    }
};

MotionDoc RotateDoc();

MotionDoc TranslateDoc();

MotionDoc MirrorDoc();

MotionDoc ScaleDoc();

std::string ExportSignature();

std::string ExportArgs(const std::string &freeDefault);

const char *ExportDoc(const std::string &call, const std::string &returns, const std::string &text, const std::string &x, bool wire, bool approximation = false);

const char *FitArcsDoc(const std::string &cls, const std::string &text, const std::string &x);

const char *CloseGapsDoc(const std::string &cls, const std::string &x, const std::string &more);

const char *RemoveSmallEdgesDoc(const std::string &cls, const std::string &x);

const char *MergeCollinearEdgesDoc(const std::string &cls, const std::string &lines, const std::string &x);

const char *OpenEndsDoc(const std::string &x);

const char *IsClosedDoc(const std::string &x);

const char *SmallEdgesDoc(const std::string &x);

const char *IntersectionsDoc(const std::string &x, const std::string &whose);

const char *ContainsDoc(const std::string &x, const std::string &where);

const char *CornerDoc(bool chamfer, bool copy, const std::string &cls);

inline const char *AT_DOC = "    at: a place along the edges, one chain: a number its arc length from the start, a point ((h, v), or (x, y, z) "
                            "on the plane) the place nearest it (of places equally near, the one with the smallest arc length). "
                            "The dict then holds that place: point and point_hv, tangent and tangent_hv (unit, along the "
                            "chain), normal and normal_hv (unit, to its left), curvature (a left turn positive), arc_length, "
                            "distance (from the point given, 0 for a number) and edge (the edge it lies on; at a vertex the "
                            "one that starts there).\n";
inline const char *AT_RAISES = "; at= along several chains, or an arc length outside 0 to the length";

const char *DistanceDoc(const std::string &x);

const char *ClosestPointDoc(const std::string &x);

const char *MeasureDoc(const std::string &x, bool along = false);

inline const std::string cutterType = "Wire | EdgeView | Face | Shape | Sketch | QuickSketch | TopoDS_Shape";
inline const std::string cutterArg = "    cutter: the edges that cut, a Wire, edge, Face, Shape, sketch or OCC shape; projected into "
                                     "the plane if it lies elsewhere.\n";

const char *TrimDoc(const std::string &x);

const char *SplitDoc(const std::string &x);
