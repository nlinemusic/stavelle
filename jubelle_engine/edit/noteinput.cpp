/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 */
#include "noteinput.h"

#include <cstdlib>

#include "model/pitch.h"

namespace F = je::model::fields;
using je::model::Kind;
using je::model::Object;

namespace je::edit {
namespace {
//! Pitch class and tonal pitch class (line of fifths) of a natural step
bool natural(char step, int& pc, int& tpc)
{
    switch (step) {
    case 'C': pc = 0; tpc = 14; return true;
    case 'D': pc = 2; tpc = 16; return true;
    case 'E': pc = 4; tpc = 18; return true;
    case 'F': pc = 5; tpc = 13; return true;
    case 'G': pc = 7; tpc = 15; return true;
    case 'A': pc = 9; tpc = 17; return true;
    case 'B': pc = 11; tpc = 19; return true;
    default: return false;
    }
}

//! The key signature's alteration of a step: sharps F C G D A E B, flats B E A D G C F
int keyAlter(char step, int64_t key)
{
    static const char sharps[] = "FCGDAEB";
    static const char flats[] = "BEADGCF";
    for (int i = 0; i < 7; ++i) {
        if (key > i && sharps[i] == step) {
            return 1;
        }
        if (-key > i && flats[i] == step) {
            return -1;
        }
    }
    return 0;
}
}

void NoteInput::start(Id staff, const Fraction& at, int voice)
{
    m_active = true;
    m_staff = staff;
    m_voice = voice;
    m_position = at;
    m_last = Id();
    // the octave reference: the last note before the position on this staff, else middle C
    m_lastPitch = 60;
    Fraction best(-1, 1);
    if (const Object* st = m.find(staff)) {
        for (Id v : st->children()) {
            for (Id e : ops.eventsOf(v)) {
                const Object* o = m.find(e);
                const Fraction t = o->fractionOr(F::TIME, Fraction());
                if (o->kind() == Kind::Chord && t < at && best < t) {
                    for (Id n : o->children()) {
                        if (m.find(n)->kind() == Kind::Note) {
                            best = t;
                            m_lastPitch = int(m.find(n)->intOr(F::PITCH, 60));
                        }
                    }
                }
            }
        }
    }
}

void NoteInput::setValueByKey(int key)
{
    static const Fraction bases[] = { Fraction(1, 64), Fraction(1, 32), Fraction(1, 16), Fraction(1, 8), Fraction(1, 4),
                                      Fraction(1, 2), Fraction(1, 1), Fraction(2, 1) };
    if (key >= 1 && key <= 8) {
        m_value = { bases[key - 1], 0 };
    }
}

void NoteInput::toggleDot()
{
    m_value.dots = m_value.dots ? 0 : 1;
}

void NoteInput::setVoice(int voice)
{
    if (voice >= 0 && voice < 4) {
        m_voice = voice;
    }
}

Id NoteInput::voiceId(Transaction& tx)
{
    return ops.voice(tx, m_staff, m_voice);
}

Id NoteInput::eventAt(Id voice, const Fraction& t) const
{
    for (Id e : ops.eventsOf(voice)) {
        const Object* o = m.find(e);
        const Fraction s = o->fractionOr(F::TIME, Fraction());
        if (!o->has(F::GRACE) && s <= t && t < s + o->fractionOr(F::DURATION, Fraction())) {
            return e;
        }
    }
    return Id();
}

Fraction NoteInput::endAt(Id event) const
{
    const Object* o = m.find(event);
    return o ? o->fractionOr(F::TIME, Fraction()) + o->fractionOr(F::DURATION, Fraction()) : m_position;
}

int NoteInput::pitchFor(char step, int reference, int64_t key) const
{
    int pc = 0, tpc = 0;
    natural(step, pc, tpc);
    const int alter = m_accidental ? *m_accidental : keyAlter(step, key);
    const int cls = pc + alter;
    // MuseScore's rule: the step in the reference's octave, moved an octave when more than a tritone away
    int pitch = cls + 12 * (reference / 12);
    if (pitch - reference > 6) {
        pitch -= 12;
    } else if (reference - pitch > 6) {
        pitch += 12;
    }
    return pitch;
}

bool NoteInput::letter(char step, bool addToChord)
{
    int pc = 0, tpcNatural = 0;
    if (!m_active || !natural(step, pc, tpcNatural)) {
        return false;
    }
    const int64_t key = ops.keyAt(m_staff, m_position);
    const int alter = m_accidental ? *m_accidental : keyAlter(step, key);
    const int64_t tpc = tpcNatural + 7 * alter;
    Transaction tx(m, addToChord ? "Add note to chord" : (m_repitch ? "Re-pitch" : "Enter note"));
    const Id voice = voiceId(tx);
    if (addToChord) {
        const Object* chord = m.find(m_last);
        if (!chord || chord->kind() != Kind::Chord) {
            tx.rollback();
            return false;
        }
        int top = -1;
        for (Id n : chord->children()) {
            if (m.find(n)->kind() == Kind::Note) {
                top = std::max(top, int(m.find(n)->intOr(F::PITCH, 0)));
            }
        }
        int pitch = pitchFor(step, top, key);
        while (pitch <= top) {
            pitch += 12;   // above the top note
        }
        const Id note = ops.addNote(tx, m_last, pitch);
        ops.setPitch(tx, note, pitch, tpc);
        m_lastPitch = pitch;
        history.push(tx.commit());
        return true;
    }
    const int pitch = pitchFor(step, m_lastPitch, key);
    if (m_repitch) {
        // the next chord from the position (rests are passed over) takes the pitch; the rhythm stays
        Id target;
        for (Id e : ops.eventsOf(voice)) {
            const Object* o = m.find(e);
            if (!target.isValid() && o->kind() == Kind::Chord && !o->has(F::GRACE) && m_position <= o->fractionOr(F::TIME, Fraction())) {
                target = e;
            }
        }
        if (!target.isValid()) {
            tx.rollback();
            return false;
        }
        std::vector<Id> notes;
        for (Id n : m.find(target)->children()) {
            if (m.find(n)->kind() == Kind::Note) {
                notes.push_back(n);
            }
        }
        for (size_t i = 1; i < notes.size(); ++i) {
            ops.removeNote(tx, notes[i]);
        }
        ops.setPitch(tx, notes.front(), pitch, tpc);
        m_last = target;
        m_position = endAt(target);
    } else {
        const Fraction at = m_position;
        const Id chord = ops.enterChord(tx, voice, at, m_value, { pitch });
        for (Id n : m.find(chord)->children()) {
            if (m.find(n)->kind() == Kind::Note) {
                ops.setPitch(tx, n, pitch, tpc);
            }
        }
        m_last = chord;
        // on past everything just entered (pieces tied across a barline included)
        Id piece = chord;
        for (int guard = 0; guard < 64; ++guard) {
            Id next;
            for (Id n : m.find(piece)->children()) {
                if (m.find(n)->kind() == Kind::Note && m.find(n)->has(F::TIE_TO)) {
                    next = m.find(m.find(n)->idOr(F::TIE_TO))->parent();
                }
            }
            if (!next.isValid() || m.find(next)->fractionOr(F::TIME, Fraction()) != endAt(piece)) {
                break;
            }
            piece = next;
        }
        m_position = endAt(piece);
    }
    m_lastPitch = pitch;
    m_accidental.reset();
    history.push(tx.commit());
    return true;
}

bool NoteInput::rest()
{
    if (!m_active) {
        return false;
    }
    Transaction tx(m, "Enter rest");
    const Id voice = voiceId(tx);
    // its length: the value, in a tuplet's terms inside one and cut at its end
    Fraction length = m_value.length();
    if (const Object* here = m.find(eventAt(voice, m_position)); here && here->has(F::TUPLET)) {
        const Object* t = m.find(here->idOr(F::TUPLET));
        length = length * Fraction(t->intOr(F::RATIO_DEN, 1), t->intOr(F::RATIO_NUM, 1));
        length = std::min(length, t->fractionOr(F::TIME, Fraction()) + t->fractionOr(F::DURATION, Fraction()) - m_position);
    }
    const Id r = ops.enterRest(tx, voice, m_position, m_value);
    if (!r.isValid()) {
        tx.rollback();
        return false;
    }
    m_last = r;
    m_position = std::min(m_position + length, ops.measureEnd(ops.measures().back()));
    history.push(tx.commit());
    return true;
}

bool NoteInput::tie()
{
    const Object* last = m.find(m_last);
    if (!m_active || !last || last->kind() != Kind::Chord) {
        return false;
    }
    std::vector<int> pitches;
    std::vector<Id> from;
    for (Id n : last->children()) {
        if (m.find(n)->kind() == Kind::Note) {
            pitches.push_back(int(m.find(n)->intOr(F::PITCH, 60)));
            from.push_back(n);
        }
    }
    Transaction tx(m, "Add tied note");
    const Id voice = voiceId(tx);
    const Id chord = ops.enterChord(tx, voice, m_position, m_value, pitches);
    if (!chord.isValid()) {
        tx.rollback();
        return false;
    }
    for (Id n : from) {
        if (m.find(n)) {
            ops.tieToNext(tx, n);
        }
    }
    m_last = chord;
    m_position = endAt(chord);
    history.push(tx.commit());
    return true;
}

bool NoteInput::upDown(bool up, bool octave)
{
    const Object* last = m.find(m_last);
    if (!last || last->kind() != Kind::Chord) {
        return false;
    }
    Transaction tx(m, octave ? (up ? "Up octave" : "Down octave") : (up ? "Up" : "Down"));
    for (Id n : last->children()) {
        const Object* note = m.find(n);
        if (note->kind() != Kind::Note) {
            continue;
        }
        const int pitch = int(note->intOr(F::PITCH, 60)) + (octave ? 12 : 1) * (up ? 1 : -1);
        if (pitch < 0 || pitch > 127) {
            continue;
        }
        // a semitone up is spelled with sharps, down with flats (MuseScore's choice); an octave keeps the spelling
        const int64_t tpc = octave ? note->intOr(F::TPC, model::defaultTpc(pitch, 0)) : model::defaultTpc(pitch, up ? 0 : -1);
        ops.setPitch(tx, n, pitch, tpc);
        m_lastPitch = pitch;
    }
    history.push(tx.commit());
    return true;
}

bool NoteInput::moveRight()
{
    Transaction tx(m, "Move");
    const Id voice = voiceId(tx);
    const Id e = eventAt(voice, m_position);
    tx.rollback();
    if (!e.isValid()) {
        return false;
    }
    m_position = endAt(e);
    return true;
}

bool NoteInput::moveLeft()
{
    Transaction tx(m, "Move");
    const Id voice = voiceId(tx);
    Id prev;
    for (Id e : ops.eventsOf(voice)) {
        const Object* o = m.find(e);
        if (!o->has(F::GRACE) && o->fractionOr(F::TIME, Fraction()) < m_position) {
            prev = e;
        }
    }
    tx.rollback();
    if (!prev.isValid()) {
        return false;
    }
    m_position = m.find(prev)->fractionOr(F::TIME, Fraction());
    return true;
}

bool NoteInput::tuplet(int actual, int normal)
{
    if (!m_active) {
        return false;
    }
    Transaction tx(m, "Tuplet");
    try {
        const Id voice = voiceId(tx);
        ops.makeTuplet(tx, voice, m_position, actual, normal, m_value.base);
    } catch (const std::logic_error&) {
        tx.rollback();   // across a barline: nothing changes
        return false;
    }
    history.push(tx.commit());
    return true;
}

bool NoteInput::deleteLast()
{
    if (!m.find(m_last)) {
        return false;
    }
    Transaction tx(m, "Delete");
    ops.deleteEvent(tx, m_last);
    m_last = Id();
    history.push(tx.commit());
    return true;
}
}
