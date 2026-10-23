// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <string>
#include <utility>

namespace planex2d::sketch {

    // the id scope a block sets, set back when it ends
    struct Scoped {
        std::string &current;
        std::string outer;
        ~Scoped() { current = std::move(outer); }
    };

    // f changes all of self or, when it raises, nothing; Self is the most derived type, so a copy keeps all of it
    template <class Self, class F>
    void Undoable(Self &self, F f) {
        const Self saved = self;
        try {
            f();
        } catch (...) {
            self = saved;
            throw;
        }
    }

}
