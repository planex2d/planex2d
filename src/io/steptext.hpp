// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cctype>
#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "core/vertex.hpp"

// The text of a STEP file (ISO 10303-21): its entities with their parameters, and how numbers and strings are written.

namespace planex2d {

    // a parameter: a number, a string, an enumeration (.T.), a reference (#12), unset ($), derived (*), a list, or a
    // typed value (LENGTH_MEASURE(1.E-07): its name in text, its one argument in list)
    struct StepValue {
        enum class Kind { Number, String, Enum, Ref, Unset, Derived, List, Typed } kind = Kind::Unset;
        double number = 0.0;
        size_t ref = 0;
        std::string text;
        std::vector<StepValue> list;
    };

    // one record of an entity: its type and parameters; a complex entity has several, "( A(...) B(...) )"
    struct StepRecord {
        std::string type;
        std::vector<StepValue> params;
    };

    struct StepEntity {
        size_t id = 0;
        std::vector<StepRecord> records;

        const StepRecord *Record(std::string_view type) const {
            for (const StepRecord &r : records)
                if (r.type == type)
                    return &r;
            return nullptr;
        }
        bool Is(std::string_view type) const { return Record(type) != nullptr; }
    };

    // a real as STEP writes it: the shortest text that reads back exactly, with the decimal point a real must have
    inline std::string StepReal(double x) {
        std::string t = NumberText(x == 0.0 ? 0.0 : x);
        const size_t e = t.find_first_of("eE");
        const std::string mantissa = t.substr(0, e), exponent = e == std::string::npos ? "" : "E" + t.substr(e + 1);
        return (mantissa.find('.') == std::string::npos ? mantissa + "." : mantissa) + exponent;
    }

    // a string as STEP writes it: quote and backslash doubled, characters beyond ASCII as UTF-16 hex between the
    // X2 and X0 escapes
    inline std::string StepString(const std::string &s) {
        std::string out = "'";
        const char *hex = "0123456789ABCDEF";
        for (size_t i = 0; i < s.size();) {
            const unsigned char c = (unsigned char)s[i];
            if (c < 0x80) {
                if (c == '\'' || c == '\\')
                    out += (char)c;
                out += (char)c;
                ++i;
                continue;
            }
            const size_t n = c >= 0xF0 ? 4 : c >= 0xE0 ? 3 : 2;
            unsigned code = c & (n == 4 ? 0x07 : n == 3 ? 0x0F : 0x1F);
            for (size_t k = 1; k < n && i + k < s.size(); ++k)
                code = (code << 6) | ((unsigned char)s[i + k] & 0x3F);
            i += n;
            out += "\\X2\\";
            const auto word = [&](unsigned w) {
                for (int shift = 12; shift >= 0; shift -= 4)
                    out += hex[(w >> shift) & 15];
            };
            if (code >= 0x10000) {
                code -= 0x10000;
                word(0xD800 + (code >> 10));
                word(0xDC00 + (code & 0x3FF));
            } else {
                word(code);
            }
            out += "\\X0\\";
        }
        return out + "'";
    }

    // the entities of a STEP file by id, and the originating system its header names
    class StepFile {
    public:
        std::string originatingSystem;

        explicit StepFile(std::string_view text) : s(text) {
            const size_t header = s.find("FILE_NAME");
            if (header != std::string_view::npos) {
                i = header + 9;
                const StepValue params = Value();
                if (params.kind == StepValue::Kind::List && params.list.size() > 5 && params.list[5].kind == StepValue::Kind::String)
                    originatingSystem = params.list[5].text;
            }
            const size_t data = s.find("DATA;");
            if (data == std::string_view::npos)
                Fail("no DATA section; is it a STEP file?");
            i = data + 5;
            while (true) {
                Space();
                if (Word("ENDSEC"))
                    break;
                if (i >= s.size() || s[i] != '#')
                    Fail("expected an entity '#n = ...'");
                ++i;
                StepEntity e;
                e.id = Integer();
                Expect('=');
                Space();
                if (Peek() == '(') {
                    ++i;
                    while (Space(), Peek() != ')')
                        e.records.push_back(Record());
                    ++i;
                } else {
                    e.records.push_back(Record());
                }
                Expect(';');
                entities.emplace(e.id, std::move(e));
            }
        }

        const StepEntity &At(size_t id) const {
            const auto it = entities.find(id);
            if (it == entities.end())
                throw std::invalid_argument("STEP file: no entity #" + std::to_string(id));
            return it->second;
        }
        const std::unordered_map<size_t, StepEntity> &Entities() const { return entities; }

    private:
        std::string_view s;
        size_t i = 0;
        std::unordered_map<size_t, StepEntity> entities;

        [[noreturn]] void Fail(const std::string &what) const {
            throw std::invalid_argument("STEP file: at character " + std::to_string(i) + ": " + what);
        }
        char Peek() const { return i < s.size() ? s[i] : '\0'; }
        void Space() {
            while (i < s.size()) {
                if (std::isspace((unsigned char)s[i])) {
                    ++i;
                } else if (s.compare(i, 2, "/*") == 0) {
                    const size_t end = s.find("*/", i + 2);
                    i = end == std::string_view::npos ? s.size() : end + 2;
                } else {
                    break;
                }
            }
        }
        bool Word(std::string_view w) {
            if (s.compare(i, w.size(), w) != 0)
                return false;
            i += w.size();
            return true;
        }
        void Expect(char c) {
            Space();
            if (Peek() != c)
                Fail(std::string("expected '") + c + "'");
            ++i;
        }
        size_t Integer() {
            const size_t start = i;
            while (std::isdigit((unsigned char)Peek()))
                ++i;
            if (i == start)
                Fail("expected a number");
            return (size_t)std::stoull(std::string(s.substr(start, i - start)));
        }
        std::string Keyword() {
            const size_t start = i;
            while (std::isalnum((unsigned char)Peek()) || Peek() == '_' || Peek() == '-')
                ++i;
            if (i == start)
                Fail("expected a name");
            return std::string(s.substr(start, i - start));
        }
        StepRecord Record() {
            Space();
            StepRecord r;
            r.type = Keyword();
            const StepValue params = Value();
            if (params.kind != StepValue::Kind::List)
                Fail("expected the parameters of " + r.type);
            r.params = params.list;
            return r;
        }
        static void Utf8(std::string &out, unsigned code) {
            if (code < 0x80) {
                out += (char)code;
            } else if (code < 0x800) {
                out += (char)(0xC0 | (code >> 6));
                out += (char)(0x80 | (code & 0x3F));
            } else if (code < 0x10000) {
                out += (char)(0xE0 | (code >> 12));
                out += (char)(0x80 | ((code >> 6) & 0x3F));
                out += (char)(0x80 | (code & 0x3F));
            } else {
                out += (char)(0xF0 | (code >> 18));
                out += (char)(0x80 | ((code >> 12) & 0x3F));
                out += (char)(0x80 | ((code >> 6) & 0x3F));
                out += (char)(0x80 | (code & 0x3F));
            }
        }
        unsigned Hex(size_t n) {
            if (i + n > s.size())
                Fail("a broken \\X escape");
            const unsigned v = (unsigned)std::stoul(std::string(s.substr(i, n)), nullptr, 16);
            i += n;
            return v;
        }
        std::string String() {
            ++i;
            std::string out;
            while (true) {
                if (i >= s.size())
                    Fail("unexpected end in a string");
                const char c = s[i];
                if (c == '\'') {
                    if (Peek2() == '\'') {
                        out += '\'';
                        i += 2;
                        continue;
                    }
                    ++i;
                    return out;
                }
                if (c == '\\' && s.compare(i, 4, "\\X2\\") == 0) {
                    i += 4;
                    while (s.compare(i, 4, "\\X0\\") != 0) {
                        unsigned code = Hex(4);
                        if (code >= 0xD800 && code < 0xDC00)
                            code = 0x10000 + ((code - 0xD800) << 10) + (Hex(4) - 0xDC00);
                        Utf8(out, code);
                    }
                    i += 4;
                } else if (c == '\\' && s.compare(i, 3, "\\X\\") == 0) {
                    i += 3;
                    Utf8(out, Hex(2));
                } else if (c == '\\' && Peek2() == '\\') {
                    out += '\\';
                    i += 2;
                } else {
                    out += c;
                    ++i;
                }
            }
        }
        char Peek2() const { return i + 1 < s.size() ? s[i + 1] : '\0'; }
        StepValue Value() {
            Space();
            StepValue v;
            const char c = Peek();
            if (c == '(') {
                ++i;
                v.kind = StepValue::Kind::List;
                Space();
                if (Peek() == ')') {
                    ++i;
                    return v;
                }
                while (true) {
                    v.list.push_back(Value());
                    Space();
                    if (Peek() == ',') {
                        ++i;
                        continue;
                    }
                    if (Peek() == ')') {
                        ++i;
                        return v;
                    }
                    Fail("expected ',' or ')'");
                }
            }
            if (c == '\'') {
                v.kind = StepValue::Kind::String;
                v.text = String();
            } else if (c == '#') {
                ++i;
                v.kind = StepValue::Kind::Ref;
                v.ref = Integer();
            } else if (c == '$') {
                ++i;
            } else if (c == '*') {
                ++i;
                v.kind = StepValue::Kind::Derived;
            } else if (c == '.') {
                ++i;
                v.kind = StepValue::Kind::Enum;
                v.text = Keyword();
                if (Peek() != '.')
                    Fail("expected '.' after an enumeration");
                ++i;
            } else if (std::isdigit((unsigned char)c) || c == '-' || c == '+') {
                const size_t start = i;
                while (std::isdigit((unsigned char)Peek()) || Peek() == '-' || Peek() == '+' || Peek() == '.' || Peek() == 'E' || Peek() == 'e')
                    ++i;
                v.kind = StepValue::Kind::Number;
                const std::optional<double> x = NumberOf(s.substr(start, i - start));
                if (!x)
                    Fail("a bad number '" + std::string(s.substr(start, i - start)) + "'");
                v.number = *x;
            } else if (std::isalpha((unsigned char)c)) {
                v.kind = StepValue::Kind::Typed;
                v.text = Keyword();
                StepValue arg = Value();
                v.list = arg.kind == StepValue::Kind::List ? std::move(arg.list) : std::vector<StepValue>{std::move(arg)};
            } else {
                Fail(std::string("unexpected character '") + c + "'");
            }
            return v;
        }
    };

}
