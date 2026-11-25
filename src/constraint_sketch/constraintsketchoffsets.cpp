// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/constraintsketch.hpp"

namespace planex2d {

    std::vector<double> ConstraintSketch::OffsetState(const std::string &name, const cs::OffsetInfo &info) const {
        const auto at = book.byId.find(name);
        if (at == book.byId.end())
            return {};
        std::vector<double> out{s.values[s.elements[at->second.index].Distance()]};
        for (const std::string &id : info.sources) {
            const auto e = book.byId.find(id);
            if (e == book.byId.end())
                return {};
            for (size_t u : s.elements[e->second.index].Values(s))
                out.push_back(s.values[u]);
        }
        return out;
    }

    // rule 2 of 0088: an offset keeps the shape it was made with. The first offset whose exact offset now (its source
    // and distance as they are) has another shape: what changes; offsets unchanged since their last check are skipped.
    // shape false: only the checks short of building the exact offset (a drag's frame), the offset still unchecked
    std::optional<std::string> ConstraintSketch::OffsetProblem(bool shape) const {
        for (const auto &[name, info] : offsetInfos) {
            const std::vector<double> state = OffsetState(name, info);
            if (state.empty() || state == info.checked)
                continue;
            const std::string call = "Offset('" + name + "')", again = "; make the offset again (Remove('" + name + "'), then Offset)";
            const double d = state.front();
            if (!(d > 0.0))
                return call + ": its distance d = " + NumberText(d) + " is no longer positive" + again;
            const sketch::OffsetChain source = info.Chain(book);
            const size_t n = source.elements.size();
            const auto pointAt = [&](size_t k, bool end) {
                const std::array<size_t, 2> ends = s.elements[source.elements[k]].Ends(s);
                return s.coordsOf[ends[end == bool(source.forward[k]) ? 1 : 0]];
            };
            if (s.elements[source.elements.front()].kind != sketch::ElementKind::Circle)
                for (size_t k = 0; k + (source.closed ? 0 : 1) < n; ++k)
                    if (pointAt(k, true) != pointAt((k + 1) % n, false))
                        return call + ": its source is no longer one chain (" + book.elementIds[source.elements[k]] + " and " +
                               book.elementIds[source.elements[(k + 1) % n]] + " came apart)" + again;
            for (const auto &[curve, id] : info.curves) {
                std::optional<std::string> wrong = CurveCusp(curve, id, d);
                if (!wrong)
                    wrong = CurveCrossing(curve, id);
                if (wrong)
                    return call + ": " + *wrong + again;
            }
            if (!shape)
                continue;
            try {
                const auto sides = SidesOf(source, source.Edges(s), d, info.towards, info.forms, call);
                if (sketch::ShapeOf(sides) == info.shape) {
                    info.checked = state;
                    continue;
                }
                return call + ": " + ShapeChange(info, sketch::SidePieces(sides), source, d) + again;
            } catch (const std::invalid_argument &x) {
                const std::string what = x.what();
                return (what.starts_with(call + ": ") ? what : call + ": " + what) + again;
            }
        }
        return std::nullopt;
    }

    void ConstraintSketch::RetakeTurnedOffsets(const std::set<size_t> &mine) {
        const auto in = [&](const std::string &id) { return mine.contains(book.byId.at(id).index); };
        for (auto &[name, info] : offsetInfos) {
            if (!std::all_of(info.sources.begin(), info.sources.end(), in) || !std::all_of(info.elements.begin(), info.elements.end(), in))
                continue;
            if (!info.closed && info.towards.size() == 1)
                info.towards.front() = -info.towards.front();
            const sketch::OffsetChain source = info.Chain(book);
            const double d = s.values[s.elements[book.byId.at(name).index].Distance()];
            const sketch::OffsetSides sides = SidesOf(source, source.Edges(s), d, info.towards, info.forms, "Mirror");
            info.shape = sketch::ShapeOf(sides);
            info.pieces = sketch::SidePieces(sides);
            info.checked = OffsetState(name, info);
        }
    }

    std::string ConstraintSketch::ShapeChange(const cs::OffsetInfo &info, const std::set<std::tuple<int, size_t, int>> &now, const sketch::OffsetChain &source, double d) const {
        const size_t n = source.elements.size();
        const auto id = [&](size_t k) { return book.elementIds[source.elements[k % n]]; };
        const auto radius = [&](size_t k) { return s.values[s.elements[source.elements[k]].Radius()]; };
        for (const auto &[kind, k, side] : info.pieces)
            if (!now.contains({kind, k, side})) {
                if (kind == (int)OffsetPiece::Kind::Vertex)
                    return "the corner where " + id(k) + " starts would vanish";
                if (kind == (int)OffsetPiece::Kind::Arc && radius(k) <= d)
                    return "the offset of " + id(k) + " would vanish at d = " + SolvedText(d) + (d > radius(k) ? " > r = " : " = r = ") + SolvedText(radius(k));
                return "the offset of " + id(k) + " would vanish";
            }
        for (const auto &[kind, k, side] : now)
            if (!info.pieces.contains({kind, k, side})) {
                if (kind == (int)OffsetPiece::Kind::Vertex)
                    return "a corner would appear where " + id(k) + " starts";
                if (kind == (int)OffsetPiece::Kind::Arc && radius(k) > d)
                    return "the offset of " + id(k) + " would come back (r = " + SolvedText(radius(k)) + " > d = " + SolvedText(d) + ")";
                return "the offset of " + id(k) + " would come back";
            }
        return "its pieces would meet otherwise";
    }

    // setting the distance of an offset (dimension k) to v: not to 0 or less (its side is fixed), and not so far that one of
    // its arcs would vanish (the solve could not get there)
    std::optional<std::string> ConstraintSketch::OffsetDistanceProblem(size_t k, double value) const {
        const cs::Constraint &c = s.constraints[k];
        if ((c.kind != sketch::Kind::Distance && c.kind != sketch::Kind::Diameter) || c.refs.size() != 1 || !c.refs[0].IsOffset(s))
            return std::nullopt;
        const double v = c.kind == sketch::Kind::Diameter ? value / 2.0 : value;
        const std::string &name = book.elementIds[c.refs[0].element];
        const auto info = offsetInfos.find(name);
        const bool slot = c.kind == sketch::Kind::Diameter;
        const std::string again = "; make the offset again (Remove('" + name + "'), then Offset)";
        if (!(v > 0.0))
            return slot ? "the width of a slot is positive" : "the distance of an offset is positive, its side is fixed" + again + " for the other side";
        if (info == offsetInfos.end())
            return std::nullopt;
        for (const auto &[arc, sign] : info->second.rounds)
            if (const auto at = book.byId.find(arc); at != book.byId.end() && sign < 0) {
                const double r = s.values[s.elements[at->second.index].Radius()];
                if (r - v <= Tol(std::max(r, v)) && slot)
                    return "the slot's width must be below 2 r = " + NumberText(2.0 * r) + " of its spine arc (" + arc + ")";
                if (r - v <= Tol(std::max(r, v)))
                    return "Offset('" + name + "'): the offset of " + arc + " would vanish at d = " + NumberText(v) + (v > r ? " > r = " : " = r = ") +
                           NumberText(r) + again;
            }
        for (const auto &[curve, id] : info->second.curves)
            if (const std::optional<std::string> cusp = CurveCusp(curve, id, v))
                return "Offset('" + name + "'): " + *cusp + again;
        return std::nullopt;
    }

    std::optional<std::string> ConstraintSketch::KeyholeProblem(size_t k, double value) const {
        const cs::Constraint &c = s.constraints[k];
        const bool width = c.kind == sketch::Kind::Diameter && c.refs.size() == 1 && c.refs[0].IsOffset(s);
        const bool radius = c.kind == sketch::Kind::Radius && constraintIds[k].ends_with(".r");
        if (!width && !radius)
            return std::nullopt;
        const std::string name = width ? book.elementIds[c.refs[0].element] : constraintIds[k].substr(0, constraintIds[k].size() - 2);
        if (!book.byId.contains(name + ".hole"))
            return std::nullopt;
        const double w = width ? value : 2.0 * s.values[s.elements[book.byId.at(name).index].Distance()];
        const double r = width ? s.values[s.elements[book.byId.at(name + ".hole").index].Radius()] : value;
        return HoleProblem(w, r, s.elements[book.byId.at(name + ".spine").index].Length(s));
    }

    void ConstraintSketch::CheckNoOffsetFrom(const std::string &id, const std::string &part) const {
        for (const auto &[o, info] : offsetInfos)
            if (o != id && info.HasSource(part))
                throw std::invalid_argument("Remove('" + id + "'): the offset '" + o + "' is made from it; keep it as construction with sk('" +
                                            id + "').construction = True, or Remove('" + o + "') first");
    }

    // a piece of an offset goes alone; its ties and points hold it together
    void ConstraintSketch::CheckHoldsNoOffset(const std::string &part) const {
        const auto at = book.byId.find(part);
        const bool point = at != book.byId.end() && at->second.isElement && s.elements[at->second.index].kind == sketch::ElementKind::Point;
        for (const auto &[o, info] : offsetInfos)
            if (info.Owns(part) && (point || std::find(info.ties.begin(), info.ties.end(), part) != info.ties.end()))
                throw std::invalid_argument("Remove('" + part + "'): it holds the offset '" + o + "' together; Remove('" + o + "')");
    }

    std::optional<std::string> ConstraintSketch::OffsetWithoutPieces(const std::string &id) const {
        for (const auto &[o, info] : offsetInfos)
            if (id.starts_with(o + ".")) {
                for (size_t e = 0; e < s.elements.size(); ++e)
                    if (s.elements[e].kind != sketch::ElementKind::Point && book.elementIds[e].starts_with(o + "."))
                        return std::nullopt;
                return o;
            }
        return std::nullopt;
    }

    void ConstraintSketch::OffsetCurveCut(size_t a, const std::vector<size_t> &pieces) {
        for (auto &[name, info] : offsetInfos)
            for (size_t k = 0, n = info.curves.size(); k < n; ++k)
                if (info.curves[k].first == book.elementIds[a])
                    for (size_t p : pieces)
                        if (p != a)
                            info.curves.push_back({book.elementIds[p], info.curves[k].second});
        for (size_t c = 0; c < s.constraints.size(); ++c) {
            cs::Constraint &k = s.constraints[c];
            if (k.kind == sketch::Kind::Offset && k.variant == (int)sketch::OffsetTie::Along && k.refs[1].element == a)
                if (const std::optional<sketch::Ref> moved = s.PieceEndAt(pieces, k.refs[0].PointAt(s)); moved && moved->element != a) {
                    k.refs[1].element = moved->element;
                    s.Refreeze(c);
                }
        }
    }

    size_t ConstraintSketch::OffsetElement(double ad, const Options &opt, bool slot) {
        offsetDof = s.Analyze().dof;
        AddElement(s.AddOffset(ad), "Offset", {opt.id, {}, true});
        offsetFirstTie = s.constraints.size();
        if (slot)
            AddUnsolved({sketch::Kind::Diameter, {{s.elements.size() - 1}}, 2.0 * ad}, book.elementIds.back() + ".w");
        else
            AddUnsolved({sketch::Kind::Distance, {{s.elements.size() - 1}}, ad}, book.elementIds.back() + ".d");
        return s.elements.size() - 1;
    }

    void ConstraintSketch::TieOffset(sketch::OffsetTie how, std::vector<sketch::Ref> refs, double sign, const std::string &id) {
        cs::Constraint c{sketch::Kind::Offset, std::move(refs)};
        c.variant = (int)how;
        c.sign = sign;
        AddUnsolved(std::move(c), id);
    }

    // the ties solved once all are in: they take no degree of freedom, and the offset curves have no cusp and do not
    // cross themselves; then what the offset was made as, for the checks after later changes
    void ConstraintSketch::OffsetUnsolved(const std::string &call, const sketch::OffsetChain &source, const EdgeList &edges, double ad, const std::vector<int> &towards) const {
        if (const std::optional<std::string> cusp = source.Cusp(s, edges, ad, towards, book.elementIds))
            throw std::invalid_argument(call + ": " + *cusp);
        if (!conflicts.empty()) {
            std::vector<std::string> suspended;
            for (const auto &[id, with] : conflicts)
                suspended.push_back(id);
            throw std::invalid_argument(call + ": the sketch is over-constrained (" + Joined(suspended) +
                                        (suspended.size() > 1 ? " are" : " is") + " suspended, contradicting the other "
                                        "constraints); an offset needs constraints that hold: remove or change the "
                                        "suspended ones first");
        }
        throw std::logic_error(call + ": internal error, the offset's ties have no solution");
    }

    cs::OffsetInfo ConstraintSketch::OffsetRecord(const std::vector<std::string> &made, const std::vector<std::string> &tieIds, const sketch::OffsetChain &source,
                                                  const std::vector<int> &towards, const OffsetForms &forms, const sketch::OffsetSides &sides,
                                                  const std::vector<std::pair<std::string, std::string>> &curveIds) const {
        const auto element = [&](size_t k) { return source.elements[k]; };
        cs::OffsetInfo info{{}, made, tieIds, source.forward, source.closed, towards, sketch::ShapeOf(sides), sketch::SidePieces(sides), {}, {}, curveIds, forms};
        for (size_t e : source.elements)
            info.sources.push_back(book.elementIds[e]);
        for (const auto &[fixed, loops] : sides)
            for (const OffsetLoop &loop : loops)
                for (const OffsetPiece &p : loop.pieces)
                    if (p.kind == OffsetPiece::Kind::Arc)
                        info.rounds.push_back({book.elementIds[element(p.k)], p.r > s.values[s.elements[element(p.k)].Radius()] ? 1 : -1});
        if (s.elements[element(0)].kind == sketch::ElementKind::Circle)
            for (int t : towards)
                info.rounds.push_back({book.elementIds[element(0)], t});
        return info;
    }

    void ConstraintSketch::OffsetMade(const std::string &name, const std::vector<std::string> &made, const std::vector<std::string> &tieIds, const sketch::OffsetChain &source,
                                      const EdgeList &edges, double ad, const std::vector<int> &towards, const OffsetForms &forms,
                                      const sketch::OffsetSides &sides,
                                      const std::vector<std::pair<std::string, std::string>> &curveIds, const std::string &call) {
        if (!s.Solve().solved)
            OffsetUnsolved(call, source, edges, ad, towards);
        analysis = s.Analyze();
        amplified = analysis->amplification;
        const std::vector<char> redundant = s.Redundancies(*analysis);
        if (analysis->dof != offsetDof || std::any_of(redundant.begin() + (std::ptrdiff_t)offsetFirstTie, redundant.end(), [](char x) { return x != 0; }))
            throw std::logic_error(call + ": internal error, the offset's ties are not independent");
        for (const auto &[curve, id] : curveIds) {
            std::optional<std::string> wrong = CurveCusp(curve, id, ad);
            if (!wrong)
                wrong = CurveCrossing(curve, id);
            if (wrong)
                throw std::invalid_argument(call + ": " + *wrong);
        }
        Sync();
        cs::OffsetInfo info = OffsetRecord(made, tieIds, source, towards, forms, sides, curveIds);
        info.checked = OffsetState(name, info);
        offsetInfos[name] = std::move(info);
    }

}
