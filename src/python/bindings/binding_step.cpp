// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "binding.hpp"
#include "io/brepread.hpp"
#include "io/brepwrite.hpp"
#include "io/stepread.hpp"
#include "io/stepwrite.hpp"

namespace {

// the parts of a planex object, a Compound's in turn, path the indices of the part in the nested Compounds; write(shape,
// kind, path) writes one
template <class Write>
void PartsOf(const py::handle &h, const std::string &path, const char *call, Write write) {
    if (py::hasattr(h, "parts") && !py::isinstance<Shape>(h)) {
        size_t k = 0;
        for (const py::handle &part : h.attr("parts"))
            PartsOf(part, path.empty() ? std::to_string(k++) : path + "." + std::to_string(k++), call, write);
        return;
    }
    if (py::isinstance<Wire>(h))
        return write(WireShape(h.cast<const Wire &>()), "Wire", path);
    if (py::isinstance<FaceView>(h))
        return write(Shape(h.cast<const FaceView &>().Copy()), "Face", path);
    if (py::isinstance<Face>(h))
        return write(Shape(h.cast<const Face &>()), "Face", path);
    if (py::isinstance<Shape>(h))
        return write(h.cast<const Shape &>(), "Shape", path);
    throw py::type_error(std::string(call) + ": writes a Face, Shape, Wire or Compound (a sketch as sk.Shape()), not " +
                         py::repr(py::type::of(h)).cast<std::string>());
}

// a part as the file has it: its numbers times factor (maxh, minh and identifications too)
Shape Scaled(const Shape &s, double factor) {
    Shape out = s;
    if (factor != 1.0)
        out.Scale(Vector3{}, factor, true, true);
    return out;
}

}  // namespace

void BindStep(py::module_ &m) {
    py::options options;
    options.disable_function_signatures();
    m.def("_StepText", [](const py::object &obj, double unit, double factor, EXPORT_PARAMS) {
        const std::optional<StepLengthUnit> named = StepLengthUnitOf(unit);
        if (!named)
            throw py::value_error("WriteStep(unit=" + NumberText(unit) + "): STEP names the meter with an SI prefix (px.m, px.cm, "
                                  "px.mm, px.um, ...) and the inch, foot and mil, not this length");
        StepWriter w(*named);
        const auto [arcs, fit, cleanup] = std::tuple{EXPORT_OPTIONS};
        PartsOf(obj, "", "WriteStep", [&](const Shape &s, const char *kind, const std::string &path) {
            w.Part(Scaled(s, factor), kind, path, arcs, fit, cleanup);
        });
        return w.Text();
    }, py::arg("obj"), py::arg("unit"), py::arg("factor"), EXPORT_ARGS,
          "for px.WriteStep: the STEP text of a planex object in unit (meters), its numbers times factor, as its export with "
          "these options has it");
    m.def("_BrepText", [](const py::object &obj, double factor, EXPORT_PARAMS) {
        BrepWriter w;
        const auto [arcs, fit, cleanup] = std::tuple{EXPORT_OPTIONS};
        PartsOf(obj, "", "WriteBrep", [&](const Shape &s, const char *, const std::string &path) {
            w.Part(Scaled(s, factor), path, arcs, fit, cleanup);
        });
        return w.Text();
    }, py::arg("obj"), py::arg("factor"), EXPORT_ARGS,
          "for px.WriteBrep: the BRep text of a planex object, its numbers times factor, as its export with these options has it");
    m.def("_FromStepText", [](const std::string &text, std::optional<double> unit, std::optional<double> scale) {
        StepReader reader(text);
        const double mm = reader.MillimetresPerUnit();
        // the parts come in millimetres: back to the file's numbers, then times scale, or from its unit into metres
        const double factor = (scale ? *scale : unit ? *unit : mm * 1e-3) / mm;
        py::list out;
        for (StepReader::Part &p : reader.Parts()) {
            p.shape = Scaled(p.shape, factor);
            // a face written as one comes back as one, with the identifications of its part
            if (p.kind == "Face" && p.shape.Faces().size() == 1) {
                Face f = p.shape.Faces().front();
                f.Ids() = p.shape.Ids();
                out.append(py::make_tuple(p.kind, p.path, std::move(f)));
            } else {
                out.append(py::make_tuple(p.kind, p.path, std::move(p.shape)));
            }
        }
        return out;
    }, py::arg("text"), py::arg("unit"), py::arg("scale"),
          "for px.FromStep: the parts of a STEP text, (kind, path, object) each: a Face, else a Shape; their numbers times "
          "scale, else from unit (the file's if none) into meters");
    m.def("_FromBrepText", [](const std::string &text, double factor) {
        BrepReader reader(text);
        py::list out;
        for (BrepReader::Part &p : reader.Parts()) {
            p.shape = Scaled(p.shape, factor);
            // the file names no kinds: one face is a Face, edges without faces a Wire if they make one
            if (p.shape.Faces().size() == 1 && p.shape.WireEdges().Empty())
                out.append(py::make_tuple("Face", p.path, p.shape.Faces().front()));
            else
                out.append(py::make_tuple(p.shape.Faces().empty() ? "Wire" : "Shape", p.path, std::move(p.shape)));
        }
        return out;
    }, py::arg("text"), py::arg("factor"),
          "for px.FromBrep: the parts of a BRep text, (kind, path, object) each: a Face, else a Shape; their numbers times factor");
}
