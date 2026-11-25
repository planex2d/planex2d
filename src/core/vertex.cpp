// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/core/vertex.hpp"

#include <clocale>
#include <cstdlib>

namespace planex2d {

    std::optional<double> NumberOf(std::string_view text) {
        double x = 0.0;
#if defined(__cpp_lib_to_chars)
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), x);
        if (error != std::errc() || end != text.data() + text.size())
            return std::nullopt;
#else
        // libc++ without floating-point from_chars: strtod, its decimal point made the locale's
        std::string local(text);
        for (char &c : local)
            if (c == '.')
                c = *std::localeconv()->decimal_point;
        char *end = nullptr;
        x = std::strtod(local.c_str(), &end);
        if (local.empty() || end != local.c_str() + local.size())
            return std::nullopt;
#endif
        // from_chars and strtod read the words inf and nan as numbers
        if (!std::isfinite(x))
            return std::nullopt;
        return x;
    }

    Vertex Rotated(const Vertex &p, const Vertex &c, double deg) {
        // multiples of 90 degrees are exact: cos(90 deg) in floating point is 6e-17
        const double quarters = std::fmod(deg, 360.0) / 90.0;
        if (quarters == std::round(quarters)) {
            constexpr double cosines[4] = {1.0, 0.0, -1.0, 0.0}, sines[4] = {0.0, 1.0, 0.0, -1.0};
            const size_t k = (size_t)((int)std::round(quarters) + 4) % 4;
            return Rotated(p, c, cosines[k], sines[k]);
        }
        const double rad = Radians(deg);
        return Rotated(p, c, std::cos(rad), std::sin(rad));
    }

}
