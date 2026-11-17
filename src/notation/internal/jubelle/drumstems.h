/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle G9: drum-component stems (File › Export drum stems…).
//
// MuseScore exports one audio file per instrument. This goes inside a drum
// kit: kick, snare, hi-hats, toms, cymbals and percussion can each be
// exported alone, or combined into groups you name.
//
// To export one stem, every other drum of the kit is switched to "don't
// play" for the length of the export and switched back afterwards. Nothing
// printed changes and nothing is left changed in the score.

#pragma once

#include <set>
#include <vector>

#include "global/types/string.h"

namespace mu::engraving {
class Part;
class Score;
}

namespace mu::notation::jubelle {
enum class DrumKind {
    Kick, Snare, ClosedHiHat, OpenHiHat, HiHatPedal, HighTom, MidTom, FloorTom, Ride, Crash, Tambourine,
    Other,   // every other drum of the kit is a component of its own
};

struct DrumComponent {
    int id = 0;                 // the kind, or 1000 + the drum's number for "other" drums
    DrumKind kind = DrumKind::Other;
    muse::String name;          // "Kick", "Snare", … or the kit's own name for the drum
    std::set<int> pitches;      // the kit's drum numbers that belong to it
    int notes = 0;              // how many notes of it the score has
};

struct StemGroup {
    muse::String name;
    std::vector<int> componentIds;
};

//! The drum-kit instruments of the score
std::vector<engraving::Part*> drumParts(engraving::Score* score);

//! The components the part actually plays in this score, in kit order
std::vector<DrumComponent> drumComponents(engraving::Score* score, const engraving::Part* part);

//! Kick, Snare, Hi-hat, Toms, Cymbals, Percussion: only the ones that have notes
std::vector<StemGroup> suggestedGroups(const std::vector<DrumComponent>& components);

//! The drum numbers of a group
std::set<int> pitchesOf(const StemGroup& group, const std::vector<DrumComponent>& components);

//! Switches every note of the part that is not one of `pitches` to "don't play".
//! Inside an open undo command. Returns how many notes still play.
int isolate(engraving::Score* score, const engraving::Part* part, const std::set<int>& pitches);

//! A name that is safe as a file name on Windows and macOS
muse::String stemFileName(const muse::String& name);

//! Groups as text and back (for saved presets). Component ids are kept as they are.
muse::String groupsToText(const std::vector<StemGroup>& groups);
std::vector<StemGroup> groupsFromText(const muse::String& text);
}
