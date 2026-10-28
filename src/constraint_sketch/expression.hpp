// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

#include "constraint_sketch/dual.hpp"

namespace planex2d::cs {

    // the operations of an expression; the trigonometric functions take and give angles in degrees, the hyperbolic ones
    // plain numbers
    enum class Op : unsigned char {
        Number, Value, Add, Sub, Mul, Div, Pow, Neg, Sqrt, Abs, Min, Max, Sin, Cos, Tan, Asin, Acos, Atan2,
        Exp, Log, Sinh, Cosh, Tanh, Asinh, Acosh, Atanh
    };
    constexpr Op lastOp = Op::Atanh;

    inline const char *OpName(Op op) {
        constexpr const char *names[] = {"num", "val", "add", "sub", "mul", "div", "pow", "neg", "sqrt", "abs", "min", "max", "sin",
                                         "cos", "tan", "asin", "acos", "atan2", "exp", "log", "sinh", "cosh", "tanh", "asinh",
                                         "acosh", "atanh"};
        return names[(size_t)op];
    }
    // how many values an operation takes
    inline size_t Arguments(Op op) {
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

    // an expression is a list of nodes in postfix order: Number gives number, Value the unknown times number (to the
    // dimension's unit: 180 / pi for an angle kept in radians), an operation replaces its arguments by its result
    struct Node {
        Op op = Op::Number;
        double number = 0.0;
        size_t unknown = 0;
    };

    // every operation has its arguments and one value is left
    inline void CheckNodes(const std::vector<Node> &nodes) {
        size_t depth = 0;
        for (const Node &n : nodes) {
            if (depth < Arguments(n.op))
                throw std::invalid_argument(std::string("expression: ") + OpName(n.op) + " lacks an argument");
            depth = depth - Arguments(n.op) + 1;
        }
        if (depth != 1)
            throw std::invalid_argument("expression: the nodes leave " + std::to_string(depth) + " values, not one");
    }

    // a number in a message
    inline std::string Shown(double v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", v);
        return buf;
    }

    // the value of an expression, leaf(u) the value of the unknown u: a double, or a Dual with its derivatives. Outside an
    // operation's domain the value is NaN; error gets the first such place, or one where the value is fine but a
    // derivative is not (sqrt of 0, asin of 1, ...), which the solver cannot use either
    template <class T, class Leaf>
    T Evaluated(const std::vector<Node> &nodes, Leaf leaf, std::string *error = nullptr) {
        thread_local std::vector<T> stack;
        stack.clear();
        constexpr double degree = std::numbers::pi / 180.0;
        const auto note = [&](const std::string &what) {
            if (error && error->empty())
                *error = what;
        };
        const auto fail = [&](const std::string &what) {
            note(what);
            return T(std::numeric_limits<double>::quiet_NaN());
        };
        const auto steep = [&](const T &x) { return error && IsVarying(x); };
        const auto pop = [&] {
            T x = std::move(stack.back());
            stack.pop_back();
            return x;
        };
        for (const Node &n : nodes) {
            if (n.op == Op::Number) {
                stack.push_back(T(n.number));
                continue;
            }
            if (n.op == Op::Value) {
                stack.push_back(leaf(n.unknown) * T(n.number));
                continue;
            }
            const T b = pop(), a = Arguments(n.op) == 2 ? pop() : b;
            const double x = ValueOf(a), y = ValueOf(b);
            T r{};
            switch (n.op) {
            case Op::Add:
                r = a + b;
                break;
            case Op::Sub:
                r = a - b;
                break;
            case Op::Mul:
                r = a * b;
                break;
            case Op::Div:
                r = y == 0.0 ? fail("division by zero") : a / b;
                break;
            case Op::Pow:
                if (x < 0.0 && (y != std::trunc(y) || IsVarying(b)))
                    r = fail("a negative number (" + Shown(x) + ") to the power " + (IsVarying(b) ? "of a dimension" : Shown(y)));
                else if (x == 0.0 && y < 0.0)
                    r = fail("0 to a negative power");
                else {
                    if (x == 0.0 && ((y < 1.0 && y != 0.0 && steep(a)) || steep(b)))
                        note("0 to the power " + Shown(y) + " has no derivative there, the solver cannot use it");
                    r = Pow(a, b);
                }
                break;
            case Op::Neg:
                r = -a;
                break;
            case Op::Sqrt:
                if (x < 0.0)
                    r = fail("sqrt of a negative number (" + Shown(x) + ")");
                else {
                    if (x == 0.0 && steep(a))
                        note("sqrt of 0 has no derivative there, the solver cannot use it; write the equation without the root");
                    r = Sqrt(a);
                }
                break;
            case Op::Abs:
                r = Abs(a);
                break;
            case Op::Min:
                r = Min(a, b);
                break;
            case Op::Max:
                r = Max(a, b);
                break;
            case Op::Sin:
                r = Sin(a * T(degree));
                break;
            case Op::Cos:
                r = Cos(a * T(degree));
                break;
            case Op::Tan:
                r = std::abs(std::remainder(x, 180.0)) == 90.0 ? fail("tan of " + Shown(x) + " degrees") : Tan(a * T(degree));
                break;
            case Op::Asin:
            case Op::Acos: {
                const std::string name = n.op == Op::Asin ? "asin" : "acos";
                if (!(std::abs(x) <= 1.0))
                    r = fail(name + " of " + Shown(x) + ", outside [-1, 1]");
                else {
                    if (std::abs(x) == 1.0 && steep(a))
                        note(name + " of " + Shown(x) + " has no derivative there, the solver cannot use it; write the equation with " +
                             name.substr(1) + " instead");
                    r = (n.op == Op::Asin ? Asin(a) : Acos(a)) * T(1.0 / degree);
                }
                break;
            }
            case Op::Atan2:
                r = x == 0.0 && y == 0.0 ? fail("atan2(0, 0) has no direction") : Atan2(a, b) * T(1.0 / degree);
                break;
            case Op::Exp:
                r = Exp(a);
                break;
            case Op::Log:
                r = x <= 0.0 ? fail("log of a number that is not positive (" + Shown(x) + ")") : Log(a);
                break;
            case Op::Sinh:
                r = Sinh(a);
                break;
            case Op::Cosh:
                r = Cosh(a);
                break;
            case Op::Tanh:
                r = Tanh(a);
                break;
            case Op::Asinh:
                r = Asinh(a);
                break;
            case Op::Acosh:
                if (!(x >= 1.0))
                    r = fail("acosh of " + Shown(x) + ", below 1");
                else {
                    if (x == 1.0 && steep(a))
                        note("acosh of 1 has no derivative there, the solver cannot use it");
                    r = Acosh(a);
                }
                break;
            case Op::Atanh:
                r = !(std::abs(x) < 1.0) ? fail("atanh of " + Shown(x) + ", outside (-1, 1)") : Atanh(a);
                break;
            default:
                break;
            }
            if (!std::isfinite(ValueOf(r)))
                note(std::string(OpName(n.op)) + " gives " + Shown(ValueOf(r)));
            stack.push_back(std::move(r));
        }
        return stack.back();
    }

}
