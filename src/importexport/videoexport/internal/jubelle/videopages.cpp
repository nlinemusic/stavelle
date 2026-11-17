/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "videopages.h"

#include <algorithm>
#include <cctype>
#include <map>

namespace mu::iex::videoexport::jubelle {
namespace {
std::string normalised(const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        // en dash and em dash (UTF-8 E2 80 93 / E2 80 94) count as a hyphen
        if (c == 0xE2 && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0x80
            && (static_cast<unsigned char>(text[i + 2]) == 0x93 || static_cast<unsigned char>(text[i + 2]) == 0x94)) {
            out += '-';
            i += 2;
        } else if (c == ';') {
            out += ',';
        } else if (!std::isspace(c)) {
            out += char(c);
        }
    }
    return out;
}

bool readNumber(const std::string& s, int* value)
{
    if (s.empty() || s.size() > 6) {
        return false;
    }
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    *value = std::stoi(s);
    return true;
}
}

PageList parsePageList(const std::string& text, int pageCount)
{
    PageList result;
    const std::string s = normalised(text);
    if (s.empty()) {
        return result;
    }
    size_t pos = 0;
    while (pos <= s.size()) {
        const size_t comma = s.find(',', pos);
        const std::string item = s.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
        pos = comma == std::string::npos ? s.size() + 1 : comma + 1;
        if (item.empty()) {
            continue;
        }
        const size_t dash = item.find('-');
        int from = 0, to = 0;
        const bool good = dash == std::string::npos
                          ? readNumber(item, &from) && (to = from, true)
                          : readNumber(item.substr(0, dash), &from) && readNumber(item.substr(dash + 1), &to);
        if (!good) {
            result.ok = false;
            result.error = "“" + item + "” is not a page or a range of pages (write it like 1-3, 5).";
            result.pages.clear();
            return result;
        }
        if (from < 1 || to < 1 || from > pageCount || to > pageCount) {
            result.ok = false;
            result.error = "The score has " + std::to_string(pageCount) + (pageCount == 1 ? " page" : " pages") + "; “" + item
                           + "” is outside that.";
            result.pages.clear();
            return result;
        }
        const int step = from <= to ? 1 : -1;   // "5-3" shows 5, 4, 3
        for (int p = from;; p += step) {
            result.pages.push_back(p);
            if (p == to) {
                break;
            }
        }
    }
    return result;
}

std::vector<VideoSegment> planSegments(const std::vector<PageVisit>& visits, const PageList& list)
{
    std::vector<VideoSegment> out;
    if (list.pages.empty()) {
        for (const PageVisit& v : visits) {
            out.push_back({ v.page, v.start, v.end });
        }
        return out;
    }
    // a page asked for again takes its next visit (the repeat), or its first again if there is none
    std::map<int, size_t> used;
    for (int page : list.pages) {
        std::vector<const PageVisit*> mine;
        for (const PageVisit& v : visits) {
            if (v.page == page) {
                mine.push_back(&v);
            }
        }
        if (mine.empty()) {
            continue;   // a page without music (a title page or a text page)
        }
        size_t& n = used[page];
        // a plain range "1-3": a page listed once gets all its visits in order
        const bool listedOnce = std::count(list.pages.begin(), list.pages.end(), page) == 1;
        if (listedOnce) {
            for (const PageVisit* v : mine) {
                out.push_back({ v->page, v->start, v->end });
            }
            continue;
        }
        const PageVisit* v = mine[n < mine.size() ? n : 0];
        out.push_back({ v->page, v->start, v->end });
        ++n;
    }
    return out;
}

double totalLength(const std::vector<VideoSegment>& segments)
{
    double t = 0.0;
    for (const VideoSegment& s : segments) {
        t += s.length();
    }
    return t;
}

int segmentAt(const std::vector<VideoSegment>& segments, double videoSeconds, double* musicSeconds)
{
    double acc = 0.0;
    for (size_t i = 0; i < segments.size(); ++i) {
        const double len = segments[i].length();
        if (videoSeconds < acc + len) {
            *musicSeconds = segments[i].start + (videoSeconds - acc);
            return int(i);
        }
        acc += len;
    }
    *musicSeconds = -1.0;
    return -1;
}
}
