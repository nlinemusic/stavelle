/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * Range selections, copy, cut, paste and delete (S3), with MuseScore's behaviour:
 * - a range is a time span on consecutive staves (all their voices);
 * - copying takes the chords, rests in voices 2–4 and in tuplets, notes (pitch, spellings), ties inside the range,
 *   grace notes, tuplets (a range is widened to whole tuplets), attachments (articulations, lyrics, fingering…),
 *   lines that start and end inside it, and items on those staves (dynamics, staff text, chord symbols…);
 * - pasting replaces the music of the target range in every voice; a chord across a barline continues tied into
 *   the next measure; voice 1 is filled with rests; what would go past the end of the score is left out;
 * - deleting a range leaves rests in voice 1 and gaps in the other voices.
 * The interface's clipboard text (MuseScore's format, for pasting between programs) is made from a copy by the
 * file writer in S10; here a copy is a structured value that doesn't depend on the score it came from.
 */
#pragma once

#include <string>
#include <vector>

#include "operations.h"

namespace je::edit {
struct RangeSelection {
    std::vector<Id> staves;   // consecutive, top to bottom
    Fraction start, end;
};

struct Clip {
    struct Note {
        std::vector<std::pair<Field, Value> > fields;   // pitch, spellings, details… (no ids)
        int tieTo = -1;                                  // index of the event and note tied to, within the copy
        int tieToNote = -1;
        std::vector<std::vector<std::pair<Field, Value> > > attachments;
    };
    struct Event {
        bool rest = false;
        Fraction rel;   // from the start of the copy
        std::vector<std::pair<Field, Value> > fields;   // values, grace, details… (no time, no ids)
        int tuplet = -1;
        std::vector<Note> notes;
        std::vector<std::vector<std::pair<Field, Value> > > attachments;
    };
    struct Tuplet {
        Fraction rel;
        std::vector<std::pair<Field, Value> > fields;
        int parent = -1;
    };
    struct Voice {
        int number = 0;
        std::vector<Event> events;   // in the voice's order
        std::vector<Tuplet> tuplets;
    };
    struct Staff {
        std::vector<Voice> voices;
    };
    struct Item {   // a line or a staff item: fields with times relative to the copy
        int staff = 0;
        model::Kind kind = model::Kind::None;
        Fraction rel;
        std::vector<std::pair<Field, Value> > fields;
        int startEvent = -1, startVoice = -1;   // a line kept inside its chord
    };
    Fraction length;
    std::vector<Staff> staves;
    std::vector<Item> items;
    bool empty() const { return staves.empty(); }
};

class Clipboard
{
public:
    Clipboard(model::Model& model, IdGenerator& ids)
        : m(model), m_ids(ids), ops(model, ids) {}

    //! The range widened so that it holds whole tuplets
    RangeSelection whole(const RangeSelection& range) const;
    Clip copy(const RangeSelection& range) const;
    //! Pastes at a staff and time; returns the range pasted into (empty if nothing fitted)
    RangeSelection paste(Transaction& tx, const Clip& clip, Id staff, const Fraction& at);
    void erase(Transaction& tx, const RangeSelection& range);
    //! Number of tuplets left out by the last paste because they would cross a barline there
    int tupletsLeftOut() const { return m_left; }

private:
    std::vector<Id> stavesInOrder() const;

    model::Model& m;
    IdGenerator& m_ids;
    Operations ops;
    int m_left = 0;
};
}
