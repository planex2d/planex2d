// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <initializer_list>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "planex/sketch/copies.hpp"
#include "planex/sketch/options.hpp"

namespace planex2d::sketch {

    struct IdBook {
        std::vector<std::string> elementIds;
        std::unordered_map<std::string, IdTarget> byId;
        std::unordered_map<std::string, size_t> counters;
        std::string idScope;
        std::map<std::string, std::vector<std::string>> wires;
        // (the wire's id, the id it had before; empty: it was made), for the undo
        std::vector<std::pair<std::string, std::string>> wireLog;
        std::set<std::string> givenIds;
        std::map<std::string, std::string> renamed;
        // (kind, id) of what one call made
        std::vector<std::pair<std::string, std::string>> groups;
        std::unordered_map<std::string, std::set<std::string>> idsUnder;

        bool IsElement(const std::string &id) const;
        std::optional<size_t> ElementAt(const std::string &id) const;
        std::optional<size_t> ConstraintAt(const std::string &id) const;
        bool IsWire(const std::string &id) const { return wires.contains(id); }
        bool IsGroup(const std::string &id, std::initializer_list<std::string_view> kinds) const;
        bool IsSlot(const std::string &id) const { return byId.contains(id + ".spine") && IsGroup(id, {"Slot", "ArcSlot", "Keyhole"}); }
        bool IsCentredRectangle(const std::string &id) const { return byId.contains(id + ".center") && IsGroup(id, {"RectangleC"}); }
        bool IsPolygon(const std::string &id) const { return byId.contains(id + ".circle") && IsGroup(id, {"Polygon", "Hexagon"}); }
        const std::string &LastId() const { return elementIds.back(); }

        std::string FreshId(const std::string &prefix);
        std::string NewId(const std::string &prefix, const std::optional<std::string> &given);
        void CheckId(const std::string &id) const;
        std::string DidYouMean(const std::string &text) const;
        [[noreturn]] void NoElement(const std::string &text) const;
        std::string FromEnd(const std::string &text) const;
        std::string FromEndIn(const std::string &text) const;
        std::vector<std::pair<std::string, std::vector<std::string>>> CallGroups() const;

        void ListUnder(const std::string &id, bool add);
        void Rename(size_t e, const std::string &id);
        void RenamePoint(size_t e, const std::string &id);
        void RenameGroup(const std::string &from, const std::string &to);
        void NameWire(const std::string &id, const std::vector<size_t> &elements, const std::string &call);
        void RenameWire(const std::string &from, const std::string &id);
        void UndoWires(size_t kept);
        void CopyWires(const std::vector<std::string> &ids, const Copies &made, const std::string &prefix);
        void JoinWires(size_t a, size_t b, size_t made);
        void Reindex();
    };

}
