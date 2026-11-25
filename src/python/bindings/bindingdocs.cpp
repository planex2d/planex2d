// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "bindingdocs.hpp"

const char *OffsetDoc(const std::string &cls, const std::string &returns, const std::string &raises, const std::string &x) {
    return Doc("Offset(d: float, inplace: bool = True, corners: OffsetForm = px.Round) -> " + returns + "\n\n"
               "Grows the faces by d: the band of width abs(d) along their boundaries added for d > 0, taken away for d < 0.\n\n"
               "The band is Wire.Offset of their edges; the faces keep their properties, the new edges those of the edges they "
               "come from. A corner that the offset goes round on its outside (a convex one growing, a reflex one shrinking) "
               "becomes an arc of radius abs(d) about it (px.Round), the two offsets extended along their end tangents until they "
               "meet (px.Sharp, without a limit: a corner of angle theta between its edges reaches abs(d) / sin(theta / 2) out from "
               "it), or the line between their ends (px.Chamfer).\n\n"
               "Args:\n"
               "    d: the width of the band, a length; d > 0 grows, d < 0 shrinks, 0 (or a width within the tolerance) changes "
               "nothing.\n"
               "    inplace: True changes this " + cls + " and returns it; False returns the offset as a new Shape and "
               "leaves this one.\n"
               "    corners: px.Round, px.Sharp or px.Chamfer.\n\n"
               "Raises:\n"
               "    ValueError: " + raises + " corners is px.Flat or px.Square.\n\n"
               "Examples:\n"
               "    " + x + ".Offset(0.5)\n"
               "    band = " + x + ".Offset(0.5, inplace=False, corners=px.Sharp)");
}

const char *FieldDoc(const std::string &field, const std::string &whose) {
    static const std::map<std::string, std::string> type{
        {"name", "str | None"},         {"maxh", "float | None"},
        {"minh", "float | None"},       {"hpref", "float | None"},
        {"layer", "int | None"},        {"col", "tuple[float, float, float, float] | None"},
        {"partition", "list[float] | None"}, {"quad_dominated", "bool | None"}};
    static const std::map<std::string, std::string> what{
        {"name", "The name"},
        {"maxh", "The largest mesh size (netgen's maxh)"},
        {"minh", "The smallest mesh size (netgen's minh)"},
        {"hpref", "Netgen's hpref"},
        {"layer", "Netgen's layer number"},
        {"col", "The color (r, g, b, a), set as (r, g, b) or (r, g, b, a)"},
        {"partition", "The partition (the mesh points along an edge as increasing fractions of its length in (0, 1))"},
        {"quad_dominated", "The quad_dominated flag (True: netgen meshes with mostly quadrilaterals)"}};
    return Doc(field + ": " + type.at(field) + "\n\n" + what.at(field) + whose);
}

const char *PolylinesDoc(const std::string &x) {
    return Doc("Polylines(tol: float) -> list[np.ndarray]\n\n"
               "The loops and open chains as (n, 2) arrays of plane coordinates, each chord within tol of its edge.\n\n"
               "A face gives its outer loop counterclockwise, then its holes clockwise; a shape its faces in turn, then its "
               "free wires; a wire's loop runs the way the wire's first edge runs; a closed loop ends at its first point.\n\n"
               "Args:\n"
               "    tol: the largest distance of a chord from its edge, a length > 0.\n\n"
               "Raises:\n"
               "    ValueError: tol is not positive and finite, or below the rounding of an arc's or a spline's "
               "coordinates.\n\n"
               "Examples:\n"
               "    " + x + ".Polylines(0.01)");
}

MotionDoc RotateDoc() {
    return {"Rotate", "Rotated", "Rotates", "rotated",
            "by angle around center in its plane, or around the axis through point along dir in 3d",
            "In 3d only its plane moves; its coordinates in the plane stay.\n\n",
            "    center: the center of the rotation in the plane, (h, v).\n"
            "    angle: the angle in degrees.\n"
            "    point: a point of the axis in 3d, (x, y, z).\n"
            "    dir: the direction of the axis in 3d, (dx, dy, dz).\n",
            "    ValueError: a value is not finite.\n",
            {"center: Point, angle: float", "point: Point3, /, dir: tuple[float, float, float], angle: float"},
            {"((0, 0), 90)", "((0, 0, 0), (0, 0, 1), 45)"}};
}

MotionDoc TranslateDoc() {
    return {"Translate", "Translated", "Moves", "moved",
            "by d: in the plane by (dh, dv), in 3d by (dx, dy, dz)",
            "In 3d only its plane moves; its coordinates in the plane stay.\n\n",
            "    d: the displacement, (dh, dv) in the plane or (dx, dy, dz) in 3d.\n"
            "    dh: the displacement along the plane's first axis.\n"
            "    dv: the displacement along the plane's second axis.\n"
            "    dx: the displacement along x in 3d.\n"
            "    dy: the displacement along y in 3d.\n"
            "    dz: the displacement along z in 3d.\n",
            "    ValueError: a value is not finite.\n",
            {"d: tuple[float, float]", "dh: float, dv: float", "d: tuple[float, float, float]", "dx: float, dy: float, dz: float"},
            {"((10, 0))", "(0, 0, 5)"}};
}

MotionDoc MirrorDoc() {
    return {"Mirror", "Mirrored", "Mirrors", "mirrored",
            "at the line through point along dir in its plane, or at the plane through point with normal in 3d",
            "In 3d its plane is mirrored and keeps a right-handed coordinate system, so its coordinates in the plane are "
            "mirrored at the plane's first axis.\n\n",
            "    point: a point of the mirror line, (h, v), or of the mirror plane in 3d, (x, y, z).\n"
            "    dir: the direction of the mirror line in the plane, (dh, dv).\n"
            "    normal: the normal of the mirror plane in 3d, (dx, dy, dz).\n",
            "    ValueError: a value is not finite.\n",
            {"point: Point, /, dir: tuple[float, float]", "point: Point3, /, normal: tuple[float, float, float]"},
            {"((0, 0), (0, 1))", "((0, 0, 0), (1, 0, 0))"}};
}

MotionDoc ScaleDoc() {
    return {"Scale", "Scaled", "Scales", "scaled",
            "by factor around center in its plane, or around point in 3d",
            "The mesh sizes maxh and minh stay unless asked for.\n\n",
            "    center: the center of the scaling in the plane, (h, v).\n"
            "    point: the center of the scaling in 3d, (x, y, z).\n"
            "    factor: the scale factor, > 0.\n"
            "    scale_maxh: True also scales maxh.\n"
            "    scale_minh: True also scales minh.\n",
            "    ValueError: factor is not positive and finite, or a coordinate is not finite.\n",
            {"center: Point, factor: float, scale_maxh: bool = False, scale_minh: bool = False",
             "point: Point3, /, factor: float, scale_maxh: bool = False, scale_minh: bool = False"},
            {"((0, 0), 2)", "((0, 0), 0.5, scale_maxh=True)"}};
}

std::string ExportSignature() {
    return "fit_splines: bool | dict | None = None, remove_edges_smaller: float | None = None, close_gaps_smaller: float | "
           "None = None, remove_free_edges: bool | None = None, merge_collinear_closer: float | None = None, "
           "remove_faces_smaller: float | None = None, merge_small_faces_instead: bool | None = None, fit_arcs: bool | "
           "dict | None = None, remove_holes_smaller: float | None = None, merge_slivers_thinner: float | None = None";
}

std::string ExportArgs(const std::string &freeDefault) {
    const std::string config = "; None takes px.config.";
    return "    fit_splines: fits splines to chains of short edges: True, False or a dict of the fit's options (tol, "
           "max_edge_length, max_edge_angle, ignore_edges_smaller)" + config + "fit_splines.\n"
           "    remove_edges_smaller: removes lines and arcs no longer than this" + config + "remove_edges_smaller.\n"
           "    close_gaps_smaller: closes gaps between ends up to this" + config + "close_gaps_smaller.\n"
           "    remove_free_edges: removes the edges that touch no other edge at either end; " + freeDefault + ". In a Shape "
           "only the edges of its faces go, its wires stay (a wire of one isolated edge too).\n"
           "    merge_collinear_closer: merges two lines whose common vertex is this close to the line through their "
           "far ends" + config + "merge_collinear_closer.\n"
           "    remove_faces_smaller: removes the faces of less area than this (0: none)" + config + "remove_faces_smaller.\n"
           "    merge_small_faces_instead: True unites such a face with the face it shares the longest boundary with "
           "instead" + config + "merge_small_faces_instead.\n"
           "    fit_arcs: fits arcs to chains of edges that lie within tol of an arc: True, False or a dict of FitArcs' options" + config +
           "fit_arcs.\n"
           "    remove_holes_smaller: fills the holes of less area than this; a hole with a face in it stays (0: none)" +
           config + "remove_holes_smaller.\n"
           "    merge_slivers_thinner: gives the parts of a face no wider than this to the face beside them (thrown away "
           "beside none) and fills the gaps that narrow inside a face; a gap between faces of different properties stays, "
           "with a warning (0: none)" + config + "merge_slivers_thinner.\n";
}

const char *ExportDoc(const std::string &call, const std::string &returns, const std::string &text, const std::string &x, bool wire, bool approximation) {
    return Doc(call + "(" + (approximation ? "approximation: bool = False, " : "") + ExportSignature() + ") -> " + returns +
               "\n\n" + text + "\n\n"
               "Args:\n" +
               (approximation ? "    approximation: True gives the splines as their arc approximation.\n" : "") +
               ExportArgs(wire ? "None: False for a wire" : "None takes px.config.remove_free_edges") + "\n"
               "Raises:\n"
               "    TypeError: fit_splines or fit_arcs has another form or names an unknown option.\n\n"
               "Examples:\n"
               "    " + x + "." + call + "()\n"
               "    " + x + "." + call + "(fit_arcs=True, remove_edges_smaller=1e-6)");
}

const char *FitArcsDoc(const std::string &cls, const std::string &text, const std::string &x) {
    const std::string config = "; None takes px.config.fit_arcs[\"";
    return Doc("FitArcs(tol: float | None = None, max_edge_length: float | None = None, max_edge_angle: float | None = None, "
               "ignore_edges_smaller: float | None = None) -> " + cls + "\n\n" + text + "\n\n"
               "Only lines join a chain; arcs already there stay as they are.\n\n"
               "Args:\n"
               "    tol: the largest distance of the arc from the chain" + config + "tol\"], whose default None takes 1e-4 "
               "of the diagonal of the bounding box of what is fitted.\n"
               "    max_edge_length: only lines shorter than this join a chain" + config + "max_edge_length\"], whose "
               "default None sets no limit.\n"
               "    max_edge_angle: the largest turn between neighboring edges of a chain, in degrees" + config +
               "max_edge_angle\"].\n"
               "    ignore_edges_smaller: edges shorter than this do not end a chain by their direction" + config +
               "ignore_edges_smaller\"], whose default None takes 1e-4 of the diagonal of the bounding box of what is fitted.\n\n"
               "Returns:\n"
               "    this " + cls + ".\n\n"
               "Examples:\n"
               "    " + x + ".FitArcs()\n"
               "    " + x + ".FitArcs(tol=1e-4, max_edge_length=0.5)");
}

const char *CloseGapsDoc(const std::string &cls, const std::string &x, const std::string &more) {
    return Doc("CloseGaps(tol: float) -> " + cls + "\n\n"
               "Joins every endpoint without an exact match on another edge with all endpoints of other edges within tol "
               "at their midpoint, in place.\n\n"
               "The ends meet halfway between where they were, whatever the order of the edges; ends that already "
               "coincide count once.\n\n" + more +
               "Args:\n"
               "    tol: the largest gap closed, a length.\n\n"
               "Returns:\n"
               "    this " + cls + ".\n\n"
               "Examples:\n"
               "    " + x + ".CloseGaps(1e-6)");
}

const char *RemoveSmallEdgesDoc(const std::string &cls, const std::string &x) {
    return Doc("RemoveSmallEdges(tol: float) -> " + cls + "\n\n"
               "Removes the lines and arcs no longer than tol in place; both ends of each merge into its midpoint.\n\n"
               "Lengths are measured before any merge; splines and the edges of a spline (fitted at export) stay.\n\n"
               "Args:\n"
               "    tol: the largest length removed.\n\n"
               "Returns:\n"
               "    this " + cls + ".\n\n"
               "Examples:\n"
               "    " + x + ".RemoveSmallEdges(1e-4)");
}

const char *MergeCollinearEdgesDoc(const std::string &cls, const std::string &lines, const std::string &x) {
    return Doc("MergeCollinearEdges(tol: float | None = None) -> " + cls + "\n\n"
               "Merges lines" + lines + " that meet alone at their vertices into fewer lines, in place: a vertex goes while "
               "every vertex removed between its neighbors stays within tol of the line through them.\n\n"
               "The result stays within tol of the edges the call was given; a second call may merge more. The edges of a "
               "spline stay.\n\n"
               "Args:\n"
               "    tol: the largest distance of a removed vertex from the new line; None takes "
               "px.config.merge_collinear_closer.\n\n"
               "Returns:\n"
               "    this " + cls + ".\n\n"
               "Examples:\n"
               "    " + x + ".MergeCollinearEdges()\n"
               "    " + x + ".MergeCollinearEdges(1e-6)");
}

const char *OpenEndsDoc(const std::string &x) {
    return Doc("OpenEnds() -> list[tuple[int, bool]]\n\n"
               "The endpoints without an exact match on another edge, as (edge index, True at its end, False at its "
               "start).\n\n"
               "Examples:\n"
               "    " + x + ".OpenEnds()");
}

const char *IsClosedDoc(const std::string &x) {
    return Doc("IsClosed() -> bool\n\nTrue if every endpoint has an exact match on another edge (OpenEnds() is empty).\n\n"
               "Examples:\n"
               "    " + x + ".IsClosed()");
}

const char *SmallEdgesDoc(const std::string &x) {
    return Doc("SmallEdges(tol: float) -> list[int]\n\n"
               "The indices of the edges no longer than tol.\n\n"
               "Args:\n"
               "    tol: the largest length counted.\n\n"
               "Examples:\n"
               "    " + x + ".SmallEdges(1e-3)");
}

const char *IntersectionsDoc(const std::string &x, const std::string &whose) {
    return Doc("Intersections(other: Wire, /) -> list[Vertex]\n\n"
               "The points where " + whose + " meet the edges of other, each once, in the plane.\n\n"
               "Args:\n"
               "    other: a Wire on the same plane.\n\n"
               "Raises:\n"
               "    ValueError: other lies on a different plane.\n\n"
               "Examples:\n"
               "    " + x + ".Intersections(px.Line((0, -5), (0, 5)))");
}

const char *ContainsDoc(const std::string &x, const std::string &where) {
    return Doc("Contains(other: np.ndarray | list[tuple[float, float]] | list[tuple[float, float, float]], /, boundary: bool = True) -> "
               "np.ndarray\n"
               "Contains(other: Point | Point3 | Face | Shape | Wire, /, boundary: bool = True) -> bool\n\n"
               "True if the point lies inside " + where + "; for a Face, Shape or Wire, True if it lies in " + where +
               " and their interiors meet (DE-9IM, see Relate).\n\n"
               "A point (x, y, z) off the plane is not inside.\n\n"
               "Args:\n"
               "    other: (h, v) in the plane or (x, y, z); many points as an (n, 2) or (n, 3) array give a bool array "
               "(n,); or a Face, Shape or Wire.\n"
               "    boundary: the answer for a point on the boundary (points only).\n\n"
               "Raises:\n"
               "    TypeError: other is not a point, Face, Shape or Wire.\n\n"
               "Examples:\n"
               "    " + x + ".Contains((1, 2))\n"
               "    " + x + ".Contains([(0, 0), (1, 2)], boundary=False)\n"
               "    " + x + ".Contains(hole)");
}

const char *CornerDoc(bool chamfer, bool copy, const std::string &cls) {
    const std::string kind = chamfer ? "Chamfer" : "Fillet", call = kind + (copy ? "ed" : ""), x = VariableOf(cls),
                      sizes = chamfer ? "d: float, /, d2: float | None = None, angle: float | None = None, reverse: bool = False"
                                      : "r: float, continuity: Constraint = px.Tangent, /";
    const std::string at = "VertexView | Point | str | list[VertexView | Point | str]";
    const std::string result = chamfer ? "new line" : "arc of radius r";
    std::string text = call + "(at: " + at + ", " + sizes + ") -> " + cls + "\n\n";
    if (copy)
        text += "A copy with the corners " + std::string(chamfer ? "chamfered" : "rounded") + " as " + kind + " does; this " + cls +
                " is not changed.\n\n";
    else
        text += std::string(chamfer ? "Cuts corners with a straight line" : "Rounds corners with an arc tangent to both edges") +
                ", in place.\n\n"
                "A corner is the end of exactly two edges of one part whose directions differ (a vertex that more edges or "
                "faces share, an open end, a point on no vertex and a tangent joint are not corners). The " + result +
                " is a new edge with the id " + kind + ".k (k counts per object, in the order the corners are given) and the "
                "properties of the two edges merged, the name of the first edge winning: the one that arrives at the corner "
                "going counterclockwise around its loop (along the chain for an open wire). The two edges keep their ids and "
                "properties, shortened to the " + (chamfer ? "cut" : "touch") + " points; the corner vertex and its properties are "
                "gone. Lines and arcs are cut exactly; on a spline the point is found on its exact curve to rounding and the "
                "spline is cut there.\n\n" +
                (chamfer ? "A length on a curved edge is the straight distance from the corner to the cut point.\n\n"
                         : "With px.Curvature a curvature-continuous blend takes the arc's place between the same touch points: a "
                           "quintic spline whose curvature runs from the first edge's to the second's without a jump. It bends "
                           "tighter than the arc inside (its smallest radius about 0.67 r to 0.71 r for corners turning 60 to "
                           "120 degrees) and passes closer to the corner (0.33 r instead of the arc's 0.41 r at a right angle).\n\n") +
                "A size that does not fit (the point on or beyond the far end of an edge, or past a neighboring cut of the "
                "same call) makes that corner not viable: it is skipped with a PlanexWarning that names the largest size that "
                "fits. If no corner is viable the call raises.\n\n";
    text += "Args:\n"
            "    at: the corners: a vertex (" + x + ".vertices[0]), a point (h, v) on a vertex, a vertex name (every vertex with "
            "that name), or a list or selection of these; there is no default.\n";
    if (chamfer)
        text += "    d: the length on the first edge, a length > 0; also on the second if d2 and angle are None.\n"
                "    d2: the length on the second edge, a length > 0; None: not given.\n"
                "    angle: with d, the angle in degrees between the chamfer and the first edge, measured at its cut point "
                "towards the corner, between 0 and 180; None: not given. d2 and angle exclude each other.\n"
                "    reverse: True makes the second edge the first.\n";
    else
        text += "    r: the radius, a length > 0.\n"
                "    continuity: px.Tangent the arc, px.Curvature the curvature-continuous blend.\n";
    text += "\nReturns:\n    " + std::string(copy ? "the copy." : "this " + cls + ", so calls chain.") + "\n\n"
            "Raises:\n"
            "    TypeError: at is none of the forms above.\n"
            "    ValueError: a size is not positive and finite" + std::string(chamfer ? ", the angle is not between 0 and 180 or d2 and "
            "angle are both given" : ", continuity is neither px.Tangent nor px.Curvature") + ", no corner is given, a name is no vertex's name, a vertex belongs to another "
            "object, or no corner can be cut (the message names each corner and why); nothing changes then.\n\n"
            "Examples:\n";
    if (chamfer)
        text += "    " + x + "." + call + "((4, 0), 1)\n    " + x + "." + call + "([(4, 0), (4, 4)], 1, d2=2)";
    else
        text += "    " + x + "." + call + "((4, 0), 0.5)\n    " + x + "." + call + "(" + x + ".vertices[0], 0.5, px.Curvature)";
    return Doc(text);
}

const char *DistanceDoc(const std::string &x) {
    // one signature line per form of the target, so a type checker knows the return (an @overload each in the stub)
    // the many points' form first: an OCC shape is Any to a type checker (netgen has no stubs) and would take them
    return Doc("Distance(target: np.ndarray | list[tuple[float, float]] | list[tuple[float, float, float]], /, project: bool = False, "
               "components: str | None = None, signed: bool = False) -> np.ndarray\n"
               "Distance(target: Point | Point3 | Face | Shape | Wire | EdgeView | VertexView | FaceView | TopoDS_Shape, /, "
               "project: bool = False, components: None = None, signed: bool = False) -> float\n"
               "Distance(target: Point | Point3 | Face | Shape | Wire | EdgeView | VertexView | FaceView | TopoDS_Shape, /, "
               "project: bool = False, *, components: str) -> float | tuple[float, ...]\n\n"
               "The smallest distance to the target; 0 inside a face.\n\n"
               "A target in another plane is measured in 3d, or projected into this plane with project.\n\n"
               "Args:\n"
               "    target: a point, (h, v) in the plane or (x, y, z) in 3d, a Face, Shape, Wire, edge, vertex, face of a "
               "shape or OCC shape; many points as an (n, 2) or (n, 3) array.\n"
               "    project: True projects a target in another plane into this one.\n"
               "    components: letters of 'hvxyz': the components of the shortest connection from here to the target "
               "instead of its length.\n"
               "    signed: True: the distance of points (h, v) to the boundary of the union of the faces (edges between "
               "faces do not count), negative inside, 0 on it.\n\n"
               "Returns:\n"
               "    a float; with components a tuple of them, one letter a float; many points an array (n,), with k "
               "components (n, k), one letter (n,).\n\n"
               "Raises:\n"
               "    ValueError: it is empty; components has other letters; signed with components, with points not "
               "(h, v), or without faces.\n"
               "    TypeError: the target has another form.\n\n"
               "Examples:\n"
               "    " + x + ".Distance((5, 0))\n"
               "    " + x + ".Distance(other, components=\"hv\")");
}

const char *ClosestPointDoc(const std::string &x) {
    return Doc(std::string("ClosestPoint(point: np.ndarray | list[tuple[float, float]] | list[tuple[float, float, float]], /) -> np.ndarray\n"
               "ClosestPoint(point: Point | Point3, /) -> tuple[float, ...]\n\n"
               "The nearest point to point, in the plane for (h, v), in 3d for (x, y, z)") +
               (x == "face" || x == "shape" ? "; inside a face the point itself" : "") + ".\n\n"
               "Args:\n"
               "    point: (h, v) or (x, y, z); many points as an (n, 2) or (n, 3) array give an array of that shape.\n\n"
               "Raises:\n"
               "    ValueError: it is empty.\n"
               "    TypeError: point is not a point.\n\n"
               "Examples:\n"
               "    " + x + ".ClosestPoint((5, 0))\n"
               "    " + x + ".ClosestPoint([(0, 0), (5, 0)])");
}

const char *MeasureDoc(const std::string &x, bool along) {
    const std::string at = along ? ", at: float | Point | Point3 | None = None" : "";
    return Doc("Measure(name: str | FaceView | EdgeView | None = None, to: str | Point | Point3 | Face | Shape | Wire | None = None, project: bool = False" + at + ") -> dict\n\n"
               "The measures of the faces and edges named name, or of the one with that id or selected, as a dict.\n\n"
               "Its keys: type, faces, area, edges, length, center (in 3d; of an arc or circle its center, with radius and "
               "angle), inertia, Ih, Iv, Ihv, I1, I2, principal_angle, bounding_box; with to also distance, du, dv, dx, "
               "dy, dz, points and the angle between two lines. Without faces (type 'edges', area 0) the center, "
               "inertia and second moments are of the edges' length; a shape without faces warns.\n\n"
               "Args:\n"
               "    name: a name, an id, or a selected face or edge; None measures all.\n"
               "    to: a name, or anything Distance takes: adds the distance to it.\n"
               "    project: True projects a target of to in another plane into this one.\n" +
               (along ? std::string(AT_DOC) : std::string()) + "\n"
               "Raises:\n"
               "    ValueError: no face or edge has that name or id" + (along ? std::string(AT_RAISES) : std::string()) + ".\n"
               "    TypeError: name has another form" + (along ? std::string("; at= and to= are given together") : std::string()) + ".\n\n"
               "Examples:\n"
               "    " + x + ".Measure()\n"
               "    " + x + (along ? ".Measure(at=12.5)[\"point_hv\"]" : ".Measure(\"iron\", to=\"air\")"));
}

const char *TrimDoc(const std::string &x) {
    return Doc("Trim(cutter: " + cutterType + ", *, keep_closest_to: Point | Point3 | None = None, remove_closest_to: Point | "
               "Point3 | None = None, keep_largest: bool = False, keep_smallest: bool = False, keep_left: bool = False, "
               "keep_right: bool = False) -> Shape\n\n"
               "Cuts every face and wire by the cutter and keeps the pieces the one option given chooses.\n\n"
               "What the cutter does not separate stays as it is.\n\n"
               "Args:\n" + cutterArg +
               "    keep_closest_to: keeps the piece closest to this point (a face piece by its center, a wire piece by "
               "itself), (h, v) or (x, y, z).\n"
               "    remove_closest_to: removes the piece closest to this point and keeps the others.\n"
               "    keep_largest: keeps the piece of the largest area (faces) or length (wires).\n"
               "    keep_smallest: keeps the piece of the smallest area or length.\n"
               "    keep_left: keeps the face pieces left of the cutter as drawn; a shape's free wires stay.\n"
               "    keep_right: keeps the face pieces right of the cutter as drawn; a shape's free wires stay.\n\n"
               "Returns:\n"
               "    a new Shape; this one is not changed.\n\n"
               "Raises:\n"
               "    ValueError: not exactly one option is given; keep_left or keep_right without faces, or no piece "
               "lies wholly on that side.\n"
               "    TypeError: the cutter or the point has the wrong form.\n\n"
               "Examples:\n"
               "    " + x + ".Trim(px.Line((0, -5), (0, 5)), " + (x == "wire" ? "keep_largest" : "keep_left") + "=True)\n"
               "    " + x + ".Trim(cutter, keep_closest_to=(2, 1))");
}

const char *SplitDoc(const std::string &x) {
    return Doc("Split(cutter: " + cutterType + ", /) -> list[Shape]\n\n"
               "Cuts every face and wire by the cutter into one Shape per piece.\n\n"
               "What the cutter does not separate stays as it is; the pieces keep the face's properties, the new edges "
               "the cutter's.\n\n"
               "Args:\n" + cutterArg + "\n"
               "Raises:\n"
               "    TypeError: the cutter has the wrong form.\n\n"
               "Examples:\n"
               "    pieces = " + x + ".Split(px.Line((0, -5), (0, 5)))");
}
