// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "planex/core/config.hpp"

namespace planex2d {

    inline std::map<std::string, size_t> &IdCounters() { return config::state->id_counters; }
    inline std::string NextId(const std::string &kind) { return kind + "." + std::to_string(++IdCounters()[kind]); }

    // a copy is another object of the same kind; an assignment changes the target, which stays the object it was
    struct ObjectId {
        std::string kind, id;
        explicit ObjectId(std::string k = "") : kind(std::move(k)) {}
        ObjectId(const ObjectId &o) : kind(o.kind) {}
        ObjectId(ObjectId &&) noexcept = default;
        ObjectId &operator=(const ObjectId &) { return *this; }
        ObjectId &operator=(ObjectId &&) noexcept { return *this; }
        const std::string &Get() {
            if (id.empty())
                id = NextId(kind);
            return id;
        }
    };

    size_t EditDistance(const std::string &a, const std::string &b);

    // close: a third of the characters changed at most (at least one), never all of them; the three closest are named
    template <class Ids>
    std::string DidYouMean(const std::string &text, const Ids &ids) {
        std::vector<std::pair<size_t, std::string>> close;
        for (const std::string &id : ids)
            if (const size_t d = EditDistance(text, id); d <= std::max<size_t>(1, text.size() / 3) && d < text.size())
                close.emplace_back(d, id);
        if (close.empty())
            return "";
        std::ranges::sort(close);
        std::string out = " (did you mean ";
        for (size_t i = 0; i < std::min<size_t>(close.size(), 3); ++i)
            out += (i ? ", '" : "'") + close[i].second + "'";
        return out + "?)";
    }

    inline std::string Joined(const std::vector<std::string> &ids) {
        std::string out;
        for (const std::string &id : ids)
            out += (out.empty() ? "" : ", ") + id;
        return out;
    }

    void CheckUserId(const std::string &id);

}
