// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cctype>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include "core/vertex.hpp"

namespace planex2d {

    // a JSON value: null, true/false, a number, a string, an array or an object (its members in the order written)
    struct Json {
        using Array = std::vector<Json>;
        using Object = std::vector<std::pair<std::string, Json>>;
        std::variant<std::nullptr_t, bool, double, std::string, Array, Object> value = nullptr;

        Json() = default;
        Json(std::nullptr_t) {}
        Json(bool b) : value(b) {}
        Json(double x) : value(x) {}
        Json(int x) : value((double)x) {}
        Json(size_t x) : value((double)x) {}
        Json(std::string s) : value(std::move(s)) {}
        Json(const char *s) : value(std::string(s)) {}
        Json(Array a) : value(std::move(a)) {}
        Json(Object o) : value(std::move(o)) {}

        bool IsNull() const { return std::holds_alternative<std::nullptr_t>(value); }
        bool IsArray() const { return std::holds_alternative<Array>(value); }
        bool IsObject() const { return std::holds_alternative<Object>(value); }

        // the member with that key, null if there is none
        const Json &operator[](std::string_view key) const {
            static const Json none;
            for (const auto &[k, v] : AsObject())
                if (k == key)
                    return v;
            return none;
        }
        bool Has(std::string_view key) const {
            for (const auto &[k, v] : AsObject())
                if (k == key)
                    return true;
            return false;
        }
        void Add(std::string key, Json v) { std::get<Object>(value).emplace_back(std::move(key), std::move(v)); }

        const Array &AsArray() const { return As<Array>("an array"); }
        const Object &AsObject() const { return As<Object>("an object"); }
        const std::string &AsString() const { return As<std::string>("a string"); }
        bool AsBool() const { return As<bool>("true or false"); }
        double AsNumber() const { return As<double>("a number"); }
        size_t AsIndex() const {
            const double x = AsNumber();
            if (!(x >= 0.0 && x <= 9007199254740992.0 && x == std::floor(x)))
                throw std::invalid_argument("planex file: " + NumberText(x) + " is no count or index");
            return (size_t)x;
        }

        // one line for an array of numbers, strings and literals; else one member or element per line
        std::string Text(size_t indent = 0) const {
            std::string out;
            Write(out, indent);
            return out;
        }

    private:
        template <class T>
        const T &As(const char *what) const {
            if (const T *t = std::get_if<T>(&value))
                return *t;
            throw std::invalid_argument(std::string("planex file: expected ") + what);
        }
        bool IsFlat() const {
            const Array *a = std::get_if<Array>(&value);
            if (!a)
                return !IsObject();
            for (const Json &e : *a)
                if (e.IsArray() || e.IsObject())
                    return false;
            return true;
        }
        static void Quote(std::string &out, const std::string &s) {
            out += '"';
            for (unsigned char c : s) {
                if (c == '"' || c == '\\') {
                    out += '\\';
                    out += (char)c;
                } else if (c < 0x20) {
                    const char *hex = "0123456789abcdef";
                    out += "\\u00";
                    out += hex[c >> 4];
                    out += hex[c & 15];
                } else {
                    out += (char)c;
                }
            }
            out += '"';
        }
        void Write(std::string &out, size_t indent) const {
            const std::string pad(indent + 1, ' ');
            std::visit([&](const auto &v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::nullptr_t>)
                    out += "null";
                else if constexpr (std::is_same_v<T, bool>)
                    out += v ? "true" : "false";
                else if constexpr (std::is_same_v<T, double>) {
                    if (!std::isfinite(v))
                        throw std::invalid_argument("planex file: a value is not finite (" + NumberText(v) + ")");
                    out += NumberText(v);
                } else if constexpr (std::is_same_v<T, std::string>)
                    Quote(out, v);
                else if constexpr (std::is_same_v<T, Array>) {
                    const bool flat = IsFlat();
                    out += '[';
                    for (size_t i = 0; i < v.size(); ++i) {
                        if (i > 0)
                            out += flat ? ", " : ",";
                        if (!flat)
                            out += "\n" + pad;
                        v[i].Write(out, indent + 1);
                    }
                    if (!flat && !v.empty())
                        out += "\n" + std::string(indent, ' ');
                    out += ']';
                } else {
                    out += '{';
                    for (size_t i = 0; i < v.size(); ++i) {
                        out += i > 0 ? ",\n" + pad : "\n" + pad;
                        Quote(out, v[i].first);
                        out += ": ";
                        v[i].second.Write(out, indent + 1);
                    }
                    if (!v.empty())
                        out += "\n" + std::string(indent, ' ');
                    out += '}';
                }
            }, value);
        }
    };

    // the JSON text as a value; numbers read back bit-exactly what NumberText wrote
    class JsonParser {
    public:
        explicit JsonParser(std::string_view text) : s(text) {}

        Json Document() {
            Json v = Value();
            Space();
            if (i != s.size())
                Fail("text after the end");
            return v;
        }

    private:
        std::string_view s;
        size_t i = 0;

        [[noreturn]] void Fail(const std::string &what) const {
            throw std::invalid_argument("planex file: not valid JSON at character " + std::to_string(i) + ": " + what);
        }
        void Space() {
            while (i < s.size() && (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t'))
                ++i;
        }
        bool Take(char c) {
            Space();
            if (i < s.size() && s[i] == c) {
                ++i;
                return true;
            }
            return false;
        }
        void Expect(char c) {
            if (!Take(c))
                Fail(std::string("expected '") + c + "'");
        }
        bool Word(std::string_view w) {
            if (s.substr(i, w.size()) != w)
                return false;
            i += w.size();
            return true;
        }
        Json Value() {
            Space();
            if (i >= s.size())
                Fail("unexpected end");
            const char c = s[i];
            if (c == '{')
                return Members();
            if (c == '[')
                return Elements();
            if (c == '"')
                return String();
            if (Word("null"))
                return nullptr;
            if (Word("true"))
                return true;
            if (Word("false"))
                return false;
            return Number();
        }
        Json Members() {
            Expect('{');
            Json o{Json::Object{}};
            if (Take('}'))
                return o;
            do {
                Space();
                std::string key = String().AsString();
                Expect(':');
                o.Add(std::move(key), Value());
            } while (Take(','));
            Expect('}');
            return o;
        }
        Json Elements() {
            Expect('[');
            Json::Array a;
            if (Take(']'))
                return a;
            do
                a.push_back(Value());
            while (Take(','));
            Expect(']');
            return a;
        }
        static void Utf8(std::string &out, unsigned code) {
            if (code < 0x80) {
                out += (char)code;
            } else if (code < 0x800) {
                out += (char)(0xC0 | (code >> 6));
                out += (char)(0x80 | (code & 0x3F));
            } else {
                out += (char)(0xE0 | (code >> 12));
                out += (char)(0x80 | ((code >> 6) & 0x3F));
                out += (char)(0x80 | (code & 0x3F));
            }
        }
        Json String() {
            if (i >= s.size() || s[i] != '"')
                Fail("expected a string");
            ++i;
            std::string out;
            while (i < s.size() && s[i] != '"') {
                if (s[i] != '\\') {
                    out += s[i++];
                    continue;
                }
                if (++i >= s.size())
                    Fail("unexpected end in a string");
                const char e = s[i++];
                switch (e) {
                    case '"': out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/': out += '/'; break;
                    case 'b': out += '\b'; break;
                    case 'f': out += '\f'; break;
                    case 'n': out += '\n'; break;
                    case 'r': out += '\r'; break;
                    case 't': out += '\t'; break;
                    case 'u': {
                        unsigned code = 0;
                        if (i + 4 > s.size() || std::from_chars(s.data() + i, s.data() + i + 4, code, 16).ptr != s.data() + i + 4)
                            Fail("a bad \\u escape");
                        i += 4;
                        Utf8(out, code);
                        break;
                    }
                    default: Fail("a bad escape");
                }
            }
            if (i >= s.size())
                Fail("unexpected end in a string");
            ++i;
            return out;
        }
        Json Number() {
            const size_t start = i;
            while (i < s.size() && (std::isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '+' || s[i] == '.' || s[i] == 'e' || s[i] == 'E'))
                ++i;
            if (i == start)
                Fail("unexpected character");
            const std::string text(s.substr(start, i - start));
            const std::optional<double> x = NumberOf(text);
            if (!x)
                Fail("a bad number '" + text + "'");
            return *x;
        }
    };

}
