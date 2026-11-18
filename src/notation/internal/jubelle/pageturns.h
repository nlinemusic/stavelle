/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle G8: Page-Turn Optimisation (Format › Page turns…).
//
// Looks at where the player has to turn the page and how much rest there is
// around each turn, then moves poor turns back to the nearest place with
// enough rest by adding page breaks. Never changes music. Page breaks you
// put in yourself are kept and treated as fixed. Jubelle's own breaks are
// marked (a private drawing-order value that MuseScore ignores), so they can
// be removed or redone later without touching yours.

#pragma once

#include <vector>

namespace mu::engraving {
class EngravingItem;
class Measure;
class Score;
}

namespace mu::notation::jubelle {
enum class TurnLayout {
    Auto,                 // two-sided pages: after right-hand pages; otherwise after every page
    AfterRightHandPages,  // page 1 on the right: turns after pages 1, 3, 5…
    AfterLeftHandPages,   // page 1 on the left: turns after pages 2, 4, 6…
    EveryPage,            // single sheets or a tablet
};

enum class TurnQuality {
    Good,   // at least the time needed
    Tight,  // at least half of it
    Poor,   // less
};

struct TurnSettings {
    double neededSeconds = 2.5;   // rest needed around a turn
    TurnLayout layout = TurnLayout::Auto;
    double minPageFill = 0.5;     // a turn is never moved further back than this share of the page's systems
};

struct PageTurn {
    int page = 0;                               // the page that is turned (1 = first)
    engraving::Measure* lastMeasure = nullptr;  // the last written bar before the turn
    int nextBar = 0;                            // number of the first bar after the turn
    double restBefore = 0.0;                    // seconds of rest up to the turn
    double restAfter = 0.0;                     // seconds of rest after it
    TurnQuality quality = TurnQuality::Poor;
    bool userBreak = false;                     // a page break you put in: fixed
    bool jubelleBreak = false;                  // a page break from an earlier optimisation

    double rest() const { return restBefore + restAfter; }
};

//! The turns of the score as it is laid out now
std::vector<PageTurn> analyseTurns(engraving::Score* score, const TurnSettings& settings);

struct TurnChange {
    int page = 0;
    int nextBarBefore = 0;     // first bar after the turn, before and after optimisation
    int nextBarAfter = 0;
    double restWas = 0.0;      // seconds
    double restNow = 0.0;
    TurnQuality was = TurnQuality::Poor;
    TurnQuality now = TurnQuality::Poor;
    bool moved = false;
    bool userBreak = false;    // fixed by your own page break
};

struct TurnResult {
    std::vector<TurnChange> turns;
    int moved = 0;
    int stillPoor = 0;
};

//! Moves poor turns to better places. Must be called inside an open undo command; lays the score out.
TurnResult optimiseTurns(engraving::Score* score, const TurnSettings& settings);

//! Removes the page breaks Jubelle added (yours are kept). Inside an open undo command. Returns how many.
int clearJubelleTurns(engraving::Score* score);

//! Jubelle: a page break that Page turns placed (not one of yours)
bool isJubelleTurnBreak(const engraving::EngravingItem* item);
//! Makes a page break Page turns placed into your own (kept when turns are optimised again). Undoable.
void keepTurnBreakAsOwn(engraving::Score* score, engraving::EngravingItem* item);

//! Seconds of rest (all staves silent) just before and just after a tick
double restBefore(engraving::Score* score, int tick);
double restAfter(engraving::Score* score, int tick);
}
