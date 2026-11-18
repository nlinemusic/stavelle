/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#pragma once

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "engraving/types/types.h"

#include "harmonyrules.h"

namespace mu::engraving {
class Score;
}

//! Jubelle: Harmonise melody, the score side of the Big Band Harmony and Orchestration System.
//!
//! Reads the melody (voice 1 of the top staff of a range selection) and the chord symbols already in the
//! score, voices it with the lesson rules (harmonyrules.h) and writes the parts into the trumpet, trombone
//! and saxophone staves. The rhythm, ties, articulations, slurs, dynamics and breath marks are copied from
//! the melody exactly as MuseScore copies and pastes them; only the pitches are then changed. Lyrics, chord
//! symbols, other text, fingering, ottavas and grace notes are not copied.
//!
//! Notes are written at concert pitch; MuseScore shows each transposing instrument in its written key.
//! Spelling follows lesson 8 per player, in the key the player reads.
//!
//! The caller wraps run() in one undoable command (startCmd/endCmd), so one Undo removes everything.
//! The melody staff is never changed.
namespace mu::notation::jubelle::harmony {
enum class ExistingNotes {
    Replace,       //!< write over what the target staff has in the passage
    KeepExisting,  //!< leave a staff that already has notes in the passage alone (it is listed in the report)
};

//! Which staff plays which part; muse::nidx where the score has no such player.
struct SectionStaves {
    std::array<engraving::staff_idx_t, 4> trumpets { muse::nidx, muse::nidx, muse::nidx, muse::nidx };
    std::array<engraving::staff_idx_t, 4> trombones { muse::nidx, muse::nidx, muse::nidx, muse::nidx };
    std::array<engraving::staff_idx_t, 5> saxes { muse::nidx, muse::nidx, muse::nidx, muse::nidx, muse::nidx };
    engraving::staff_idx_t chordSymbols = muse::nidx;   //!< piano, else guitar, else the top staff (your decision 4)
};

struct RunOptions {
    HarmonyOptions harmony;
    OrchestrationOptions orchestration;
    ExistingNotes existing = ExistingNotes::Replace;
    //! Chord symbols that the passage changes (alterations, the half-step slide) are updated where they are;
    //! with this on they are also written on the chord-symbol staff when it has none at that point.
    bool writeChordSymbols = true;
    bool useStaves = false;      //!< false: the staves are found by instrument (findSections)
    SectionStaves staves;
};

//! The section staves found by instrument, in score order: B♭/C trumpets (and flugelhorns) as trumpets 1–4,
//! trombones then bass trombone as 1–4, altos, tenors and baritone as the sax section.
SectionStaves findSections(const engraving::Score* score);

//! What run() would work on right now, for the dialog.
struct Scope {
    bool ok = false;
    std::string problem;          //!< why not, in plain words
    engraving::staff_idx_t melodyStaff = muse::nidx;
    std::string melodyStaffName;
    int firstBar = 0;             //!< 1-based
    int lastBar = 0;
    int melodyNotes = 0;
    int chordSymbols = 0;         //!< chord symbols that sound under the melody
    int identifiedChords = 0;     //!< chords named from the rhythm section's notes where there is no chord symbol
    int notesWithoutChord = 0;    //!< melody notes with no chord at all (left in unison)
    std::vector<std::string> chordNames;
    std::optional<std::string> saxWarning;   //!< the lessons' speed rule for spread voicings (lesson 6)
    int fastNotes = 0;            //!< melody notes shorter than FAST_NOTE_SECONDS
    std::vector<std::string> notices;   //!< what will happen that you might not expect, in plain words
    double longestNoteSeconds = 0;
};

Scope scope(engraving::Score* score, const RunOptions& options);

struct Report {
    bool ok = false;
    std::string problem;
    int notesWritten = 0;
    int stavesWritten = 0;
    std::vector<std::string> stavesKept;      //!< staves left alone because they already had notes
    int outOfRange = 0;                        //!< written notes outside the player's professional range
    std::vector<std::string> outOfRangeWhere; //!< "Tenor Sax 2, bar 6"
    int chordSymbolsChanged = 0;
    int chordSymbolsWritten = 0;
    int nonChordTones = 0;
    int planed = 0;
    int diatonic = 0;
    int diminished = 0;
    std::vector<std::string> notes;           //!< plain remarks from the rules
    std::vector<std::string> notices;         //!< as in Scope
    int awkwardSpellings = 0;                 //!< steps that look wrong however the line is spelled (Proofread lists them)
    std::vector<std::string> awkwardWhere;
    int unisonNotes = 0;                      //!< melody notes left in unison (no chord, or none that keeps the rules)
    std::vector<int> unisonBars;
};

Report run(engraving::Score* score, const RunOptions& options);
}
