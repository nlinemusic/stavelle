/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * The operation vocabulary (docs/10_edit_vocabulary.md, S3): the edits the interface's commands are made of,
 * each working inside a transaction so that a command is one undo step and a failure rolls everything back.
 *
 * Note entry follows MuseScore's rules, so that scores edited in Jubelle behave as users expect:
 * - entering a chord or rest overwrites the music it covers in that voice; an event cut at the start of the range is
 *   shortened, the rest of one cut at its end becomes rests;
 * - an event longer than what is left of its measure continues into the next measures, tied for chords;
 * - lengths are written as note values that start on a multiple of their (undotted) value from the barline, dots
 *   where they fit, so the written music stays readable;
 * - in voice 1 every measure stays full (deleting a chord leaves a rest); voices 2–4 may have gaps;
 * - a tuplet is entered into as a whole: music inside it is replaced in its own values; a range that covers only
 *   part of a tuplet from outside removes the whole tuplet, and what it held beyond the range becomes rests.
 */
#pragma once

#include <optional>
#include <vector>

#include "changeset.h"
#include "structure.h"
#include "model/model.h"

namespace je::edit {
//! A written note value: base (1/4 for a quarter) and dots
struct NoteValue {
    Fraction base;
    int dots = 0;
    Fraction length() const;
    bool operator==(const NoteValue& o) const { return base == o.base && dots == o.dots; }
};

//! The written values that fill `length` starting `position` after the barline (as MuseScore writes them): rests
//! undotted and on their own beat; notes may be dotted
std::vector<NoteValue> valuesFor(const Fraction& position, const Fraction& length, bool forRest);

class Operations
{
public:
    Operations(model::Model& model, IdGenerator& ids)
        : m(model), m_ids(ids) {}

    // ---- reading the current state (inside or outside a transaction) ----
    //! The measure holding time t (the last one at or after the end)
    Id measureAt(const Fraction& t) const;
    Fraction measureStart(Id measure) const;
    Fraction measureEnd(Id measure) const;
    std::vector<Id> measures() const;
    //! The events of a voice (chords and rests, grace notes included) in time order
    std::vector<Id> eventsOf(Id voice) const;
    //! The voice `number` (0 … 3) of a staff, made if it doesn't exist
    Id voice(Transaction& tx, Id staff, int number);
    Id staffOf(Id voice) const;
    int64_t keyAt(Id staff, const Fraction& t) const;

    //! A new note's fields on a staff: pitch, spelling for the key, written spelling on a transposing instrument
    std::vector<std::pair<Field, Value> > noteFields(Id staff, const Fraction& t, int pitch) const;
    //! The written spelling on this staff's instrument for a concert spelling (none if it doesn't transpose)
    std::optional<int64_t> writtenTpc(Id staff, int64_t tpc) const;

    // ---- note entry ----
    //! Enters a chord with these pitches (sounding) at t; returns the first chord made (later ones are tied to it)
    Id enterChord(Transaction& tx, Id voice, const Fraction& t, const NoteValue& value, const std::vector<int>& pitches);
    //! Enters a rest at t (split at barlines)
    Id enterRest(Transaction& tx, Id voice, const Fraction& t, const NoteValue& value);
    //! Adds a note to a chord (nothing if the pitch is already there); returns the note
    Id addNote(Transaction& tx, Id chord, int pitch);
    //! Removes a note; a chord left without notes becomes a rest
    void removeNote(Transaction& tx, Id note);
    //! A new pitch, spelled for the key unless a spelling is given
    void setPitch(Transaction& tx, Id note, int pitch, int64_t tpc = -1);
    //! Ties the note to the note of the same pitch in the next chord of its voice (false if there is none)
    bool tieToNext(Transaction& tx, Id note);
    void untie(Transaction& tx, Id note);

    // ---- changing what is there ----
    //! Deletes a chord or rest: in voice 1 it becomes a rest of the same length; in other voices it leaves a gap
    void deleteEvent(Transaction& tx, Id event);
    //! A new written value for a chord or rest: longer overwrites what follows, shorter leaves rests after it
    void setValue(Transaction& tx, Id event, const NoteValue& value);
    //! Turns the range [t, t + span) of a voice into a tuplet (ratio actual:normal of `base` values) holding rests
    Id makeTuplet(Transaction& tx, Id voice, const Fraction& t, int actual, int normal, const Fraction& base);
    //! Removes the music of a voice in [t0, t1) (an event cut at the start is shortened, the rest of one cut at the
    //! end becomes rests); voice 1 is refilled with rests; returns nothing
    void clearRange(Transaction& tx, Id voice, const Fraction& t0, const Fraction& t1);
    //! Rests for [t0, t1) of a voice that is empty there, in MuseScore's values, measure by measure
    void fillGap(Transaction& tx, Id voice, const Fraction& t0, const Fraction& t1) { fillRests(tx, voice, t0, t1); }

    // ---- transposition ----
    //! Moves notes by an interval (diatonic steps and semitones: a major third up is 2, 4); spellings follow the
    //! interval along the line of fifths, so C → E and F♯ → A♯; ties stay (tied notes move together)
    void transposeNotes(Transaction& tx, const std::vector<Id>& notes, int diatonic, int chromatic);
    //! Every note of these staves in [t0, t1) (chord symbols' roots too)
    void transposeRange(Transaction& tx, const std::vector<Id>& staves, const Fraction& t0, const Fraction& t1, int diatonic,
                        int chromatic);

    // ---- measures ----
    //! Inserts `count` measures (num/den) before the measure at index `before` (or at the end): everything later
    //! moves; every staff's voice 1 gets measure rests
    void insertMeasures(Transaction& tx, size_t before, int count, int num, int den);
    //! Removes measures [first, first + count) and their music; everything later moves back
    void removeMeasures(Transaction& tx, size_t first, size_t count);

private:
    const model::Object* obj(Id id) const { return m.find(id); }
    Fraction timeOf(Id id) const;
    Fraction endOf(Id id) const;
    Fraction tupletFactor(Id tuplet) const;

    void removeEvent(Transaction& tx, Id event);
    void fillRests(Transaction& tx, Id voice, Fraction t0, const Fraction& t1, Id tuplet = Id());
    //! Writes events for [t, t + length) in measure-sized pieces; returns the ids made, in order
    std::vector<Id> writeEvents(Transaction& tx, Id voice, const Fraction& t, const Fraction& length, bool rest,
                                const std::vector<int>& pitches, Id tuplet);
    void shiftFrom(Transaction& tx, const Fraction& from, const Fraction& delta);

    model::Model& m;
    IdGenerator& m_ids;
};
}
