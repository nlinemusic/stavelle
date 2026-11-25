/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * The editing commands of the interface that build on the operations (operations.h): spelling and accidentals,
 * intervals, articulations, voices, grace notes, lines, clefs and key signatures. Each one follows the MuseScore
 * Studio 4.7.7 command it replaces (named with each function), so the interface behaves as it does today. Every
 * command works in the caller's transaction (one undo step) and leaves the model consistent.
 */
#pragma once

#include <string>
#include <vector>

#include "operations.h"

namespace je::edit {
class Commands
{
public:
    explicit Commands(Operations& ops)
        : m_ops(ops) {}

    // ---- pitch and spelling -------------------------------------------------------------------------------------
    //! Score::changeEnharmonicSpelling: each note's spelling moves to the next enharmonic one (C♯ → D♭ → B𝄪 …).
    //! `concertPitch`: the spelling shown (concert or written); `both`: change the other spelling too (always
    //! for instruments that don't transpose)
    void changeEnharmonicSpelling(Transaction& tx, const std::vector<Id>& notes, bool concertPitch, bool both);

    //! Score::changeAccidental: the note's letter stays, with this alteration (−2 … 2) of the spelling shown
    void changeAccidental(Transaction& tx, Id note, int alteration, bool concertPitch);
    //! Score::toggleAccidental: that alteration, or back to the key's when the note already has it
    void toggleAccidental(Transaction& tx, Id note, int alteration, bool concertPitch);

    //! Score::addInterval: a note `interval` steps above (2 = a second, 3 = a third …) or below (−2 …) each note, in
    //! the key (an octave or unison keeps the spelling); tied notes get it all along the tie, tied too. Returns the
    //! new notes
    std::vector<Id> addInterval(Transaction& tx, const std::vector<Id>& notes, int interval);

    // ---- articulations and ornaments ----------------------------------------------------------------------------
    //! Score::toggleArticulation: each chord gets the articulation, or loses it if it has it (above or below)
    void toggleArticulation(Transaction& tx, const std::vector<Id>& chords, const std::string& name);

    // ---- voices --------------------------------------------------------------------------------------------------
    //! Score::changeSelectedElementsVoice: chords and rests (or single notes) move to voice `voice` (0–3) of their
    //! staff where it has room at that time: a rest of the same length there, or nothing (voices 2–4); a note can
    //! join a chord of the same length. What can't move stays. Returns how many moved
    int changeVoice(Transaction& tx, const std::vector<Id>& items, int voice);

    //! Score::swapVoices (Swap voices in a range): the music of voices `a` and `b` of the staff in [t0, t1)
    void swapVoices(Transaction& tx, Id staff, const Fraction& t0, const Fraction& t1, int a, int b);

    // ---- grace notes ---------------------------------------------------------------------------------------------
    //! Score::addGraceNote: a grace chord with the note's pitch, before the chord (acciaccatura, appoggiatura,
    //! grace4, grace16, grace32) or after it (grace8after, grace16after, grace32after), next to it
    Id addGraceNote(Transaction& tx, Id note, const std::string& type, const Fraction& value);

    // ---- lines and items -----------------------------------------------------------------------------------------
    //! A line on a staff from t0 to t1 in a voice (slur, hairpin, ottava, pedal, trill, textline, letring,
    //! palmmute, vibrato …); `subtype` is MuseScore's (a hairpin's 0 crescendo / 1 decrescendo, an ottava's "8va")
    Id addLine(Transaction& tx, const std::string& kind, Id staff, int voice, const Fraction& t0, const Fraction& t1,
               int64_t intSubtype = 0, const std::string& textSubtype = std::string());
    //! Score::addSlur over chords: from the first chord to the last (one chord: to the next in its voice)
    Id addSlur(Transaction& tx, Id firstChord, Id lastChord);
    //! A line from one note to another (glissando, guitar bend, note line)
    Id addNoteLine(Transaction& tx, const std::string& type, Id fromNote, Id toNote);

    //! Score::undoChangeKeySig: a key signature (fifths) from t, on every staff (or one staff) until the next
    Id insertKeySignature(Transaction& tx, const Fraction& t, int64_t fifths, Id staff = Id());
    //! Score::undoChangeClef: a clef (MuseScore's name: "G", "F", "C3", "TAB" …) on the staff at t
    Id insertClef(Transaction& tx, Id staff, const Fraction& t, const std::string& clef);

private:
    struct Spelling {
        Field field;     // the spelling shown: TPC (concert) or TPC2 (written)
        int64_t tpc;
        int64_t dia, chrom;   // the instrument's transposition
    };
    Spelling shown(Id note, bool concertPitch) const;
    void setSpelling(Transaction& tx, Id note, const Spelling& s, int64_t newTpc, int semitones, bool both);
    Id staffOfEvent(Id event) const;
    Id flowOf() const;
    bool drums(Id staff) const;
    std::pair<std::vector<Id>, std::vector<Id> > gracesOf(Id chord) const;
    void moveGraces(Transaction& tx, const std::pair<std::vector<Id>, std::vector<Id> >& graces, Id toVoice, Id beside);

    Operations& m_ops;
};
}
