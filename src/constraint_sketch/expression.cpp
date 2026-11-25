// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#include "planex/constraint_sketch/expression.hpp"

namespace planex2d::cs {

    size_t Arguments(Op op) {
        switch (op) {
        case Op::Number:
        case Op::Value:
            return 0;
        case Op::Add:
        case Op::Sub:
        case Op::Mul:
        case Op::Div:
        case Op::Pow:
        case Op::Min:
        case Op::Max:
        case Op::Atan2:
            return 2;
        default:
            return 1;
        }
    }

}
