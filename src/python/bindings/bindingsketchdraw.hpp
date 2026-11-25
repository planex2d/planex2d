// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <cmath>
#include <iterator>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "bindingbase.hpp"
#include "bindingargs.hpp"
#include "bindingconstruction.hpp"
#include "bindingsketchargs.hpp"

template <class S>
inline py::object ForHelpers(S &s, const py::handle &h) {
    if (IsA<SketchAxis>(h) && h.cast<SketchAxis>() == SketchAxis::Origin)
        return Tuple2(s.Location(s.GlobalOrigin()));
    if (IsA<SketchAxis>(h))
        return py::str(h.cast<SketchAxis>() == SketchAxis::H ? "H" : "V");
    if (py::isinstance<py::str>(h)) {
        const std::string t = h.cast<std::string>();
        return s.Book().IsElement(t) ? py::str(t) : py::object(Tuple2(s.Location(t)));
    }
    if (py::isinstance<py::list>(h) || py::isinstance<py::tuple>(h))
        return Mapped(h, [&](const py::handle &x) { return ForHelpers(s, x); });
    return py::reinterpret_borrow<py::object>(h);
}

inline std::optional<std::string> TargetId(const py::handle &h) {
    if (IsA<SketchAxis>(h))
        return RefText(h, "");
    if (py::isinstance<py::str>(h))
        return h.cast<std::string>();
    if (py::isinstance<py::tuple>(h) && py::len(h) == 2 && (py::isinstance<py::str>(h[py::int_(0)]) || IsA<SketchAxis>(h[py::int_(0)])))
        return TargetId(h[py::int_(0)]);
    return std::nullopt;
}

template <class S>
inline std::vector<std::string> TangentTargets(S &s, const py::object &tangent) {
    const bool sequence = py::isinstance<py::list>(tangent) || py::isinstance<py::tuple>(tangent);
    std::vector<py::object> items{tangent};
    if (sequence && py::len(tangent) == 2) {
        const py::sequence mirrored = ForHelpers(s, tangent);
        if (!(IsTarget(mirrored[0]) && IsPoint(mirrored[1])))
            items = {tangent[py::int_(0)], tangent[py::int_(1)]};
    } else if (py::isinstance<py::list>(tangent)) {
        items.clear();
        for (const py::handle &x : tangent)
            items.push_back(py::reinterpret_borrow<py::object>(x));
    }
    std::vector<std::string> targets;
    for (const py::object &x : items)
        if (const auto t = TargetId(x))
            targets.push_back(*t);
    return targets;
}

template <class S>
inline sketch::HelperTargets HelperTargetsOf(S &s, const py::dict &h) {
    const auto target = [&](const char *key) { return h.contains(key) ? TargetId(h[key]) : std::nullopt; };
    return {target("from_"), target("to"), target("parallel"), target("perpendicular"), target("coincident"), target("through"),
            target("center"), target("concentric"), h.contains("tangent") ? TangentTargets(s, h["tangent"]) : std::vector<std::string>(),
            h.contains("until")};
}

inline const char *LINE_FORMS = "Line(l), Line(dx, dy), Line(to) or Line(start, end), then flags; the helpers place them";
inline const char *ARC_FORMS = "Arc(r, sweep), Arc(to), Arc(to, r), Arc(start, end) or Arc(start, end, r), then flags; the helpers "
                               "place them";

template <class S>
inline void EndsAndSizes(S &s, const CallArgs &a, const std::string &call, const char *forms, std::vector<py::object> &ends,
                         std::vector<double> &sizes) {
    for (const py::object &v : a.values) {
        if (IsNumber(v)) {
            sizes.push_back(v.cast<double>());
            continue;
        }
        if (!sizes.empty())
            throw py::type_error(call + ": the ends come first, then the sizes; " + forms);
        ends.push_back(IsRef(v) && !IsA<SketchAxis>(v) ? py::object(py::str(RefIn(s, v, call))) : v);
    }
    if (ends.size() > 2)
        throw py::type_error(call + ": at most two ends, from and to; " + forms);
}

std::string HelpedCall(const std::string &call, const std::vector<py::object> &ends, const std::vector<double> &numbers, const py::dict &helpers);

template <class S>
struct HelpedPlacement {
    S &s;
    const std::string &text;
    const std::vector<double> &numbers;
    const std::vector<sketch::Kind> &flags;
    const sketch::Options &opt;
    sketch::HelperView<S> view;
    py::dict kw;
    bool endOption, isFrom, reverse;
    py::object from, to;

    HelpedPlacement(S &sketch, const std::string &call, const std::string &callText, const std::vector<py::object> &ends,
                    const std::vector<double> &nums, const py::dict &helpers, const std::vector<sketch::Kind> &fl,
                    const sketch::Options &o)
        : s(sketch), text(callText), numbers(nums), flags(fl), opt(o), view{sketch} {
        for (const auto &[k, v] : helpers)
            kw[k] = k.cast<std::string>() == "until" && (IsA<Until>(v) || py::isinstance<py::str>(v)) ? py::reinterpret_borrow<py::object>(v)
                                                                                                             : ForHelpers(s, v);
        endOption = IsGiven("until") || (call == "Arc" && IsGiven("sweep"));
        isFrom = ends.size() == 2 || (ends.size() == 1 && endOption);
        from = isFrom ? ForHelpers(s, ends[0]) : py::object(py::none());
        to = ends.size() == 2 ? ForHelpers(s, ends[1]) : ends.size() == 1 && !endOption ? ForHelpers(s, ends[0]) : py::object(py::none());
        reverse = kw.contains("reverse") && py::cast<bool>(kw["reverse"]);
    }
    bool IsGiven(const char *key) const { return kw.contains(key) && !kw[key].is_none(); }
    py::object Arg(const char *key) const { return kw.contains(key) ? py::object(kw[key]) : py::object(py::none()); }
    std::optional<double> Number(size_t k) const { return k < numbers.size() ? std::optional(numbers[k]) : std::nullopt; }

    void PlaceCircle() {
        s.ConstructCircle(CircleRefsOf(view, Number(0), Arg("center"), Arg("concentric"), Arg("coincident"), Arg("through"), Arg("tangent"),
                                       py::none(), reverse), flags, opt);
    }
    void PlaceLine() {
        s.ConstructLine(LineRefsOf(view, text, Number(0), Arg("until"), from, to, Arg("parallel"), Arg("perpendicular"), Arg("angle"),
                                   Arg("coincident"), Arg("tangent"), Arg("distance"), Arg("through"), py::none(), reverse), flags, opt);
    }
    void PlaceArc() {
        const std::optional<double> sweep = Number(1) ? Number(1) : kw.contains("sweep") ? std::optional(py::cast<double>(kw["sweep"])) : std::nullopt;
        s.ConstructArc(ArcRefsOf(view, text, Number(0), sweep, Arg("center"), Arg("concentric"), Arg("coincident"), Arg("tangent"),
                                 Arg("through"), Arg("until"), from, to, py::none(), reverse), flags, opt);
    }
};

template <class S>
inline void DrawHelped(S &s, const std::string &call, const std::vector<py::object> &ends, const std::vector<double> &numbers,
                       const py::dict &helpers, const std::vector<sketch::Kind> &flags, const sketch::Options &opt) {
    const std::string text = HelpedCall(call, ends, numbers, helpers);
    NamingTheCall([&] { s.Drawing(opt, [&] {
        HelpedPlacement<S> placement(s, call, text, ends, numbers, helpers, flags, opt);
        const std::string before = s.Book().LastId();
        if (call == "Circle")
            placement.PlaceCircle();
        else if (call == "Line")
            placement.PlaceLine();
        else
            placement.PlaceArc();
        if constexpr (HasConstraints<S>)
            if (s.autoConstraint && s.autoConstraint->named_helpers && s.Book().LastId() != before) {
                py::dict implied;
                for (const auto &[k, v] : helpers)
                    implied[k] = v;
                if (placement.isFrom)
                    implied["from_"] = ends[0];
                if (!ends.empty() && !(ends.size() == 1 && placement.endOption))
                    implied["to"] = ends.back();
                s.ConstrainHelped(call, s.Book().LastId(), HelperTargetsOf(s, implied));
            }
    }); }, [&] { return text; });
}

struct DrawArgs {
    CallArgs a;
    sketch::Options opt;
    py::dict helpers;
};
template <class S>
DrawArgs DrawArgsOf(S &s, const py::args &args, const py::kwargs &kw, const std::string &call, const std::vector<const char *> &helpers) {
    const py::tuple given(Resolved(s, UnpenArgs(s.Axes(), args, call), call));
    const py::dict keywords = UnpenKeywords(s, kw, call);
    Prepare(s, given);
    Prepare(s, keywords);
    return {Split(given, call), OptionsOf(keywords), HelpersOf(s, keywords, call, helpers)};
}

template <class S>
S &DrawLine(S &s, const py::args &args, const py::kwargs &kw) {
    const DrawArgs d = DrawArgsOf(s, args, kw, "Line", LineHelpers);
    const AutoScope<S> scope(s, d.a.autoFlag, "Line");
    const CallArgs &a = d.a;
    const sketch::Options &opt = d.opt;
    const py::dict &helpers = d.helpers;
    if (helpers.empty()) {
        if (Numbers(a, 1)) {
            s.Line(a.values[0].cast<double>(), a.flags, opt);
            return s;
        }
        if (Numbers(a, 2)) {
            s.Line(a.values[0].cast<double>(), a.values[1].cast<double>(), a.flags, opt);
            return s;
        }
        if (a.values.size() == 1 && IsPointLike(a.values[0])) {
            s.Line(PointArg(s.Axes(), a.values[0], "Line"), a.flags, opt);
            return s;
        }
    }
    std::vector<py::object> ends;
    std::vector<double> sizes;
    EndsAndSizes(s, a, "Line", LINE_FORMS, ends, sizes);
    if (sizes.size() > 1 || (!sizes.empty() && !ends.empty()) || (ends.empty() && sizes.empty() && helpers.empty()))
        throw py::type_error(std::string("Line: ") + LINE_FORMS);
    DrawHelped(s, "Line", ends, sizes, helpers, a.flags, opt);
    return s;
}

std::vector<py::object> FilletEnds(const std::vector<py::object> &ends, const std::vector<double> &sizes, const py::dict &helpers);

template <class S>
S &DrawArc(S &s, const py::args &args, const py::kwargs &kw) {
    const DrawArgs d = DrawArgsOf(s, args, kw, "Arc", ArcHelpers);
    const AutoScope<S> scope(s, d.a.autoFlag, "Arc");
    const CallArgs &a = d.a;
    const sketch::Options &opt = d.opt;
    const py::dict &helpers = d.helpers;
    std::vector<py::object> ends;
    std::vector<double> sizes;
    EndsAndSizes(s, a, "Arc", ARC_FORMS, ends, sizes);
    if (sizes.size() > (ends.empty() ? 2u : 1u))
        throw py::type_error(std::string("Arc: ") + ARC_FORMS);
    const bool circle = helpers.contains("center") || helpers.contains("concentric") || helpers.contains("coincident");
    if (helpers.contains("sweep") && sizes.size() == 2)
        throw py::type_error("Arc() got multiple values for argument 'sweep'");
    if (helpers.contains("sweep") && !circle && ends.empty())
        throw py::type_error("Arc(r, sweep=...): along the pen the sweep is positional, Arc(r, sweep); sweep= ends an arc on "
                             "a circle given by center=, concentric= or coincident=");
    if (sizes.size() == 2 && circle)
        throw py::type_error("Arc(r, sweep, ...): on a circle given by center=, concentric= or coincident= the sweep is "
                             "sweep=: Arc(r, center=(0, 0), sweep=90)");
    if (helpers.empty()) {
        if (ends.empty() && sizes.size() == 2) {
            s.Arc(sizes[0], sizes[1], a.flags, opt);
            return s;
        }
        if (ends.size() == 1 && sizes.empty() && IsPointLike(ends[0])) {
            s.Arc(PointArg(s.Axes(), ends[0], "Arc"), s.Heading(), a.flags, opt);
            return s;
        }
        if (ends.empty())
            throw py::type_error(std::string("Arc: ") + ARC_FORMS);
    }
    if (helpers.size() == 1 && helpers.contains("tangent") && IsPointLike(helpers["tangent"]) && ends.size() == 1 && sizes.empty() &&
        IsPointLike(ends[0])) {
        s.Arc(PointArg(s.Axes(), ends[0], "Arc"), PointArg(helpers["tangent"], "Arc(tangent=)"), a.flags, opt);
        return s;
    }
    DrawHelped(s, "Arc", FilletEnds(ends, sizes, helpers), sizes, helpers, a.flags, opt);
    return s;
}

template <class S>
S &DrawCircle(S &s, const py::args &args, const py::kwargs &kw) {
    const py::dict keywords = UnpenKeywords(s, kw, "Circle");
    Prepare(s, keywords);
    const CallArgs a = Split(py::tuple(Resolved(s, UnpenArgs(s.Axes(), args, "Circle"), "Circle")), "Circle");
    const AutoScope<S> scope(s, a.autoFlag, "Circle");
    const sketch::Options opt = OptionsOf(keywords);
    const py::dict helpers = HelpersOf(s, keywords, "Circle", CircleHelpers);
    if (!helpers.empty()) {
        if (!a.values.empty() && !Numbers(a, 1))
            throw py::type_error("Circle(r, helpers...): only a radius goes with the helpers");
        DrawHelped(s, "Circle", {}, a.values.empty() ? std::vector<double>() : std::vector<double>{a.values[0].cast<double>()},
                   helpers, a.flags, opt);
        return s;
    }
    if (Numbers(a, 1)) {
        s.Circle(s.Position(), a.values[0].cast<double>(), a.flags, opt);
        return s;
    }
    // the center as one point; Circle(h, v, r) is gone
    if (a.values.size() == 2 && IsPointLike(a.values[0]) && IsNumber(a.values[1])) {
        s.Circle(PointArg(s.Axes(), a.values[0], "Circle"), a.values[1].cast<double>(), a.flags, opt);
        return s;
    }
    if (Numbers(a, 3))
        throw py::type_error("Circle(h, v, r): the center is one point now, Circle((h, v), r)");
    throw py::type_error("Circle(r) or Circle((h, v), r), then flags; or the helpers");

}

template <class S>
S &DrawSlot(S &s, const std::string &kind, const py::args &args, const py::kwargs &kw) {
    std::vector<py::object> given;
    for (const py::handle &h : args)
        given.push_back(py::reinterpret_borrow<py::object>(h));
    const char *forms = kind == "Slot"      ? "Slot(l, w), Slot(dh, dv, w), Slot(to, w), Slot(start, end, w), then flags; Line's helpers place the spine"
                        : kind == "Keyhole" ? "Keyhole(l, w, r), Keyhole(dh, dv, w, r), Keyhole(to, w, r), Keyhole(start, end, w, r), then flags; Line's helpers "
                                              "place the spine"
                                            : "ArcSlot(r, sweep, w), ArcSlot(to, w), ArcSlot(to, r, w), ArcSlot(start, end, r, w), then flags; Arc's "
                                              "helpers place the spine";
    const auto lastNumber = [&](const char *what) {
        const auto at = std::find_if(given.rbegin(), given.rend(), [](const py::object &h) { return !IsA<sketch::Kind>(h); });
        if (at == given.rend() || !IsNumber(*at))
            throw py::type_error(kind + ": " + what + " is the last number, before the flags; " + forms);
        const double v = at->template cast<double>();
        given.erase(std::next(at).base());
        return v;
    };
    const std::optional<double> hole = kind == "Keyhole" ? std::optional<double>(lastNumber("the hole's radius r")) : std::nullopt;
    const double w = lastNumber(hole ? "the width w, then r," : "the width w");
    std::vector<std::pair<const char *, py::handle>> shown;
    for (const py::object &h : given)
        shown.push_back({nullptr, h});
    const py::float_ widthText(w), holeText(hole.value_or(0.0));
    shown.push_back({nullptr, widthText});
    if (hole)
        shown.push_back({nullptr, holeText});
    const std::string call = CallText(kind, shown);
    py::kwargs spineKw;
    for (const auto &[k, v] : kw)
        if (std::find(OptionKeys.begin(), OptionKeys.end(), k.cast<std::string>()) == OptionKeys.end())
            spineKw[k] = v;
    spineKw["construction"] = true;
    const py::args spineArgs(py::tuple(py::cast(given)));
    NamingTheCall([&] {
        s.Slot(kind, w, hole, OptionsOf(kw), call, [&] {
            if (kind == "ArcSlot")
                DrawArc(s, spineArgs, spineKw);
            else
                DrawLine(s, spineArgs, spineKw);
        });
    }, [&] { return call; });
    return s;
}

template <class S>
S &DrawPolygon(S &s, const std::string &kind, const py::args &args, const py::kwargs &kw) {
    const bool hexagon = kind == "Hexagon";
    std::vector<py::object> given;
    for (const py::handle &h : args)
        given.push_back(py::reinterpret_borrow<py::object>(h));
    std::vector<std::pair<const char *, py::handle>> shown;
    for (const py::object &h : given)
        shown.push_back({nullptr, h});
    const std::string call = CallText(kind, shown);
    const auto at = std::find_if(given.rbegin(), given.rend(), [](const py::object &h) { return !IsA<sketch::Kind>(h); });
    const char *form = hexagon ? "Hexagon(af), Hexagon(h, v, af), then flags; Circle's helpers place the circle, the width af across flats is the last number"
                               : "Polygon(r, n), Polygon(h, v, r, n), then flags; Circle's helpers place the circle, the number of sides n (an int) is the last number";
    if (at == given.rend() || !(hexagon ? IsNumber(*at) : py::isinstance<py::int_>(*at) && !py::isinstance<py::bool_>(*at)))
        throw py::type_error(call + ": " + form);
    std::optional<double> flats;
    size_t n = 6;
    if (hexagon) {
        flats = at->template cast<double>();
        *at = py::float_(*flats / std::sqrt(3.0));
    } else {
        const long long count = at->template cast<long long>();
        if (count < 3)
            throw py::value_error(call + ": a polygon has n >= 3 sides, n = " + std::to_string(count) + "; make n larger");
        n = (size_t)count;
        given.erase(std::next(at).base());
    }
    py::kwargs circleKw;
    for (const auto &[k, v] : kw)
        if (std::find(OptionKeys.begin(), OptionKeys.end(), k.cast<std::string>()) == OptionKeys.end())
            circleKw[k] = v;
    circleKw["construction"] = true;
    const py::args circleArgs(py::tuple(py::cast(given)));
    NamingTheCall([&] {
        s.Polygon(kind, n, flats, OptionsOf(kw), call, [&] {
            try {
                DrawCircle(s, circleArgs, circleKw);
            } catch (const py::type_error &e) {
                throw py::type_error(call + ": the circle takes the forms of Circle: " + e.what());
            }
        });
    }, [&] { return call; });
    return s;
}

template <class S>
S &AddSymmetryLine(S &s, const py::object &a, const py::object &b, bool reverse, const std::optional<std::string> &id,
                   const py::kwargs &kw) {
    NoMoreKeywords(kw, "SymmetryLine", "reverse=True takes the other bisector");
    UserId(id);
    Prepare(s, py::make_tuple(a, b));
    NamingTheCall([&] { s.Atomic([&] {
        const bool refs = IsRef(a) && IsRef(b);
        const std::string ta = refs ? RefIn(s, a, "SymmetryLine") : "", tb = refs ? RefIn(s, b, "SymmetryLine") : "";
        const std::string ka = refs ? s.RefKind(ta) : "", kb = refs ? s.RefKind(tb) : "";
        std::string axis;
        if (ka == "round" && kb == "round")
            axis = s.AddCentreBisector(ta, tb, id);
        else {
            sketch::HelperView view{s};
            const py::object ra = ForHelpers(s, a), rb = ForHelpers(s, b);
            const std::string call = CallText("SymmetryLine", {{"a", ra}, {"b", rb}});
            const sketch::SymRef sa = SymRefOf(view, ra, s.Position(), false, call), sb = SymRefOf(view, rb, s.Position(), false, call);
            const Line line = sketch::SymmetryAxis(sa, sb, reverse, call);
            axis = s.AddAxisLine(line.Start(), line.End() - line.Start(), "SymmetryLine", id);
        }
        if (!refs)
            return;
        const bool lines = (ka == "line" || ka == "axis") && (kb == "line" || kb == "axis");
        if (!lines && !(ka == "point" && kb == "point") && !(ka == "round" && kb == "round"))
            throw py::value_error("SymmetryLine: a and b are two points, two lines or two circles");
        if constexpr (HasConstraints<S>)
            s.TieSymmetryLine(ta, tb, axis);
    }); }, [&] { return CallText("SymmetryLine", {{"a", a}, {"b", b}}); });
    return s;
}
