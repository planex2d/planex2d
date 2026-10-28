// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding_sketch.hpp"
#include "constraint_sketch/constraintsketch.hpp"

// prints as the arguments of the Constrain call that made it
struct ConstraintText {
    std::string text;
};

static py::dict SolveDict(const SolveResult &r, const cs::System &s) {
    py::dict out;
    out["solved"] = r.solved;
    out["iterations"] = r.iterations;
    out["residual"] = r.residual;
    out["values"] = s.values;
    return out;
}

// what the constraint sketch adds: constraints, dimensions, expressions, the analysis
// the expression parsed by planex._expressions against the sketch's names: its text, nodes, names and what it drives
static py::object ParsedExpression(ConstraintSketch &s, const std::string &text, const std::optional<std::string> &rhs) {
    const cs::ExpressionNames names = s.ExpressionNames();
    py::dict dimensions, elements, others;
    for (const auto &[name, turn] : names.dimensions)
        dimensions[py::str(name)] = turn ? "angle" : "length";
    for (const auto &[name, kind] : names.others)
        others[py::str(name)] = kind;
    for (const auto &[kind, ids] : s.Elements())
        for (const std::string &e : ids)
            elements[py::str(e)] = kind;
    const py::cpp_function ispoint([&s](const std::string &r) {
        try {
            return s.RefKind(r) == "point";
        } catch (const std::invalid_argument &) {
            return false;
        }
    });
    static planex2d::Timer timer("planex Expression tree");
    const planex2d::Region region(timer);
    return py::module_::import("planex._expressions").attr("_Parse")(text, rhs, dimensions, elements, ispoint, others);
}

// the expression with each id that counts from the end (Chain.-1.length) as the id it counts to, kept so in its text
static std::string FromEndIn(const ConstraintSketch &s, const std::string &text) {
    const auto inId = [&](size_t k) { return std::isalnum((unsigned char)text[k]) || text[k] == '_' || text[k] == '.' || text[k] == ' '; };
    std::string out;
    size_t done = 0;
    for (size_t at = text.find(".-"); at != std::string::npos; at = text.find(".-", std::max(at + 2, done))) {
        size_t start = at, end = at;
        while (start > done && inId(start - 1))
            --start;
        while (end < text.size() && (inId(end) || (text[end] == '-' && end > 0 && text[end - 1] == '.')))
            ++end;
        while (start < end && text[start] == ' ')
            ++start;
        while (end > start && text[end - 1] == ' ')
            --end;
        if (!std::isalpha((unsigned char)text[start]))
            continue;
        out += text.substr(done, start - done) + s.FromEnd(text.substr(start, end - start));
        done = end;
    }
    return out + text.substr(done);
}

static ConstraintSketch &AddExpression(ConstraintSketch &s, const std::string &given, const std::optional<std::string> &givenRhs,
                                       const std::optional<std::string> &id) {
    const std::string text = FromEndIn(s, given);
    const std::optional<std::string> rhs = givenRhs ? std::optional(FromEndIn(s, *givenRhs)) : std::nullopt;
    try {
        UserId(id);
    } catch (const std::invalid_argument &e) {
        throw py::value_error("Expression '" + (rhs ? text + " = " + *rhs : text) + "': " + e.what());
    }
    s.Flush();
    const py::object parsed = ParsedExpression(s, text, rhs);
    std::vector<cs::ExpressionName> leaves;
    for (const py::handle &h : py::list(parsed["names"])) {
        const py::tuple leaf = h.cast<py::tuple>();
        leaves.push_back({leaf[1].cast<std::string>(), leaf[0].cast<std::string>() == "measure" ? leaf[2].cast<std::string>() : ""});
    }
    const py::object drives = parsed["drives"];
    return s.AddExpression(parsed["text"].cast<std::string>(), NodesOf(parsed["nodes"].cast<std::vector<std::pair<std::string, double>>>()),
                           leaves, drives.is_none() ? std::nullopt : std::optional<std::string>(drives.cast<std::string>()), id);
}

// None (rounding) or a length, 0 or more
static std::optional<double> InferenceTol(const std::optional<double> &tol, const std::string &call) {
    if (tol && !(*tol >= 0.0))
        throw py::value_error(call + "(tol=" + NumberText(*tol) + "): the tolerance is a length, 0 or more (None: to rounding)");
    return tol;
}

// True: the config's relations, False: none, a dict: the config's with the ones it names switched
static std::optional<AutoConstraint> GeometricArg(const py::object &o, const std::string &call) {
    if (py::isinstance<py::bool_>(o))
        return o.cast<bool>() ? std::optional(config::state->auto_constraint_options) : std::nullopt;
    if (!py::isinstance<py::dict>(o))
        throw py::type_error(call + "(geometric=): True, False or a dict of relations, not " + py::repr(o).cast<std::string>());
    return AutoConstraintOptions(o.cast<py::dict>(), config::state->auto_constraint_options, call + "(geometric=)");
}

static py::dict SuggestionDict(const cs::Suggestion &g) {
    py::list args;
    for (const std::string &r : g.refs)
        args.append(r == "H" ? py::cast(SketchAxis::H) : r == "V" ? py::cast(SketchAxis::V) : py::object(py::str(r)));
    args.append(py::cast(g.kind));
    if (g.value)
        args.append(*g.value);
    py::dict out;
    out["call"] = "sk.Constrain(" + g.text + ")";
    out["args"] = py::tuple(args);
    out["kind"] = sketch::KindName(g.kind);
    if (!g.removes.empty())
        out["removes"] = g.removes;
    return out;
}

static void BindConstraints(py::class_<ConstraintSketch> &sketch) {
    auto ref = py::return_value_policy::reference_internal;
    sketch
        .def_property("mode", &ConstraintSketch::Mode, &ConstraintSketch::SetMode,
                      "mode: str\n\nThe mode, 'normal' or 'strict': setting 'strict' also switches error_on_branch_change on (not while "
                      "the constraints are disabled).")
        .def_property("auto_constraint",
                      [](const ConstraintSketch &s) { return s.autoConstraint ? py::object(AutoConstraintDict(*s.autoConstraint)) : py::object(py::bool_(false)); },
                      [](ConstraintSketch &s, const py::object &v) { s.SetAutoConstraint(AutoConstraintOf(v, "Sketch.auto_constraint")); },
                      "auto_constraint: dict | bool\n\nThe relations the next drawing calls find as constraints (see Sketch()): read as False "
                      "or the dict of relations, set as auto_constraint= is (True, False, a dict, None for px.config.auto_constraint), "
                      "taken when set.")
        .def_readwrite("error_on_branch_change", &ConstraintSketch::errorOnBranchChange,
                       "error_on_branch_change: bool\n\nWhether a solve that turns a line round or an arc to its other side (it leaves the branch the sketch "
                       "was drawn in, even in the smallest steps Update takes) is an error that changes nothing, else a "
                       "warning; on by default in mode='strict'.")
        .def("Constrain", NamingIds("Constrain", +[](ConstraintSketch &s, const py::args &args, const std::optional<std::string> &id, bool reference) -> ConstraintSketch & {
            Prepare(s, args);
            std::vector<std::string> refs;
            std::optional<sketch::Kind> kind;
            std::optional<double> value;
            for (const py::handle &h : args) {
                if (py::isinstance<sketch::Kind>(h)) {
                    if (kind)
                        throw py::type_error("Constrain: one kind (px.Distance, ...) per call");
                    kind = h.cast<sketch::Kind>();
                } else if (kind && IsNumber(h) && !value)
                    value = h.cast<double>();
                else if (!kind && occ->isShape(h))
                    refs.push_back(s.ProjectedReference(*occ->projected(h, s.Axes(), true), "Constrain"));
                else if (!kind)
                    refs.push_back(RefIn(s, h, "Constrain"));
                else
                    throw py::type_error("Constrain(ids..., kind[, value])");
            }
            if (!kind)
                throw py::type_error("Constrain: which kind? (px.Distance, ...)");
            return s.Constrain(refs, *kind, value, UserId(id), reference);
        }), py::arg("id") = py::none(), py::arg("reference") = false, ref,
             "Constrain(*args: Id | Constraint | float, id: str | None = None, reference: bool = False) -> Sketch\n\n"
             "Add a constraint or dimension and solve at once.\n\n"
             "Args:\n"
             "    args: the ids of the elements or their points ('Line.1.end', .start, .center, .mid), px.H, px.V, or an "
             "OCC edge (projected first as a construction element, as Project does), then "
             "the kind (px.Distance, ...), then its value for a dimension. px.Angle of one arc is its sweep in degrees, "
             "negative for a clockwise arc.\n"
             "    id: the constraint's id.\n"
             "    reference: a dimension without a value that measures and drives nothing (sk[id] gives what it "
             "measures).\n\n"
             "Raises:\n"
             "    TypeError: no kind, two kinds, or arguments after the kind other than one value.\n"
             "    ValueError: a wrong value, or an expression out of its domain. A constraint that contradicts the others is "
             "kept but not solved (PlanexWarning naming the constraints it contradicts): status is 'over', its elements are "
             "pink, and it is solved again once the other dimensions allow it.\n\n"
             "Examples:\n"
             "    sk.Constrain(\"Line.1\", px.Horizontal)\n"
             "    sk.Constrain(\"Line.1\", px.Distance, 10, id=\"w\")")
        .def("__getitem__", [](ConstraintSketch &s, const py::object &given) -> py::object {
            const std::string id = RefIn(s, given, "sk[id]");
            const double v = s.Value(id);
            return s.IsCount(id) ? py::object(py::int_((long long)v)) : py::object(py::float_(v));
        }, py::arg("id"),
           "__getitem__(id: Id) -> float | int\n\n"
           "The value of a dimension, sk[id].\n\n"
           "Args:\n"
           "    id: the dimension; a pattern's count gives an int.\n\n"
           "Examples:\n"
           "    w = sk[\"w\"]")
        .def("__setitem__", [](ConstraintSketch &s, const py::object &id, double value) { s.SetValue(RefIn(s, id, "sk[id]"), value); },
             py::arg("id"), py::arg("value"),
             "__setitem__(id: Id, value: float) -> None\n\n"
             "Set a dimension, sk[id] = value; it takes effect with Update(), or before the sketch is drawn on or read.\n\n"
             "Args:\n"
             "    id: the dimension.\n"
             "    value: its new value.\n\n"
             "Raises:\n"
             "    ValueError: id is a reference or a dimension an expression drives; a value that is not finite, a "
             "length or radius not positive, a count not a whole number of at least 1.\n\n"
             "Examples:\n"
             "    sk[\"w\"] = 12")
        .def("Update", [](ConstraintSketch &s, const std::optional<py::dict> &values, size_t steps) -> ConstraintSketch & {
            std::vector<std::pair<std::string, double>> v;
            if (values)
                for (const auto &[id, value] : *values)
                    v.emplace_back(RefIn(s, id, "Update"), value.cast<double>());
            return s.Update(v, steps);
        }, py::arg("values") = py::none(), py::arg("steps") = 1, ref,
             "Update(values: dict[str, float] | None = None, steps: int = 1) -> Sketch\n\n"
             "Solve with the set values, walked there in steps, smaller ones where the geometry would jump (to stay in "
             "the assembly it was drawn in).\n\n"
             "Args:\n"
             "    values: more values to set, {id: value}.\n"
             "    steps: the fewest steps.\n\n"
             "Raises:\n"
             "    ValueError: there is no solution; nothing changes.\n\n"
             "Examples:\n"
             "    sk.Update()\n"
             "    sk.Update({\"w\": 12, \"phi\": 30}, steps=10)")
        .def("RigidGroup", NamingIds("RigidGroup", +[](ConstraintSketch &s, const py::object &ids, const std::optional<std::string> &id) -> ConstraintSketch & {
            Prepare(s, ids);
            return s.RigidGroup(RefsIn(s, ids, "RigidGroup"), UserId(id));
        }), py::arg("ids"), py::arg("id") = py::none(), ref,
           "RigidGroup(ids: Ids, id: str | None = None) -> Sketch\n\n"
           "Keep the elements' shape as it is now, together: they move and turn as one; remove it to change the shape.\n\n"
           "Args:\n"
           "    ids: the elements: one id, a list or a selection.\n"
           "    id: the constraint's id.\n\n"
           "Examples:\n"
           "    sk.RigidGroup([\"Line.1\", \"Arc.1\"])\n"
           "    sk.RigidGroup(\"Chain.2\", id=\"bracket\")")
        .def_property_readonly("dimensions", [](ConstraintSketch &s) {
            py::dict out;
            for (const auto &[id, v] : s.Dimensions())
                out[py::str(id)] = s.IsCount(id) ? py::object(py::int_((long long)v)) : py::object(py::float_(v));
            return out;
        }, "dimensions: dict[str, float]\n\nEvery dimension with its value, {id: value}: references measured, those an expression drives as it gives "
           "them, and the patterns' counts.")
        .def_property_readonly("condition", &ConstraintSketch::Condition,
                               "condition: float\n\nThe most a small change of the dimensions can move the geometry, relative to it; large "
                               "(above config.sensitivity_warning) means nearly singular.")
        .def("Expression", &AddExpression, py::arg("text"), py::arg("rhs") = py::none(), py::kw_only(), py::arg("id") = py::none(), ref,
             "Expression(text: str, rhs: str | None = None, *, id: str | None = None) -> Sketch\n\n"
             "Add an equation between dimensions and measures of the solved geometry, solved with all the other "
             "constraints.\n\n"
             "Its terms: the ids of driving, driven or reference dimensions, and measures (Line.1.length; Line.1.angle, "
             "its direction from h; Arc.1.r, Circle.1.r; Arc.1.sweep; .h and .v of any point, e.g. Line.1.end.v; each a "
             "reference dimension Reference.N). A dimension alone on the left side is driven by it: it follows, and "
             "setting it raises (a reference stays a reference); any other equation is one more constraint, reported "
             "like one when it is redundant or contradicts. ``+ - * / ^`` (or ``**``), brackets, sqrt, abs, min, max, sin, cos, "
             "tan, asin, acos, atan2 (angles in degrees), exp, log (natural), sinh, cosh, tanh, asinh, acosh, atanh, pi and the units mm, cm, m, um, inch, mil (as px.mm, ...). "
             "Removing a dimension it uses removes it, with a warning.\n\n"
             "Args:\n"
             "    text: the equation (\"h = w/2\"), or its left side when rhs is given.\n"
             "    rhs: the right side.\n"
             "    id: the expression's id, default Expression.N.\n\n"
             "Raises:\n"
             "    ValueError: an expression out of its domain (the sqrt of a negative, ...); an invalid id.\n\n"
             "Examples:\n"
             "    sk.Expression(\"h = w/2\")\n"
             "    sk.Expression(\"h\", \"w/2 + 3\", id=\"half\")")
        .def_property_readonly("expressions", [](const ConstraintSketch &s) {
            py::dict out;
            for (const auto &[id, text] : s.Expressions())
                out[py::str(id)] = text;
            return out;
        }, "expressions: dict[str, str]\n\nThe expressions, {id: the text as given}.")
        .def("RemoveExpression", [](ConstraintSketch &s, const py::object &id) -> ConstraintSketch & {
            return s.RemoveExpression(RefIn(s, id, "RemoveExpression"));
        }, py::arg("id"), ref,
             "RemoveExpression(id: Id) -> Sketch\n\n"
             "Remove an expression, with the reference dimensions made for its measures if no other expression uses "
             "them; a dimension it drove drives again, with the value it has now.\n\n"
             "Args:\n"
             "    id: the expression.\n\n"
             "Examples:\n"
             "    sk.RemoveExpression(\"Expression.1\")")
        .def_property_readonly("constraints", [](const ConstraintSketch &s) {
            py::dict out;
            for (const cs::ConstraintEntry &e : s.Constraints()) {
                if (!out.contains(e.kind))
                    out[py::str(e.kind)] = py::dict();
                out[py::str(e.kind)][py::str(e.id)] = ConstraintText{e.text};
            }
            return out;
        }, "constraints: dict\n\nThe constraints by kind, each id with what made it.")
        .def_property_readonly("status", &ConstraintSketch::Status,
                               "status: str\n\nWhether the constraints fix everything: 'full', or 'under' (it can still move), or 'over' while a constraint that contradicts the others is kept but not solved; redundant constraints are in redundant.")
        .def_property_readonly("redundant", &ConstraintSketch::Redundant,
                               "redundant: list[str]\n\nThe ids of the constraints that follow from the others (consistently: the sketch is solved).")
        .def_property_readonly("dof", &ConstraintSketch::Dof, "dof: int\n\nHow many ways the geometry can still move.")
        .def("FreeMotions", [](ConstraintSketch &s) {
            py::list out;
            for (const cs::FreeMotion &m : s.FreeMotions()) {
                py::dict velocity;
                for (const auto &[reference, v] : m.points)
                    velocity[py::str(reference)] = py::make_tuple(v.x, v.y);
                for (const auto &[name, v] : m.values)
                    velocity[py::str(name)] = v;
                py::dict motion;
                motion["drag"] = m.drag;
                motion["moves"] = py::cast(m.moves);
                motion["velocity"] = velocity;
                out.append(motion);
            }
            return out;
        },
             "FreeMotions() -> list[dict]\n\n"
             "The ways the geometry can still move, one per degree of freedom, each led by the value it drags.\n\n"
             "Each motion is a dict: 'drag', the value that leads it ('Line.2.end.h' or 'Line.2.end.v' for a point, "
             "'Circle.1.r', 'Spline.1.p2.angle'); 'moves', the ids of the elements it moves (a point element on another "
             "element's point moves with it and is not listed); 'velocity', how far each "
             "point that moves goes, as (h, v), and each other value (angles in degrees), per unit of the drag. Each "
             "connected part's shifts in h and v and its rotation come first where the constraints leave them free, "
             "led by a point of the part (the rotation turns about the part's fixed point if it has one); then one "
             "motion per further value, points before radii before the rest, each keeping the values all the other "
             "motions drag fixed. An H/V rectangle without dimensions gives shift h, shift v, width and height. There "
             "are sk.dof of them.\n\n"
             "Examples:\n"
             "    sk.FreeMotions()[0][\"drag\"]\n"
             "    [m[\"drag\"] for m in sk.FreeMotions()]")
        .def("_UseParameters", [](ConstraintSketch &s, const std::map<std::string, double> &values) -> ConstraintSketch & {
            return s.UseParameters({values.begin(), values.end()});
        }, py::arg("values"), ref, "for px.Parameters: the sketch uses these global parameters (sk.Use)")
        .def("_UpdateParameters", [](ConstraintSketch &s, const std::map<std::string, double> &values) -> ConstraintSketch & {
            return s.UpdateParameters({values.begin(), values.end()});
        }, py::arg("values"), ref, "for px.Parameters: its new values, the sketch solved with them (as before if it has no solution)")
        .def("SuggestConstraints", [](ConstraintSketch &s, const py::object &geometric, bool dimensional, const std::optional<double> &tol) {
            py::list out;
            for (const cs::Suggestion &g : s.SuggestConstraints(GeometricArg(geometric, "SuggestConstraints"), dimensional,
                                                                InferenceTol(tol, "SuggestConstraints")))
                out.append(SuggestionDict(g));
            return out;
        }, py::arg("geometric") = true, py::arg("dimensional") = true, py::arg("tol") = py::none(),
             "SuggestConstraints(geometric: bool | dict = True, dimensional: bool = True, tol: float | None = None) -> list[dict]\n\n"
             "The constraints that would make the sketch fully defined, as the geometry is now: the relations it nearly has, "
             "then the dimensions.\n\n"
             "Each suggestion is a dict: 'call', the line that adds it ('sk.Constrain(\"Line.1\", px.Distance, 40)'); 'args', "
             "the arguments of that call; 'kind', the constraint's kind ('Tangent', 'Distance'); for a dimension 'removes', "
             "the drag of the free motion it takes (as sk.FreeMotions() names them). The relations come first: those of "
             "px.config.auto_constraint_options that hold within tol and do not follow from the constraints there are. Then the "
             "dimensions, each keeping the value the geometry has and removing one free motion: sizes first (lengths, radii, "
             "diameters of full circles, angles between lines that meet), then positions from the origin (a point's distance "
             "from px.V and px.H, Coincident with an axis it lies on). Together they make sk.dof 0 where these kinds reach "
             "every motion (not a spline's tangent angles), and none of them is redundant. Nothing is added: "
             "sk.AutoConstrain adds them.\n\n"
             "Args:\n"
             "    geometric: True: the relations of px.config.auto_constraint_options; a dict switches some of them "
             "({\"horizontal\": True}, the keys of auto_constraint_options); False: none.\n"
             "    dimensional: whether to suggest dimensions.\n"
             "    tol: how far an end may be from a relation for it to count, a length; None: only relations that hold to "
             "rounding. An angle relation on a line of length L counts within about tol / L.\n\n"
             "Raises:\n"
             "    TypeError: geometric is not True, False or a dict of relations.\n"
             "    ValueError: tol is negative.\n\n"
             "Examples:\n"
             "    [g[\"call\"] for g in sk.SuggestConstraints()]\n"
             "    sk.SuggestConstraints(dimensional=False, tol=1e-6)")
        .def("AutoConstrain", [](ConstraintSketch &s, const py::object &geometric, bool dimensional, const std::optional<double> &tol) {
            return s.AutoConstrain(GeometricArg(geometric, "AutoConstrain"), dimensional, InferenceTol(tol, "AutoConstrain"));
        }, py::arg("geometric") = true, py::arg("dimensional") = true, py::arg("tol") = py::none(),
             "AutoConstrain(geometric: bool | dict = True, dimensional: bool = True, tol: float | None = None) -> list[str]\n\n"
             "EXPERIMENTAL: adds the constraints sk.SuggestConstraints suggests and returns their ids.\n\n"
             "The relations found within tol are solved, which moves the geometry by up to about tol (a warning gives the "
             "largest move); the dimensions keep the values the geometry has then. A sketch from a DXF or from sk.Add gets "
             "its relations with sk.AutoConstrain(dimensional=False, tol=...); it is usually not fully defined afterwards. "
             "The constraints added are ordinary ones (Coincident.N, Distance.N, ...) and can be removed.\n\n"
             "Args:\n"
             "    geometric: True: the relations of px.config.auto_constraint_options; a dict switches some of them; False: "
             "none.\n"
             "    dimensional: whether to add dimensions.\n"
             "    tol: how far an end may be from a relation for it to count, a length; None: only relations that hold to "
             "rounding.\n\n"
             "Raises:\n"
             "    TypeError: geometric is not True, False or a dict of relations.\n"
             "    ValueError: tol is negative; the relations found within tol have no common solution (a smaller tol).\n\n"
             "Examples:\n"
             "    sk.AutoConstrain(dimensional=False, tol=1e-6)\n"
             "    sk.AutoConstrain()")
        .def_property_readonly("jacobian", [](ConstraintSketch &s) {
            const cs::SketchJacobian j = s.Jacobian();
            const auto integers = [](const std::vector<size_t> &v) {
                py::array_t<std::int64_t> a((py::ssize_t)v.size());
                std::copy(v.begin(), v.end(), a.mutable_data());
                return a;
            };
            py::list rows, columns;
            for (const cs::JacobianRow &r : j.rows)
                if (r.row)
                    rows.append(py::make_tuple(r.id, *r.row));
                else
                    rows.append(py::make_tuple(r.id, "value"));
            for (const auto &[reference, coordinate] : j.columns)
                columns.append(py::make_tuple(reference, coordinate));
            const py::tuple shape = py::make_tuple(j.rows.size(), j.columns.size());
            py::object matrix = py::make_tuple(py::array_t<double>((py::ssize_t)j.data.size(), j.data.data()),
                                               integers(j.indices), integers(j.indptr));
            try {
                matrix = py::module_::import("scipy.sparse").attr("csr_array")(matrix, py::arg("shape") = shape);
            } catch (py::error_already_set &e) {
                if (!e.matches(PyExc_ImportError))
                    throw;
            }
            py::dict out;
            out["matrix"] = matrix;
            out["shape"] = shape;
            out["rows"] = rows;
            out["columns"] = columns;
            out["scale"] = py::array_t<double>((py::ssize_t)j.scale.size(), j.scale.data());
            out["row_scale"] = py::array_t<double>((py::ssize_t)j.row_scale.size(), j.row_scale.data());
            return out;
        }, "jacobian: dict\n\nThe solver's Jacobian at the geometry now, as a dict.\n\n"
           "'matrix': the derivatives of the residuals (rows) with respect to the solver's scaled unknowns (columns), a "
           "scipy.sparse.csr_array when scipy is installed, else its arrays (data, indices, indptr), as "
           "scipy.sparse.csr_array((data, indices, indptr), shape=shape) takes them.\n"
           "'shape': (rows, columns).\n"
           "'rows': per row (constraint id, row in it); an arc's own equations (arc id, row); the equation that holds "
           "a dimension an expression uses (dimension id, 'value').\n"
           "'columns': per unknown (reference, coordinate), as ('Line.1.end', 'x'), ('Circle.1', 'r'), "
           "('Spline.1.p2', 'angle'), a linked dimension's ('w', 'value') or a constraint's own ('RigidGroup.1', "
           "'param 0').\n"
           "'scale': per column the unknown per scaled unknown: 1 for a length, 180 / (pi x the size of the sketch) for "
           "an angle (in degrees); matrix[:, k] / scale[k] is the derivative per unit (per degree) of unknown k.\n"
           "'row_scale': per row the change of its residual per unit of its value: 1 for a length dimension and a row "
           "without a value; per degree for an angle dimension: its lever (the shortest element it relates) x pi/180, "
           "the size of the sketch x pi/180 on the 'value' row of a linked one or on a pattern's angle.\n\n"
           "The change of every unknown per unit (per degree) of the dimension in row i is "
           "scale * spsolve(matrix, row_scale[i] * e_i).\n\n"
           "Reading it raises ValueError while the constraints are disabled.\n\n"
           "How far every unknown moves per degree of the dimension phi, with scipy:\n\n"
           "    from scipy.sparse.linalg import spsolve\n"
           "    J = sk.jacobian\n"
           "    i = J['rows'].index(('phi', 0))\n"
           "    e = np.zeros(J['shape'][0])\n"
           "    e[i] = J['row_scale'][i]\n"
           "    per_degree = dict(zip(J['columns'], J['scale'] * spsolve(J['matrix'].tocsc(), e)))\n"
           "    per_degree[('crank.end', 'x')]  # how far crank.end moves in x per degree of phi")
        .def("_Constraints", [](ConstraintSketch &s) {
            py::list out;
            for (const cs::ConstraintInfo &i : s.Infos()) {
                py::dict d;
                d["id"] = i.id;
                d["kind"] = i.kind;
                d["text"] = i.text;
                d["refs"] = i.refs;
                d["value"] = i.value ? py::object(py::float_(*i.value)) : py::object(py::none());
                d["state"] = i.redundant || i.over ? "over" : "ok";
                d["reference"] = i.reference;
                d["derived"] = i.derived;
                out.append(d);
            }
            return out;
        }, "for the plots: every constraint with the ids it relates, its value and state ('over' if redundant, suspended or in the set of one that is)")
        .def("_States", [](ConstraintSketch &s) {
            py::dict out;
            for (const auto &[id, state] : s.States())
                out[py::str(id)] = state;
            return out;
        }, "for the plot until step 4: per element 'full', 'under' or 'over'");
}

static py::dict ElementDict(const cs::System &s, size_t element) {
    const sketch::Element &e = s.elements.at(element);
    const std::vector<double> &v = s.values;
    auto point = [&](size_t p) { return py::make_tuple(v[s.coordsOf[p]], v[s.coordsOf[p] + 1]); };
    py::dict out;
    switch (e.kind) {
    case sketch::ElementKind::Point:
        out["kind"] = "Point";
        out["point"] = point(e.Point());
        break;
    case sketch::ElementKind::Line:
        out["kind"] = "Line";
        out["start"] = point(e.Start());
        out["end"] = point(e.End());
        break;
    case sketch::ElementKind::Circle:
        out["kind"] = "Circle";
        out["center"] = point(e.Center());
        out["r"] = v[e.Radius()];
        break;
    case sketch::ElementKind::Arc:
        out["kind"] = "Arc";
        out["center"] = point(e.Center());
        out["r"] = v[e.Radius()];
        out["start"] = point(e.Start());
        out["end"] = point(e.End());
        out["ccw"] = e.ccw;
        break;
    case sketch::ElementKind::Axis:
        out["kind"] = "Axis";
        out["normal_angle"] = Degrees(v[e.AxisAngle()]);
        out["d"] = v[e.AxisDistance()];
        break;
    case sketch::ElementKind::Pattern:
        out["kind"] = "Pattern";
        out["step"] = v[e.Step()];
        break;
    case sketch::ElementKind::Offset:
        out["kind"] = "Offset";
        out["d"] = v[e.Distance()];
        break;
    case sketch::ElementKind::Spline: {
        const sketch::SplineData &d = s.splines[e.SplineIndex()];
        out["kind"] = "Spline";
        py::list points;
        for (size_t p : d.points)
            points.append(point(p));
        out["points"] = points;
        out["closed"] = d.closed;
        out["tangent_at"] = d.tangentAt;
        std::vector<double> degrees;
        for (size_t a : d.tangent)
            degrees.push_back(Degrees(v[a]));
        out["tangents"] = degrees;
        break;
    }
    case sketch::ElementKind::OffsetCurve: {
        const sketch::OffsetCurveData &c = s.offsetCurves[e.OffsetCurveIndex()];
        out["kind"] = "OffsetCurve";
        out["spline"] = c.spline;
        out["side"] = c.side;
        out["d"] = v[c.distance];
        break;
    }
    }
    out["unknowns"] = e.Values(s);
    return out;
}

// px.Sketch(sketch, copy=True): a copy of a Sketch, the constraint sketch of a QuickSketch
static ConstraintSketch CopiedConstraintSketch(const py::tuple &args, const py::object &autoConstraint,
                                               const std::optional<bool> &errorOnBranchChange, const std::string &mode,
                                               const std::optional<std::string> &id, const py::object &dir) {
    py::object source = SketchToCopy("Sketch", args, autoConstraint, errorOnBranchChange, mode, dir);
    if (!py::isinstance<ConstraintSketch>(source) && !py::isinstance<Sketch>(source))
        source = py::cast(QuickSketchOfObjects(source, "Sketch"));
    ConstraintSketch s = py::isinstance<Sketch>(source) ? ConstraintSketch(source.cast<const Sketch &>(), AutoConstraintOf(autoConstraint, "Sketch"))
                                                       : ConstraintSketch(source.cast<const ConstraintSketch &>());
    if (!autoConstraint.is_none())
        s.autoConstraint = AutoConstraintOf(autoConstraint, "Sketch");
    GiveId(s, id);
    return s;
}

static size_t PointOfElement(const cs::System &s, size_t element) {
    const sketch::Element &e = s.elements.at(element);
    if (e.kind != sketch::ElementKind::Point)
        throw std::invalid_argument("the element is no point");
    return e.Point();
}

void BindConstraintSketch(py::module_ &m) {
    py::options options;
    options.disable_function_signatures();
    m.def("_EvaluateExpression", [](const std::vector<std::pair<std::string, double>> &nodes, const std::vector<double> &values) {
        const std::vector<cs::Node> tree = NodesOf(nodes);
        for (const cs::Node &n : tree)
            if (n.op == cs::Op::Value && n.unknown >= values.size())
                throw py::value_error("expression: no value " + std::to_string(n.unknown));
        if (values.size() > cs::maxLocal)
            throw py::value_error("expression: more than " + std::to_string(cs::maxLocal) + " values");
        const double plain = cs::Evaluated<double>(tree, [&](size_t u) { return values[u]; });
        cs::active = values.size();
        std::string error;
        const cs::Dual dual = cs::Evaluated<cs::Dual>(tree, [&](size_t u) {
            cs::Dual x(values[u]);
            x.g[u] = 1.0;
            return x;
        }, &error);
        py::dict out;
        out["value"] = dual.v;
        out["plain"] = plain;
        out["gradient"] = std::vector<double>(dual.g.begin(), dual.g.begin() + (std::ptrdiff_t)values.size());
        out["error"] = error.empty() ? py::object(py::none()) : py::object(py::str(error));
        return out;
    }, py::arg("nodes"), py::arg("values"),
       "test hook: an expression's postfix nodes (as _expressions._Parse gives them, ('val', k) the k-th of values) "
       "evaluated with derivatives: value, plain (evaluated without them), gradient, error (the first domain error or None)");
    py::class_<cs::System>(m, "_ConstraintSystem", "test hook: the elements and constraints of the constraint sketch")
        .def(py::init<>())
        .def("Point", [](cs::System &s, double x, double y, bool fixed) { return s.AddPoint(s.NewPoint({x, y}, fixed)); },
             py::arg("x"), py::arg("y"), py::arg("fixed") = false)
        .def("Line", [](cs::System &s, size_t a, size_t b) { return s.AddLine(PointOfElement(s, a), PointOfElement(s, b)); },
             py::arg("start"), py::arg("end"))
        .def("Circle", [](cs::System &s, size_t c, double r) { return s.AddCircle(PointOfElement(s, c), r); }, py::arg("center"),
             py::arg("r"))
        .def("Arc", [](cs::System &s, size_t c, double r, size_t a, size_t b, bool ccw) {
            return s.AddArc(PointOfElement(s, c), r, PointOfElement(s, a), PointOfElement(s, b), ccw);
        }, py::arg("center"), py::arg("r"), py::arg("start"), py::arg("end"), py::arg("ccw") = true)
        .def("Spline", [](cs::System &s, const std::vector<size_t> &points, const std::map<size_t, double> &tangents, bool closed,
                          const py::object &style) {
            std::vector<size_t> ids, at;
            std::vector<double> angles;
            for (size_t p : points)
                ids.push_back(PointOfElement(s, p));
            for (const auto &[k, degrees] : tangents) {
                at.push_back(k);
                angles.push_back(Radians(degrees));
            }
            return s.AddSpline(ids, at, angles, closed, StyleArg(style, "Spline"));
        }, py::arg("points"), py::pos_only(), py::arg("tangents") = std::map<size_t, double>{}, py::arg("closed") = false, py::arg("style") = "averaged",
           "through the point elements, tangent angles (degrees) at some of them")
        .def("Axis", [](cs::System &s, double degrees, double d, bool fixed) {
            return s.AddAxis(Radians(degrees), d, fixed);
        }, py::arg("normal_angle"), py::arg("d"), py::arg("fixed") = true, "the line n.x = d, n at normal_angle degrees")
        .def("Constrain", [](cs::System &s, const std::string &kind, const py::list &refs, double value) {
            cs::Constraint c{KindNamed(kind)};
            for (const py::handle &h : refs)
                c.refs.push_back(RefOf(h));
            c.value = value;
            return s.Add(std::move(c));
        }, py::arg("kind"), py::arg("refs"), py::arg("value") = 0.0)
        .def("SetValue", [](cs::System &s, size_t c, double v) { s.constraints.at(c).value = v; }, py::arg("constraint"), py::arg("value"))
        .def("Remove", &cs::System::RemoveConstraint, py::arg("constraint"))
        .def("Count", [](cs::System &s) {
            s.Build();
            return s.Count();
        }, "the constraints, then the arcs' own equations")
        .def("Evaluate", [](cs::System &s, size_t c, bool build) {
            if (build)
                s.Build();
            const std::vector<size_t> &idx = s.Indices(c);
            const size_t rows = s.Rows(c);
            size_t size = 0;
            for (const RowBlock &b : s.Blocks(c))
                size += b.rows * b.indices.size();
            std::vector<double> r(rows), J(size);
            s.Evaluate(c, s.values, r.data(), J.data());
            std::vector<std::vector<double>> jac(rows, std::vector<double>(idx.size()));
            size_t i = 0, at = 0;
            for (const RowBlock &b : s.Blocks(c))
                for (size_t row = 0; row < b.rows; ++row, ++i)
                    for (size_t u : b.indices)
                        jac[i][(size_t)(std::lower_bound(idx.begin(), idx.end(), u) - idx.begin())] = J[at++];
            return py::make_tuple(r, idx, jac);
        }, py::arg("constraint"), py::arg("build") = true,
             "(residuals, unknowns, Jacobian rows) at the current values; build=False: with the scales of the last build")
        .def("Solve", [](cs::System &s) { return SolveDict(s.Solve(), s); })
        .def("Analyze", [](cs::System &s) {
            const ConstraintAnalysis a = s.Analyze();
            py::dict out;
            out["rank"] = a.rank;
            out["dof"] = a.dof;
            out["determined"] = std::vector<bool>(a.determined.begin(), a.determined.end());
            out["redundant"] = std::vector<bool>(a.redundant.begin(), a.redundant.end());
            return out;
        })
        .def("PointOf", [](const cs::System &s, const py::object &ref) {
            const Vertex p = (RefOf(ref)).PointValue(s);
            return py::make_tuple(p.x, p.y);
        }, py::arg("ref"))
        .def("Element", &ElementDict, py::arg("element"), "kind, geometry at the current values and unknowns of an element")
        .def_property("values", [](const cs::System &s) { return s.values; },
                      [](cs::System &s, const std::vector<double> &v) {
                          if (v.size() != s.values.size())
                              throw py::value_error("one value per unknown");
                          s.values = v;
                      });

    py::class_<ConstraintText>(m, "ConstraintText")
        .def("__repr__", [](const ConstraintText &t) { return t.text; })
        .def("__str__", [](const ConstraintText &t) { return t.text; });

    py::class_<ConstraintSketch> sketch(m, "Sketch",
                                        "A sketch of elements and constraints between them, drawn with a pen. Ids (id=, id_wire=) "
                                        "start with a letter and contain none of + - * / ^ ( ) = , %, the operators of "
                                        "expressions.");
    BindDrawing(sketch);
    BindConstraints(sketch);
    sketch
        .def(py::init([](const py::args &args, const py::object &autoConstraint, const py::object &branch,
                         const std::string &mode, const std::optional<std::string> &id, const py::object &dir, bool copy) {
            const std::optional<bool> errorOnBranchChange = BoolOrAuto(branch, "Sketch", "error_on_branch_change");
            if (copy)
                return CopiedConstraintSketch(args, autoConstraint, errorOnBranchChange, mode, id, dir);
            return NewSketch<ConstraintSketch>("Sketch", args, py::none(), autoConstraint, errorOnBranchChange, mode, id, dir);
        }), py::arg("auto_constraint") = py::none(), py::arg("error_on_branch_change") = PlainAuto(),
             py::arg("mode") = "normal", py::arg("id") = py::none(), py::arg("dir") = py::none(), py::arg("copy") = false,
             "Sketch(plane: Plane | Axes | Sketch | Shape | Face | Wire | Compound | TopoDS_Shape | list | None = None, /, *, "
             "copy: bool = False, auto_constraint: bool | dict | None = None, error_on_branch_change: bool | Auto = px.Auto, "
             "mode: str = \"normal\", id: str | None = None)\n"
             "Sketch(start: Point, /, *flags: Constraint, dir: tuple[float, float] | None = None, "
             "auto_constraint: bool | dict | None = None, error_on_branch_change: bool | Auto = px.Auto, mode: str = \"normal\", "
             "id: str | None = None)\n\n"
             "A sketch with constraints: empty on a plane or with its pen at a start point, or a copy of a sketch or of "
             "geometry.\n\n"
             "The first argument decides by its type. A plane (a Plane, netgen Axes), a planex object (sketch, Shape, "
             "Face, Wire, Compound), a planar OCC shape or a list of these gives the plane of the sketch: the object's own "
             "origin and axes, for an OCC shape the normal as oriented and the global origin and x axis projected; the items "
             "of a list must lie on one plane. A start (h, v) is on the xy plane, the pen moved there. Nothing is the "
             "empty sketch on the xy plane.\n\n"
             "With copy=True the geometry is copied in as elements: a Sketch of a QuickSketch has its elements (ids, "
             "properties, construction flags, groups, pen, id counters) and no constraints; a Wire, Face, Shape or "
             "Compound its edges with their names and properties; an OCC shape is projected as by "
             "Project(shape, construction=False). With auto_constraint on also the relations the copied geometry shows "
             "where they hold to rounding (as Add finds them). The drawing calls are not taken over: a Rectangle, Polygon, Slot, Fillet, Offset, pattern or "
             "mirror converts as its lines and arcs, without its shape constraints, dimensions or ties.\n\n"
             "Args:\n"
             "    plane: the object or list of objects giving the plane.\n"
             "    start: the pen's start (h, v), or (x, y, z) on the xy plane (z = 0), as Move does; not an id.\n"
             "    flags: as for Move.\n"
             "    dir: the drawing direction (dh, dv) from the start.\n"
             "    auto_constraint: the relations of the geometry drawn (and with copy=True copied) to the rest become "
             "constraints where they hold to rounding: True those px.config.auto_constraint_options switches on, a dict "
             "of True or False per relation switches the ones it names, False none; default (None) "
             "px.config.auto_constraint. The relations: coincident, tangent, concentric, point_on_curve (on by default), "
             "horizontal, vertical, perpendicular, parallel, equal_radius, equal_length, collinear (off); named_helpers "
             "(on): the drawing helpers a call names (to, tangent=, ...) become constraints too. A relation that follows "
             "from the others is left out; never with the origin or the axes, never a dimension.\n"
             "    error_on_branch_change: a solve that leaves the branch the sketch was drawn in is an error, else a "
             "warning; px.Auto: on in mode='strict'.\n"
             "    mode: 'normal' or 'strict': the exports (Face, Shape, Wire(s), Edge, Construction, the OCC exports and "
             "the writers that use them) raise while the sketch can still move (dof > 0).\n"
             "    copy: True copies the geometry of the first argument in; only id= and auto_constraint= go with it. A "
             "Plane or Axes has nothing to copy: a PlanexWarning and the empty sketch. Default False: the sketch is "
             "empty on the plane of the object.\n"
             "    id: the sketch's id, default Sketch.N.\n\n"
             "Raises:\n"
             "    ValueError: dir has no length; an invalid id; a list or a compound whose items lie on different "
             "planes, or an empty one.\n"
             "    TypeError: an unexpected keyword (plane= does not exist, the plane is the first argument); positional "
             "arguments other than a start and flags, dir= without a start, a string or 3d point as the start; "
             "copy=True without exactly one argument, or with dir=, error_on_branch_change= or mode=.\n\n"
             "Examples:\n"
             "    sk = px.Sketch((0, 0), dir=(1, 0))\n"
             "    sk = px.Sketch(face, mode=\"strict\", id=\"bracket\")\n"
             "    constrained = px.Sketch(quick, copy=True, auto_constraint=True)\n"
             "    sk = px.Sketch(px.FromDXF(\"part.dxf\"), copy=True, auto_constraint={\"horizontal\": True, \"vertical\": True})");
    quickOfConstraintSketch = [](const py::handle &h) -> py::object {
        if (!py::isinstance<ConstraintSketch>(h))
            return py::none();
        ConstraintSketch &c = h.cast<ConstraintSketch &>();
        c.Flush();
        return py::cast(Sketch(c));
    };
    constraintSketchAt = [](const py::tuple &start, const py::object &axes, const py::object &autoConstraint, const std::optional<bool> &errorOnBranchChange,
                            const std::string &mode, const std::optional<std::string> &id, const py::object &dir) {
        return py::cast(NewSketch<ConstraintSketch>("Move", start, axes, autoConstraint, errorOnBranchChange, mode, id, dir));
    };
}
