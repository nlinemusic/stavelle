/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::layout). Not part of MuseScore.
 *
 * The first part of stage ② shaping (S7): what each note shows before anything is placed — its staff line, its
 * notehead and its accidental — as MuseScore works them out (ModifyDom::cmdUpdateNotes, Chord::cmdUpdateNotes,
 * Note::updateAccidental, Note::updateRelLine, Note::noteHead), with Jubelle's automatic courtesy accidentals.
 */
#pragma once

#include <vector>

#include "context.h"

namespace je::layout {
struct NoteLayout {
    Id note;
    int pitch = 0;
    int tpc = 0;            // as shown (written or concert)
    int line = 0;           // the staff line (0: top line, down in half spaces)
    SymId headUp = SymId::noSym, headDown = SymId::noSym;   // the notehead for a stem up and down
    bool headKnown = true;  // false where the notehead depends on what isn't worked out yet (drum sets, shape notes)
    SymId accidental = SymId::noSym;
    bool userAccidental = false;
    bool courtesy = false;  // shown only as a Jubelle courtesy accidental
};

struct ChordLayout {
    Id chord;
    char kind = 'C';        // 'G' a grace chord before, 'A' after, 'C' the chord
    int staff = 0;          // its own staff (not where a cross-staff chord is shown)
    int shownStaff = 0;
    int voice = 0;
    Fraction tick;          // the chord's time (a grace chord's: its chord's)
    bool standard = true;   // on a standard staff (drum and tablature staves are worked out differently)
    bool shown = true;      // its instrument is shown (MuseScore lays out no other)
    std::vector<NoteLayout> notes;   // in MuseScore's order (Chord::sortNotes: from the bottom)
};

//! Every chord of the score, in score order (measure, time, staff, voice; grace chords around their chord)
std::vector<ChordLayout> layoutNotes(const ScoreContext& ctx);
}
