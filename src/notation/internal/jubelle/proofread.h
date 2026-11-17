/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G5 (Phase 6.3, approved): Score Proofreading, the core checks.
//
// Looks through the score and reports what a copyist would flag. It never
// changes notes or notation: findings can only be jumped to, dismissed for
// now, or marked as intentional (remembered in the score as a metaTag, which
// MuseScore keeps and ignores).
//
// Checks:
//  - range: notes beyond the instrument's professional range (error) or its
//    amateur range (warning), with the same limits MuseScore colours notes by;
//  - large leaps, by instrument group (suggestion);
//  - spelling: double sharps and flats (suggestion), and a courtesy accidental
//    worth adding after a bar line (suggestion, unless automatic courtesy
//    accidentals are on);
//  - bars that aren't full in voice 1 (error);
//  - dynamics: a part's first note without a dynamic, and the same dynamic
//    repeated with nothing changed in between (suggestion);
//  - rehearsal marks: gaps and duplicates in the sequence (warning);
//  - instrument names: two parts with the same name (warning), a missing
//    short name when the score has more than one system (suggestion);
//  - voice crossings: voice 2 above voice 1 on a staff (suggestion);
//  - collisions (G7): overlapping markings, from the layout's overlap checker
//    (warning).
//
// Professional music-preparation practice (17 Nov 2026). Principles from the
// Music Publishers Association's "Standard Music Notation Practice", the Major
// Orchestra Librarians' Association's "Music Preparation Guidelines for
// Orchestral Music" and standard copyists' practice, expressed as checks (no
// text or illustrations are taken from those publications):
//  - metric grouping: in 4/4 a note or rest that hides beat 3, other than the
//    plain quarter-half-quarter syncopation or a note starting on the beat
//    (suggestion);
//  - placement: dynamics below instrumental staves, above vocal staves with
//    lyrics (suggestion; only for dynamics still placed automatically);
//  - preparation: more than 16 bars without a rehearsal mark; a player
//    resting 16 bars or more who then enters without a cue; no tempo marking
//    at the start; no title or composer; bar numbers switched off.

#pragma once

#include <set>
#include <string>
#include <vector>

#include "global/types/string.h"
#include "engraving/types/fraction.h"

namespace mu::engraving {
class EngravingItem;
class Score;
}

namespace mu::notation::jubelle {
enum class Severity {
    Error,
    Warning,
    Suggestion,
};

enum class CheckCategory {
    Range,
    Leaps,
    Spelling,
    IncompleteBars,
    Dynamics,
    RehearsalMarks,
    InstrumentNames,
    VoiceCrossings,
    Collisions,
    // Professional music-preparation practice (17 Nov 2026; added at the end so
    // keys of findings already marked as intentional stay valid)
    MetricGrouping,   // rhythms that hide the middle of the bar
    Placement,        // dynamics above for voices, below for instruments
    Preparation,      // what players, conductors and librarians expect in score and parts
};

struct Finding {
    Severity severity = Severity::Suggestion;
    CheckCategory category = CheckCategory::Range;
    muse::String message;                     // translated, ready to show
    engraving::EngravingItem* item = nullptr; // where to jump (may be null)
    engraving::Fraction tick;                 // where it is
    size_t staffIdx = 0;
    int bar = 0;                              // 1-based
    std::string key;                          // stable identity, for "Mark as intentional"
};

struct ProofreadOptions {
    std::set<CheckCategory> categories {
        CheckCategory::Range, CheckCategory::Leaps, CheckCategory::Spelling, CheckCategory::IncompleteBars,
        CheckCategory::Dynamics, CheckCategory::RehearsalMarks, CheckCategory::InstrumentNames,
        CheckCategory::VoiceCrossings, CheckCategory::Collisions, CheckCategory::MetricGrouping,
        CheckCategory::Placement, CheckCategory::Preparation,
    };
    bool includeIntentional = false;   // also list findings marked as intentional
};

//! All findings, in score order (by bar, then staff)
std::vector<Finding> proofread(engraving::Score* score, const ProofreadOptions& options = {});

//! The metaTag that holds the keys of findings marked as intentional
constexpr const char16_t* INTENTIONAL_TAG = u"jubelleProofreadIntentional";
std::set<std::string> intentionalKeys(const engraving::Score* score);
//! Marks or unmarks a finding as intentional (stored in the score; undoable
//! as a score change)
void setIntentional(engraving::Score* score, const std::string& key, bool intentional);

muse::String categoryTitle(CheckCategory c);
}
