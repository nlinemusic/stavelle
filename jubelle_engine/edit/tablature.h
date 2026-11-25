/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * Strings and frets for notes on a tablature staff. When music changes on a tablature staff (or on the standard
 * staff it is linked to), each note needs a string and a fret: MuseScore assigns them after every edit
 * (Chord::cmdUpdateNotes → StringData::fretChords) and doesn't when it reads a file, so the engine writes them.
 *
 * The rules are MuseScore's (ported from StringData, engraving/dom/stringdata.cpp, GPL v3): the notes sounding at
 * one time on the staff (all voices) are taken from the highest pitch down; a note keeps its string while that
 * still gives its pitch; otherwise it gets the highest string it can be played on; a string already taken is
 * swapped for the first free one, from the top, that can play the note. A note that can't be played keeps the
 * string MuseScore would show it on (fret 0) and is reported as a conflict.
 *
 * Not yet covered (as MuseScore's defaults leave them): capo, string tunings changed mid-score, the staff's own
 * pitch offset, harmonics and "prefer the same string when transposing" (a preference that is off by default).
 */
#pragma once

#include <vector>

#include "changeset.h"
#include "model/model.h"

namespace je::edit {
//! An instrument's strings (lowest first, MuseScore's internal order) and frets
struct StringTuning {
    struct String {
        int pitch = 0;
        bool open = false;   // never fretted (a banjo's drone string)
        int startFret = 0;   // the 5th string of a banjo starts at fret 5
    };
    std::vector<String> strings;
    int frets = 24;

    //! From a player's STRING_TUNING and FRETS; empty if it has none
    static StringTuning of(const model::Object& player);
    bool empty() const { return strings.empty(); }
    bool fiveStringBanjo() const { return strings.size() == 5 && strings[0].pitch > strings[1].pitch; }

    //! The highest string that can play `pitch` (+ offset), MuseScore's visual number (0 = highest), and its fret;
    //! false if the note can't be played (then string and fret are where MuseScore shows it)
    bool convertPitch(int pitch, int offset, int& string, int& fret) const;
    //! The fret that gives `pitch` on `string` (visual number), or -1
    int fretFor(int pitch, int string, int offset) const;
    //! The pitch of `fret` on `string`, or -1
    int pitchOf(int string, int fret, int offset) const;
};

//! True for a staff whose notes are written as frets
bool isTablature(const model::Object& staff);

//! Assigns strings and frets to the notes sounding at time t on a tablature staff (grace chords each on their own,
//! as MuseScore does); returns the notes that can't be played there
std::vector<je::Id> fretNotes(Transaction& tx, const model::Model& m, je::Id staff, const je::Fraction& t);

//! Frets every time on the staff at which a note has no string and fret yet (notes entered). A note whose pitch
//! changed is fretted again with fretNotes, for its time only: a fretting that doesn't give the note's written
//! pitch can be intended (the end of a bend is played on the fret it started on), so it is left alone otherwise
std::vector<je::Id> fretStaff(Transaction& tx, const model::Model& m, je::Id staff);
}
