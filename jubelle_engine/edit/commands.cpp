/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * The rules follow MuseScore Studio's commands (Score::changeEnharmonicSpelling, changeAccidental, addInterval,
 * toggleArticulation, changeSelectedElementsVoice, addGraceNote; MuseScore Limited and others, GPL v3).
 */
#include "commands.h"

#include <algorithm>
#include <set>
#include <stdexcept>

#include "structure.h"
#include "model/fields.h"
#include "model/notation.h"
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
