/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 */
#include "fragment.h"

#include <set>

namespace je::model::fragment {

//! Finds the fragment's root content and its children; false if the text isn't a fragment
bool children(const std::string& d, size_t& contentBegin, size_t& contentEnd, std::vector<Child>& out)
{
    size_t i = d.find('<');
    while (i != std::string::npos && (d.compare(i, 2, "<?") == 0 || d.compare(i, 4, "<!--") == 0)) {
        i = d.compare(i, 2, "<?") == 0 ? d.find("?>", i) : d.find("-->", i);
        i = i == std::string::npos ? i : d.find('<', i);
    }
    if (i == std::string::npos) {
        return false;
    }
    const size_t rootEnd = d.find('>', i);
    if (rootEnd == std::string::npos || d[rootEnd - 1] == '/') {
        return false;
    }
    size_t n = i + 1;
    while (n < rootEnd && d[n] != ' ' && d[n] != '>' && d[n] != '\t' && d[n] != '\n') {
        ++n;
    }
    const std::string root = d.substr(i + 1, n - i - 1);
    contentBegin = rootEnd + 1;
    contentEnd = d.rfind("</" + root);
    if (contentEnd == std::string::npos || contentEnd < contentBegin) {
        return false;
    }
    int depth = 0;
    for (size_t pos = d.find('<', contentBegin); pos != std::string::npos && pos < contentEnd; pos = d.find('<', pos)) {
        if (d.compare(pos, 4, "<!--") == 0) {
            const size_t e = d.find("-->", pos);
            pos = e == std::string::npos ? contentEnd : e + 3;
            continue;
        }
        const size_t e = d.find('>', pos);
        if (e == std::string::npos || e > contentEnd) {
            return false;
        }
        if (d[pos + 1] == '/') {
            if (--depth == 0 && !out.empty()) {
                out.back().end = e + 1;
            }
        } else {
            const bool empty = d[e - 1] == '/';
            if (depth == 0) {
                size_t k = pos + 1;
                while (k < e && d[k] != ' ' && d[k] != '/' && d[k] != '\t' && d[k] != '\n') {
                    ++k;
                }
                out.push_back({ d.substr(pos + 1, k - pos - 1), pos, empty ? e + 1 : std::string::npos });
            }
            if (!empty) {
                ++depth;
            }
        }
        pos = e + 1;
    }
    return depth == 0;
}

//! The text of a child element (empty if there is none)
std::string value(const std::string& d, const std::string& tag)
{
    size_t b = 0, e = 0;
    std::vector<Child> kids;
    if (!children(d, b, e, kids)) {
        return std::string();
    }
    for (const Child& k : kids) {
        if (k.name == tag && k.end != std::string::npos) {
            const size_t open = d.find('>', k.begin) + 1;
            const size_t close = d.rfind("</", k.end);
            return close > open ? d.substr(open, close - open) : std::string();
        }
    }
    return std::string();
}

//! The fragment with these child elements set (an empty value: removed); empty when no child is left
std::string withValues(std::string d, const std::vector<std::pair<std::string, std::string> >& values)
{
    size_t b = 0, e = 0;
    std::vector<Child> kids;
    if (d.empty() || !children(d, b, e, kids)) {
        d = "<x>\n</x>\n";
        kids.clear();
        children(d, b, e, kids);
    }
    std::set<std::string> names;
    for (const auto& [name, value] : values) {
        names.insert(name);
    }
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
        if (names.count(it->name) && it->end != std::string::npos) {
            size_t from = it->begin;
            while (from > b && (d[from - 1] == ' ' || d[from - 1] == '\t')) {
                --from;   // with its indentation
            }
            size_t to = it->end;
            if (to < d.size() && d[to] == '\n') {
                ++to;
            }
            d.erase(from, to - from);
            e -= to - from;
        }
    }
    std::string added;
    for (const auto& [name, value] : values) {
        if (!value.empty()) {
            added += "<" + name + ">" + value + "</" + name + ">\n";
        }
    }
    d.insert(e, added);
    size_t b2 = 0, e2 = 0;
    std::vector<Child> left;
    return children(d, b2, e2, left) && left.empty() ? std::string() : d;
}


//! The first value of a tag anywhere in a fragment (a staff type's <lines>…)
std::string anyValue(const std::string& d, const std::string& tag)
{
    const std::string open = "<" + tag + ">";
    const size_t a = d.find(open);
    if (a == std::string::npos) {
        return std::string();
    }
    const size_t b = d.find("</" + tag + ">", a);
    return b == std::string::npos ? std::string() : d.substr(a + open.size(), b - a - open.size());
}

bool has(const std::string& d, const std::string& tag)
{
    size_t b = 0, e = 0;
    std::vector<Child> kids;
    if (!children(d, b, e, kids)) {
        return false;
    }
    for (const Child& k : kids) {
        if (k.name == tag) {
            return true;
        }
    }
    return false;
}
}
