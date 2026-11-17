/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle Phase 5A, level 3: articulation playback for a highlighted passage.
// Creates, updates and removes the hidden "Jubelle playback:" lines (see
// engraving/playback/jubelle/articulationplayback.h), one per highlighted
// instrument, and tells where each value comes from.

#pragma once

#include <optional>
#include <vector>

#include "global/types/string.h"

#include "engraving/playback/jubelle/articulationplayback.h"

namespace mu::engraving {
class EngravingItem;
class Part;
class Score;
}

namespace mu::notation::jubelle {
struct PassageRange {
    int startTick = 0;
    int endTick = 0;                          // exclusive
    std::vector<engraving::Part*> parts;      // in the main score
    bool isValid() const { return endTick > startTick && !parts.empty(); }
};

//! The highlighted passage (range or list selection), in the main score
PassageRange passageFromSelection(const engraving::Score* score);

enum class ValueSource {
    MuseScore,     // nothing set anywhere
    WholeScore,
    Instrument,
    OtherPassage,  // a passage around or overlapping this one
    Here,          // set for exactly this passage
};

struct PassageRow {
    engraving::jubelle::ArtParam param;
    int value = 0;
    ValueSource source = ValueSource::MuseScore;
    muse::String sourceName;   // instrument name, or "bars 33–40"
    bool inSelection = false;  // the articulation occurs in the passage
    bool varies = false;       // differs between the selected instruments
};

//! One row per setting, as it applies at the start of the passage for its
//! first instrument
std::vector<PassageRow> passageRows(const engraving::Score* score, const PassageRange& range);

//! Sets (or with nullopt clears) one value for the passage, for each of its
//! instruments. Undoable: call inside startCmd/endCmd.
void setPassageValue(engraving::Score* score, const PassageRange& range, engraving::jubelle::ArtParam param,
                     std::optional<int> value);

//! Clears every value for the passage (removes its lines)
void clearPassage(engraving::Score* score, const PassageRange& range);

// --- Properties panel (selected articulations) ------------------------------

//! The level a Properties-panel change is made at
enum class EditLevel {
    Passage = 0,     // the selected notes' passage (a playback line per instrument)
    Instrument = 1,  // the selected notes' instruments
    WholeScore = 2,
};

//! The passage (first to last selected articulation) and instruments of
//! the selected articulations, in the main score
PassageRange passageForItems(const engraving::Score* score, const std::vector<engraving::EngravingItem*>& items);
//! The settings that matter for these articulations, in display order
std::vector<engraving::jubelle::ArtParam> paramsForItems(const std::vector<engraving::EngravingItem*>& items);
//! Rows for `params` as seen at `level` (value, where it comes from, varies)
std::vector<PassageRow> levelRows(const engraving::Score* score, const PassageRange& range, EditLevel level,
                                  const std::vector<engraving::jubelle::ArtParam>& params);
//! Sets or (nullopt) clears a value at a level. Undoable: inside a command.
void setLevelValue(engraving::Score* score, const PassageRange& range, EditLevel level, engraving::jubelle::ArtParam param,
                   std::optional<int> value);

//! Untranslated English wording for a setting (translate in context "notation")
const char* paramGroup(engraving::jubelle::ArtParam param);
const char* paramTitle(engraving::jubelle::ArtParam param);
const char* paramUnit(engraving::jubelle::ArtParam param);   // "%", "semitones", "curve", "% of normal"
}
