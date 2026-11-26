/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::style). Not part of MuseScore.
 */
#include "json.h"

#include <cctype>
#include <cstdlib>

namespace je::style::json {
namespace {
struct Parser {
    std::string_view s;
    size_t pos = 0;
    std::string error;

    void ws()
    {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\n' || s[pos] == '\r' || s[pos] == '\t')) {
            ++pos;
        }
    }
    bool fail(const char* what)
    {
        if (error.empty()) {
            error = std::string(what) + " at " + std::to_string(pos);
        }
        return false;
    }
    static void utf8(std::string& out, unsigned cp)
    {
        if (cp < 0x80) {
            out += char(cp);
        } else if (cp < 0x800) {
            out += char(0xC0 | (cp >> 6));
            out += char(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += char(0xE0 | (cp >> 12));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        } else {
            out += char(0xF0 | (cp >> 18));
            out += char(0x80 | ((cp >> 12) & 0x3F));
            out += char(0x80 | ((cp >> 6) & 0x3F));
            out += char(0x80 | (cp & 0x3F));
        }
    }
    bool hex4(unsigned& v)
    {
        if (pos + 4 > s.size()) {
            return fail("bad escape");
        }
        v = unsigned(std::strtoul(std::string(s.substr(pos, 4)).c_str(), nullptr, 16));
        pos += 4;
        return true;
    }
    bool string(std::string& out)
    {
        ++pos;   // "
        while (pos < s.size() && s[pos] != '"') {
            char c = s[pos++];
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos >= s.size()) {
                return fail("bad escape");
            }
            c = s[pos++];
            switch (c) {
            case 'n': out += '\n';
                break;
            case 't': out += '\t';
                break;
            case 'r': out += '\r';
                break;
            case 'b': out += '\b';
                break;
            case 'f': out += '\f';
                break;
            case 'u': {
                unsigned cp = 0;
                if (!hex4(cp)) {
                    return false;
                }
                if (cp >= 0xD800 && cp < 0xDC00 && pos + 6 <= s.size() && s[pos] == '\\' && s[pos + 1] == 'u') {
                    pos += 2;
                    unsigned lo = 0;
                    if (!hex4(lo)) {
                        return false;
                    }
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                }
                utf8(out, cp);
            } break;
            default: out += c;
            }
        }
        if (pos >= s.size()) {
            return fail("unterminated string");
        }
        ++pos;
        return true;
    }
    bool value(Value& v)
    {
        ws();
        if (pos >= s.size()) {
            return fail("unexpected end");
        }
        const char c = s[pos];
        if (c == '{') {
            v.type = Value::Type::Object;
            ++pos;
            ws();
            if (pos < s.size() && s[pos] == '}') {
                ++pos;
                return true;
            }
            for (;;) {
                ws();
                if (pos >= s.size() || s[pos] != '"') {
                    return fail("expected a key");
                }
                std::string key;
                if (!string(key)) {
                    return false;
                }
                ws();
                if (pos >= s.size() || s[pos] != ':') {
                    return fail("expected ':'");
                }
                ++pos;
                v.object.emplace_back(std::move(key), Value());
                if (!value(v.object.back().second)) {
                    return false;
                }
                ws();
                if (pos < s.size() && s[pos] == ',') {
                    ++pos;
                    continue;
                }
                if (pos < s.size() && s[pos] == '}') {
                    ++pos;
                    return true;
                }
                return fail("expected ',' or '}'");
            }
        }
        if (c == '[') {
            v.type = Value::Type::Array;
            ++pos;
            ws();
            if (pos < s.size() && s[pos] == ']') {
                ++pos;
                return true;
            }
            for (;;) {
                v.array.emplace_back();
                if (!value(v.array.back())) {
                    return false;
                }
                ws();
                if (pos < s.size() && s[pos] == ',') {
                    ++pos;
                    continue;
                }
                if (pos < s.size() && s[pos] == ']') {
                    ++pos;
                    return true;
                }
                return fail("expected ',' or ']'");
            }
        }
        if (c == '"') {
            v.type = Value::Type::String;
            return string(v.string);
        }
        if (s.compare(pos, 4, "true") == 0) {
            v.type = Value::Type::Bool, v.boolean = true, pos += 4;
            return true;
        }
        if (s.compare(pos, 5, "false") == 0) {
            v.type = Value::Type::Bool, pos += 5;
            return true;
        }
        if (s.compare(pos, 4, "null") == 0) {
            pos += 4;
            return true;
        }
        const size_t start = pos;
        while (pos < s.size() && (std::isdigit(static_cast<unsigned char>(s[pos])) || s[pos] == '-' || s[pos] == '+'
                                  || s[pos] == '.' || s[pos] == 'e' || s[pos] == 'E')) {
            ++pos;
        }
        if (pos == start) {
            return fail("unexpected character");
        }
        v.type = Value::Type::Number;
        v.number = std::strtod(std::string(s.substr(start, pos - start)).c_str(), nullptr);
        return true;
    }
};
}

const Value* Value::get(std::string_view key) const
{
    for (const auto& [k, v] : object) {
        if (k == key) {
            return &v;
        }
    }
    return nullptr;
}

double Value::num(std::string_view key, double fallback) const
{
    const Value* v = get(key);
    return v && v->type == Type::Number ? v->number : fallback;
}

std::string Value::str(std::string_view key) const
{
    const Value* v = get(key);
    return v && v->type == Type::String ? v->string : std::string();
}

bool parse(std::string_view text, Value& out, std::string& error)
{
    Parser p;
    p.s = text;
    out = Value();
    if (!p.value(out)) {
        error = p.error;
        return false;
    }
    return true;
}
}
