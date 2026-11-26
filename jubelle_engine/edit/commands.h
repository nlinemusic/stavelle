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

    // ---- measures -----------------------------------------------------------------------------------------------
    //! Score::cmdAddTimeSig (a time signature for every staff): from measure `measure` to the next time signature
    //! or section break the music is rewritten into measures of num/den (rewriteMeasures: notes split and tied at
    //! the new barlines, the last measure filled with rests); the same length (4/4 → 2/2) only renames them; a
    //! pickup keeps its length. False, with nothing changed, where MuseScore refuses: a tuplet across a new
    //! barline, or a staff with its own time signature in music that isn't all measure rests
    bool changeTimeSignature(Transaction& tx, size_t measure, int num, int den);

    //! SplitJoinMeasure::splitMeasure: the measure is split at `tick` (where a chord or rest starts) into two
    //! measures of the same time signature, the music written again into them. False, nothing changed, at the
    //! first beat, inside a tuplet, on a measure repeat, or for a length finer than 1/128
    bool splitMeasure(Transaction& tx, const Fraction& tick);
    //! Where MuseScore can split: a chord or rest starts there, or something is attached there (a chord symbol…)
    bool canSplitAt(const Fraction& tick) const;
    //! SplitJoinMeasure::joinMeasures: measures first … last become one, with the first one's time signature; the
    //! time signatures inside go (each staff's last one moves to the next measure if it has none)
    bool joinMeasures(Transaction& tx, size_t first, size_t last);

    // ---- explode and implode (staves by index in score order) ----------------------------------------------------
    //! Score::cmdExplode: staff `first`'s music spread over the staves first … last-1 (one staff chosen: as many as
    //! needed), in whole measures from `from` to `to`: with voices 2–4, each voice to its own staff; otherwise the
    //! music copied to the staves below and each keeping one note of every chord, from the top. Returns the staff
    //! after the last one used, or -1 where MuseScore refuses (staves with different local time signatures)
    int explode(Transaction& tx, size_t first, size_t last, const Fraction& from, const Fraction& to);
    //! Score::cmdImplode: one staff: its voices joined into voice 1 where the lengths match; several: their first
    //! four voices with music become the first staff's voices
    bool implode(Transaction& tx, size_t first, size_t last, const Fraction& t0, const Fraction& t1);

    // ---- slashes (staves by index in score order) ------------------------------------------------------------------
    //! Score::cmdSlashFill: slashes on every beat of staves first … last-1 in [t0, t1) (stemless, not played), in
    //! each measure's first voice that is free there; returns how many were made
    int slashFill(Transaction& tx, size_t first, size_t last, const Fraction& t0, const Fraction& t1, bool concertPitch);
    //! Score::cmdSlashRhythm: the chords shown as slashes with their rhythm, or back; rests in voices 3 and 4 as
    //! accent rests, or back
    bool slashRhythm(Transaction& tx, const std::vector<Id>& events);
    //! Chord::slash
    bool isSlash(Id chord) const;
    //! A chord symbol of the score, by itself or in a fret diagram (whose chord the model doesn't hold yet: read
    //! from its details); track as MuseScore counts it (staff × 4 + voice)
    struct ChordSymbol {
        Id item;
        Fraction time;
        int track = 0;
        int root = -9, bass = -9;   // MuseScore's TPC_INVALID when none ("N.C.")
        std::string name;
        bool inFretDiagram = false;
        bool polychord = false;   // several chords stacked: the first one's root, bass and name
    };
    //! Every chord symbol in time order (MuseScore's segment order)
    std::vector<ChordSymbol> chordSymbols() const;

    //! Score::cmdRealizeChordSymbols: each chord symbol written out as notes on its staff and voice, from its time
    //! for its duration (tied across barlines). Without `override`, each symbol's own settings (or the style's):
    //! literal or jazz, voicing, duration. Returns how many were realized
    struct RealizeOptions {
        bool override = false;
        bool literal = true;
        int voicing = 0;    // chords::Voicing
        int duration = 0;   // chords::Duration
    };
    int realizeChordSymbols(Transaction& tx, const std::vector<Id>& harmonies, const RealizeOptions& options, bool concertPitch);

    //! Score::moveUp / moveDown ("move chord/rest to staff above / below"): each chord or rest shown on the next
    //! staff of its instrument (cross-staff, one staff at most either way; standard staves only). Returns how many
    //! moved
    int moveToStaff(Transaction& tx, const std::vector<Id>& events, bool up);

    // ---- text (edit/text.cpp) -----------------------------------------------------------------------------------
    //! Score::addText with MuseScore's text style name ("staff", "system", "expression", "dynamics",
    //! "rehearsal_mark", "tempo", "fingering", "lh_guitar_fingering", "rh_guitar_fingering", "string_number") on a
    //! chord, rest or note (fingerings: a note), and the text typed in it (TextBase::endEdit): the item made, or none
    //! (refused, or left empty: MuseScore takes an empty text away). A rehearsal mark gets the next in the score's
    //! sequence and a tempo text the time signature's beat at 80 unless text is typed; a dynamic typed as its name
    //! ("mf") becomes that dynamic. Lyrics: edit/lyrics.h
    Id addText(Transaction& tx, const std::string& style, Id destination, const std::string& text);
    //! Score::createRehearsalMarkText: the next rehearsal mark at t, in the sequence of those before it
    std::string rehearsalMarkText(const Fraction& t) const;

    //! The staves in score order (players in order, their staves in order), as the indices above count them
    std::vector<Id> stavesInOrder() const { return allStaves(); }

private:
    struct StaffLines {
        int lines = 5, stepOffset = 0;
        bool upsideDown = false;
    };
    struct SlashPitch {
        bool ok = true;
        int pitch = 71;
        int64_t tpc = 19;
        int string = -1, fret = -1;
    };
    //! Sets (an empty value: removes) child elements of an object's kept details (appearance the model doesn't hold)
    void setDetailValues(Transaction& tx, Id id, const std::vector<std::pair<std::string, std::string> >& values,
                         Field addTo = model::fields::DETAILS);
    //! A child element's value in an object's kept details (before or after its notes or pitch), if they still fit
    std::string detailOf(Id id, const std::string& tag) const;
    std::vector<Id> notesOf(Id chord) const;
    StaffLines staffLines(Id staff) const;
    std::string clefAt(Id staff, const Fraction& t, bool concertPitch) const;
    void setSlash(Transaction& tx, Id chord, bool on, bool stemless);
    std::vector<Id> linkedStavesOf(Id staff) const;
    Id eventAt(Id staff, int voice, const Fraction& t, model::Kind kind) const;
    SlashPitch slashPitch(Id staff, const Fraction& t, bool concertPitch) const;
    bool insideTuplet(Id voice, const Fraction& t) const;

    std::vector<Id> timeSigsAt(const Fraction& t) const;
    int measureRepeatCount(Id measure) const;
    Id voiceNumbered(Id staff, int number) const;
    std::vector<Id> eventsIn(Id voice, const Fraction& t0, const Fraction& t1) const;
    bool linkedTogether(const std::vector<Id>& staves) const;
    bool sameStretch(const std::vector<Id>& staves, const Fraction& t0, const Fraction& t1) const;
    //! MuseScore's CloneVoice: a voice's music (notes, ties, tuplets, its slurs, hairpins and annotations) to
    //! another voice, replacing it; `move`: taken from the source (voice 1 left with rests)
    void cloneVoice(Transaction& tx, Id fromStaff, int fromVoice, Id toStaff, int toVoice, const Fraction& t0,
                    const Fraction& t1, bool move);
    std::vector<Id> timeItemsAt(const Fraction& t) const;
    bool localTimeSignatures(const Fraction& t0, const Fraction& t1) const;
    std::vector<Id> allStaves() const;
    void setTimeSigs(Transaction& tx, const Fraction& t, int64_t num, int64_t den);
    bool rewriteMeasures(Transaction& tx, size_t from, int num, int den);
    //! The measures [first, last] written again as measures of the given lengths (none: of num/den, as many as the
    //! music needs), with num/den their nominal time signature
    void rewriteRun(Transaction& tx, const std::vector<Id>& measures, size_t first, size_t last, int num, int den,
                    std::vector<Fraction> lengths = {});
    //! An event written again as tied pieces: the first is the event itself, the others new (with its notes)
    struct Pieces {
        Id source, voice, prev;
        bool rest = true, first = true, gone = false;
        std::vector<Id> notes, tiedTo;
        std::vector<bool> tieGenerated;
    };
    Pieces startPieces(Id source, Id voice) const;
    bool hasOwnTie(Id note) const;
    Id addPiece(Transaction& tx, Pieces& p, const Fraction& t, const Fraction& duration, const NoteValue* v, bool measureRest, Id tuplet);
    void finishPieces(Transaction& tx, const Pieces& p);

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
