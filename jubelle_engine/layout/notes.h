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
    bool mirror = false;    // on the other side of the stem (layoutMirrors)
    double x = 0.0, y = 0.0; // its place in the chord, in its staff's spaces, with the user's offset (layoutNotePositions)
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
    // the value shown (MuseScore's durationType: 0 longa, 1 breve, 2 whole … 12 1024th) and its dots; a cross-measure
    // chord (crossMeasureValues) shows its whole length: 1 the first of the two, 2 the second
    int durationType = 4;
    int dots = 0;
    int crossMeasure = 0;
    bool up = true;          // the stem up (layoutStemDirections; also the notehead side for up and down heads)
    bool upKnown = true;     // false where the direction depends on positions not worked out yet
    bool mirrorKnown = true; // false where a chord shown with it has a direction not known yet
    double mag = 1.0;        // its size (Chord::mag: the staff's, small and grace notes')
    bool hasStem = false;    // (layoutStemLengths) Chord::shouldHaveStem
    bool beamed = false;     // in a beam (ChordRest::beam: none in a stemless measure)
    bool hook = false;       // a flag on the stem (not beamed)
    SymId hookSym = SymId::noSym;   // its symbol (Hook::symIdForHookIndex)
    double stemLength = 0.0; // in its staff's spaces, the user's length added (for a beamed chord: before its beam)
    bool defaultStemKnown = false;  // stemLength worked out (a beamed chord's too, before its beam)
    bool stemLengthKnown = false;   // false for beamed chords (their beam sets it) and what isn't ported yet
    double x = 0.0;          // (layoutChordOffsets) its offset from the others at its time, in its staff's spaces
    bool xKnown = false;
    bool stemless = false;   // in a stemless measure of its staff: no stem, and no beam shown (ChordRest::beam)
    std::vector<NoteLayout> notes;   // in MuseScore's order (Chord::sortNotes: from the bottom)
};

//! Every chord of the score, in score order (measure, time, staff, voice; grace chords around their chord)
std::vector<ChordLayout> layoutNotes(const ScoreContext& ctx);
}
