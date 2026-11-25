// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include "planex/sketch/sketch.hpp"

namespace planex2d {

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Cut(const std::string &id, const std::vector<std::string> &cutterIds, std::optional<Vertex> nearPoint, bool reverse,
                                                bool trim, bool tied) {
        const std::string call = std::string(trim ? "Trim" : "Split") + "('" + id + "')";
        sketch::Undoable(*this, [&] {
            CutPieces(id, cutterIds, nearPoint, reverse, trim, tied, call);
        });
        return *this;
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Extend(const std::string &id, const std::vector<std::string> &targets, std::optional<double> length) {
        const auto [point, at] = ExtendedEnd(id, targets, length, "Extend('" + id + "')");
        s.MovePoint(point, at);
        Sync();
        return *this;
    }

    template <class Store>
    std::pair<size_t, Vertex> BasicSketch<Store>::ExtendedEnd(const std::string &id, const std::vector<std::string> &targets, std::optional<double> length,
                                                              const std::string &call) {
        Finish();
        const sketch::Ref r = RefOf(id);
        const size_t e = r.element;
        const sketch::Element el = s.elements[e];
        const bool line = el.kind == sketch::ElementKind::Line;
        if (e < sketch::ownElements || (!line && el.kind != sketch::ElementKind::Arc) ||
            (r.part != sketch::Part::Whole && r.part != sketch::Part::Start && r.part != sketch::Part::End))
            throw std::invalid_argument(call + ": a line or an arc, or one of its ends, extends (a spline does not)");
        if (length && r.part == sketch::Part::Whole)
            throw std::invalid_argument(call + ": a length moves one end; give '" + id + ".start' or '" + id + ".end'");
        if (length && !(std::isfinite(*length) && *length > 0.0))
            throw std::invalid_argument(call + ": the length is above 0 (Trim shortens)");
        const Wire to = ExtendTargets(e, targets, call);
        const bool atEnd = r.part == sketch::Part::End ||
                           (r.part == sketch::Part::Whole && to.Distance(Coords(el.End())) < to.Distance(Coords(el.Start())));
        const size_t point = atEnd ? el.End() : el.Start();
        for (size_t k = sketch::ownElements; k < s.elements.size(); ++k)
            if (k != e && s.elements[k].kind != sketch::ElementKind::Point)
                for (size_t q : s.elements[k].PointsAndCentre(s))
                    if (s.coordsOf[q] == s.coordsOf[point])
                        throw std::invalid_argument(call + ": " + book.elementIds[k] + " ends there too; a free end extends");
        const std::string what = targets.size() == 1 ? targets[0] : Joined(targets);
        if (line)
            return {point, LineExtended(Coords(point), Coords(atEnd ? el.Start() : el.End()), to, length, what, call)};
        return {point, ArcExtended(el, atEnd, to, length, what, call)};
    }

    template <class Store>
    Wire BasicSketch<Store>::AsEdges(size_t e, const std::unordered_map<size_t, Properties> &vertexProps) const {
        Wire w(axes);
        if (s.elements[e].kind == sketch::ElementKind::Axis) {
            const double reach = 10.0 * std::max(1.0, s.Size());
            const auto [o, u] = s.elements[e].Carrier(s);
            w.Add(planex2d::Line(o - u * reach, o + u * reach));
        } else
            AddEdges(e, w, vertexProps);
        return w;
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::TrimChain(const std::string &wire, const std::vector<std::string> &cutterIds, const Vertex &nearPoint) {
        const std::string call = "Trim('" + wire + "')";
        sketch::Undoable(*this, [&] {
            const std::vector<std::string> ids = MembersOf(wire);
            const size_t n = ids.size();
            if (n == 1) {
                Cut(ids[0], cutterIds, nearPoint, false, true);
                return;
            }
            const CutChain chain = MetByCutters(ids, cutterIds, nearPoint);
            const std::vector<char> &met = chain.met;
            const size_t k = chain.nearest;
            if (met[k]) {
                Cut(ids[k], cutterIds, nearPoint, false, true);
                return;
            }
            const bool closed = IsClosedChain(ids);
            std::vector<char> done(n, 0);
            done[k] = 1;
            std::vector<std::string> whole{ids[k]};
            size_t cut = 0;
            for (const int way : {1, -1}) {
                for (size_t j = k, before = k;;) {
                    if (!closed && ((way > 0 && j + 1 == n) || (way < 0 && j == 0)))
                        break;
                    before = j;
                    j = (j + n + (size_t)(way > 0 ? 1 : n - 1)) % n;
                    if (done[j])
                        break;
                    done[j] = 1;
                    if (!met[j]) {
                        whole.push_back(ids[j]);
                        continue;
                    }
                    // trimmed on the run's side: the piece at its end shared with the element before it
                    const std::array<size_t, 2> ends = s.elements[RefOf(ids[j]).element].Ends(s);
                    const Wire previous = ElementWire(ids[before]);
                    const bool first = previous.Distance(Coords(ends[0])) <= previous.Distance(Coords(ends[1]));
                    Cut(ids[j], cutterIds, Coords(ends[first ? 0 : 1]), false, true);
                    ++cut;
                    break;
                }
            }
            if (cut == 0)
                throw std::invalid_argument(call + ": the cutters do not meet it");
            SetConstruction(whole, true, call);
        });
        return *this;
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Fillet(const std::vector<Vertex> &at, double r, bool curvature, const std::vector<std::string> &ids,
                                                   const std::vector<Properties> &given) {
        if (curvature)
            return CutCorners(at, CurvatureFilletRule{{r}}, ids, given, "Fillet");
        return CutCorners(at, FilletRule{r}, ids, given, "Fillet");
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Chamfer(const std::vector<Vertex> &at, const ChamferRule &rule, const std::vector<std::string> &ids,
                                                    const std::vector<Properties> &given) {
        return CutCorners(at, rule, ids, given, "Chamfer");
    }

    template <class Store>
    BasicSketch<Store> &BasicSketch<Store>::Connect(const std::string &a, const std::string &b, sketch::Kind continuity, bool reverse, const sketch::Options &opt) {
        const std::string call = "Connect('" + a + "', '" + b + "')";
        Flush();
        Finish();
        sketch::Undoable(*this, [&] {
            const std::array<std::pair<size_t, size_t>, 2> ends = ConnectedEnds(a, b, reverse, call);
            if (Coords(ends[0].second).IsClose(Coords(ends[1].second), Tol(s.Size())))
                throw std::invalid_argument(call + ": the ends meet already, there is no gap to connect");
            if (continuity == sketch::Kind::Coincident) {
                AddElement(s.AddLine(ends[0].second, ends[1].second), "Connect", opt);
                return;
            }
            AddElement(s.AddBlend(ends[0].second, ends[1].second, {{ends[0].first, ends[1].first}, sketch::none, continuity == sketch::Kind::Curvature}),
                       "Connect", opt);
        });
        return *this;
    }

    template <class Store>
    Wire BasicSketch<Store>::ExtendTargets(size_t e, const std::vector<std::string> &targets, const std::string &call) const {
        const std::unordered_map<size_t, Properties> vertexProps = VertexProps();
        Wire to(axes);
        for (const std::string &t : targets) {
            const sketch::Ref tr = RefOf(t);
            const sketch::ElementKind k = s.elements[tr.element].kind;
            if (tr.part != sketch::Part::Whole || tr.element == e || k == sketch::ElementKind::Point || k == sketch::ElementKind::Pattern ||
                k == sketch::ElementKind::Offset)
                throw std::invalid_argument(call + ": '" + t + "' is no line, arc, circle, spline or axis to extend to");
            for (const Edge &x : AsEdges(tr.element, vertexProps))
                to.Add(x);
        }
        return to;
    }

    template <class Store>
    Vertex BasicSketch<Store>::LineExtended(const Vertex &from, const Vertex &other, const Wire &to, std::optional<double> length, const std::string &what,
                                            const std::string &call) const {
        const double tol = Tol(std::max(1.0, s.Size()));
        const Vertex u = planex2d::Normalized(from - other);
        if (length)
            return from + u * *length;
        Wire ray(axes);
        ray.Add(planex2d::Line(from, from + u * (10.0 * std::max(1.0, s.Size()) + Norm(from))));
        std::optional<Vertex> hit;
        for (const Vertex &x : planex2d::Intersections(ray, to))
            if (!x.IsClose(from, tol) && (!hit || Norm(x - from) < Norm(*hit - from)))
                hit = x;
        if (!hit)
            throw std::invalid_argument(call + ": it does not meet " + what + " along its line");
        return *hit;
    }

    template <class Store>
    Vertex BasicSketch<Store>::ArcExtended(const sketch::Element &el, bool atEnd, const Wire &to, std::optional<double> length, const std::string &what,
                                           const std::string &call) const {
        const Vertex from = Coords(atEnd ? el.End() : el.Start()), other = Coords(atEnd ? el.Start() : el.End());
        const double tol = Tol(std::max(1.0, s.Size()));
        // an arc goes on the way it runs at its end, back at its start, round the gap to its other end
        const Vertex c = Coords(el.Center());
        const double radius = s.values[el.Radius()];
        const bool forward = atEnd == el.ccw;
        const auto turn = [&](const Vertex &p) {
            const Vertex a = from - c, b = p - c;
            const double t = std::atan2(Cross(a, b), Dot(a, b)) * (forward ? 1.0 : -1.0);
            return t < 0.0 ? t + 2.0 * std::numbers::pi : t;
        };
        const double gap = turn(other);
        if (length) {
            const double angle = *length / radius;
            if (angle >= gap)
                throw std::invalid_argument(call + ": the arc would close into a circle");
            return planex2d::Rotated(from, c, std::cos(angle), std::sin(angle) * (forward ? 1.0 : -1.0));
        }
        Wire rest(axes);
        rest.Add(planex2d::Arc(from, other, c, forward));
        std::optional<Vertex> hit;
        double first = gap;
        for (const Vertex &x : planex2d::Intersections(rest, to))
            if (const double t = turn(x); t > tol / radius && t < first) {
                first = t;
                hit = x;
            }
        if (!hit)
            throw std::invalid_argument(call + ": it does not meet " + what + " along its circle");
        return *hit;
    }

    template <class Store>
    typename BasicSketch<Store>::CutChain BasicSketch<Store>::MetByCutters(const std::vector<std::string> &ids, const std::vector<std::string> &cutterIds,
                                                                           const Vertex &nearPoint) {
        const size_t n = ids.size();
        Wire cutters(axes);
        for (const std::string &c : cutterIds)
            for (const Edge &e : ElementWire(c))
                cutters.Add(e);
        std::vector<char> met(n, 0);
        std::vector<double> away(n, std::numeric_limits<double>::infinity());
        for (size_t k = 0; k < n; ++k) {
            const Wire w = ElementWire(ids[k]);
            away[k] = w.Distance(nearPoint);
            const std::array<size_t, 2> ends = s.elements[RefOf(ids[k]).element].Ends(s);
            for (const Vertex &x : Intersections(w, cutters)) {
                bool atEnd = false;
                for (size_t p : ends)
                    atEnd = atEnd || (p != sketch::none && x.IsClose(Coords(p), Tol(NormInf(x))));
                met[k] = met[k] || !atEnd;
            }
        }
        return {met, (size_t)(std::min_element(away.begin(), away.end()) - away.begin())};
    }

    template <class Store>
    bool BasicSketch<Store>::IsClosedChain(const std::vector<std::string> &ids) const {
        // two elements close a chain only by sharing both ends
        size_t shared = 0;
        for (size_t p : s.elements[RefOf(ids.front()).element].Ends(s))
            for (size_t q : s.elements[RefOf(ids.back()).element].Ends(s))
                shared += p != sketch::none && q != sketch::none && Coords(p) == Coords(q);
        return shared >= (ids.size() == 2 ? 2 : 1);
    }

    template <class Store>
    void BasicSketch<Store>::CutPieces(const std::string &id, const std::vector<std::string> &cutterIds, std::optional<Vertex> nearPoint, bool reverse, bool trim,
                                       bool tied, const std::string &call) {
        Finish();
        const sketch::Ref ar = RefOf(id);
        const size_t a = ar.element;
        if (IsSpan(a))
            throw std::invalid_argument(call + ": it is cut into " + Joined(book.wires.at(book.elementIds[a])) + " already; cut one of them");
        if (ar.part != sketch::Part::Whole || a < sketch::ownElements || !s.elements[a].IsEdge())
            throw std::invalid_argument(call + ": not a line, arc, circle or spline");
        std::vector<size_t> cutters;
        if (cutterIds.empty()) {
            for (size_t e = sketch::ownElements; e < s.elements.size(); ++e)
                if (e != a && !IsHidden(e) && s.elements[e].IsEdge())
                    cutters.push_back(e);
        } else
            for (const std::string &c : cutterIds) {
                const sketch::Ref r = RefOf(c);
                const sketch::ElementKind k = s.elements[r.element].kind;
                if (r.part != sketch::Part::Whole || k == sketch::ElementKind::Point || k == sketch::ElementKind::Pattern || k == sketch::ElementKind::Offset ||
                    r.element == a)
                    throw std::invalid_argument(call + ": '" + c + "' cuts nothing");
                cutters.push_back(r.element);
            }
        sketch::ElementCutter<BasicSketch>(*this, a, id, call).Cut(cutters, cutterIds.empty(), nearPoint.value_or(pen.pos), reverse, trim, tied);
    }

    template <class Store>
    void BasicSketch<Store>::CornerMade(const CurvatureFilletRule &, const sketch::CornerParts &c) {
        const sketch::Element arc = s.elements[c.made];
        s.AddBlend(arc.Start(), arc.End(), {{c.arms[0], c.arms[1]}});
        s.elements[c.made] = s.elements.back();
        s.elements.pop_back();
        RemoveOne(book.elementIds[pointElementOf.at(arc.Center())]);
    }

    template <class Store>
    std::array<std::pair<size_t, size_t>, 2> BasicSketch<Store>::ConnectedEnds(const std::string &a, const std::string &b, bool reverse, const std::string &call) const {
        const auto ends = [&](const std::string &id) {
            const sketch::Ref r = RefOf(id);
            const sketch::Element &e = s.elements[r.element];
            if (r.element < sketch::ownElements || !e.HasEnds() || IsHidden(r.element) || e.IsClosed(s))
                throw std::invalid_argument(call + ": '" + id + "' is no line, arc or open spline");
            const std::array<size_t, 2> p = e.Ends(s);
            if (r.part == sketch::Part::Whole)
                return std::pair{r.element, std::vector<size_t>{p[0], p[1]}};
            if (r.part == sketch::Part::Start || r.part == sketch::Part::End)
                return std::pair{r.element, std::vector<size_t>{p[r.part == sketch::Part::Start ? 0 : 1]}};
            throw std::invalid_argument(call + ": '" + id + "' is no end; give an element or one of its ends, '" + book.elementIds[r.element] + ".end'");
        };
        const auto [ea, pa] = ends(a);
        const auto [eb, pb] = ends(b);
        if (ea == eb)
            throw std::invalid_argument(call + ": both ends are on one element");
        if (reverse && pa.size() == 1 && pb.size() == 1)
            throw std::invalid_argument(call + ": reverse=True takes the other end of an element given whole; the ends named are the ones joined");
        size_t i = 0, j = 0;
        double nearest = std::numeric_limits<double>::infinity();
        for (size_t x = 0; x < pa.size(); ++x)
            for (size_t y = 0; y < pb.size(); ++y)
                if (const double d = Norm(Coords(pa[x]) - Coords(pb[y])); d < nearest) {
                    nearest = d;
                    i = x;
                    j = y;
                }
        if (reverse) {
            i = pa.size() - 1 - i;
            j = pb.size() - 1 - j;
        }
        return {{{ea, pa[i]}, {eb, pb[j]}}};
    }

    template <class Store>
    template <class Rule>
    BasicSketch<Store> &BasicSketch<Store>::CutCorners(const std::vector<Vertex> &at, const Rule &rule, const std::vector<std::string> &ids,
                                                       const std::vector<Properties> &given, const std::string &call) {
        rule.Check(call);
        if (!ids.empty() && ids.size() != at.size())
            throw std::invalid_argument(call + ": " + std::to_string(ids.size()) + " ids for " + std::to_string(at.size()) +
                                        " corners; give one id per corner (a str for one corner, a list for several)");
        if (given.size() > 1 && given.size() != at.size())
            throw std::invalid_argument(call + ": " + std::to_string(given.size()) + " values of a property for " +
                                        std::to_string(at.size()) + " corners; give one value for all or one per corner");
        Flush();
        Finish();
        sketch::Undoable(*this, [&] {
            sketch::CornerMaker<BasicSketch, Rule>(*this, rule, call).Make(at, ids, given);
        });
        return *this;
    }

}
