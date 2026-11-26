/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 *
 * Key–value lists kept in one text field (a part layout's breaks…): pairs separated by control characters that
 * XML text can't hold, so keys and values may contain any text.
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace je::model {
inline std::string packPairs(const std::vector<std::pair<std::string, std::string> >& pairs)
{
    std::string out;
    for (const auto& [k, v] : pairs) {
        out += k + "\x1F" + v + "\x1E";
    }
    return out;
}

inline std::vector<std::pair<std::string, std::string> > unpackPairs(const std::string& packed)
{
    std::vector<std::pair<std::string, std::string> > out;
    size_t at = 0;
    while (at < packed.size()) {
        const size_t nl = packed.find('\x1E', at);
        const std::string line = packed.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        const size_t tab = line.find('\x1F');
        if (tab != std::string::npos) {
            out.emplace_back(line.substr(0, tab), line.substr(tab + 1));
        }
        if (nl == std::string::npos) {
            break;
        }
        at = nl + 1;
    }
    return out;
}
}
