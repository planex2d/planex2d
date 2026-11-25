// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "planex/sketch/sketch.hpp"

namespace planex2d {

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Remove(const std::string &id) {
        RemoveOne(id);
        return *this;
    }

    template <class Store>
    void BasicSketch<Store>::RemoveWire(const std::string &id) {
        const auto w = book.wires.find(id);
        const std::vector<std::string> elements = w->second;
        book.wires.erase(w);
        // blends first: they are computed from the others
        for (const bool blends : {true, false})
            for (auto e = elements.rbegin(); e != elements.rend(); ++e)
                if (const auto at = book.byId.find(*e); at != book.byId.end() && s.elements[at->second.index].IsBlend(s) == blends)
                    RemoveOne(*e);
        if (const auto at = book.byId.find(id); at != book.byId.end() && at->second.isElement && IsSpan(at->second.index))
            RemoveOne(id);
        if (book.IsSlot(id))
            RemoveOne(id + ".spine");
        if (book.IsPolygon(id))
            RemoveOne(id + ".circle");
        if (book.IsCentredRectangle(id)) {
            RemoveOne(id + ".center");
            RemoveOne(id + ".diagonal");
        }
    }

    template <class Store>
    void BasicSketch<Store>::RemoveOne(const std::string &id) {
        if (book.wires.contains(id)) {
            RemoveWire(id);
            return;
        }
        const auto it = book.byId.find(id);
        if (it == book.byId.end() || !it->second.isElement)
            throw std::invalid_argument("Remove('" + id + "'): no such element");
        const size_t k = it->second.index;
        if (const std::optional<size_t> blend = s.BlendOn(k))
            throw std::invalid_argument("Remove('" + id + "'): '" + book.elementIds[*blend] + "' is computed from it; Remove('" + book.elementIds[*blend] +
                                        "') first");
        const std::vector<std::string> ends = EndsToRemove(k, id);
        const std::vector<std::string> legs = LegsGoingWith(k);
        ElementGoes(k, id);
        Finish();
        std::erase_if(book.wires, [&](auto &w) {
            std::erase(w.second, id);
            return w.second.empty();
        });
        s.RemoveElement(k);
        EraseElementData(k);
        Reindex();
        for (const std::string &end : ends)
            if (Unused(book.byId.at(end).index))
                RemoveOne(end);
        Reindex();
        for (const std::string &leg : legs)
            RemoveOne(leg);
    }

    template <class Store>
    std::vector<std::string> BasicSketch<Store>::LegsGoingWith(size_t k) const {
        std::vector<std::string> legs;
        for (size_t leg : s.LegsGoingWith(k))
            legs.push_back(book.elementIds[leg]);
        return legs;
    }

    template <class Store>
    std::vector<std::string> BasicSketch<Store>::EndsToRemove(size_t k, const std::string &id) const {
        if (k < sketch::ownElements)
            throw std::invalid_argument("Remove('" + id + "'): the sketch's own axes and origin stay");
        const sketch::Element &e = s.elements[k];
        std::vector<std::string> ends;
        const std::vector<size_t> on = e.PointsAndCentre(s);
        for (size_t j = sketch::ownElements; j < s.elements.size(); ++j)
            if (s.elements[j].kind == sketch::ElementKind::Point && std::find(on.begin(), on.end(), s.elements[j].Point()) != on.end())
                ends.push_back(book.elementIds[j]);
        if (e.kind == sketch::ElementKind::Point)
            for (size_t j = 0; j < s.elements.size(); ++j) {
                const sketch::Element &other = s.elements[j];
                const std::vector<size_t> through = other.PointsOn(s);
                const bool centre = other.IsRound() && other.Center() == e.Point();
                if (j != k && (centre || std::find(through.begin(), through.end(), e.Point()) != through.end()))
                    throw std::invalid_argument("Remove('" + id + "'): " + book.elementIds[j] +
                                                (centre                                      ? " has its center there"
                                                 : other.IsPoled(s)                          ? " has it as a pole"
                                                 : other.kind == sketch::ElementKind::Spline ? " runs through it"
                                                                                             : " ends there") +
                                                ", remove it first");
            }
        return ends;
    }

    template <class Store>
    void BasicSketch<Store>::FreezeOffsetCurve(size_t e) {
        const sketch::OffsetRun run = s.elements[e].Run(s);
        const std::array<size_t, 2> ends = s.elements[e].Ends(s);
        const size_t spline = FitCurve(run.g, run.reversed ? run.to : run.from, run.reversed ? run.from : run.to, {ends[0], ends[1]},
                                       {std::nullopt, {}, !IsDrawn(e)});
        s.elements[e] = s.elements[spline];
        s.elements.pop_back();
    }

    template <class Store>
    void BasicSketch<Store>::DropSolverElements() {
        std::vector<size_t> moved(s.elements.size()), gone;
        for (size_t e = 0; e < moved.size(); ++e) {
            moved[e] = e - gone.size();
            if (s.elements[e].kind == sketch::ElementKind::Pattern || s.elements[e].kind == sketch::ElementKind::Offset)
                gone.push_back(e);
        }
        for (auto e = gone.rbegin(); e != gone.rend(); ++e) {
            s.RemoveElement(*e);
            EraseElementData(*e);
        }
        pen.Renumber(moved);
        for (std::optional<sketch::PenState> *held : {&parked, &normalPen})
            if (*held)
                (*held)->Renumber(moved);
    }

    template <class Store>
    void BasicSketch<Store>::WithoutConstraints() {
        for (size_t e = 0; e < s.elements.size(); ++e)
            if (s.elements[e].kind == sketch::ElementKind::OffsetCurve)
                FreezeOffsetCurve(e);
        for (sketch::SplineData &d : s.splines)
            if (d.fitted)
                d.fitted->formula.reset();
        DropSolverElements();
        Reindex();
    }

    template <class Store>
    void BasicSketch<Store>::EraseElementData(size_t k) {
        book.elementIds.erase(book.elementIds.begin() + (std::ptrdiff_t)k);
        props.erase(props.begin() + (std::ptrdiff_t)k);
        roles.erase(roles.begin() + (std::ptrdiff_t)k);
    }

    template <class Store>
    void BasicSketch<Store>::Reindex() {
        book.Reindex();
        pointElementOf.clear();
        for (size_t e = 0; e < book.elementIds.size(); ++e)
            if (s.elements[e].kind == sketch::ElementKind::Point)
                pointElementOf.emplace(s.elements[e].Point(), e);
    }

}
