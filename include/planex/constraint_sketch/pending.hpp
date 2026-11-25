// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace planex2d::cs {

    struct Pending {
        std::vector<std::pair<std::string, double>> values;
        std::vector<std::pair<std::string, size_t>> counts;
        std::vector<std::pair<std::string, bool>> flips;

        std::optional<double> Value(const std::string &id) const { return Waiting(values, id); }
        std::optional<size_t> Count(const std::string &id) const { return Waiting(counts, id); }
        std::optional<bool> Flip(const std::string &id) const { return Waiting(flips, id); }
        void SetCount(const std::string &id, size_t n) {
            Erase(counts, id);
            counts.push_back({id, n});
        }
        void SetFlip(const std::string &id, bool reverse) {
            Erase(flips, id);
            flips.push_back({id, reverse});
        }
        void EraseValue(const std::string &id) { Erase(values, id); }
        void Forget(const std::string &element) {
            Erase(flips, element);
            Erase(counts, element + ".count");
            Erase(counts, element + ".count2");
        }
        bool IsEmpty() const { return values.empty() && counts.empty() && flips.empty(); }
        void Clear() {
            values.clear();
            counts.clear();
            flips.clear();
        }

    private:
        template <class T>
        static std::optional<T> Waiting(const std::vector<std::pair<std::string, T>> &list, const std::string &id) {
            for (const auto &[i, v] : list)
                if (i == id)
                    return v;
            return std::nullopt;
        }
        template <class T>
        static void Erase(std::vector<std::pair<std::string, T>> &list, const std::string &id) {
            std::erase_if(list, [&](const auto &x) { return x.first == id; });
        }
    };

}
