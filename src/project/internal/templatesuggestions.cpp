/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 * Music Composition & Notation
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "templatesuggestions.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <vector>

namespace mu::project::templatesuggestions {
namespace {
std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

//! Lower-case words of at least 3 letters; a plural "s" is dropped from longer words.
std::vector<std::string> words(const std::string& text)
{
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        if (cur.size() >= 3) {
            if (cur.size() > 4 && cur.back() == 's' && cur[cur.size() - 2] != 's') {
                cur.pop_back();
            }
            out.push_back(cur);
        }
        cur.clear();
    };
    for (unsigned char c : lower(text)) {
        if (std::isalnum(c) || c >= 0x80) {
            cur.push_back(static_cast<char>(c));
        } else {
            flush();
        }
    }
    flush();
    return out;
}

bool contains(const std::string& haystack, const std::string& needle)
{
    return haystack.find(needle) != std::string::npos;
}

//! A family of ensembles: if the project category mentions any trigger, a template
//! whose category or title mentions any target is suggested.
struct Family {
    std::vector<std::string> triggers;
    std::vector<std::string> targets;
};

const std::vector<Family>& families()
{
    static const std::vector<Family> list {
        { { "jazz", "big band", "swing", "combo" },
          { "jazz", "big band", "combo", "swing" } },
        { { "concert band", "wind band", "wind ensemble", "symphonic band", "marching", "brass band" },
          { "concert band", "wind band", "wind ensemble", "symphonic band", "marching band", "brass band",
            "band and percussion" } },
        { { "orchestra", "orchestral", "symphony", "string" },
          { "orchestra", "orchestral", "string" } },
        { { "chamber", "small ensemble", "quartet", "quintet", "trio", "duet", "duo", "sextet", "octet" },
          { "chamber", "quartet", "quintet", "trio", "duet", "duo", "sextet", "octet", "ensemble" } },
        { { "choir", "choral", "vocal", "voice", "satb", "chorus" },
          { "choir", "choral", "vocal", "voice", "satb", "chorus" } },
        { { "solo" },
          { "solo" } },
    };
    return list;
}

//! Words too general to match on their own ("Small Ensembles" should not suggest
//! every template that has "band" or "ensemble" in it).
const std::set<std::string>& genericWords()
{
    static const std::set<std::string> set {
        "and", "the", "for", "band", "ensemble", "small", "large", "other", "music", "score", "group",
        "project", "piece", "work", "misc", "general", "your", "template", "new", "big", "wind",
    };
    return set;
}
}

bool isSuggested(const std::string& projectCategory, const std::string& templateCategory, const std::string& templateTitle)
{
    const std::string category = lower(projectCategory);
    const std::string haystack = lower(templateCategory) + " | " + lower(templateTitle);

    for (const Family& f : families()) {
        bool triggered = false;
        for (const std::string& t : f.triggers) {
            if (contains(category, t)) {
                triggered = true;
                break;
            }
        }
        if (!triggered) {
            continue;
        }
        for (const std::string& t : f.targets) {
            if (contains(haystack, t)) {
                return true;
            }
        }
    }

    // Any distinctive word of the category name that appears in the template's title
    // or category, e.g. a custom category "Pit Orchestra" or "Steel Pan".
    const std::vector<std::string> templateWords = words(haystack);
    for (const std::string& w : words(category)) {
        if (genericWords().count(w)) {
            continue;
        }
        if (std::find(templateWords.begin(), templateWords.end(), w) != templateWords.end()) {
            return true;
        }
    }
    return false;
}
}
