/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * The rules follow MuseScore Studio's commands (Score::changeEnharmonicSpelling, changeAccidental, addInterval,
 * toggleArticulation, changeSelectedElementsVoice, addGraceNote; MuseScore Limited and others, GPL v3).
 */
#include "commands.h"

#include <algorithm>
#include <functional>
#include <map>
#include <set>
#include <stdexcept>

#include "structure.h"
#include "model/fields.h"
#include "model/notation.h"
#include "model/packing.h"
#include "model/pitch.h"

namespace F = je::model::fields;
using je::model::IdList;
using je::model::Kind;
using je::model::Object;
using je::model::Text;
using je::model::Value;

namespace je::edit {
namespace {
//! The pitch of a letter's natural in octave `octave` (MIDI: C4 = 60 is octave 4)
int naturalPitch(int letter, int octave)
{
    static const int semis[7] = { 0, 2, 4, 5, 7, 9, 11 };
    return (octave + 1) * 12 + semis[((letter % 7) + 7) % 7];
}

//! The octave the note's letter is written in (B♯3 sounds as C4 but is written in octave 3)
int octaveOf(int pitch, int64_t tpc)
{
    return (pitch - model::accidentalOfTpc(tpc)) / 12 - 1;
}
}

Id Commands::staffOfEvent(Id event) const
{
    const Object* e = m_ops.obj(event);
    return e ? m_ops.staffOf(e->parent()) : Id();
}

bool Commands::drums(Id staff) const
{
    const Object* s = m_ops.obj(staff);
    const Object* player = s ? m_ops.obj(s->parent()) : nullptr;
    return player && player->text(F::FAMILY) == "percussion";
}

Id Commands::flowOf() const
{
    for (Id c : m_ops.obj(m_ops.m.score())->children()) {
        if (m_ops.obj(c)->kind() == Kind::Flow) {
            return c;
        }
    }
    return Id();
}

Commands::Spelling Commands::shown(Id note, bool concertPitch) const
{
    const Object* n = m_ops.obj(note);
    const Object* staff = m_ops.obj(staffOfEvent(n->parent()));
    const Object* player = staff ? m_ops.obj(staff->parent()) : nullptr;
    Spelling s { F::TPC, n->intOr(F::TPC, model::defaultTpc(n->intOr(F::PITCH, 60), 0)), 0, 0 };
    if (player) {
        s.chrom = player->intOr(F::TRANSPOSE_CHROMATIC, 0);
        s.dia = player->intOr(F::TRANSPOSE_DIATONIC, model::diatonicOf(s.chrom));
    }
    if (!concertPitch && (s.dia != 0 || s.chrom != 0)) {
        s.field = F::TPC2;
        s.tpc = n->has(F::TPC2) ? n->intOr(F::TPC2, s.tpc) : model::transposeTpc(s.tpc, -s.dia, -s.chrom);
    }
    return s;
}

void Commands::setSpelling(Transaction& tx, Id note, const Spelling& s, int64_t newTpc, int semitones, bool both)
{
    const Object* n = m_ops.obj(note);
    const int pitch = int(n->intOr(F::PITCH, 60)) + semitones;
    if (semitones != 0) {
        tx.set(note, F::PITCH, int64_t(pitch));
        // a tie holds only between equal pitches
        if (const Object* to = m_ops.obj(m_ops.obj(note)->idOr(F::TIE_TO)); to && to->intOr(F::PITCH, -1) != pitch) {
            tx.set(note, F::TIE_TO, Value());
        }
    }
    tx.set(note, s.field, newTpc);
    const bool transposing = s.dia != 0 || s.chrom != 0;
    if (both || !transposing || semitones != 0) {
        // the other spelling follows (always when the pitch moved: both name the same sound)
        if (s.field == F::TPC) {
            if (transposing) {
                tx.set(note, F::TPC2, model::transposeTpc(newTpc, -s.dia, -s.chrom));
            }
        } else {
            tx.set(note, F::TPC, model::transposeTpc(newTpc, s.dia, s.chrom));
        }
    }
}

void Commands::changeEnharmonicSpelling(Transaction& tx, const std::vector<Id>& notes, bool concertPitch, bool both)
{
    // MuseScore's table: for each pitch class its three spellings (sharp, natural or the only one, flat)
    static const int tab[36] = {
        26, 14, 2, 21, 21, 9, 28, 16, 4, 23, 23, 11, 30, 18, 6, 25, 13, 1,
        20, 20, 8, 27, 15, 3, 22, 22, 10, 29, 17, 5, 24, 24, 12, 31, 19, 7,
    };
    std::set<Id> seen;
    for (Id note : notes) {
        const Object* n = m_ops.obj(note);
        if (!n || n->kind() != Kind::Note || !seen.insert(note).second) {
            continue;
        }
        if (drums(staffOfEvent(n->parent()))) {
            continue;
        }
        const Spelling s = shown(note, concertPitch);
        int64_t tpc = s.tpc;
        for (int i = 0; i < 36; ++i) {
            if (tab[i] == tpc) {
                if ((i % 3) < 2) {
                    tpc = tab[i] == tab[i + 1] ? tab[i + 2] : tab[i + 1];
                } else {
                    tpc = tab[i - 2];
                }
                break;
            }
        }
        if (tpc != s.tpc) {
            setSpelling(tx, note, s, tpc, 0, both);
        }
    }
}

void Commands::changeAccidental(Transaction& tx, Id note, int alteration, bool concertPitch)
{
    const Object* n = m_ops.obj(note);
    if (!n || n->kind() != Kind::Note || alteration < -2 || alteration > 2) {
        return;
    }
    const Spelling s = shown(note, concertPitch);
    const int letter = model::letterOfTpc(s.tpc);
    const int64_t tpc = model::tpcOf(letter, alteration);
    const int semitones = alteration - model::accidentalOfTpc(s.tpc);
    if (tpc != s.tpc) {
        setSpelling(tx, note, s, tpc, semitones, true);
    }
}

void Commands::toggleAccidental(Transaction& tx, Id note, int alteration, bool concertPitch)
{
    const Object* n = m_ops.obj(note);
    if (!n || n->kind() != Kind::Note) {
        return;
    }
    const Spelling s = shown(note, concertPitch);
    if (model::accidentalOfTpc(s.tpc) != alteration) {
        changeAccidental(tx, note, alteration, concertPitch);
        return;
    }
    // already so: back to what the key gives the letter (the key shown, concert or written)
    const Object* chord = m_ops.obj(n->parent());
    int64_t key = m_ops.keyAt(staffOfEvent(chord->id()), m_ops.timeOf(chord->id()));
    if (s.field == F::TPC2) {
        key = model::transposeTpc(key + 14, -s.dia, -s.chrom) - 14;   // the written key (C = 14 on the line of fifths)
    }
    changeAccidental(tx, note, model::keyAccidental(model::letterOfTpc(s.tpc), key), concertPitch);
}

std::vector<Id> Commands::addInterval(Transaction& tx, const std::vector<Id>& notes, int interval)
{
    std::vector<Id> made;
    if (interval == 0 || interval == 1 || interval == -1) {
        return made;
    }
    const int deltaLine = interval < 0 ? interval + 1 : interval - 1;
    const bool octaveRule = deltaLine % 7 == 0;
    // the notes and all their tied continuations, in time order
    std::vector<Id> all;
    std::set<Id> seen;
    std::vector<Id> sorted = notes;
    std::sort(sorted.begin(), sorted.end(), [this](Id a, Id b) {
        return m_ops.timeOf(m_ops.obj(a)->parent()) < m_ops.timeOf(m_ops.obj(b)->parent());
    });
    for (Id n : sorted) {
        for (Id x = n; x.isValid() && m_ops.obj(x) && seen.insert(x).second; x = m_ops.obj(x)->idOr(F::TIE_TO)) {
            all.push_back(x);
        }
    }
    Id prevNew;   // the note just added, when the next one continues its tie
    for (Id on : all) {
        const Object* n = m_ops.obj(on);
        if (!n || n->kind() != Kind::Note) {
            continue;
        }
        const Id chord = n->parent();
        const Id staffId = staffOfEvent(chord);
        if (drums(staffId)) {
            continue;   // drum lines need the drum set: not yet
        }
        const int pitch = int(n->intOr(F::PITCH, 60));
        const int64_t tpc = n->intOr(F::TPC, model::defaultTpc(pitch, 0));
        const bool tiedIn = prevNew.isValid();
        const Id tiedTo = n->idOr(F::TIE_TO);
        int newPitch;
        int64_t newTpc;
        if (octaveRule) {
            newPitch = pitch + 12 * (deltaLine / 7);
            newTpc = tpc;
        } else {
            // the letter deltaLine steps away, with the key's accidental (concert key, concert spelling: a
            // transposing instrument's written spelling follows with the same interval)
            const int letter = model::letterOfTpc(tpc);
            const int octave = octaveOf(pitch, tpc);
            const int steps = letter + deltaLine;
            const int newLetter = ((steps % 7) + 7) % 7;
            const int newOctave = octave + (steps >= 0 ? steps / 7 : -((-steps + 6) / 7));
            const int acc = model::keyAccidental(newLetter, m_ops.keyAt(staffId, m_ops.timeOf(chord)));
            newPitch = naturalPitch(newLetter, newOctave) + acc;
            newTpc = model::tpcOf(newLetter, acc);
        }
        if (newPitch < 0 || newPitch > 127) {
            prevNew = Id();
            continue;
        }
        const size_t before = m_ops.obj(chord)->children().size();
        const Id note = m_ops.addNote(tx, chord, newPitch);
        if (m_ops.obj(chord)->children().size() == before) {   // the chord already had it
            prevNew = Id();
            continue;
        }
        if (m_ops.obj(note)->intOr(F::TPC, -100) != newTpc) {
            m_ops.setPitch(tx, note, newPitch, newTpc);
        }
        if (tiedIn) {
            tx.set(prevNew, F::TIE_TO, note);
        }
        prevNew = tiedTo.isValid() ? note : Id();
        made.push_back(note);
    }
    return made;
}

void Commands::toggleArticulation(Transaction& tx, const std::vector<Id>& chords, const std::string& name)
{
    const std::string kind = model::articulationKind(name);
    for (Id c : chords) {
        const Object* chord = m_ops.obj(c);
        if (!chord || chord->kind() != Kind::Chord) {
            continue;
        }
        Id existing;
        for (Id a : chord->children()) {
            const Object* at = m_ops.obj(a);
            if (at->kind() == Kind::Attachment && model::articulationKind(at->text(F::SUBKIND)) == kind) {
                existing = a;
                break;
            }
        }
        if (existing.isValid()) {
            removeTree(tx, existing);
        } else {
            // before the notes, as articulations are kept
            const Id made = create(tx, m_ops.m_ids, c, { Kind::Attachment, { { F::SUBKIND, Text(name) } } });
            std::vector<Id> kids = m_ops.obj(c)->children().ids();
            kids.erase(std::remove(kids.begin(), kids.end(), made), kids.end());
            auto pos = std::find_if(kids.begin(), kids.end(), [this](Id x) { return m_ops.obj(x)->kind() == Kind::Note; });
            kids.insert(pos, made);
            tx.set(c, F::CHILDREN, IdList(std::move(kids)));
        }
    }
}

//! The grace chords of a main chord: those just before it (acciaccaturas, appoggiaturas…) and just after it
std::pair<std::vector<Id>, std::vector<Id> > Commands::gracesOf(Id chord) const
{
    std::pair<std::vector<Id>, std::vector<Id> > g;
    const Object* c = m_ops.obj(chord);
    if (!c) {
        return g;
    }
    const std::vector<Id>& kids = m_ops.obj(c->parent())->children().ids();
    const auto at = std::find(kids.begin(), kids.end(), chord);
    if (at == kids.end()) {
        return g;
    }
    auto isAfter = [this](Id x) { return m_ops.obj(x)->text(F::GRACE_TYPE).find("after") != std::string::npos; };
    for (auto it = at; it != kids.begin();) {
        --it;
        if (!m_ops.obj(*it)->has(F::GRACE) || isAfter(*it)) {
            break;
        }
        g.first.insert(g.first.begin(), *it);
    }
    for (auto it = at + 1; it != kids.end() && m_ops.obj(*it)->has(F::GRACE) && isAfter(*it); ++it) {
        g.second.push_back(*it);
    }
    return g;
}

//! Moves a chord's graces to another voice, around `beside` there (the chord itself, or the chord it joins)
void Commands::moveGraces(Transaction& tx, const std::pair<std::vector<Id>, std::vector<Id> >& graces, Id toVoice, Id beside)
{
    if (graces.first.empty() && graces.second.empty()) {
        return;
    }
    for (const auto* list : { &graces.first, &graces.second }) {
        for (Id g : *list) {
            moveTo(tx, g, toVoice);
        }
    }
    std::vector<Id> kids = m_ops.obj(toVoice)->children().ids();
    auto drop = [&](Id x) { kids.erase(std::remove(kids.begin(), kids.end(), x), kids.end()); };
    for (const auto* list : { &graces.first, &graces.second }) {
        for (Id g : *list) {
            drop(g);
        }
    }
    auto pos = std::find(kids.begin(), kids.end(), beside);
    pos = kids.insert(pos, graces.first.begin(), graces.first.end());
    pos += std::ptrdiff_t(graces.first.size()) + 1;
    // after the chord and any after-graces it already has
    while (pos != kids.end() && m_ops.obj(*pos)->has(F::GRACE) && m_ops.obj(*pos)->text(F::GRACE_TYPE).find("after") != std::string::npos) {
        ++pos;
    }
    kids.insert(pos, graces.second.begin(), graces.second.end());
    tx.set(toVoice, F::CHILDREN, IdList(std::move(kids)));
}

int Commands::changeVoice(Transaction& tx, const std::vector<Id>& items, int voice)
{
    if (voice < 0 || voice > 3) {
        return 0;
    }
    int moved = 0;
    // whole chords and rests, and notes on their own (a note whose whole chord is chosen moves with it)
    std::set<Id> events;
    std::vector<Id> loneNotes;
    for (Id i : items) {
        const Object* o = m_ops.obj(i);
        if (o && (o->kind() == Kind::Chord || o->kind() == Kind::Rest)) {
            events.insert(i);
        }
    }
    for (Id i : items) {
        const Object* o = m_ops.obj(i);
        if (o && o->kind() == Kind::Note && !events.count(o->parent())) {
            size_t notes = 0;
            for (Id x : m_ops.obj(o->parent())->children()) {
                notes += m_ops.obj(x)->kind() == Kind::Note;
            }
            if (notes == 1) {
                events.insert(o->parent());   // all of it: the chord goes, with its graces and articulations
            } else {
                loneNotes.push_back(i);
            }
        }
    }
    // the event of the target voice at the same time and of the same length (none: there is room when the
    // target voice has nothing that overlaps)
    auto slot = [this](Id voiceId, const Fraction& t, const Fraction& len, bool& room) -> Id {
        room = true;
        for (Id e : m_ops.eventsOf(voiceId)) {
            const Object* o = m_ops.obj(e);
            if (o->has(F::GRACE)) {
                continue;
            }
            const Fraction a = m_ops.timeOf(e), b = m_ops.endOf(e);
            if (a == t && b == t + len) {
                return e;
            }
            if (a < t + len && t < b) {
                room = false;
            }
        }
        return Id();
    };
    for (Id e : std::vector<Id>(events.begin(), events.end())) {
        const Object* ev = m_ops.obj(e);
        if (!ev || ev->has(F::TUPLET) || ev->has(F::GRACE) || ev->has(F::MEASURE_REST)) {
            continue;   // tuplets and graces only with their whole group (not yet)
        }
        const Id fromVoice = ev->parent();
        const int fromNumber = int(m_ops.obj(fromVoice)->intOr(F::NUMBER, 0));
        if (fromNumber == voice) {
            continue;
        }
        const Id staff = m_ops.staffOf(fromVoice);
        const Fraction t = m_ops.timeOf(e), len = m_ops.endOf(e) - t;
        const Id toVoice = m_ops.voice(tx, staff, voice);
        bool room = false;
        const Id there = slot(toVoice, t, len, room);
        const Object* target = m_ops.obj(there);
        if (target && target->has(F::TUPLET)) {
            continue;
        }
        if (target && target->kind() == Kind::Chord && ev->kind() == Kind::Chord) {
            // into the chord there: the notes it doesn't have
            for (Id n : std::vector<Id>(ev->children().ids())) {
                const Object* note = m_ops.obj(n);
                if (note->kind() != Kind::Note) {
                    continue;
                }
                bool has = false;
                for (Id x : m_ops.obj(there)->children()) {
                    has = has || (m_ops.obj(x)->kind() == Kind::Note && m_ops.obj(x)->intOr(F::PITCH, -1) == note->intOr(F::PITCH, -2));
                }
                if (!has) {
                    m_ops.addNote(tx, there, int(note->intOr(F::PITCH, 60)));
                }
            }
            if (ev->kind() == Kind::Chord) {
                moveGraces(tx, gracesOf(e), toVoice, there);
            }
            m_ops.removeEvent(tx, e);
        } else if ((target && target->kind() == Kind::Rest) || (!target && room && voice > 0)) {
            const auto graces = gracesOf(e);
            if (target) {
                m_ops.removeEvent(tx, there);
            }
            moveTo(tx, e, toVoice, Placement::ByTime);
            moveGraces(tx, graces, toVoice, e);
        } else {
            continue;
        }
        if (fromNumber == 0) {
            m_ops.fillGap(tx, fromVoice, t, t + len);   // voice 1 has no gaps
        }
        ++moved;
    }
    for (Id n : loneNotes) {
        const Object* note = m_ops.obj(n);
        if (!note) {
            continue;
        }
        const Id chord = note->parent();
        const Object* ch = m_ops.obj(chord);
        if (!ch || ch->has(F::TUPLET) || ch->has(F::GRACE) || int(m_ops.obj(ch->parent())->intOr(F::NUMBER, 0)) == voice) {
            continue;
        }
        const Id staff = m_ops.staffOf(ch->parent());
        const Fraction t = m_ops.timeOf(chord), len = m_ops.endOf(chord) - t;
        const Id toVoice = m_ops.voice(tx, staff, voice);
        bool room = false;
        const Id there = slot(toVoice, t, len, room);
        const Object* target = m_ops.obj(there);
        const int pitch = int(note->intOr(F::PITCH, 60));
        size_t notesInChord = 0;
        for (Id x : ch->children()) {
            notesInChord += m_ops.obj(x)->kind() == Kind::Note;
        }
        if (target && target->kind() == Kind::Chord && !target->has(F::TUPLET)) {
            m_ops.addNote(tx, there, pitch);
        } else if ((target && target->kind() == Kind::Rest && !target->has(F::TUPLET)) || (!target && room && voice > 0)) {
            const NoteValue v { ch->fractionOr(F::BASE_DURATION, len), int(ch->intOr(F::DOTS, 0)) };
            m_ops.enterChord(tx, toVoice, t, v, { pitch });
        } else {
            continue;
        }
        if (notesInChord > 1) {
            m_ops.removeNote(tx, n);
        } else {
            m_ops.enterRest(tx, ch->parent(), t, { ch->fractionOr(F::BASE_DURATION, len), int(ch->intOr(F::DOTS, 0)) });
        }
        ++moved;
    }
    return moved;
}

void Commands::swapVoices(Transaction& tx, Id staff, const Fraction& t0, const Fraction& t1, int a, int b)
{
    if (a == b || a < 0 || b < 0 || a > 3 || b > 3) {
        return;
    }
    const Id va = m_ops.voice(tx, staff, a), vb = m_ops.voice(tx, staff, b);
    auto inRange = [&](Id voice) {
        std::vector<Id> out;
        for (Id c : m_ops.obj(voice)->children().ids()) {
            const Object* o = m_ops.obj(c);
            const Kind k = o->kind();
            if ((k == Kind::Chord || k == Kind::Rest || k == Kind::Tuplet) && t0 <= m_ops.timeOf(c) && m_ops.endOf(c) <= t1) {
                out.push_back(c);
            }
        }
        return out;
    };
    const std::vector<Id> fromA = inRange(va), fromB = inRange(vb);
    for (Id e : fromA) {
        moveTo(tx, e, vb, Placement::ByTime);
    }
    for (Id e : fromB) {
        moveTo(tx, e, va, Placement::ByTime);
    }
    // voice 1 has no gaps: rests where the other voice had none
    const Id v1 = a == 0 ? va : (b == 0 ? vb : Id());
    if (v1.isValid()) {
        Fraction at = t0;
        for (Id e : m_ops.eventsOf(v1)) {
            const Fraction s = m_ops.timeOf(e);
            if (s >= t1) {
                break;
            }
            if (m_ops.endOf(e) <= t0) {
                continue;
            }
            if (at < s) {
                m_ops.fillGap(tx, v1, at, s);
            }
            at = std::max(at, m_ops.endOf(e));
        }
        if (at < t1) {
            m_ops.fillGap(tx, v1, at, t1);
        }
    }
}

Id Commands::addGraceNote(Transaction& tx, Id note, const std::string& type, const Fraction& value)
{
    const Object* n = m_ops.obj(note);
    if (!n || n->kind() != Kind::Note) {
        return Id();
    }
    const Id chord = n->parent();
    const Object* main = m_ops.obj(chord);
    if (main->has(F::GRACE)) {
        return Id();
    }
    const Id voice = main->parent();
    const bool after = type.find("after") != std::string::npos;
    const Id grace = create(tx, m_ops.m_ids, voice, { Kind::Chord, { { F::TIME, m_ops.timeOf(chord) }, { F::GRACE, true },
                                                                     { F::GRACE_TYPE, Text(type) }, { F::BASE_DURATION, value },
                                                                     { F::DURATION, value } } });
    // next to its chord: just before it, or just after it
    std::vector<Id> kids = m_ops.obj(voice)->children().ids();
    kids.erase(std::remove(kids.begin(), kids.end(), grace), kids.end());
    auto pos = std::find(kids.begin(), kids.end(), chord);
    kids.insert(after ? pos + 1 : pos, grace);
    tx.set(voice, F::CHILDREN, IdList(std::move(kids)));
    const Id g = create(tx, m_ops.m_ids, grace, { Kind::Note, { { F::PITCH, n->intOr(F::PITCH, 60) } } });
    for (Field f : { F::TPC, F::TPC2 }) {
        if (m_ops.obj(note)->has(f)) {
            tx.set(g, f, m_ops.obj(note)->intOr(f, 0));
        }
    }
    return grace;
}

Id Commands::addLine(Transaction& tx, const std::string& kind, Id staff, int voice, const Fraction& t0, const Fraction& t1,
                     int64_t intSubtype, const std::string& textSubtype)
{
    if (!(t0 < t1) || !m_ops.obj(staff)) {
        return Id();
    }
    std::vector<std::pair<Field, Value> > f { { F::SUBKIND, Text(kind) }, { F::ANCHOR, staff }, { F::TIME, t0 },
                                               { F::DURATION, t1 - t0 }, { F::VOICE_INDEX, int64_t(voice) } };
    if (kind == "hairPin" || kind == "hairpin") {
        f.push_back({ F::INT_VALUE, intSubtype });
    } else if (!textSubtype.empty()) {
        f.push_back({ F::TEXT, Text(textSubtype) });
    }
    return create(tx, m_ops.m_ids, m_ops.m.score(), { Kind::Spanner, f });
}

Id Commands::addSlur(Transaction& tx, Id firstChord, Id lastChord)
{
    const Object* a = m_ops.obj(firstChord);
    if (!a || a->kind() != Kind::Chord) {
        return Id();
    }
    Id last = lastChord;
    if (!last.isValid() || last == firstChord) {   // to the next chord in the voice
        last = Id();
        bool past = false;
        for (Id e : m_ops.eventsOf(a->parent())) {
            if (past && m_ops.obj(e)->kind() == Kind::Chord && !m_ops.obj(e)->has(F::GRACE)) {
                last = e;
                break;
            }
            past = past || e == firstChord;
        }
        if (!last.isValid()) {
            return Id();
        }
    }
    const Fraction t0 = m_ops.timeOf(firstChord), t1 = m_ops.timeOf(last);
    if (!(t0 < t1)) {
        return Id();
    }
    const Id voice = a->parent();
    return addLine(tx, "slur", m_ops.staffOf(voice), int(m_ops.obj(voice)->intOr(F::NUMBER, 0)), t0, t1);
}

Id Commands::addNoteLine(Transaction& tx, const std::string& type, Id fromNote, Id toNote)
{
    const Object* a = m_ops.obj(fromNote);
    const Object* b = m_ops.obj(toNote);
    if (!a || !b || a->kind() != Kind::Note || b->kind() != Kind::Note || fromNote == toNote) {
        return Id();
    }
    const Fraction t0 = m_ops.timeOf(a->parent()), t1 = m_ops.timeOf(b->parent());
    if (t1 < t0) {
        return Id();
    }
    std::string tag = type;
    if (!tag.empty()) {
        tag[0] = char(std::toupper(static_cast<unsigned char>(tag[0])));
    }
    return create(tx, m_ops.m_ids, m_ops.m.score(), { Kind::Spanner, { { F::SUBKIND, Text(type) }, { F::TAG, Text(tag) },
                                                                       { F::ANCHOR, fromNote }, { F::END_ANCHOR, toNote },
                                                                       { F::TIME, t0 }, { F::DURATION, t1 - t0 } } });
}

Id Commands::insertKeySignature(Transaction& tx, const Fraction& t, int64_t fifths, Id staff)
{
    const Id flow = flowOf();
    // one already there (for the same staves) changes
    for (Id c : m_ops.obj(flow)->children()) {
        const Object* it = m_ops.obj(c);
        if (it->kind() == Kind::FlowItem && it->text(F::SUBKIND) == "keySig" && m_ops.timeOf(c) == t
            && it->idOr(F::ANCHOR) == staff) {
            tx.set(c, F::INT_VALUE, fifths);
            return c;
        }
    }
    std::vector<std::pair<Field, Value> > f { { F::SUBKIND, Text("keySig") }, { F::TIME, t }, { F::INT_VALUE, fifths } };
    if (staff.isValid()) {
        f.push_back({ F::ANCHOR, staff });
    }
    return create(tx, m_ops.m_ids, flow, { Kind::FlowItem, f }, Placement::ByTime);
}

Id Commands::insertClef(Transaction& tx, Id staff, const Fraction& t, const std::string& clef)
{
    const Id flow = flowOf();
    for (Id c : m_ops.obj(flow)->children()) {
        const Object* it = m_ops.obj(c);
        if (it->kind() == Kind::FlowItem && it->text(F::SUBKIND) == "clef" && m_ops.timeOf(c) == t && it->idOr(F::ANCHOR) == staff) {
            tx.set(c, F::TEXT, Text(clef));
            return c;
        }
    }
    return create(tx, m_ops.m_ids, flow, { Kind::FlowItem, { { F::SUBKIND, Text("clef") }, { F::ANCHOR, staff }, { F::TIME, t },
                                                             { F::TEXT, Text(clef) } } },
                  Placement::ByTime);
}
}

// ---- time signatures ------------------------------------------------------------------------------------------
namespace je::edit {
namespace {
bool isTimeSig(const Object* o)
{
    return o && o->kind() == Kind::FlowItem && o->text(F::SUBKIND) == "timeSig";
}

//! A note's kept appearance without a tie kept as written (a laissez vibrer, a partial tie)
std::string withoutOwnTie(std::string details, const std::string& tag)
{
    for (size_t a; (a = details.find("<" + tag)) != std::string::npos;) {
        const size_t close = details.find('>', a);
        if (close == std::string::npos) {
            break;
        }
        size_t end = close + 1;   // <LaissezVib/>
        if (details[close - 1] != '/') {
            const size_t b = details.find("</" + tag + ">", close);
            if (b == std::string::npos) {
                break;
            }
            end = b + tag.size() + 3;
        }
        details.erase(a, end - a);
    }
    return details;
}

//! The fields of a chord, rest or note to give the next piece of it (not its identity, links, place or ties)
bool copiedToPiece(Field f)
{
    static const std::set<Field> no { F::KIND, F::PARENT, F::CHILDREN, F::TIME, F::DURATION, F::BASE_DURATION, F::DOTS,
                                      F::MEASURE_REST, F::TIE_TO, F::ORIGIN_EID, F::LINKED_TO, F::LINK_EID, F::LINK_MARK,
                                      F::TIE_EID, F::TIE_LINK, F::TIE_MARK, F::LINE_DETAILS, F::SEQ, F::TIE_GENERATED };
    return !no.count(f);
}
}

std::vector<Id> Commands::timeSigsAt(const Fraction& t) const
{
    std::vector<Id> out;
    const std::vector<Id>& kids = m_ops.obj(flowOf())->children().ids();
    auto it = std::lower_bound(kids.begin(), kids.end(), t, [this](Id x, const Fraction& time) { return m_ops.timeOf(x) < time; });
    for (; it != kids.end() && m_ops.timeOf(*it) == t; ++it) {
        if (isTimeSig(m_ops.obj(*it))) {
            out.push_back(*it);
        }
    }
    return out;
}

std::vector<Id> Commands::allStaves() const
{
    std::vector<Id> out;
    for (Id p : m_ops.obj(m_ops.m.score())->children()) {
        if (m_ops.obj(p)->kind() == Kind::Player) {
            for (Id s : m_ops.obj(p)->children()) {
                if (m_ops.obj(s)->kind() == Kind::Staff) {
                    out.push_back(s);
                }
            }
        }
    }
    return out;
}

void Commands::setTimeSigs(Transaction& tx, const Fraction& t, int64_t num, int64_t den)
{
    // every staff has its own (as MuseScore adds one to each)
    std::set<Id> done;
    for (Id ts : timeSigsAt(t)) {
        tx.set(ts, F::TIMESIG_NUM, num);
        tx.set(ts, F::TIMESIG_DEN, den);
        if (!m_ops.obj(ts)->has(F::ANCHOR)) {
            return;   // one for every staff
        }
        done.insert(m_ops.obj(ts)->idOr(F::ANCHOR));
    }
    for (Id staff : allStaves()) {
        if (!done.count(staff)) {
            create(tx, m_ops.m_ids, flowOf(), { Kind::FlowItem, { { F::SUBKIND, Text("timeSig") }, { F::ANCHOR, staff }, { F::TIME, t },
                                                                  { F::TIMESIG_NUM, num }, { F::TIMESIG_DEN, den },
                                                                  { F::VOICE_INDEX, int64_t(0) } } },
                   Placement::ByTime);
        }
    }
}

bool Commands::changeTimeSignature(Transaction& tx, size_t index, int num, int den)
{
    const std::vector<Id> ms = m_ops.measures();
    if (index >= ms.size() || num <= 0 || den <= 0) {
        return false;
    }
    const Id fm = ms[index];
    const Fraction tick = m_ops.measureStart(fm);
    const Fraction ns(num, den);
    const std::vector<Id> here = timeSigsAt(tick);
    const Object* ots = here.empty() ? nullptr : m_ops.obj(here.front());
    if (ots && ots->intOr(F::TIMESIG_NUM, 0) == num && ots->intOr(F::TIMESIG_DEN, 0) == den) {
        return true;   // already so
    }
    auto hasTimeSig = [this](Id measure) { return !timeSigsAt(m_ops.measureStart(measure)).empty(); };
    if (ots && Fraction(ots->intOr(F::TIMESIG_NUM, 4), ots->intOr(F::TIMESIG_DEN, 4)) == ns) {
        // the same length (4/4 → 2/2): the measures up to the next time signature are only renamed
        for (size_t i = index; i < ms.size() && (i == index || !hasTimeSig(ms[i])); ++i) {
            tx.set(ms[i], F::TIMESIG_NUM, int64_t(num));
            tx.set(ms[i], F::TIMESIG_DEN, int64_t(den));
        }
        setTimeSigs(tx, tick, num, den);
        return true;
    }
    // staves with their own (local) time signatures: MuseScore rewrites them only in special cases (measure rests
    // only), which the engine doesn't do yet; it leaves such a score unchanged rather than risk a wrong rewrite
    for (size_t i = index; i < ms.size(); ++i) {
        if (i > index + 1 && hasTimeSig(ms[i]) && i > index) {
            break;
        }
        for (Id staff : allStaves()) {
            for (Id v : m_ops.obj(staff)->children()) {
                if (m_ops.obj(v)->kind() == Kind::Voice && !(m_ops.stretchAt(v, m_ops.measureStart(ms[i])) == Fraction(1, 1))) {
                    return false;
                }
                break;
            }
        }
    }
    const Object* m0 = m_ops.obj(fm);
    size_t from = index;
    bool rewrite = true;
    if (index == 0 && ms.size() > 1
        && !(m_ops.measureEnd(fm) - tick == Fraction(m0->intOr(F::TIMESIG_NUM, 4), m0->intOr(F::TIMESIG_DEN, 4)))) {
        // a pickup keeps its length and takes the new nominal; the measures after it are rewritten
        tx.set(fm, F::TIMESIG_NUM, int64_t(num));
        tx.set(fm, F::TIMESIG_DEN, int64_t(den));
        from = 1;
        rewrite = !hasTimeSig(ms[1]);
    } else if (m0->intOr(F::TIMESIG_NUM, 4) == num && m0->intOr(F::TIMESIG_DEN, 4) == den) {
        rewrite = false;   // the measures already have it
    }
    if (rewrite && !rewriteMeasures(tx, from, num, den)) {
        return false;
    }
    setTimeSigs(tx, tick, num, den);
    return true;
}

bool Commands::rewriteMeasures(Transaction& tx, size_t from, int num, int den)
{
    const Fraction ns(num, den);
    const std::vector<Id> ms = m_ops.measures();
    // the measures to rewrite: up to the next time signature or section break, in runs between frames
    std::vector<std::pair<size_t, size_t> > runs;   // [first, last]
    std::set<Fraction> frames;
    for (Id c : m_ops.obj(flowOf())->children()) {
        if (m_ops.obj(c)->kind() == Kind::FlowItem && m_ops.obj(c)->text(F::SUBKIND) == "frame") {
            frames.insert(m_ops.timeOf(c));
        }
    }
    size_t end = from;   // one past the last measure rewritten
    {
        size_t first = from;
        for (size_t i = from; i < ms.size(); ++i) {
            const Fraction at = m_ops.measureStart(ms[i]);
            if (i > from && !timeSigsAt(at).empty()) {
                break;
            }
            if (i > first && frames.count(at)) {   // a frame before this measure: the run ends
                runs.push_back({ first, i - 1 });
                first = i;
            }
            end = i + 1;
            if (m_ops.obj(ms[i])->text(F::BREAK) == "section") {
                break;
            }
        }
        runs.push_back({ first, end - 1 });
    }
    const std::vector<Id> staves = allStaves();
    auto voicesOf = [this](Id staff) {
        std::vector<Id> v;
        for (Id c : m_ops.obj(staff)->children()) {
            if (m_ops.obj(c)->kind() == Kind::Voice) {
                v.push_back(c);
            }
        }
        return v;
    };
    // a tuplet's elements (and those of the tuplets in it) follow each other exactly from its start to its end
    std::function<bool(Id, Id)> tiles = [&](Id voice, Id tuplet) {
        Fraction at = m_ops.timeOf(tuplet);
        for (Id c : m_ops.obj(voice)->children()) {
            const Object* o = m_ops.obj(c);
            if (c == tuplet || o->idOr(F::TUPLET) != tuplet || o->has(F::GRACE)) {
                continue;
            }
            if (!(m_ops.timeOf(c) == at) || (o->kind() == Kind::Tuplet && !tiles(voice, c))) {
                return false;
            }
            at = m_ops.endOf(c);
        }
        return at == m_ops.endOf(tuplet);
    };
    // first every check, so a rewrite that can't be done changes nothing (MuseScore undoes it)
    for (const auto& [a, b] : runs) {
        const Fraction t0 = m_ops.measureStart(ms[a]), t1 = m_ops.measureEnd(ms[b]);
        bool onlyMeasureRests = true;
        for (Id staff : staves) {
            for (Id v : voicesOf(staff)) {
                Fraction end = t0;   // where the music before ends: it may not overlap the next (a damaged file)
                for (Id e : m_ops.obj(v)->children()) {
                    const Object* o = m_ops.obj(e);
                    const Fraction t = m_ops.timeOf(e);
                    if (t < t0 || !(t < t1) || o->has(F::TUPLET) || o->has(F::GRACE)
                        || (o->kind() != Kind::Chord && o->kind() != Kind::Rest && o->kind() != Kind::Tuplet)) {
                        continue;
                    }
                    if (t < end || t1 < m_ops.endOf(e)) {
                        return false;
                    }
                    end = m_ops.endOf(e);
                    onlyMeasureRests = onlyMeasureRests && o->kind() == Kind::Rest && o->has(F::MEASURE_REST);
                    if (o->kind() == Kind::Tuplet && !tiles(v, e)) {
                        return false;   // a tuplet whose music doesn't fill it exactly (a damaged file) isn't rewritten
                    }
                    if (o->kind() == Kind::Tuplet) {
                        // a tuplet may cross a new barline only at its middle (MuseScore: "cannot split tuplet"
                        // otherwise), and here only one without tuplets inside it
                        const Fraction s = t - t0, f = m_ops.endOf(e) - t0;
                        const Fraction k = s / ns;
                        const Fraction nextBar = ns * Fraction(k.numerator() / k.denominator() + 1, 1);
                        if (nextBar < f) {
                            bool nested = false;
                            for (Id x : m_ops.obj(v)->children()) {
                                nested = nested || (m_ops.obj(x)->kind() == Kind::Tuplet && m_ops.obj(x)->idOr(F::TUPLET) == e);
                            }
                            if (!(f - s == (nextBar - s) * Fraction(2, 1)) || nested || f - nextBar > ns) {
                                return false;
                            }
                        }
                    }
                }
            }
        }
        if (!onlyMeasureRests) {
            for (size_t i = a; i <= b; ++i) {
                for (Id staff : staves) {
                    const Id v = voicesOf(staff).empty() ? Id() : voicesOf(staff).front();
                    if (v.isValid() && !(m_ops.stretchAt(v, m_ops.measureStart(ms[i])) == Fraction(1, 1))) {
                        return false;   // a staff with its own time signature can't be rewritten
                    }
                }
            }
        }
    }
    // the old time signature, given back after the range when the next measure has none (a section break)
    const Object* last = m_ops.obj(ms[end - 1]);
    const int64_t oldNum = last->intOr(F::TIMESIG_NUM, 4), oldDen = last->intOr(F::TIMESIG_DEN, 4);
    const bool reinstate = end < ms.size() && timeSigsAt(m_ops.measureStart(ms[end])).empty();
    // the runs, last first: a run's new length moves only what comes after it
    for (auto run = runs.rbegin(); run != runs.rend(); ++run) {
        rewriteRun(tx, m_ops.measures(), run->first, run->second, num, den);
    }
    if (reinstate) {
        setTimeSigs(tx, m_ops.measureStart(ms[end]), oldNum, oldDen);   // that measure is kept (moved by the runs)
    }
    return true;
}

void Commands::rewriteRun(Transaction& tx, const std::vector<Id>& ms, size_t a, size_t b, int num, int den)
{
    const Fraction ns(num, den);
    const Fraction t0 = m_ops.measureStart(ms[a]), t1 = m_ops.measureEnd(ms[b]);
    const Fraction len = t1 - t0;
    const Fraction k = len / ns;
    const int64_t count = (k.numerator() + k.denominator() - 1) / k.denominator();
    const Fraction fill = ns * Fraction(count, 1) - len;

    // what the old measures carry that MuseScore keeps: repeats (by time), line and page breaks (by the time they
    // follow), a section break (on the last measure), part layouts' own breaks
    std::vector<Fraction> repeatStarts, repeatEnds;
    std::vector<std::pair<Fraction, std::string> > breaks;
    bool sectionBreak = false;
    for (size_t i = a; i <= b; ++i) {
        const Object* mo = m_ops.obj(ms[i]);
        if (mo->has(F::REPEAT_START)) {
            repeatStarts.push_back(m_ops.measureStart(ms[i]));
        }
        if (mo->has(F::REPEAT_END)) {
            repeatEnds.push_back(m_ops.measureEnd(ms[i]));
        }
        const std::string& br = mo->text(F::BREAK);
        if (br == "line" || br == "page") {
            breaks.push_back({ m_ops.measureEnd(ms[i]), br });
        } else if (br == "section" && i == b) {
            sectionBreak = true;
        }
    }
    std::vector<Fraction> oldEnds;   // every measure's end, for the part layouts' breaks (kept by measure index)
    for (Id x : ms) {
        oldEnds.push_back(m_ops.measureEnd(x));
    }

    // each voice's music in the range as MuseScore reads it: chords, rests and whole tuplets, a gap becoming a rest
    // (joined to a rest before it), the new measures' extra length a gap at the end
    struct Item {
        Id id;   // invalid: a gap
        Fraction t, len;
        bool rest = false, tuplet = false;
        Id lastJoined;        // the last chord joined to it
        bool hidden = false;  // a hole in a voice (MuseScore's invisible gap rest): written as a hole again
    };
    struct Track {
        Id voice;
        int number;
        std::vector<Item> items;
    };
    std::vector<Track> tracks;
    std::vector<Id> absorbed;                    // rests joined to the rest before
    std::vector<std::pair<Id, Id> > joined;      // (chord, the rewrite piece after it that joins it)
    // lines and attached items by time: where a rest doesn't join the one before
    std::set<std::pair<Id, std::pair<int64_t, Fraction> > > lineEnds;   // (staff, (voice, time))
    for (Id c : m_ops.obj(m_ops.m.score())->children()) {
        const Object* o = m_ops.obj(c);
        if (o->kind() == Kind::Spanner && o->idOr(F::ANCHOR).isValid() && m_ops.obj(o->idOr(F::ANCHOR))
            && m_ops.obj(o->idOr(F::ANCHOR))->kind() == Kind::Staff) {
            const auto key = std::make_pair(o->intOr(F::VOICE_INDEX, 0), m_ops.timeOf(c));
            lineEnds.insert({ o->idOr(F::ANCHOR), key });
            lineEnds.insert({ o->idOr(F::ANCHOR), { key.first, m_ops.timeOf(c) + o->fractionOr(F::DURATION, Fraction()) } });
        }
    }
    std::set<Fraction> attachedAt;
    static const std::set<std::string> annotations { "harmony", "dynamic", "staffText", "systemText", "rehearsalMark", "tempo",
                                                     "instrumentChange", "fermata", "expression", "playTechnique" };
    // and those kept as written (MuseScore's other segment annotations)
    static const std::set<std::string> annotationTags { "Symbol", "Image", "TripletFeel", "Capo", "StringTunings", "FretDiagram",
                                                        "HarpPedalDiagram", "FiguredBass", "StaffState", "Sticking", "TempoText",
                                                        "StaffText", "SystemText", "Expression", "Harmony", "Dynamic", "Fermata",
                                                        "RehearsalMark", "InstrumentChange", "PlayTechAnnotation" };
    for (Id c : m_ops.obj(flowOf())->children()) {
        const Object* o = m_ops.obj(c);
        if (o->kind() == Kind::FlowItem
            && (annotations.count(o->text(F::SUBKIND)) || (o->text(F::SUBKIND) == "detail" && annotationTags.count(o->text(F::TAG))))) {
            attachedAt.insert(m_ops.timeOf(c));
        }
    }
    // (time, at the end of the measure before: MuseScore's end-of-measure segments, read with that measure)
    std::map<std::pair<Id, int64_t>, std::vector<std::pair<Fraction, bool> > > trackMarks;
    {
        static const std::set<std::string> elementKinds { "clef", "keySig", "timeSig", "breath" };
        const Fraction scoreEnd = m_ops.measureEnd(ms.back());
        static const std::set<std::string> elementTags { "BarLine", "Ambitus", "Breath", "Clef", "KeySig", "TimeSig" };
        for (Id c : m_ops.obj(flowOf())->children()) {
            const Object* o = m_ops.obj(c);
            const Fraction t = m_ops.timeOf(c);
            // (repeat barlines aren't: MuseScore makes them from the measures, as generated elements it doesn't read)
            if (o->kind() != Kind::FlowItem || !o->idOr(F::ANCHOR).isValid()) {
                continue;
            }
            const bool plainBarline = o->text(F::TAG) == "BarLine"
                                      && (o->text(F::DETAILS).find("<subtype>") == std::string::npos
                                          || o->text(F::DETAILS).find("<subtype>normal</subtype>") != std::string::npos);
            const bool atEnd = o->has(F::AT_END);
            if (o->text(F::TAG) == "BarLine" && atEnd && t == scoreEnd) {
                continue;   // the score's final barline: the segment the read stops at (not read)
            }
            if (atEnd && o->text(F::SUBKIND) == "timeSig") {
                continue;   // an end-of-measure time signature is removed before the rewrite reads the range
            }
            if (elementKinds.count(o->text(F::SUBKIND)) || (o->text(F::SUBKIND) == "detail" && elementTags.count(o->text(F::TAG)) && !plainBarline)) {
                trackMarks[{ o->idOr(F::ANCHOR), o->intOr(F::VOICE_INDEX, 0) }].push_back({ t, atEnd });
            }
        }
        for (auto& [k, v] : trackMarks) {
            std::sort(v.begin(), v.end());
        }
    }
    auto anchoredAt = [&](Id staff, int voice, const Fraction& t) {
        return attachedAt.count(t) || lineEnds.count({ staff, { int64_t(voice), t } });
    };
    auto joinsChordBefore = [this](Id before, Id chord) {
        std::vector<Id> a, b;
        for (Id x : m_ops.obj(before)->children()) {
            if (m_ops.obj(x)->kind() == Kind::Note) {
                a.push_back(x);
            }
        }
        for (Id x : m_ops.obj(chord)->children()) {
            if (m_ops.obj(x)->kind() == Kind::Note) {
                b.push_back(x);
            }
        }
        if (b.empty() || a.size() != b.size()) {
            return false;
        }
        for (size_t i = 0; i < a.size(); ++i) {
            if (m_ops.obj(a[i])->idOr(F::TIE_TO) != b[i] || !m_ops.obj(a[i])->has(F::TIE_GENERATED)) {
                return false;
            }
        }
        return true;
    };
    for (Id staff : allStaves()) {
        if (m_ops.obj(staff)->has(F::LINKED_STAFF)) {
            continue;   // a linked copy follows its main staff (the linked-staves hook)
        }
        for (Id v : m_ops.obj(staff)->children()) {
            if (m_ops.obj(v)->kind() != Kind::Voice) {
                continue;
            }
            Track tr { v, int(m_ops.obj(v)->intOr(F::NUMBER, 0)), {} };
            Fraction at = t0;
            // elements of this staff and voice between two events (clef, key, time signature, breath, a barline
            // other than a plain one): MuseScore reads them into the voice, so the rests on either side stay apart
            const auto& marks = trackMarks[{ staff, int64_t(tr.number) }];
            // a mark after `after` and before the event at `upTo` (all marks at that time come before it); at the
            // range's end only the end-of-measure ones (the others belong to the next measure)
            auto interrupted = [&marks](const Fraction& after, const Fraction& upTo, bool rangeEnd = false) {
                for (auto it = std::upper_bound(marks.begin(), marks.end(), std::make_pair(after, true)); it != marks.end() && it->first <= upTo; ++it) {
                    if (it->first < upTo || !rangeEnd || it->second) {
                        return true;
                    }
                }
                return false;
            };
            auto append = [&tr, &interrupted, t1](const Fraction& t, const Fraction& l, bool hidden, const Fraction& upTo, bool joinable) {
                if (!tr.items.empty() && tr.items.back().rest && joinable && !interrupted(tr.items.back().t, upTo, upTo == t1)) {
                    tr.items.back().len += l;
                } else {
                    tr.items.push_back({ Id(), t, l, true, false, Id(), hidden });
                }
            };
            // the measures (of the range) where this voice has music: MuseScore fills a hole there with an invisible
            // gap rest when it reads the score; a hole elsewhere is read as a new (visible) rest
            std::vector<bool> used(b - a + 1, false);
            for (Id e : m_ops.obj(v)->children()) {
                const Object* o = m_ops.obj(e);
                if (o->has(F::TUPLET) || o->has(F::GRACE) || (o->kind() != Kind::Chord && o->kind() != Kind::Rest && o->kind() != Kind::Tuplet)) {
                    continue;
                }
                const Fraction s0 = m_ops.timeOf(e), s1 = m_ops.endOf(e);
                for (size_t i = a; i <= b; ++i) {
                    if (s0 < m_ops.measureEnd(ms[i]) && m_ops.measureStart(ms[i]) < s1) {
                        used[i - a] = true;
                    }
                }
            }
            auto gap = [&](const Fraction& t, const Fraction& l, bool hole, const Fraction& upTo) {
                if (!(Fraction() < l)) {
                    return;
                }
                if (!hole || tr.number == 0) {
                    append(t, l, false, upTo, true);
                    return;
                }
                // a hole, measure by measure
                for (size_t i = a; i <= b; ++i) {
                    const Fraction ms0 = std::max(t, m_ops.measureStart(ms[i])), ms1 = std::min(t + l, m_ops.measureEnd(ms[i]));
                    if (!(ms0 < ms1)) {
                        continue;
                    }
                    if (used[i - a]) {   // a gap rest: an element, joined as rests are
                        append(ms0, ms1 - ms0, true, ms0, !anchoredAt(staff, tr.number, ms0));
                    } else {
                        append(ms0, ms1 - ms0, false, upTo, true);
                    }
                }
            };
            for (Id e : m_ops.obj(v)->children()) {
                const Object* o = m_ops.obj(e);
                const Fraction t = m_ops.timeOf(e);
                if (t < t0 || !(t < t1) || o->has(F::TUPLET) || o->has(F::GRACE)
                    || (o->kind() != Kind::Chord && o->kind() != Kind::Rest && o->kind() != Kind::Tuplet)) {
                    continue;
                }
                gap(at, t - at, true, t);
                const Fraction l = m_ops.endOf(e) - t;
                // MuseScore's TrackList::append: a rest joins the rest before it (unless a line starts or ends there or
                // something is attached at that time); a chord all of whose notes are tied from the chord before by a
                // rewrite joins it
                Item* back = tr.items.empty() ? nullptr : &tr.items.back();
                if (back && back->rest && o->kind() == Kind::Rest && !anchoredAt(staff, tr.number, t) && !interrupted(back->t, t)) {
                    back->len += l;
                    absorbed.push_back(e);
                    at = t + l;
                    continue;
                }
                if (back && !back->rest && !back->tuplet && back->id.isValid() && o->kind() == Kind::Chord
                    && joinsChordBefore(back->lastJoined.isValid() ? back->lastJoined : back->id, e) && !interrupted(back->t, t)) {
                    back->len += l;
                    back->lastJoined = e;
                    joined.push_back({ back->id, e });
                    at = t + l;
                    continue;
                }
                tr.items.push_back({ e, t, l, o->kind() == Kind::Rest, o->kind() == Kind::Tuplet, Id(), false });
                at = t + l;
            }
            gap(at, t1 - at, true, t1);
            gap(t1, fill, false, t1);   // the new measures' extra length (a visible rest)
            if (!tr.items.empty()) {
                tracks.push_back(std::move(tr));
            }
        }
    }

    // the new measures, and what follows moved by the extra length (lines that end where the range ends grow with
    // it, as MuseScore's ScoreRange::fill makes them)
    const Id flow = flowOf();
    if (Fraction() < fill) {
        std::vector<Id> endingThere;
        for (Id c : m_ops.obj(m_ops.m.score())->children()) {
            const Object* o = m_ops.obj(c);
            if (o->kind() == Kind::Spanner && m_ops.timeOf(c) < t1 && m_ops.timeOf(c) + o->fractionOr(F::DURATION, Fraction()) == t1) {
                endingThere.push_back(c);
            }
        }
        m_ops.shiftFrom(tx, t1, fill);
        for (Id c : endingThere) {
            tx.set(c, F::DURATION, m_ops.obj(c)->fractionOr(F::DURATION, Fraction()) + fill);
        }
    }
    std::vector<Id> stale;
    for (Id c : m_ops.obj(flow)->children()) {
        const Object* o = m_ops.obj(c);
        const Fraction t = m_ops.timeOf(c);
        if (t0 <= t && t < t1
            && (o->kind() == Kind::Measure
                || (o->kind() == Kind::FlowItem && (o->text(F::SUBKIND) == "measureDetail" || o->text(F::SUBKIND) == "mmrMeasure")))) {
            stale.push_back(c);
        }
    }
    for (Id c : stale) {
        removeTree(tx, c);
    }
    std::vector<Id> made;
    for (int64_t i = 0; i < count; ++i) {
        const Fraction at = t0 + ns * Fraction(i, 1);
        std::vector<std::pair<Field, Value> > f { { F::TIME, at }, { F::DURATION, ns }, { F::TIMESIG_NUM, int64_t(num) },
                                                   { F::TIMESIG_DEN, int64_t(den) } };
        for (const Fraction& r : repeatStarts) {
            if (r == at) {
                f.push_back({ F::REPEAT_START, true });
            }
        }
        for (const Fraction& r : repeatEnds) {
            if (r == at + ns) {
                f.push_back({ F::REPEAT_END, int64_t(2) });
            }
        }
        for (const auto& [p, kind] : breaks) {
            if (at < p && p <= at + ns) {
                f.push_back({ F::BREAK, Text(kind) });
            }
        }
        if (sectionBreak && i == count - 1) {
            f.push_back({ F::BREAK, Text("section") });
        }
        // before the items at its time (a measure comes first, as read)
        const Id m = create(tx, m_ops.m_ids, flow, { Kind::Measure, f });
        std::vector<Id> kids = m_ops.obj(flow)->children().ids();
        kids.erase(std::remove(kids.begin(), kids.end(), m), kids.end());
        auto pos = std::lower_bound(kids.begin(), kids.end(), at, [this](Id x, const Fraction& time) { return m_ops.timeOf(x) < time; });
        kids.insert(pos, m);
        tx.set(flow, F::CHILDREN, IdList(std::move(kids)));
        made.push_back(m);
    }
    // rehearsal marks go to the start of their measure; voltas stay only where they still begin and end at barlines
    std::vector<Id> items;
    for (Id c : m_ops.obj(flow)->children()) {
        const Object* o = m_ops.obj(c);
        if (o->kind() == Kind::FlowItem && o->text(F::SUBKIND) == "rehearsalMark" && t0 <= m_ops.timeOf(c) && m_ops.timeOf(c) < t1 + fill) {
            items.push_back(c);
        }
    }
    for (Id c : items) {
        const Fraction rel = m_ops.timeOf(c) - t0;
        const Fraction q = rel / ns;
        tx.set(c, F::TIME, t0 + ns * Fraction(q.numerator() / q.denominator(), 1));
    }
    std::vector<Id> voltas;
    for (Id c : m_ops.obj(m_ops.m.score())->children()) {
        const Object* o = m_ops.obj(c);
        if (o->kind() == Kind::Spanner && o->text(F::SUBKIND) == "volta" && t0 <= m_ops.timeOf(c) && m_ops.timeOf(c) < t1 + fill) {
            const Fraction s = m_ops.timeOf(c) - t0, e = s + o->fractionOr(F::DURATION, Fraction());
            const bool startOk = (s / ns).denominator() == 1;
            const bool endOk = (e / ns).denominator() == 1 || t1 + fill < t0 + e;
            if (!startOk || !endOk) {
                voltas.push_back(c);
            }
        }
    }
    for (Id c : voltas) {
        removeTree(tx, c);
    }

    // the joined chords and rests go: a joined chord's notes pass their ties on to the chord they join
    for (const auto& [into, piece] : joined) {
        std::vector<Id> a, b;
        for (Id x : m_ops.obj(into)->children()) {
            if (m_ops.obj(x)->kind() == Kind::Note) {
                a.push_back(x);
            }
        }
        for (Id x : m_ops.obj(piece)->children()) {
            if (m_ops.obj(x)->kind() == Kind::Note) {
                b.push_back(x);
            }
        }
        for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
            const Id next = m_ops.obj(b[i])->idOr(F::TIE_TO);
            tx.set(a[i], F::TIE_TO, next.isValid() ? Value(next) : Value());
            tx.set(a[i], F::TIE_GENERATED, m_ops.obj(b[i])->has(F::TIE_GENERATED) ? Value(true) : Value());
            tx.set(b[i], F::TIE_TO, Value());
        }
        m_ops.removeEvent(tx, piece);
    }
    for (Id r : absorbed) {
        m_ops.removeEvent(tx, r);
    }

    // each voice written again into the new measures: values split at the barlines (MuseScore's toDurationList:
    // notes with dots, rests without), tied; a rest that fills a measure from its start a measure rest (voice 1;
    // nothing in the other voices)
    auto measureOf = [&](const Fraction& at) {
        const Fraction q = (at - t0) / ns;
        return t0 + ns * Fraction(q.numerator() / q.denominator(), 1);
    };
    for (Track& tr : tracks) {
        if (tr.number > 0 && tr.items.size() == 1 && tr.items.front().rest) {
            if (tr.items.front().id.isValid()) {
                m_ops.removeEvent(tx, tr.items.front().id);
            }
            continue;
        }
        Fraction at = t0;
        // a tuplet written again (MuseScore's TrackList::writeTuplet): its notes and rests in undotted values,
        // tied; one that crosses a new barline at its middle is split into two halves
        std::function<void(Id, Id)> writeTuplet = [&](Id tuplet, Id parent) {
            const Fraction du = m_ops.endOf(tuplet) - m_ops.timeOf(tuplet);
            const Fraction start = at;
            std::vector<std::pair<Field, Value> > fields;   // read before it changes (a changed object is a new one)
            m_ops.obj(tuplet)->forEachField([&fields](Field fl, const Value& val) {
                if (copiedToPiece(fl) && fl != F::TUPLET) {
                    fields.push_back({ fl, val });
                }
            });
            const Fraction base = m_ops.obj(tuplet)->fractionOr(F::BASE_DURATION, Fraction(1, 8));
            const bool split = measureOf(at) + ns - at < du;
            Id dt = tuplet;
            if (split) {   // two halves, each half as long (MuseScore: baseLen shifted by one)
                tx.set(dt, F::DURATION, du * Fraction(1, 2));
                tx.set(dt, F::BASE_DURATION, base * Fraction(1, 2));
            }
            tx.set(dt, F::TIME, at);
            tx.set(dt, F::TUPLET, parent.isValid() ? Value(parent) : Value());
            std::vector<Id> elements;
            for (Id c : m_ops.obj(tr.voice)->children()) {
                if (c != tuplet && m_ops.obj(c)->idOr(F::TUPLET) == tuplet && !m_ops.obj(c)->has(F::GRACE)) {
                    elements.push_back(c);
                }
            }
            for (const Id e : elements) {
                if (m_ops.obj(e)->kind() == Kind::Tuplet) {
                    writeTuplet(e, dt);
                    continue;
                }
                Pieces pw = startPieces(e, tr.voice);
                Fraction left = m_ops.endOf(e) - m_ops.timeOf(e);
                while (Fraction() < left) {
                    const Fraction remains = measureOf(at) + ns - at;
                    if (split && dt == tuplet && start < at && at == measureOf(at)) {
                        // the second half, from the barline
                        std::vector<std::pair<Field, Value> > f = fields;
                        f.push_back({ F::TIME, at });
                        f.push_back({ F::DURATION, du * Fraction(1, 2) });
                        f.push_back({ F::BASE_DURATION, base * Fraction(1, 2) });
                        if (parent.isValid()) {
                            f.push_back({ F::TUPLET, parent });
                        }
                        dt = create(tx, m_ops.m_ids, tr.voice, { Kind::Tuplet, f }, Placement::ByTime);
                    }
                    const Fraction factor = m_ops.tupletFactor(dt);   // written → actual
                    const Fraction actual = std::min(remains, left);
                    const auto values = durationList(actual / factor, 0);
                    if (values.empty()) {
                        break;
                    }
                    for (const NoteValue& v : values) {
                        addPiece(tx, pw, at, v.length() * factor, &v, false, dt);
                        at += v.length() * factor;
                        left -= v.length() * factor;
                    }
                }
                finishPieces(tx, pw);
            }
        };
        for (size_t n = 0; n < tr.items.size(); ++n) {
            const Item& it = tr.items[n];
            if (it.tuplet) {
                writeTuplet(it.id, Id());
                at = it.t + it.len;
                continue;
            }
            if (it.hidden) {   // a hole stays a hole (a rest it took in goes)
                if (it.id.isValid()) {
                    m_ops.removeEvent(tx, it.id);
                }
                at += it.len;
                continue;
            }
            const bool lastItem = n + 1 == tr.items.size();
            Pieces pw = startPieces(it.id, tr.voice);
            pw.rest = it.rest;
            Fraction left = it.len;
            while (Fraction() < left) {
                const Fraction mStart = measureOf(at);
                const Fraction remains = mStart + ns - at;
                if (it.rest && (!(left < remains) || lastItem) && remains == ns) {
                    // a measure rest (voice 1 only)
                    if (tr.number == 0) {
                        addPiece(tx, pw, at, ns, nullptr, true, Id());
                    } else if (pw.first && it.id.isValid()) {
                        m_ops.removeEvent(tx, it.id);
                        pw.first = false;
                        pw.gone = true;
                    }
                    at += ns;
                    left = left < ns ? Fraction() : left - ns;
                    continue;
                }
                const Fraction du = std::min(remains, left);
                const auto values = durationList(du, it.rest ? 0 : 4);
                if (values.empty()) {
                    break;   // not expressible (shorter than 1/1024): left as it is
                }
                for (const NoteValue& v : values) {
                    addPiece(tx, pw, at, v.length(), &v, false, Id());
                    at += v.length();
                    left -= v.length();
                }
            }
            finishPieces(tx, pw);
        }
    }

    // part layouts keep their breaks by measure index: each after the measure that now holds the time it followed
    const std::vector<Id> now = m_ops.measures();
    for (Id c : m_ops.obj(m_ops.m.score())->children()) {
        const Object* layout = m_ops.obj(c);
        if (layout->kind() != Kind::Layout || !layout->has(F::LAYOUT_BREAKS)) {
            continue;
        }
        std::vector<std::pair<std::string, std::string> > moved;
        for (const auto& [index, kind] : model::unpackPairs(layout->text(F::LAYOUT_BREAKS))) {
            const size_t i = size_t(std::stoul(index));
            if (i >= oldEnds.size()) {
                moved.push_back({ index, kind });
                continue;
            }
            const Fraction p = t1 <= oldEnds[i] ? oldEnds[i] + fill : oldEnds[i];
            for (size_t j = 0; j < now.size(); ++j) {
                if (m_ops.measureStart(now[j]) < p && p <= m_ops.measureEnd(now[j])) {
                    moved.push_back({ std::to_string(j), kind });
                    break;
                }
            }
        }
        tx.set(c, F::LAYOUT_BREAKS, Text(model::packPairs(moved)));
    }
}

bool Commands::hasOwnTie(Id note) const
{
    const Object* n = m_ops.obj(note);
    return n && (n->text(F::DETAILS).find("<LaissezVib") != std::string::npos || n->text(F::DETAILS).find("<PartialTie") != std::string::npos);
}

Commands::Pieces Commands::startPieces(Id source, Id voice) const
{
    Pieces p;
    p.source = source;
    p.voice = voice;
    const Object* o = m_ops.obj(source);
    p.rest = !o || o->kind() == Kind::Rest;
    if (o && o->kind() == Kind::Chord) {
        for (Id x : o->children()) {
            if (m_ops.obj(x)->kind() == Kind::Note) {
                p.notes.push_back(x);
                p.tiedTo.push_back(m_ops.obj(x)->idOr(F::TIE_TO));
                p.tieGenerated.push_back(m_ops.obj(x)->has(F::TIE_GENERATED));
            }
        }
    }
    return p;
}

Id Commands::addPiece(Transaction& tx, Pieces& p, const Fraction& t, const Fraction& duration, const NoteValue* v, bool measureRest, Id tuplet)
{
    Id e;
    if (p.first && p.source.isValid() && !p.gone) {
        e = p.source;   // the first piece is the event itself (its markings, lines and links stay with it)
        tx.set(e, F::TIME, t);
    } else {
        std::vector<std::pair<Field, Value> > f { { F::TIME, t } };
        if (p.source.isValid() && m_ops.obj(p.source)) {
            m_ops.obj(p.source)->forEachField([&f](Field fl, const Value& val) {
                if (copiedToPiece(fl) && fl != F::TUPLET) {
                    f.push_back({ fl, val });
                }
            });
        }
        e = create(tx, m_ops.m_ids, p.voice, { p.rest ? Kind::Rest : Kind::Chord, f }, Placement::ByTime);
        // a continuation carries the notes, not the markings (MuseScore's removeMarkings)
        std::vector<Id> pieceNotes;
        for (Id nt : p.notes) {
            std::vector<std::pair<Field, Value> > nf;
            m_ops.obj(nt)->forEachField([&nf](Field fl, const Value& val) {
                if (copiedToPiece(fl)) {
                    nf.push_back({ fl, val });
                }
            });
            // a laissez vibrer or partial tie (a tie of its own, kept as written) stays on the first piece only
            for (auto& [fl, val] : nf) {
                if (fl == F::DETAILS && std::holds_alternative<Text>(val)) {
                    val = Text(withoutOwnTie(withoutOwnTie(std::get<Text>(val).str(), "LaissezVib"), "PartialTie"));
                }
            }
            pieceNotes.push_back(create(tx, m_ops.m_ids, e, { Kind::Note, nf }));
        }
        if (p.prev.isValid()) {
            std::vector<Id> before;
            for (Id x : m_ops.obj(p.prev)->children()) {
                if (m_ops.obj(x)->kind() == Kind::Note) {
                    before.push_back(x);
                }
            }
            for (size_t i = 0; i < before.size() && i < pieceNotes.size(); ++i) {
                if (hasOwnTie(before[i])) {
                    continue;   // MuseScore ties a piece on only when its note has no tie of its own
                }
                tx.set(before[i], F::TIE_TO, pieceNotes[i]);
                tx.set(before[i], F::TIE_GENERATED, true);
            }
        }
    }
    tx.set(e, F::DURATION, duration);
    tx.set(e, F::MEASURE_REST, measureRest ? Value(true) : Value());
    tx.set(e, F::BASE_DURATION, v ? Value(v->base) : Value());
    tx.set(e, F::DOTS, v && v->dots ? Value(int64_t(v->dots)) : Value());
    tx.set(e, F::STRETCH, Value());
    tx.set(e, F::TUPLET, tuplet.isValid() ? Value(tuplet) : Value());
    if (m_ops.obj(e)->text(F::SUBKIND) == "measureRepeat") {
        // a measure repeat is rewritten as the rest it stands for (MuseScore's ScoreRange: "TODO: copy previous measure")
        tx.set(e, F::SUBKIND, Value());
        tx.set(e, F::DETAILS, Value());
        tx.set(e, F::DETAILS_KEY, Value());
        tx.set(e, F::TAG, Value());
    }
    p.prev = e;
    p.first = false;
    return e;
}

void Commands::finishPieces(Transaction& tx, const Pieces& p)
{
    // the last piece continues the ties the chord had
    if (p.rest || !p.prev.isValid() || p.prev == p.source) {
        return;
    }
    std::vector<Id> lastNotes;
    for (Id x : m_ops.obj(p.prev)->children()) {
        if (m_ops.obj(x)->kind() == Kind::Note) {
            lastNotes.push_back(x);
        }
    }
    for (size_t i = 0; i < lastNotes.size() && i < p.tiedTo.size(); ++i) {
        if (p.tiedTo[i].isValid()) {
            tx.set(lastNotes[i], F::TIE_TO, p.tiedTo[i]);
            if (p.tieGenerated[i]) {
                tx.set(lastNotes[i], F::TIE_GENERATED, true);
            }
        }
    }
}
}
