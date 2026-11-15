/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "usertemplates.h"

#include <vector>

namespace mu::project::usertemplates {
namespace {
std::vector<std::string> split(const std::string& path)
{
    std::vector<std::string> parts;
    std::string cur;
    for (char c : path) {
        if (c == '/' || c == '\\') {
            if (!cur.empty()) {
                parts.push_back(cur);
            }
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        parts.push_back(cur);
    }
    return parts;
}

std::string stem(const std::string& file)
{
    const size_t dot = file.rfind('.');
    return dot == std::string::npos ? file : file.substr(0, dot);
}
}

std::string categoryFor(const std::string& relativePath)
{
    std::vector<std::string> parts = split(relativePath);
    if (parts.size() < 2) {
        return {};
    }
    // a template kept as a folder: "Name/Name.mscx" is one template
    if (parts.size() >= 2 && parts[parts.size() - 2] == stem(parts.back())) {
        parts.erase(parts.end() - 2);
    }
    return parts.size() >= 2 ? parts.front() : std::string();
}

std::string fileNameFor(const std::string& title)
{
    std::string out;
    for (char c : title) {
        const bool bad = c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>'
                         || c == '|' || (static_cast<unsigned char>(c) < 32);
        out += bad ? '_' : c;
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) {
        out.pop_back();
    }
    while (!out.empty() && out.front() == ' ') {
        out.erase(out.begin());
    }
    return out.empty() ? std::string("Template") : out;
}
}
