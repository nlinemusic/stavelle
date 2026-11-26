/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::style). Not part of MuseScore.
 *
 * A small JSON reader for the music fonts' SMuFL metadata: objects, arrays, strings, numbers, true, false, null.
 */
#pragma once

#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace je::style::json {
struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object } type = Type::Null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Value> array;
    std::vector<std::pair<std::string, Value> > object;   // in the file's order

    const Value* get(std::string_view key) const;
    double num(std::string_view key, double fallback = 0.0) const;
    std::string str(std::string_view key) const;
    bool isObject() const { return type == Type::Object; }
    bool isArray() const { return type == Type::Array; }
};

//! Parses `text`; false (with `error`) if it isn't valid JSON
bool parse(std::string_view text, Value& out, std::string& error);
}
