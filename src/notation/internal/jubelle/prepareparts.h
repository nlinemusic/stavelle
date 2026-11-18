/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle: Prepare parts (Format › Prepare parts…), one step for every part.
//
// For each part, as copyists and librarians expect:
//  - multi-measure rests on;
//  - the instrument line on the part's first page names the instrument and
//    what the player doubles ("Alto Saxophone 1 (doubling Flute, Clarinet)");
//  - "Tacet" where the player has nothing: on the first page when the whole
//    piece is empty for them, otherwise at the start of each section (between
//    section breaks) with no notes. The text is in that part only;
//  - page turns moved to where the player has time (the Page turns tool);
//  - the part checked with Proofread (preparation and copying), so the report
//    says what is left to look at.
// Never changes the music. One undo step for everything.

#pragma once

#include <vector>

#include "global/types/string.h"

namespace mu::engraving {
class MasterScore;
class Score;
}

namespace mu::notation::jubelle {
struct PreparePartsOptions {
    bool multiMeasureRests = true;
    bool instrumentLine = true;
    bool tacet = true;
    bool pageTurns = true;
};

struct PreparedPart {
    engraving::Score* part = nullptr;
    muse::String name;
    bool turnedOnMMRests = false;
    muse::String instrumentLine;          // as written on the first page ("" when not changed)
    bool wholeTacet = false;
    std::vector<int> tacetSectionBars;    // first bar of each section marked Tacet (1-based)
    int turnsMoved = 0;
    int turnsStillPoor = 0;
    size_t findings = 0;                  // Proofread suggestions and warnings left for this part
};

//! "Alto Saxophone 1", or "Alto Saxophone 1 (doubling Flute, Clarinet)" when the part changes instrument
muse::String instrumentLine(const engraving::Score* part);

//! Prepares every part of the score. Call inside one open undo command on the master score.
std::vector<PreparedPart> prepareParts(engraving::MasterScore* master, const PreparePartsOptions& options = {});
}
