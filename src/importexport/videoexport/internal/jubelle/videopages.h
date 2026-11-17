/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle G9: which pages a video shows, and in what order.
//
// While the music plays, each page is on screen for as long as its music
// lasts: a "visit". With repeats a page can be visited more than once. A page
// list such as "1-3, 5" keeps only those visits; "1, 2, 1, 3" shows page 1
// again after page 2 (the music of page 1 is heard again). The sound is cut
// to the same pieces, so picture and sound stay together.
//
// Plain C++: no Qt, no engine, so it is tested on its own.

#pragma once

#include <algorithm>
#include <string>
#include <vector>

namespace mu::iex::videoexport::jubelle {
struct PageVisit {
    int page = 0;          // 1 = first page
    double start = 0.0;    // seconds of music
    double end = 0.0;
};

struct VideoSegment {
    int page = 0;
    double start = 0.0;
    double end = 0.0;

    double length() const { return end - start; }
};

struct PageList {
    std::vector<int> pages;   // in the order asked for; empty = every page
    bool ok = true;
    std::string error;        // a plain sentence when !ok
};

//! "1-3, 5, 2" (also "1–3" with a dash, spaces optional). Empty text = all pages.
PageList parsePageList(const std::string& text, int pageCount);

//! The visits of the music, from the page shown at each moment. `pageAt(seconds)` gives the page number;
//! sampled every `step` seconds from 0 to `total`.
template<typename PageAt>
std::vector<PageVisit> pageVisits(double total, double step, PageAt pageAt)
{
    std::vector<PageVisit> out;
    for (double t = 0.0; t < total; t += step) {
        const int page = pageAt(t);
        if (page <= 0) {
            continue;
        }
        if (!out.empty() && out.back().page == page && out.back().end >= t - step * 0.5) {
            out.back().end = std::min(total, t + step);
        } else {
            out.push_back({ page, t, std::min(total, t + step) });
        }
    }
    return out;
}

//! The pieces of music the video shows, in order. Every page = every visit in playing order.
std::vector<VideoSegment> planSegments(const std::vector<PageVisit>& visits, const PageList& list);

//! Total length of the pieces, in seconds
double totalLength(const std::vector<VideoSegment>& segments);

//! Where a moment of the video falls in the music: segment index and seconds of music (-1 if past the end)
int segmentAt(const std::vector<VideoSegment>& segments, double videoSeconds, double* musicSeconds);
}
