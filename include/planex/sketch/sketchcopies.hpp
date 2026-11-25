// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "planex/sketch/sketch.hpp"

namespace planex2d {

    template <class Store>
    std::vector<std::pair<size_t, Vertex>> BasicSketch<Store>::MirrorElements(const std::vector<size_t> &elements, const std::string &axis) {
        const auto [a0, u] = LineOf(axis, "Mirror");
        return s.Mirror(elements, a0, u);
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Mirror(const std::vector<std::string> &ids, const std::string &axis) {
        Flush();
        MirrorElements(Originals(WithSlotSpines(ids), "Mirror"), axis);
        return *this;
    }

    template <class Store>
    std::vector<std::string> BasicSketch<Store>::WithSlotSpines(std::vector<std::string> ids) const {
        for (size_t k = 0, n = ids.size(); k < n; ++k)
            if (book.IsSlot(ids[k]))
                ids.push_back(ids[k] + ".spine");
        return ids;
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Mirrored(const std::vector<std::string> &ids, const std::string &axis, bool tie, bool internal,
                                                     const std::optional<std::string> &id) {
        if (tie)
            RequireConstraints("Mirrored(auto_constraint=True)");
        if (internal)
            RequireConstraints("Mirrored(internal_constraints=True)");
        return Atomic([&] {
            const auto [a0, u] = LineOf(axis, "Mirrored");
            const std::string scope = id ? *id : book.FreshId("Mirrored");
            if (id)
                book.CheckId(scope);
            const std::vector<size_t> originals = Originals(ids, "Mirrored");
            const sketch::Copies made = CopyElements(originals, [&](const Vertex &p) { return planex2d::Mirrored(p, a0, u); }, true, scope);
            if (tie)
                MirrorTied(made, RefOf(axis));
            else if (internal)
                ConstraintsCopied(scope, true);
            book.CopyWires(ids, made, scope);
            std::vector<size_t> edges;
            for (const auto &[o, c] : made.elements)
                edges.push_back(c);
            if (!edges.empty())
                book.NameWire(scope, edges, "Mirrored: ");
            book.groups.push_back({"Mirrored", scope});
            MirrorMade(scope, ids, axis, tie, internal);
        });
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::AddPattern(const std::vector<std::string> &ids, const std::string &ref, size_t count, double step, bool linear, bool keep,
                                                       bool internal, const std::optional<std::string> &id, bool, const std::optional<sketch::PatternDir2> &second,
                                                       bool reverse) {
        const std::string call = linear ? "LinearPattern" : "CircularPattern";
        CheckPattern(call, count, step, second);
        if (keep)
            RequireConstraints(call + "(auto_constraint=True)");
        if (internal)
            RequireConstraints(call + "(internal_constraints=True)");
        return Atomic([&] {
            const sketch::Ref r = PatternRef(call, ref, linear);
            const std::optional<sketch::Ref> r2 = second ? std::optional(PatternRef(call, second->ref, true)) : std::nullopt;
            const std::vector<size_t> originals = Originals(ids, call);
            const std::string scope = id ? *id : book.FreshId(call);
            if (id)
                book.CheckId(scope);
            std::vector<size_t> edges;
            for (const auto &[k, j] : PatternSteps(count, second ? second->count : 0)) {
                const std::string prefix = StepPrefix(scope, k, j, second.has_value());
                const sketch::Copies made = PlaceCopy(originals, r, linear, reverse ? -step : step, k, prefix, r2,
                                                      second ? (second->reverse ? -second->step : second->step) : 0.0, j);
                if (internal)
                    ConstraintsCopied(prefix, false);
                book.CopyWires(ids, made, prefix);
                for (const auto &[o, c] : made.elements)
                    edges.push_back(c);
            }
            if (!edges.empty())
                book.NameWire(scope, edges, call + ": ");
            book.groups.push_back({call, scope});
        });
    }

    template <class Store>
    void BasicSketch<Store>::CheckPattern(const std::string &call, size_t count, double step, const std::optional<sketch::PatternDir2> &second) {
        if (count < (second ? 1u : 2u))
            throw std::invalid_argument(call + ": count is the number of instances with the original, at least " + (second ? "1" : "2"));
        IsFinite(call.c_str(), {step});
        if (second) {
            if (second->count < 1 || count * second->count < 2)
                throw std::invalid_argument(call + ": count2 is the number of instances along dir2 with the original, at least 1, and "
                                            "count times count2 at least 2");
            IsFinite(call.c_str(), {second->step});
        }
    }

    template <class Store>
    std::vector<std::pair<size_t, size_t>> BasicSketch<Store>::PatternSteps(size_t count, size_t count2) {
        std::vector<std::pair<size_t, size_t>> out;
        for (size_t j = 0; j < std::max<size_t>(count2, 1); ++j)
            for (size_t k = 0; k < count; ++k)
                if (k > 0 || j > 0)
                    out.push_back({k, j});
        return out;
    }

    template <class Store>
    std::string BasicSketch<Store>::StepPrefix(const std::string &scope, size_t k, size_t j, bool two) {
        return scope + "." + std::to_string(k) + (two ? "." + std::to_string(j) : "");
    }

    template <class Store>
    sketch::Ref BasicSketch<Store>::PatternRef(const std::string &call, const std::string &ref, bool linear) const {
        const sketch::Ref r = RefOf(ref);
        if (linear ? !r.IsLine(s) : !r.IsPoint(s))
            throw std::invalid_argument(call + ": '" + ref + (linear ? "' is no line" : "' is no point"));
        return r;
    }

    template <class Store>
    sketch::Copies BasicSketch<Store>::PlaceCopy(const std::vector<size_t> &originals, const sketch::Ref &r, bool linear, double step, size_t k,
                                                 const std::string &prefix, const std::optional<sketch::Ref> &r2, double step2, size_t j) {
        Vertex o, u;
        if (linear)
            u = s.elements[r.element].Carrier(s).second;
        else
            o = r.PointValue(s);
        const Vertex u2 = r2 ? s.elements[r2->element].Carrier(s).second * (step2 * (double)j) : Vertex{};
        return CopyElements(originals, [&](const Vertex &p) {
            return linear ? (r2 ? p + u * (step * (double)k) + u2 : p + u * (step * (double)k)) : Rotated(p, o, step * (double)k);
        }, false, prefix);
    }

    template <class Store>
    std::vector<size_t> BasicSketch<Store>::Originals(const std::vector<std::string> &ids, const std::string &call) const {
        std::vector<size_t> out;
        for (const std::string &id : Expanded(ids)) {
            const sketch::Ref r = RefOf(id);
            const sketch::ElementKind k = s.elements[r.element].kind;
            if (r.part != sketch::Part::Whole || r.element < sketch::ownElements || k == sketch::ElementKind::Axis || k == sketch::ElementKind::Pattern ||
                k == sketch::ElementKind::Offset)
                throw std::invalid_argument(call + ": '" + id + "' is no point, line, arc or circle of the drawing");
            if (k == sketch::ElementKind::OffsetCurve)
                throw std::invalid_argument(call + ": '" + id + "' is an offset curve, it follows its source; copy its source and the "
                                                                "offset's distance instead (offset the copy)");
            if (std::find(out.begin(), out.end(), r.element) == out.end())
                out.push_back(r.element);
        }
        for (size_t k = 0, n = out.size(); k < n; ++k)
            if (s.elements[out[k]].IsBlend(s))
                if (const size_t h = s.splines[s.elements[out[k]].SplineIndex()].blend->helper;
                    h != sketch::none && std::find(out.begin(), out.end(), h) == out.end())
                    out.push_back(h);
        return out;
    }

    template <class Store>
    template <class F>
    sketch::Copies BasicSketch<Store>::CopyElements(const std::vector<size_t> &originals, F f, bool flip, const std::string &prefix) {
        return sketch::CopyMaker<BasicSketch, F>(*this, f, flip, prefix).Make(originals);
    }

}
