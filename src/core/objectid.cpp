// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/core/objectid.hpp"

#include <numeric>
#include <stdexcept>

namespace planex2d {

    size_t EditDistance(const std::string &a, const std::string &b) {
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

    void CheckUserId(const std::string &id) {
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
