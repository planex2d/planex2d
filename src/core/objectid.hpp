// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <algorithm>
#include <charconv>
#include <map>
#include <numeric>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "core/config.hpp"

namespace planex2d {

    inline std::map<std::string, size_t> &IdCounters() { return config::state->id_counters; }
    inline std::string NextId(const std::string &kind) { return kind + "." + std::to_string(++IdCounters()[kind]); }
    // an id "kind.N" read in: the auto ids of its kind go on after N
    inline void AdvanceIdCounter(const std::string &kind, const std::string &id) {
        if (id.size() <= kind.size() + 1 || id.compare(0, kind.size(), kind) != 0 || id[kind.size()] != '.')
            return;
        size_t n = 0;
        const char *end = id.data() + id.size();
        const auto [p, error] = std::from_chars(id.data() + kind.size() + 1, end, n);
        if (error == std::errc() && p == end)
            IdCounters()[kind] = std::max(IdCounters()[kind], n);
    }

    // an object's id: the kind that made it and its number, given on first use or set. A copy is another object of
    // the same kind; an assignment changes the target, which stays the object it was
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

    // the number of single characters to insert, delete or change to turn a into b (Levenshtein)
    inline size_t EditDistance(const std::string &a, const std::string &b) {
        std::vector<size_t> row(b.size() + 1);
        std::iota(row.begin(), row.end(), size_t{0});
        for (size_t i = 1; i <= a.size(); ++i) {
            size_t diagonal = row[0];
            row[0] = i;
            for (size_t j = 1; j <= b.size(); ++j) {
                const size_t above = row[j];
                row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] == b[j - 1] ? 0 : 1)});
                diagonal = above;
            }
        }
        return row[b.size()];
    }

    // " (did you mean 'Line.2'?)": the ids closest to text, at most three, or nothing when none is close: a third of
    // its characters changed at most (at least one), never all of them
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

    // a user-given id: an ASCII letter, then ASCII letters, digits, _ and single inner spaces, so that every user id can
    // be named in an expression; the dot separates scopes and references (Line.1.end), automatic ids keep theirs
    inline void CheckUserId(const std::string &id) {
        if (id.empty())
            throw std::invalid_argument("an id must not be empty");
        if (id == "h" || id == "v" || id == "t")
            throw std::invalid_argument("id '" + id + "' is reserved: h, v and t are a curve's coordinates and parameter (Curve), no id may be one "
                                        "of them");
        const auto letter = [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); };
        if (!letter(id[0]))
            throw std::invalid_argument("id '" + id + "' must start with a letter (a to z, A to Z)");
        if (const size_t at = id.find_first_of("+-*/^()=,%"); at != std::string::npos)
            throw std::invalid_argument("id '" + id + "' contains '" + id[at] + "': ids may not contain + - * / ^ ( ) = , % (the "
                                        "operators of expressions)");
        for (size_t i = 1; i < id.size(); ++i) {
            const unsigned char c = id[i];
            if (letter(c) || (c >= '0' && c <= '9') || c == '_' || (c == ' ' && id[i - 1] != ' ' && i + 1 < id.size()))
                continue;
            const std::string what = c >= 0x80  ? "a non-ASCII character (Länge is Laenge, φ is phi)"
                                     : c == ' ' ? "a trailing or double space"
                                                : "'" + std::string(1, char(c)) + "'";
            throw std::invalid_argument("id '" + id + "' contains " + what + ": an id is an ASCII letter, then ASCII letters, "
                                        "digits, _ and single inner spaces" +
                                        (c == '.' ? std::string(" (the dot separates scopes and references: Line.1.end)") : ""));
        }
    }

}
