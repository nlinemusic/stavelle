/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G5 (Phase 6.2, approved): Advanced Selection and Filtering.
//
// The query engine behind Select › Find and Select…: which items in the score
// match a set of criteria (kind of item, instruments or staves, voices, pitch
// range, note length, articulation, bars or the current selection). Finding
// never changes the score; selecting only selects.
//
// Saved filters are plain text (one criterion per line, see toText/fromText),
// kept in Jubelle's settings, not in the score.

#pragma once

#include <optional>
#include <set>
#include <vector>

#include "global/types/string.h"
#include "engraving/types/fraction.h"
#include "engraving/types/symid.h"

namespace mu::engraving {
class EngravingItem;
class Score;
}

namespace mu::notation::jubelle {
enum class FindKind {
    Notes,
    Rests,
    Articulations,  // articulations and ornaments on chords
    Dynamics,
    Hairpins,
    Slurs,
    StaffText,      // staff text and expressions
    ChordSymbols,
    Lyrics,
};

struct FindQuery {
    std::set<FindKind> kinds { FindKind::Notes };

    //! staves to search (empty: all)
    std::set<size_t> staves;
    //! voices 1-4 as bits 0-3 (default: all)
    unsigned voices = 0xF;

    //! notes only: MIDI pitch range, inclusive
    std::optional<int> lowestPitch;
    std::optional<int> highestPitch;
    //! compare written pitch (as the player reads it) instead of sounding pitch
    bool writtenPitch = false;
    bool includeGraceNotes = true;
    //! notes only: skip the second and later notes of a tie
    bool skipTiedContinuations = false;

    //! notes and rests: note value range (written length), inclusive
    std::optional<engraving::Fraction> shortest;
    std::optional<engraving::Fraction> longest;

    //! notes only: only notes whose chord has one of these articulations (empty: any or none)
    std::set<engraving::SymId> withArticulation;
    //! articulations only: only these symbols (empty: all)
    std::set<engraving::SymId> articulationSymbols;

    //! bars to search, 1-based, inclusive (none: the whole score)
    std::optional<int> fromBar;
    std::optional<int> toBar;
    //! search only the current range selection
    bool inSelection = false;
};

//! Items matching the query, in score order
std::vector<engraving::EngravingItem*> findItems(const engraving::Score* score, const FindQuery& query);

//! Replaces the score's selection with the items (returns how many were selected)
size_t selectItems(engraving::Score* score, const std::vector<engraving::EngravingItem*>& items);

//! Saved filters: a query as text and back (unknown lines are skipped)
muse::String toText(const FindQuery& query);
FindQuery fromText(const muse::String& text);
}
