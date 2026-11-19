/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * Note input (S3): the state machine behind the interface's note-input commands, with MuseScore's behaviour so
 * that the existing shortcuts and workflows stay the same. Every command is one undo step (a transaction pushed
 * on the history); the input position, value and voice are state, not part of the score.
 *
 * - A–G enter a note at the input position, in the octave nearest the previous note, with the key signature's
 *   accidental (or the accidental chosen for it); the position moves on by the note's length.
 * - With "add to chord" the letter adds the note above the chord's top note instead.
 * - 0 enters a rest. Values: 64th … breve, dots; "+" (tie) enters the same chord again, tied.
 * - Up/down move the last note a semitone (an octave with the octave flag); left/right move the position.
 * - Re-pitch mode: letters change the pitches of the music already there, keeping its rhythm.
 * - A tuplet command turns the next stretch into a tuplet of the current value and entry continues inside it.
 */
#pragma once

#include <optional>
#include <string>

#include "history.h"
#include "operations.h"

namespace je::edit {
class NoteInput
{
public:
    NoteInput(model::Model& model, IdGenerator& ids, History& history)
        : m(model), ops(model, ids), history(history) {}

    //! Starts note input on a staff at a time (the selected chord or rest, or the start of a selected measure)
    void start(Id staff, const Fraction& at, int voice = 0);
    void stop() { m_active = false; }
    bool active() const { return m_active; }

    // ---- state ----
    void setValue(const NoteValue& value) { m_value = value; }
    //! MuseScore's number keys: 1 = 64th … 5 = quarter, 6 = half, 7 = whole, 8 = breve
    void setValueByKey(int key);
    void toggleDot();
    //! The accidental for the next note: -2 … 2 (double flat … double sharp), or none (the key's)
    void setAccidental(std::optional<int> alter) { m_accidental = alter; }
    void setVoice(int voice);
    void setRepitch(bool on) { m_repitch = on; }

    Fraction position() const { return m_position; }
    NoteValue value() const { return m_value; }
    int voiceNumber() const { return m_voice; }
    //! The chord or rest last entered (what the interface shows selected)
    Id last() const { return m_last; }

    // ---- commands (each one undo step; false when it does nothing) ----
    //! A letter A–G; with addToChord the note goes above the last chord's top note
    bool letter(char step, bool addToChord = false);
    bool rest();
    bool tie();
    //! Moves the last note: a semitone, or an octave
    bool upDown(bool up, bool octave = false);
    bool moveLeft();
    bool moveRight();
    //! Makes a tuplet of the current value at the position (3:2 for a triplet, 5:4 …)
    bool tuplet(int actual, int normal);
    //! The last entered chord or rest becomes a rest (voice 1) or goes (other voices); the position stays
    bool deleteLast();

private:
    Id voiceId(Transaction& tx);
    int pitchFor(char step, int reference, int64_t key) const;
    Id eventAt(Id voice, const Fraction& t) const;
    Fraction endAt(Id event) const;

    model::Model& m;
    Operations ops;
    History& history;
    bool m_active = false;
    bool m_repitch = false;
    Id m_staff;
    int m_voice = 0;
    Fraction m_position;
    NoteValue m_value { Fraction(1, 4), 0 };
    std::optional<int> m_accidental;
    Id m_last;
    int m_lastPitch = 60;   // middle C before anything is entered
};
}
