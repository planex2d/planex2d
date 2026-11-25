// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/sketch/idbook.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

#include "planex/core/objectid.hpp"
#include "planex/sketch/ref.hpp"

namespace planex2d::sketch {

    bool IdBook::IsElement(const std::string &id) const {
        const auto it = byId.find(id);
        return it != byId.end() && it->second.isElement;
    }

    std::optional<size_t> IdBook::ElementAt(const std::string &id) const {
        const auto it = byId.find(id);
        return it != byId.end() && it->second.isElement ? std::optional(it->second.index) : std::nullopt;
    }

    std::optional<size_t> IdBook::ConstraintAt(const std::string &id) const {
        const auto it = byId.find(id);
        return it != byId.end() && !it->second.isElement ? std::optional(it->second.index) : std::nullopt;
    }

    bool IdBook::IsGroup(const std::string &id, std::initializer_list<std::string_view> kinds) const {
        for (const auto &[kind, group] : groups)
            if (group == id && std::find(kinds.begin(), kinds.end(), kind) != kinds.end())
                return true;
        return false;
    }

    std::string IdBook::FreshId(const std::string &prefix) {
        size_t &k = counters[prefix];
        while (true) {
            const std::string id = prefix + "." + std::to_string(++k);
            if (!byId.contains(id) && !wires.contains(id))
                return id;
        }
    }

    std::string IdBook::NewId(const std::string &prefix, const std::optional<std::string> &given) {
        if (given) {
            CheckId(*given);
            givenIds.insert(*given);
            return *given;
        }
        return FreshId(idScope.empty() ? prefix : idScope + "." + prefix);
    }

    // an id not yet used, nor read as a point of an element; a user-given one checked by CheckUserId first. After a
    // wire's id a place word is a name (RectangleC.1.center): wires and elements share the ids, so it never names
    // an element's place
    void IdBook::CheckId(const std::string &id) const {
        if (id.empty())
            throw std::invalid_argument("an id must not be empty");
        if (byId.contains(id) || wires.contains(id))
            throw std::invalid_argument("id '" + id + "' is taken");
        for (const std::string part : {".start", ".end", ".center", ".mid"})
            if (id.ends_with(part) && !wires.contains(id.substr(0, id.size() - part.size())))
                throw std::invalid_argument("id '" + id + "' ends like a point of an element (" + part + ")");
        if (const size_t dot = id.rfind('.'); dot != std::string::npos && SplinePart(id.substr(dot + 1)))
            throw std::invalid_argument("id '" + id + "' ends like a point of a spline (.p0, .t0)");
    }

    std::string IdBook::DidYouMean(const std::string &text) const {
        std::vector<std::string> ids;
        for (const auto &[id, at] : byId)
            ids.push_back(id);
        for (const auto &[id, w] : wires)
            ids.push_back(id);
        return planex2d::DidYouMean(text, ids);
    }

    void IdBook::NoElement(const std::string &text) const {
        if (const auto w = wires.find(text); w != wires.end() && !w->second.empty())
            throw std::invalid_argument("'" + text + "' is a wire of " + std::to_string(w->second.size()) +
                                        " elements, not one element; name one of them, such as '" + w->second.front() + "'");
        if (const auto under = idsUnder.find(text); under != idsUnder.end()) {
            std::vector<std::string> members;
            for (const std::string &x : under->second)
                if (const auto at = byId.find(x); at != byId.end() && at->second.isElement && elementIds[at->second.index] == x)
                    members.push_back(x);
            if (!members.empty())
                throw std::invalid_argument("'" + text + "' is a group of " + std::to_string(members.size()) +
                                            " elements, not one element; name one of them, such as '" + members.front() + "'");
        }
        throw std::invalid_argument("no element '" + text + "'" + DidYouMean(text));
    }

    // a negative number in a part (Chain.-1, LinearPattern.1.-1.hole) counts from the end
    std::string IdBook::FromEnd(const std::string &text) const {
        if (text.find(".-") == std::string::npos)
            return text;
        std::string out;
        size_t at = 0;
        while (at <= text.size()) {
            const size_t dot = std::min(text.find('.', at), text.size());
            std::string part = text.substr(at, dot - at);
            if (at > 0 && part.size() > 1 && part[0] == '-' && part.find_first_not_of("0123456789", 1) == std::string::npos) {
                std::vector<size_t> numbers;
                const auto add = [&](const std::string &id) {
                    if (!id.starts_with(out + "."))
                        return;
                    const std::string next = id.substr(out.size() + 1, id.find('.', out.size() + 1) - out.size() - 1);
                    if (!next.empty() && next.find_first_not_of("0123456789") == std::string::npos)
                        numbers.push_back((size_t)std::stoull(next));
                };
                for (const auto &[id, element] : byId)
                    add(id);
                for (const auto &[id, elements] : wires)
                    add(id);
                std::sort(numbers.begin(), numbers.end());
                numbers.erase(std::unique(numbers.begin(), numbers.end()), numbers.end());
                const size_t k = (size_t)std::stoull(part.substr(1));
                if (k == 0 || k > numbers.size())
                    throw std::invalid_argument("'" + text + "': there are " + std::to_string(numbers.size()) + " numbers under '" + out +
                                                "' (-1 is the last)");
                part = std::to_string(numbers[numbers.size() - k]);
            }
            out += (at > 0 ? "." : "") + part;
            at = dot + 1;
        }
        return out;
    }

    std::vector<std::pair<std::string, std::vector<std::string>>> IdBook::CallGroups() const {
        std::vector<std::pair<std::string, std::vector<std::string>>> out;
        for (const auto &[kind, id] : groups) {
            if (!wires.contains(id) && !byId.contains(id))
                continue;
            auto it = std::find_if(out.begin(), out.end(), [&](const auto &g) { return g.first == kind; });
            if (it == out.end())
                it = out.insert(out.end(), {kind, {}});
            if (std::find(it->second.begin(), it->second.end(), id) == it->second.end())
                it->second.push_back(id);
        }
        return out;
    }

    void IdBook::ListUnder(const std::string &id, bool add) {
        for (size_t dot = id.find('.'); dot != std::string::npos; dot = id.find('.', dot + 1)) {
            const std::string parent = id.substr(0, dot);
            if (add) {
                idsUnder[parent].insert(id);
                continue;
            }
            std::set<std::string> &under = idsUnder[parent];
            under.erase(id);
            if (under.empty())
                idsUnder.erase(parent);
        }
    }

    void IdBook::Rename(size_t e, const std::string &id) {
        if (elementIds[e] == id)
            return;
        CheckId(id);
        byId.erase(elementIds[e]);
        ListUnder(elementIds[e], false);
        ListUnder(id, true);
        elementIds[e] = id;
        byId[id] = {true, e};
    }

    void IdBook::RenamePoint(size_t e, const std::string &id) {
        if (elementIds[e] == id)
            return;
        CheckUserId(id);
        CheckId(id);
        for (auto &[from, to] : renamed)
            if (to == elementIds[e])
                to = id;
        renamed[elementIds[e]] = id;
        givenIds.insert(id);
        ListUnder(elementIds[e], false);
        ListUnder(id, true);
        elementIds[e] = id;
        byId[id] = {true, e};
    }

    void IdBook::RenameGroup(const std::string &from, const std::string &to) {
        for (auto &[kind, id] : groups)
            if (id == from)
                id = to;
    }

    void IdBook::NameWire(const std::string &id, const std::vector<size_t> &elements, const std::string &call) {
        if (elements.empty())
            throw std::invalid_argument(call + "'" + id + "': no chain is drawn to name");
        CheckId(id);
        std::vector<std::string> &w = wires[id];
        for (size_t e : elements)
            w.push_back(elementIds[e]);
        wireLog.push_back({id, std::string()});
    }

    void IdBook::RenameWire(const std::string &from, const std::string &id) {
        CheckUserId(id);
        CheckId(id);
        auto node = wires.extract(from);
        node.key() = id;
        wires.insert(std::move(node));
        RenameGroup(from, id);
        wireLog.push_back({id, from});
    }

    void IdBook::UndoWires(size_t kept) {
        while (wireLog.size() > kept) {
            const auto [id, before] = wireLog.back();
            wireLog.pop_back();
            if (before.empty())
                wires.erase(id);
            else if (auto node = wires.extract(id); !node.empty()) {
                node.key() = before;
                wires.insert(std::move(node));
                RenameGroup(id, before);
            }
        }
    }

    void IdBook::CopyWires(const std::vector<std::string> &ids, const Copies &made, const std::string &prefix) {
        for (const std::string &id : ids) {
            const auto w = wires.find(id);
            if (w == wires.end())
                continue;
            std::vector<size_t> copies;
            for (const std::string &e : w->second)
                for (const auto &[o, c] : made.elements)
                    if (elementIds[o] == e)
                        copies.push_back(c);
            if (!copies.empty())
                NameWire(prefix + "." + id, copies, "copy: ");
        }
    }

    void IdBook::JoinWires(size_t a, size_t b, size_t made) {
        for (auto &[name, ids] : wires) {
            const auto ia = std::find(ids.begin(), ids.end(), elementIds[a]), ib = std::find(ids.begin(), ids.end(), elementIds[b]);
            if (ia == ids.end() || ib == ids.end())
                continue;
            ids.insert(ia + 1 == ib ? ib : ib + 1 == ia ? ia : ids.end(), elementIds[made]);
        }
    }

    void IdBook::Reindex() {
        byId.clear();
        idsUnder.clear();
        for (size_t e = 0; e < elementIds.size(); ++e) {
            ListUnder(elementIds[e], true);
            byId[elementIds[e]] = {true, e};
        }
        for (const auto &[old, id] : renamed)
            if (const auto it = byId.find(id); it != byId.end()) {
                const IdTarget at = it->second;
                byId[old] = at;
            }
    }

    std::string IdBook::FromEndIn(const std::string &text) const {
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
            out += text.substr(done, start - done) + FromEnd(text.substr(start, end - start));
            done = end;
        }
        return out + text.substr(done);
    }

}
