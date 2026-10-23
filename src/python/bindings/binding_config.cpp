// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"
#include <pybind11/eval.h>

// read as a dict with "enabled"; set as the fit_splines= / fit_arcs= keywords, a dict may also give "enabled"
template <class Fit>
static void ConfigFit(py::class_<Config> &cls, const char *name, Fit &fit) {
    const std::string key = std::string("config.") + name;
    cls.def_property_static(name, [&fit](py::object) {
        return py::dict(py::arg("enabled") = fit.enabled, py::arg("tol") = fit.tol, py::arg("max_edge_length") = fit.max_edge_length,
                        py::arg("max_edge_angle") = fit.max_edge_angle, py::arg("ignore_edges_smaller") = fit.ignore_edges_smaller);
    }, [&fit, key](py::object, const py::object &v) {
        if (v.is_none())
            throw py::type_error(key + " = None: it is True, False or a dict of the fit's options");
        if (!py::isinstance<py::dict>(v)) {
            fit = FitOf(v, fit, key);
            return;
        }
        py::dict options = v.attr("copy")();
        const py::object enabled = options.attr("pop")("enabled", true);
        Fit f = FitOf(options, fit, key);
        f.enabled = enabled.cast<bool>();
        fit = f;
    }, Doc(std::string(name) + ": dict\n\nThe export default of " + std::string(name) + "=, read as a dict (enabled, tol, max_edge_length, max_edge_angle, "
           "ignore_edges_smaller), set as True, False or such a dict, which changes only the options it names."));
}

void BindConfig(py::module_ &m) {
    py::options options;
    options.disable_function_signatures();
    // a typo (config.max_preview_edge = ...) raises instead of adding an attribute that does nothing
    py::dict scope;
    scope["__builtins__"] = py::module_::import("builtins");
    scope["base"] = py::handle((PyObject *)py::detail::get_internals().default_metaclass);
    py::exec(R"(
class ConfigMeta(base):
    def __setattr__(cls, name, value):
        if getattr(cls, "_closed", False) and not name.startswith("_") and name not in dir(cls):
            settings = ", ".join(n for n in dir(cls) if not n.startswith("_"))
            raise AttributeError("config." + name + ": no such setting; the settings are " + settings)
        base.__setattr__(cls, name, value)
)", scope);
    auto cfg = py::class_<Config>(m, "config", py::metaclass(scope["ConfigMeta"]),
                                  "The defaults of planex: the absolute tolerance k * eps * S (S the largest coordinate "
                                  "magnitude involved), and the fit and cleanup values the OCC exports take by default.");
    // the tolerance k * eps * S is positive and finite
    for (auto [name, value, doc] : {std::tuple{"eps", &config::eps, "eps: float\n\nThe relative rounding eps of the tolerance k * eps * S, "
                                                                     "positive and finite."},
                                    std::tuple{"k", &config::k, "k: float\n\nThe factor k of the tolerance k * eps * S, positive and finite."}})
        cfg.def_property_static(name, [value](py::object) { return *value; }, [name, value](py::object, double v) {
            if (!(v > 0.0) || !std::isfinite(v))
                throw py::value_error(std::string("config.") + name + " = " + std::to_string(v) + ": it is positive and finite");
            *value = v;
        }, doc);
    ConfigValue(cfg, "spline_tol", config::spline_tol,
                "spline_tol: float\n\nThe largest deviation of a spline's arc approximation, relative to the diagonal of its points' bounding box.");
    cfg.def_property_static("spline_style", [](py::object) { return std::string(StyleName(config::spline_style)); },
                            [](py::object, const py::object &v) {
                                if (v.is_none())
                                    throw py::value_error("config.spline_style = None: it is 'px' or 'occ'");
                                config::spline_style = StyleArg(v, "config.spline_style");
                            },
                            "spline_style: str\n\nThe style of a spline drawn without style=: 'px' (planex's) or 'occ' (OCC's, netgen's splines).");
    cfg.def_property_static("default_sketch_type", [m](py::object) { return m.attr(quickSketchDefault ? "QuickSketch" : "Sketch"); },
                            [m](py::object, const py::object &v) {
                                if (v.is(m.attr("QuickSketch")))
                                    quickSketchDefault = true;
                                else if (py::hasattr(m, "Sketch") && v.is(m.attr("Sketch")))
                                    quickSketchDefault = false;
                                else if (py::hasattr(m, "Sketch"))
                                    throw py::value_error("config.default_sketch_type is px.QuickSketch or px.Sketch");
                                else
                                    throw py::value_error("config.default_sketch_type is px.QuickSketch: this planex is built "
                                                          "without px.Sketch (PLANEX_CONSTRAINTS=OFF)");
                            },
                            "default_sketch_type: type\n\nWhat px.Move makes: px.QuickSketch (the default, without constraints) or px.Sketch.");
    ConfigValue(cfg, "max_preview_edges", config::max_preview_edges,
                "max_preview_edges: int\n\nThe most edges a notebook picture of an object draws; above it the object shows its text repr.");
    ConfigFit(cfg, "fit_splines", config::spline_fit);
    ConfigFit(cfg, "fit_arcs", config::arc_fit);
    ConfigValue(cfg, "remove_edges_smaller", config::cleanup.remove_edges_smaller,
                "remove_edges_smaller: float\n\nThe export default of remove_edges_smaller=: lines and arcs no longer than this are removed.");
    ConfigValue(cfg, "close_gaps_smaller", config::cleanup.close_gaps_smaller,
                "close_gaps_smaller: float\n\nThe export default of close_gaps_smaller=: gaps between ends up to this are closed.");
    ConfigValue(cfg, "remove_free_edges", config::cleanup.remove_free_edges,
                "remove_free_edges: bool\n\nThe export default of remove_free_edges=: True removes the edges that touch no other edge at either end.");
    ConfigValue(cfg, "merge_collinear_closer", config::cleanup.merge_collinear_closer,
                "merge_collinear_closer: float\n\nThe export default of merge_collinear_closer=: two lines whose common vertex is this close to the line "
                "through their far ends become one.");
    ConfigValue(cfg, "remove_faces_smaller", config::cleanup.remove_faces_smaller,
                "remove_faces_smaller: float\n\nThe export default of remove_faces_smaller=: faces of less area than this are removed (0: none).");
    ConfigValue(cfg, "merge_small_faces_instead", config::cleanup.merge_small_faces_instead,
                "merge_small_faces_instead: bool\n\nThe export default of merge_small_faces_instead=: True unites such a face with the face it shares the "
                "longest boundary with instead.");
    ConfigValue(cfg, "sensitivity_warning", config::sensitivity_warning,
                "sensitivity_warning: float\n\nA constraint sketch warns when a small change of its dimensions can move its geometry more than this many "
                "times as much.");
    ConfigValue(cfg, "branch_check_min_length", config::branch_check_min_length,
                "branch_check_min_length: float\n\nA line of a constraint sketch shorter than this times the sketch's size has no direction for the branch "
                "check (the check that no line or arc turns round in a solve).");

    py::enum_<CutResult>(m, "CutResult", "How two edges meet in LineEdge.Cut and ArcEdge.Cut.")
        .value("Untouched", CutResult::Untouched, "They have no common point.")
        .value("Crossing", CutResult::Crossing, "They cross or touch at single points.")
        .value("Overlap", CutResult::Overlap, "They share a stretch.")
        .value("Congruent", CutResult::Congruent, "They are the same edge.");

    m.def("Timers", [] {
        py::list timers;
        for (const auto &[name, total] : Profiler::Get().Totals())
            timers.append(py::dict(py::arg("name") = name, py::arg("time") = total.time, py::arg("counts") = total.counts));
        return timers;
    },
          "Timers() -> list[dict]\n\n"
          "The planex timers summed over all threads, each a dict of name, time (s) and counts.\n\n"
          "Examples:\n"
          "    px._planex.Timers()");
    m.def("ResetTimers", [] { Profiler::Get().Reset(); },
          "ResetTimers() -> None\n\n"
          "Sets every planex timer back to zero.\n\n"
          "Examples:\n"
          "    px._planex.ResetTimers()");
    m.def("StartTrace", [] { Profiler::Get().StartTrace(); },
          "StartTrace() -> None\n\n"
          "Records the planex timers of all threads until StopTrace.\n\n"
          "Examples:\n"
          "    px._planex.StartTrace()");
    m.def("StopTrace", [](const std::string &filename) { Profiler::Get().StopTrace(filename); }, py::arg("filename"),
          "StopTrace(filename: str) -> None\n\n"
          "Writes the recorded trace as Chrome trace JSON (ui.perfetto.dev or chrome://tracing).\n\n"
          "Args:\n"
          "    filename: the path of the JSON file.\n\n"
          "Examples:\n"
          "    px._planex.StopTrace(\"trace.json\")");
    cfg.attr("_closed") = true;
}
