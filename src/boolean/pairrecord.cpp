// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/boolean/pairrecord.hpp"

namespace planex2d {

    VertexMerges::VertexMerges(const std::vector<Identified> &ids) {
        for (const Identified &d : ids) {
            pts.push_back(d.from);
            pts.push_back(d.to);
        }
        std::sort(pts.begin(), pts.end(), VertexLess);
        pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
        parent.resize(pts.size());
        for (size_t i = 0; i < pts.size(); ++i)
            parent[i] = i;
        for (const Identified &d : ids) {
            const size_t a = Root(Index(d.from));
            const size_t b = Root(Index(d.to));
            if (a != b)
                parent[a] = b;
        }
        for (size_t i = 0; i < pts.size(); ++i)
            parent[i] = Root(i);
    }

    void SideLinks::Link(uint32_t a, uint32_t b, unsigned char p) {
        auto [ra, pa] = Find(a);
        auto [rb, pb] = Find(b);
        if (ra == rb) {
            if ((pa ^ pb) != p)
                conflict[ra] = 1;
            return;
        }
        if (size[ra] > size[rb])
            std::swap(ra, rb);
        parent[ra] = rb;
        parity[ra] = (unsigned char)(pa ^ pb ^ p);
        size[rb] += size[ra];
        conflict[rb] |= conflict[ra];
    }

    // the pieces of one operand at a vertex lie in the sectors between the other operand's pieces, neighbouring sectors
    // on opposite sides of it; a piece whose direction rounding cannot tell from one of the other's stays unlinked
    void LinkBySectors(const std::vector<Spoke> &own, const std::vector<Spoke> &other, SideLinks &links) {
        if (other.size() % 2 != 0)
            return;
        bool first = true;
        uint32_t ref = 0;
        size_t refSector = 0;
        for (const Spoke &s : own) {
            size_t below = 0;
            bool sure = true;
            for (const Spoke &o : other) {
                double d = std::abs(s.angle - o.angle);
                d = std::min(d, 2.0 * std::numbers::pi - d);
                sure = sure && d > s.spread + o.spread;
                below += o.angle < s.angle;
            }
            if (!sure)
                continue;
            if (first) {
                ref = s.piece;
                refSector = below;
                first = false;
            } else {
                links.Link(s.piece, ref, (unsigned char)((below ^ refSector) & 1));
            }
        }
    }

    // the pieces linked across the vertices: the side changes only at the crossings the cut recorded where no edge ends
    SideLinks SideLinksOf(const Wire &c, const std::vector<CutState> &state, const CutRecord &rec, const VertexMerges &ids, double tol) {
        const size_t n = c.Size();
        std::vector<std::pair<Vertex, uint32_t>> ends;
        for (size_t k = 0; k < n; ++k)
            if (state[k].IsToCut())
                std::visit([&](const auto &x) {
                    ends.emplace_back(x.Start(), (uint32_t)k);
                    ends.emplace_back(x.End(), (uint32_t)k);
                }, c[k]);
        auto byVertex = [](const auto &p, const auto &q) { return VertexLess(p.first, q.first); };
        std::sort(ends.begin(), ends.end(), byVertex);
        std::vector<std::pair<Vertex, std::optional<PairKind>>> through;
        for (const PairEvent &e : rec.events)
            if (e.kind != PairKind::Overlap)
                through.emplace_back(ids.Of(e.at), e.la == Location::Inside && e.lb == Location::Inside ? std::optional(e.kind) : std::nullopt);
        std::sort(through.begin(), through.end(), byVertex);

        SideLinks links(n);
        std::vector<uint32_t> own[2];
        std::vector<Spoke> spokes[2];
        for (size_t i = 0; i < ends.size();) {
            const Vertex v = ends[i].first;
            size_t j = i;
            bool shared = false;
            own[0].clear();
            own[1].clear();
            for (; j < ends.size() && ends[j].first == v; ++j) {
                const unsigned char m = state[ends[j].second].mask;
                shared = shared || m == 3;
                if (m == 1 || m == 2)
                    own[m - 1].push_back(ends[j].second);
            }
            i = j;
            if (shared)
                continue;
            if (own[0].empty() || own[1].empty()) {
                for (const std::vector<uint32_t> &o : own)
                    for (size_t k = 1; k < o.size(); ++k)
                        links.Link(o[k], o[0], 0);
                continue;
            }
            const auto at = std::equal_range(through.begin(), through.end(), std::pair{v, std::optional<PairKind>()}, byVertex);
            const bool known = at.first != at.second && at.first->second &&
                               std::all_of(at.first, at.second, [&](const auto &e) { return e.second == at.first->second; });
            if (known && own[0].size() == 2 && own[1].size() == 2) {
                const unsigned char p = *at.first->second == PairKind::Crossing ? 1 : 0;
                links.Link(own[0][1], own[0][0], p);
                links.Link(own[1][1], own[1][0], p);
                continue;
            }
            for (size_t o = 0; o < 2; ++o) {
                spokes[o].clear();
                for (uint32_t k : own[o])
                    spokes[o].push_back(std::visit([&](const auto &x) { return SpokeOf(x, v, tol, k); }, c[k]));
            }
            LinkBySectors(spokes[0], spokes[1], links);
            LinkBySectors(spokes[1], spokes[0], links);
        }
        return links;
    }

}
