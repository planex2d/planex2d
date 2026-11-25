// SPDX-FileCopyrightText: 2026 Sebastian Hirnschall
// SPDX-License-Identifier: LGPL-2.1-or-later

#pragma once

#include <cctype>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "planex/core/vertex.hpp"

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
        void AddRefs(std::vector<size_t> &refs) const {
            for (const StepRecord &r : records)
                for (const StepValue &v : r.params) {
                    if (v.kind == StepValue::Kind::Ref)
                        refs.push_back(v.ref);
                    for (const StepValue &w : v.list)
                        if (w.kind == StepValue::Kind::Ref)
                            refs.push_back(w.ref);
                }
        }
    };

    std::string StepReal(double x);

    std::string StepString(const std::string &s);

    class StepFile {
    public:
        std::string originatingSystem;

        explicit StepFile(std::string_view text);

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
        void Space();
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
        static void Utf8(std::string &out, unsigned code);
        unsigned Hex(size_t n) {
            if (i + n > s.size())
                Fail("a broken \\X escape");
            const unsigned v = (unsigned)std::stoul(std::string(s.substr(i, n)), nullptr, 16);
            i += n;
            return v;
        }
        std::string String();
        char Peek2() const { return i + 1 < s.size() ? s[i + 1] : '\0'; }
        StepValue List();
        StepValue Number();
        StepValue Value();
    };

}
