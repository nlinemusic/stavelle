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
#include "chordsymbol.h"
#include "clipboard.h"
#include "links.h"
#include "tablature.h"
#include "model/detailskey.h"
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
        const Operations::Transposition tr = m_ops.transpositionAt(staff->id(), m_ops.timeOf(n->parent()));
        s.chrom = tr.chromatic;
        s.dia = tr.diatonic;
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
        // a global time signature has no stretch: a staff's own one kept as written loses it (MuseScore sets the
        // new signature's stretch, 1)
        std::string d = m_ops.obj(ts)->text(F::DETAILS);
        for (const char* tag : { "stretchN", "stretchD" }) {
            const std::string open = std::string("<") + tag + ">", close = std::string("</") + tag + ">";
            for (size_t a; (a = d.find(open)) != std::string::npos;) {
                const size_t b = d.find(close, a);
                if (b == std::string::npos) {
                    break;
                }
                d.erase(a, b + close.size() - a);
            }
        }
        if (d != m_ops.obj(ts)->text(F::DETAILS)) {
            tx.set(ts, F::DETAILS, Text(d));
        }
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
        // the measures already have it; only staves with their own time signature there change, and only where
        // they hold nothing but measure rests (MuseScore's rewriteMeasures for a staff: otherwise refused)
        rewrite = false;
        std::vector<Id> measureRests;
        for (Id staff : allStaves()) {
            std::vector<Id> voices;
            for (Id v : m_ops.obj(staff)->children()) {
                if (m_ops.obj(v)->kind() == Kind::Voice) {
                    voices.push_back(v);
                }
            }
            if (voices.empty() || m_ops.stretchAt(voices.front(), tick) == Fraction(1, 1)) {
                continue;
            }
            // up to the staff's next time signature
            Fraction until = m_ops.measureEnd(ms.back());
            for (size_t i = index + 1; i < ms.size(); ++i) {
                bool own = false;
                for (Id ts : timeSigsAt(m_ops.measureStart(ms[i]))) {
                    own = own || !m_ops.obj(ts)->has(F::ANCHOR) || m_ops.obj(ts)->idOr(F::ANCHOR) == staff;
                }
                if (own) {
                    until = m_ops.measureStart(ms[i]);
                    break;
                }
            }
            for (Id v : voices) {
                for (Id e : m_ops.eventsOf(v)) {
                    const Fraction t = m_ops.timeOf(e);
                    if (t < tick || !(t < until)) {
                        continue;
                    }
                    if (!m_ops.obj(e)->has(F::MEASURE_REST)) {
                        return false;   // "cannot change local time signature: measure is not empty"
                    }
                    measureRests.push_back(e);
                }
            }
        }
        for (Id r : measureRests) {
            tx.set(r, F::STRETCH, Value());
        }
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
                        // otherwise); so may the tuplets inside it (MuseScore halves them wherever the barline falls,
                        // which makes no sense musically: the engine refuses that instead)
                        std::function<bool(Id)> fits = [&](Id tu) {
                            const Fraction s = m_ops.timeOf(tu) - t0, f = m_ops.endOf(tu) - t0;
                            const Fraction k = s / ns;
                            const Fraction nextBar = ns * Fraction(k.numerator() / k.denominator() + 1, 1);
                            if (nextBar < f && (!(f - s == (nextBar - s) * Fraction(2, 1)) || f - nextBar > ns)) {
                                return false;
                            }
                            for (Id x : m_ops.obj(v)->children()) {
                                if (m_ops.obj(x)->kind() == Kind::Tuplet && m_ops.obj(x)->idOr(F::TUPLET) == tu && !fits(x)) {
                                    return false;
                                }
                            }
                            return true;
                        };
                        if (!fits(e)) {
                            return false;
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
    // the staves' old time signatures are given back after the range where a staff has none there
    const bool reinstate = end < ms.size();
    // the runs, last first: a run's new length moves only what comes after it
    for (auto run = runs.rbegin(); run != runs.rend(); ++run) {
        rewriteRun(tx, m_ops.measures(), run->first, run->second, num, den);
    }
    if (reinstate) {
        // every staff without a time signature of its own there gets the old one back (MuseScore's rewriteMeasures);
        // that measure is kept (moved by the runs)
        const Fraction at = m_ops.measureStart(ms[end]);
        std::set<Id> have;
        bool global = false;
        for (Id ts : timeSigsAt(at)) {
            const Object* o = m_ops.obj(ts);
            global = global || !o->has(F::ANCHOR);
            have.insert(o->idOr(F::ANCHOR));
        }
        // (a copy of the staff's own time signature in effect there; a staff that has none gets none)
        const std::vector<Id>& kids = m_ops.obj(flowOf())->children().ids();
        for (Id staff : global ? std::vector<Id>() : allStaves()) {
            if (have.count(staff)) {
                continue;
            }
            Id before;
            for (Id c : kids) {
                const Object* o = m_ops.obj(c);
                if (!(m_ops.timeOf(c) < at)) {
                    break;
                }
                if (isTimeSig(o) && (!o->has(F::ANCHOR) || o->idOr(F::ANCHOR) == staff)) {
                    before = c;
                }
            }
            if (!before.isValid()) {
                continue;
            }
            std::vector<std::pair<Field, Value> > f;
            m_ops.obj(before)->forEachField([&f](Field fl, const Value& val) {
                if (fl != F::KIND && fl != F::PARENT && fl != F::CHILDREN && fl != F::ORIGIN_EID && fl != F::SEQ && fl != F::AT_END
                    && fl != F::TIME && fl != F::ANCHOR && fl != F::LINKED_TO) {
                    f.push_back({ fl, val });
                }
            });
            f.push_back({ F::TIME, at });
            f.push_back({ F::ANCHOR, staff });
            create(tx, m_ops.m_ids, flowOf(), { Kind::FlowItem, f }, Placement::ByTime);
        }
    }
    return true;
}

void Commands::rewriteRun(Transaction& tx, const std::vector<Id>& ms, size_t a, size_t b, int num, int den,
                          std::vector<Fraction> lens)
{
    const Fraction ns(num, den);
    const Fraction t0 = m_ops.measureStart(ms[a]), t1 = m_ops.measureEnd(ms[b]);
    const Fraction len = t1 - t0;
    if (lens.empty()) {   // as many measures of the new time signature as the music needs
        const Fraction k = len / ns;
        lens.assign(size_t((k.numerator() + k.denominator() - 1) / k.denominator()), ns);
    }
    const int64_t count = int64_t(lens.size());
    std::vector<Fraction> starts { t0 };   // the new measures' starts, and the end
    for (const Fraction& l : lens) {
        starts.push_back(starts.back() + l);
    }
    const Fraction fill = starts.back() - t1;
    // the new measure holding a time (its start, its length)
    auto newMeasure = [&starts](const Fraction& at) {
        auto it = std::upper_bound(starts.begin(), starts.end() - 1, at);
        const size_t i = it == starts.begin() ? 0 : size_t(it - starts.begin()) - 1;
        return std::make_pair(starts[i], starts[i + 1] - starts[i]);
    };
    auto isBarline = [&starts](const Fraction& at) { return std::binary_search(starts.begin(), starts.end(), at); };

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
        const Fraction at = starts[size_t(i)], ml = lens[size_t(i)];
        std::vector<std::pair<Field, Value> > f { { F::TIME, at }, { F::DURATION, ml }, { F::TIMESIG_NUM, int64_t(num) },
                                                   { F::TIMESIG_DEN, int64_t(den) } };
        for (const Fraction& r : repeatStarts) {
            if (r == at) {
                f.push_back({ F::REPEAT_START, true });
            }
        }
        for (const Fraction& r : repeatEnds) {
            if (r == at + ml) {
                f.push_back({ F::REPEAT_END, int64_t(2) });
            }
        }
        for (const auto& [p, kind] : breaks) {
            if (at < p && p <= at + ml) {
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
        tx.set(c, F::TIME, newMeasure(m_ops.timeOf(c)).first);
    }
    std::vector<Id> voltas;
    for (Id c : m_ops.obj(m_ops.m.score())->children()) {
        const Object* o = m_ops.obj(c);
        if (o->kind() == Kind::Spanner && o->text(F::SUBKIND) == "volta" && t0 <= m_ops.timeOf(c) && m_ops.timeOf(c) < t1 + fill) {
            const Fraction s = m_ops.timeOf(c) - t0, e = s + o->fractionOr(F::DURATION, Fraction());
            const bool startOk = isBarline(t0 + s);
            const bool endOk = isBarline(t0 + e) || t1 + fill < t0 + e;
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
    auto measureOf = [&](const Fraction& at) { return newMeasure(at).first; };
    auto measureEndOf = [&](const Fraction& at) { const auto m = newMeasure(at); return m.first + m.second; };
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
        // (nested tuplets too: a tuplet split in two gives the tuplets inside it a second parent, made when the
        // first music after the barline needs it)
        struct Level {
            Id first, current, second;
            std::vector<std::pair<Field, Value> > fields;   // read before it changes (a changed object is a new one)
            Fraction du, base, start;
            bool split = false;
            Level* parent = nullptr;
        };
        std::function<Id(Level&)> secondOf = [&](Level& l) -> Id {
            if (!l.split || l.second.isValid()) {
                return l.current;
            }
            std::vector<std::pair<Field, Value> > f = l.fields;
            f.push_back({ F::TIME, at });
            f.push_back({ F::DURATION, l.du * Fraction(1, 2) });
            f.push_back({ F::BASE_DURATION, l.base * Fraction(1, 2) });
            const Id parent = l.parent ? secondOf(*l.parent) : Id();
            if (parent.isValid()) {
                f.push_back({ F::TUPLET, parent });
            }
            l.second = l.current = create(tx, m_ops.m_ids, tr.voice, { Kind::Tuplet, f }, Placement::ByTime);
            return l.current;
        };
        auto atItsBarline = [&](const Level& l) { return l.split && !l.second.isValid() && l.start < at && at == measureOf(at); };
        std::function<void(Id, Level*)> writeTuplet = [&](Id tuplet, Level* parentLevel) {
            Level l;
            l.first = l.current = tuplet;
            l.parent = parentLevel;
            l.du = m_ops.endOf(tuplet) - m_ops.timeOf(tuplet);
            l.start = at;
            m_ops.obj(tuplet)->forEachField([&l](Field fl, const Value& val) {
                if (copiedToPiece(fl) && fl != F::TUPLET) {
                    l.fields.push_back({ fl, val });
                }
            });
            l.base = m_ops.obj(tuplet)->fractionOr(F::BASE_DURATION, Fraction(1, 8));
            l.split = measureEndOf(at) - at < l.du;
            if (l.split) {   // two halves, each half as long (MuseScore: baseLen shifted by one)
                tx.set(tuplet, F::DURATION, l.du * Fraction(1, 2));
                tx.set(tuplet, F::BASE_DURATION, l.base * Fraction(1, 2));
            }
            tx.set(tuplet, F::TIME, at);
            tx.set(tuplet, F::TUPLET, parentLevel ? Value(parentLevel->current) : Value());
            std::vector<Id> elements;
            for (Id c : m_ops.obj(tr.voice)->children()) {
                if (c != tuplet && m_ops.obj(c)->idOr(F::TUPLET) == tuplet && !m_ops.obj(c)->has(F::GRACE)) {
                    elements.push_back(c);
                }
            }
            for (const Id e : elements) {
                if (atItsBarline(l)) {
                    secondOf(l);
                }
                if (m_ops.obj(e)->kind() == Kind::Tuplet) {
                    writeTuplet(e, &l);
                    continue;
                }
                Pieces pw = startPieces(e, tr.voice);
                Fraction left = m_ops.endOf(e) - m_ops.timeOf(e);
                while (Fraction() < left) {
                    const Fraction remains = measureEndOf(at) - at;
                    if (atItsBarline(l)) {
                        secondOf(l);   // the second half, from the barline
                    }
                    const Fraction factor = m_ops.tupletFactor(l.current);   // written → actual
                    const Fraction actual = std::min(remains, left);
                    const auto values = durationList(actual / factor, 0);
                    if (values.empty()) {
                        break;
                    }
                    for (const NoteValue& v : values) {
                        addPiece(tx, pw, at, v.length() * factor, &v, false, l.current);
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
                writeTuplet(it.id, nullptr);
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
                const Fraction mLen = newMeasure(at).second;
                const Fraction remains = measureEndOf(at) - at;
                if (it.rest && (!(left < remains) || lastItem) && remains == mLen) {
                    // a measure rest (voice 1 only)
                    if (tr.number == 0) {
                        addPiece(tx, pw, at, mLen, nullptr, true, Id());
                    } else if (pw.first && it.id.isValid()) {
                        m_ops.removeEvent(tx, it.id);
                        pw.first = false;
                        pw.gone = true;
                    }
                    at += mLen;
                    left = left < mLen ? Fraction() : left - mLen;
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
    // grace notes after the chord come after its last piece (those before it stay before the first)
    if (const std::vector<Id> after = gracesOf(p.source).second; !after.empty() && m_ops.obj(p.prev)) {
        for (Id g : after) {
            tx.set(g, F::TIME, m_ops.timeOf(p.prev));
        }
        moveGraces(tx, { {}, after }, m_ops.obj(p.prev)->parent(), p.prev);
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

// ---- splitting and joining measures ---------------------------------------------------------------------------
int Commands::measureRepeatCount(Id measure) const
{
    // MuseScore's measureRepeatCount (the place of the measure in a measure repeat group, per staff; kept with the
    // measure's other per-staff properties as written): the largest over the staves
    int count = 0;
    const Fraction a = m_ops.measureStart(measure);
    for (Id c : m_ops.obj(flowOf())->children()) {
        const Object* o = m_ops.obj(c);
        if (o->kind() == Kind::FlowItem && o->text(F::SUBKIND) == "measureDetail" && m_ops.timeOf(c) == a) {
            const std::string& d = o->text(F::DETAILS);
            const size_t at = d.find("<measureRepeatCount>");
            if (at != std::string::npos) {
                count = std::max(count, std::atoi(d.c_str() + at + std::string("<measureRepeatCount>").size()));
            }
        }
    }
    return count;
}

bool Commands::canSplitAt(const Fraction& tick) const
{
    // MuseScore splits at a chord-rest segment: a chord or rest, or something attached there (a chord symbol…)
    static const std::set<std::string> attached { "harmony", "dynamic", "staffText", "systemText", "rehearsalMark", "tempo",
                                                  "instrumentChange", "fermata", "expression", "playTechnique" };
    for (Id staff : allStaves()) {
        for (Id v : m_ops.obj(staff)->children()) {
            if (m_ops.obj(v)->kind() != Kind::Voice) {
                continue;
            }
            for (Id c : m_ops.obj(v)->children()) {
                const Object* o = m_ops.obj(c);
                if ((o->kind() == Kind::Chord || o->kind() == Kind::Rest) && !o->has(F::GRACE) && m_ops.timeOf(c) == tick) {
                    return true;
                }
            }
        }
    }
    for (Id c : timeItemsAt(tick)) {
        if (attached.count(m_ops.obj(c)->text(F::SUBKIND))) {
            return true;
        }
    }
    return false;
}

std::vector<Id> Commands::timeItemsAt(const Fraction& t) const
{
    std::vector<Id> out;
    const std::vector<Id>& kids = m_ops.obj(flowOf())->children().ids();
    auto it = std::lower_bound(kids.begin(), kids.end(), t, [this](Id x, const Fraction& time) { return m_ops.timeOf(x) < time; });
    for (; it != kids.end() && m_ops.timeOf(*it) == t; ++it) {
        out.push_back(*it);
    }
    return out;
}

bool Commands::localTimeSignatures(const Fraction& t0, const Fraction& t1) const
{
    for (Id m : m_ops.measures()) {
        const Fraction at = m_ops.measureStart(m);
        if (at < t0 || !(at < t1)) {
            continue;
        }
        for (Id staff : allStaves()) {
            for (Id v : m_ops.obj(staff)->children()) {
                if (m_ops.obj(v)->kind() == Kind::Voice) {
                    if (!(m_ops.stretchAt(v, at) == Fraction(1, 1))) {
                        return true;
                    }
                    break;
                }
            }
        }
    }
    return false;
}

bool Commands::splitMeasure(Transaction& tx, const Fraction& tick)
{
    const std::vector<Id> ms = m_ops.measures();
    const Id m = m_ops.measureAt(tick);
    const auto pos = std::find(ms.begin(), ms.end(), m);
    if (!m.isValid() || pos == ms.end()) {
        return false;
    }
    const Fraction start = m_ops.measureStart(m), end = m_ops.measureEnd(m);
    if (!(start < tick) || !(tick < end)) {
        return false;   // MuseScore: "cannot split measure here: first beat of measure"
    }
    if (!canSplitAt(tick) || measureRepeatCount(m) > 0 || localTimeSignatures(start, end)) {
        return false;   // "cannot split measure repeat"; staves with their own time signature: not yet
    }
    // no tuplet is cut ("cannot split measure here: cannot split tuplet")
    for (Id staff : allStaves()) {
        for (Id v : m_ops.obj(staff)->children()) {
            if (m_ops.obj(v)->kind() != Kind::Voice) {
                continue;
            }
            for (Id c : m_ops.obj(v)->children()) {
                const Object* o = m_ops.obj(c);
                const Fraction t = m_ops.timeOf(c);
                if (o->kind() == Kind::Tuplet && t < tick && tick < m_ops.endOf(c)) {
                    return false;
                }
            }
        }
    }
    // the two lengths, no finer than MuseScore allows ("cannot split measure here: too short")
    const Fraction first = tick - start, second = end - tick;
    if (first.denominator() > 128 || second.denominator() > 128) {
        return false;
    }
    const Object* mo = m_ops.obj(m);
    const size_t index = size_t(pos - ms.begin());
    rewriteRun(tx, ms, index, index, int(mo->intOr(F::TIMESIG_NUM, 4)), int(mo->intOr(F::TIMESIG_DEN, 4)), { first, second });
    return true;
}

bool Commands::joinMeasures(Transaction& tx, size_t first, size_t last)
{
    const std::vector<Id> ms = m_ops.measures();
    if (!(first < last) || last >= ms.size()) {
        return false;
    }
    // a measure repeat group may not be cut by the join (MuseScore: "cannot split measure repeat")
    if (measureRepeatCount(ms[first]) > 1 || (last + 1 < ms.size() && measureRepeatCount(ms[last + 1]) > 1)) {
        return false;
    }
    const Fraction t0 = m_ops.measureStart(ms[first]), t1 = m_ops.measureEnd(ms[last]);
    if (localTimeSignatures(t0, t1)) {
        return false;   // staves with their own time signature: not yet (MuseScore keeps their stretch)
    }
    const Object* m1 = m_ops.obj(ms[first]);
    const int num = int(m1->intOr(F::TIMESIG_NUM, 4)), den = int(m1->intOr(F::TIMESIG_DEN, 4));
    // the time signatures inside go; the last one of each staff moves to the next measure if it has none
    std::map<Id, Id> lastSig;   // staff (or none: every staff) → its last time signature inside
    std::vector<Id> inside;
    for (Id c : m_ops.obj(flowOf())->children()) {
        const Object* o = m_ops.obj(c);
        const Fraction t = m_ops.timeOf(c);
        if (isTimeSig(o) && t0 < t && t < t1) {
            inside.push_back(c);
            lastSig[o->idOr(F::ANCHOR)] = c;
        }
    }
    std::vector<std::vector<std::pair<Field, Value> > > moves;
    if (last + 1 < ms.size()) {
        const std::vector<Id> atNext = timeSigsAt(t1);
        for (const auto& [staff, sig] : lastSig) {
            bool there = false;
            for (Id n : atNext) {
                there = there || m_ops.obj(n)->idOr(F::ANCHOR) == staff;
            }
            if (!there) {
                std::vector<std::pair<Field, Value> > f;
                m_ops.obj(sig)->forEachField([&f](Field fl, const Value& val) {
                    if (fl != F::KIND && fl != F::PARENT && fl != F::CHILDREN && fl != F::ORIGIN_EID && fl != F::SEQ
                        && fl != F::AT_END) {
                        f.push_back({ fl, val });
                    }
                });
                f.push_back({ F::TIME, t1 });
                moves.push_back(std::move(f));
            }
        }
    }
    std::vector<Fraction> lens { t1 - t0 };
    rewriteRun(tx, ms, first, last, num, den, lens);
    for (Id c : inside) {
        if (m_ops.obj(c)) {
            removeTree(tx, c);
        }
    }
    for (auto& f : moves) {
        create(tx, m_ops.m_ids, flowOf(), { Kind::FlowItem, f }, Placement::ByTime);
    }
    return true;
}

// ---- explode and implode --------------------------------------------------------------------------------------
Id Commands::voiceNumbered(Id staff, int number) const
{
    for (Id v : m_ops.obj(staff)->children()) {
        if (m_ops.obj(v)->kind() == Kind::Voice && m_ops.obj(v)->intOr(F::NUMBER, 0) == number) {
            return v;
        }
    }
    return Id();
}

std::vector<Id> Commands::eventsIn(Id voice, const Fraction& t0, const Fraction& t1) const
{
    std::vector<Id> out;
    if (!voice.isValid()) {
        return out;
    }
    for (Id e : m_ops.eventsOf(voice)) {
        const Fraction t = m_ops.timeOf(e);
        if (t0 <= t && t < t1) {
            out.push_back(e);
        }
    }
    return out;
}

bool Commands::sameStretch(const std::vector<Id>& staves, const Fraction& t0, const Fraction& t1) const
{
    // MuseScore: "cannot explode or implode between staves with different local time signatures"
    for (Id m : m_ops.measures()) {
        const Fraction at = m_ops.measureStart(m);
        if (m_ops.measureEnd(m) <= t0 || !(at < t1)) {
            continue;
        }
        Fraction first(-1, 1);
        for (Id staff : staves) {
            const Id v = voiceNumbered(staff, 0);
            const Fraction st = v.isValid() ? m_ops.stretchAt(v, at) : Fraction(1, 1);
            if (first < Fraction()) {
                first = st;
            } else if (!(st == first)) {
                return false;
            }
        }
    }
    return true;
}

bool Commands::linkedTogether(const std::vector<Id>& staves) const
{
    // two staves that show the same music (a guitar's standard staff and its tablature): exploding or imploding
    // between them would take notes out of both (MuseScore's result is damaged); refused
    std::set<Id> mains;
    for (Id s : staves) {
        const Id main = m_ops.obj(s)->idOr(F::LINKED_STAFF);
        if (!mains.insert(main.isValid() ? main : s).second) {
            return true;
        }
    }
    return false;
}

void Commands::cloneVoice(Transaction& tx, Id fromStaff, int fromVoice, Id toStaff, int toVoice, const Fraction& t0,
                          const Fraction& t1, bool move)
{
    Clipboard clip(m_ops.m, m_ops.m_ids);
    const Clip voice = Clipboard::voiceOf(clip.copy({ { fromStaff }, t0, t1 }, true), fromVoice, toVoice);
    clip.paste(tx, voice, toStaff, t0, true);
    if (!move) {
        return;
    }
    // the source voice gives its music up (voice 1: rests), with its slurs, hairpins and annotations
    const Id v = voiceNumbered(fromStaff, fromVoice);
    if (!v.isValid() || (fromStaff == toStaff && fromVoice == toVoice)) {
        return;
    }
    m_ops.clearRange(tx, v, t0, t1);
    if (fromVoice == 0) {
        m_ops.fillGap(tx, v, t0, t1);
    }
    static const std::set<std::string> lines { "slur", "hammerOnPullOff", "hairPin", "hairpin" };
    std::vector<Id> gone;
    for (Id c : m_ops.obj(m_ops.m.score())->children()) {
        const Object* o = m_ops.obj(c);
        if (o->kind() == Kind::Spanner && lines.count(o->text(F::SUBKIND)) && o->idOr(F::ANCHOR) == fromStaff
            && o->intOr(F::VOICE_INDEX, 0) == fromVoice && t0 <= m_ops.timeOf(c) && m_ops.timeOf(c) < t1) {
            gone.push_back(c);
        }
    }
    static const std::set<std::string> notItems { "clef", "keySig", "timeSig", "breath", "frame", "measureDetail", "mmrMeasure",
                                                  "detail" };
    for (Id c : m_ops.obj(flowOf())->children()) {
        const Object* o = m_ops.obj(c);
        if (o->kind() == Kind::FlowItem && !notItems.count(o->text(F::SUBKIND)) && o->idOr(F::ANCHOR) == fromStaff
            && o->intOr(F::VOICE_INDEX, 0) == fromVoice && t0 <= m_ops.timeOf(c) && m_ops.timeOf(c) < t1) {
            gone.push_back(c);
        }
    }
    for (Id c : gone) {
        removeTree(tx, c);
    }
}

int Commands::explode(Transaction& tx, size_t first, size_t last, const Fraction& from, const Fraction& to)
{
    const std::vector<Id> staves = allStaves();
    const std::vector<Id> ms = m_ops.measures();
    if (first >= staves.size() || last <= first || ms.empty()) {
        return -1;
    }
    last = std::min(last, staves.size());
    const Id src = staves[first];
    // whole measures: from the measure of the start to the measure the range ends in
    const Id startMeasure = m_ops.measureAt(from);
    const Fraction t0 = m_ops.measureStart(startMeasure);
    Id endMeasure = m_ops.measureAt(to);
    if (!(m_ops.measureEnd(ms.back()) > to)) {
        endMeasure = ms.back();
    } else if (m_ops.measureStart(endMeasure) == to && to > t0) {
        endMeasure = m_ops.measureAt(to - Fraction(1, 1024));
    }
    const Fraction t1 = m_ops.measureEnd(endMeasure);
    // voices 2–4 with music in the range: the voices go to the staves below; otherwise the chords' notes do
    bool voices = false;
    for (int n = 1; n < 4 && !voices; ++n) {
        voices = !eventsIn(voiceNumbered(src, n), t0, t1).empty();
    }
    if (!voices) {
        if (first + 1 == last) {   // one staff chosen: as many staves as the largest chord has notes
            size_t notes = 0;
            for (Id e : eventsIn(voiceNumbered(src, 0), t0, t1)) {
                size_t k = 0;
                for (Id x : m_ops.obj(e)->children()) {
                    k += m_ops.obj(x)->kind() == Kind::Note;
                }
                notes = std::max(notes, k);
            }
            last = std::min(staves.size(), first + notes);
        }
        if (last <= first || !sameStretch(std::vector<Id>(staves.begin() + long(first), staves.begin() + long(last)), t0, t1)
            || linkedTogether(std::vector<Id>(staves.begin() + long(first), staves.begin() + long(last)))) {
            return last <= first ? int(first + 1) : -1;
        }
        // the staff's music to every staff below it, then each keeps one note of every chord (the top one for the
        // first staff, the next one down for the next…; reused from the bottom when there are more staves)
        Clipboard clip(m_ops.m, m_ops.m_ids);
        const Clip copied = clip.copy({ { src }, t0, t1 }, true);   // its rests as written, as MuseScore copies them
        for (size_t i = first + 1; i < last; ++i) {
            clip.paste(tx, copied, staves[i], t0, true);   // the staff's other voices stay, as in MuseScore
        }
        for (size_t i = 0; first + i < last; ++i) {
            for (Id e : eventsIn(voiceNumbered(staves[first + i], 0), t0, t1)) {
                const Object* c = m_ops.obj(e);
                if (c->kind() != Kind::Chord) {
                    continue;
                }
                std::vector<Id> notes;
                for (Id x : c->children()) {
                    if (m_ops.obj(x)->kind() == Kind::Note) {
                        notes.push_back(x);
                    }
                }
                std::sort(notes.begin(), notes.end(), [this](Id a, Id b) {
                    return m_ops.obj(a)->intOr(F::PITCH, 0) < m_ops.obj(b)->intOr(F::PITCH, 0);
                });
                if (notes.empty()) {
                    continue;
                }
                const size_t perNote = std::max((last - first) / notes.size(), size_t(1));
                const size_t keep = size_t(std::max(int(notes.size()) - 1 - int(i / perNote), 0));
                for (size_t k = 0; k < notes.size(); ++k) {
                    if (k != keep) {
                        m_ops.removeNote(tx, notes[k]);
                    }
                }
            }
        }
        return int(last);
    }
    // the voices: each voice with music goes to the first voice below that is free (empty or only rests) from there
    // to the end of the range: the next staff's voice 1 for the second, and so on (MuseScore's cmdExplode)
    if (first + 1 == last) {
        last = std::min(staves.size(), first + 4);
    }
    auto onlyRests = [this](Id voice, const Fraction& a, const Fraction& b) {
        for (Id e : eventsIn(voice, a, b)) {
            if (m_ops.obj(e)->kind() != Kind::Rest) {
                return false;
            }
        }
        return true;
    };
    struct Move {
        int voice;
        size_t staff;
        int toVoice;
        Fraction from;   // the measure its music starts in: the destination is only checked, and written, from there
    };
    std::vector<Move> moves;
    // the voices in the order their music first appears
    std::vector<std::pair<Fraction, int> > firstMusic;
    for (int n = 0; n < 4; ++n) {
        for (Id e : eventsIn(voiceNumbered(src, n), t0, t1)) {
            if (m_ops.obj(e)->kind() != Kind::Rest) {
                firstMusic.push_back({ m_ops.measureStart(m_ops.measureAt(m_ops.timeOf(e))), n });
                break;
            }
        }
    }
    std::stable_sort(firstMusic.begin(), firstMusic.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (size_t k = 0; k < firstMusic.size() && k < 4; ++k) {
        const auto [measureStart, n] = firstMusic[k];
        bool found = false;
        for (size_t staff = first + k; staff < last && !found; ++staff) {
            for (int dv = (staff == first + k ? 0 : 0); dv < 4 && !found; ++dv) {
                if (staff == first && dv == n) {
                    moves.push_back({ n, staff, dv, measureStart });   // already there
                    found = true;
                } else if (onlyRests(voiceNumbered(staves[staff], dv), measureStart, t1)) {
                    moves.push_back({ n, staff, dv, measureStart });
                    found = true;
                }
            }
        }
    }
    if (moves.empty()) {
        return -1;
    }
    size_t newLast = first + 1;
    for (const Move& mv : moves) {
        newLast = std::max(newLast, mv.staff + 1);
    }
    if (!sameStretch(std::vector<Id>(staves.begin() + long(first), staves.begin() + long(newLast)), t0, t1)
        || linkedTogether(std::vector<Id>(staves.begin() + long(first), staves.begin() + long(newLast)))) {
        return -1;
    }
    for (const Move& mv : moves) {
        if (!(staves[mv.staff] == src && mv.toVoice == mv.voice)) {
            // from the measure the voice's music starts in: the destination's music before it stays (MuseScore)
            cloneVoice(tx, src, mv.voice, staves[mv.staff], mv.toVoice, mv.from, t1, true);
            // before that measure the voice held only rests: they go with it
            const Id v = voiceNumbered(src, mv.voice);
            if (t0 < mv.from && v.isValid()) {
                m_ops.clearRange(tx, v, t0, mv.from);
                if (mv.voice == 0) {
                    m_ops.fillGap(tx, v, t0, mv.from);
                }
            }
        }
    }
    return int(newLast);
}

bool Commands::implode(Transaction& tx, size_t first, size_t last, const Fraction& t0, const Fraction& t1)
{
    const std::vector<Id> staves = allStaves();
    if (first >= staves.size() || last <= first) {
        return false;
    }
    last = std::min(last, staves.size());
    if (!sameStretch(std::vector<Id>(staves.begin() + long(first), staves.begin() + long(last)), t0, t1)
        || linkedTogether(std::vector<Id>(staves.begin() + long(first), staves.begin() + long(last)))) {
        return false;
    }
    const Id dst = staves[first];
    if (first + 1 == last) {
        // one staff: its voices 2–4 join voice 1 where a chord there has the same length (notes added, articulations
        // moved, ties continued); what joined goes, as do the rests of voices 2–4 where voice 1 has a rest
        const Id v0 = voiceNumbered(dst, 0);
        // a chord or rest goes with its grace notes (MuseScore removes the chord, which holds them)
        auto removeWithGraces = [&](Id event) {
            const auto graces = gracesOf(event);
            for (const auto* list : { &graces.first, &graces.second }) {
                for (Id g : *list) {
                    m_ops.removeEvent(tx, g);
                }
            }
            m_ops.removeEvent(tx, event);
        };
        for (Id e : eventsIn(v0, t0, t1)) {
            const Object* d = m_ops.obj(e);
            if (!d || d->has(F::GRACE)) {
                continue;   // the chords and rests of the measure (grace notes belong to their chord)
            }
            const Fraction t = m_ops.timeOf(e);
            if (d->kind() == Kind::Chord) {
                // the chord this one is tied from, if any
                Id tied;
                for (Id x : d->children()) {
                    if (m_ops.obj(x)->kind() == Kind::Note && !tied.isValid()) {
                        for (Id p : eventsIn(v0, Fraction(), t)) {
                            for (Id pn : m_ops.obj(p)->children()) {
                                if (m_ops.obj(pn)->idOr(F::TIE_TO) == x) {
                                    tied = p;
                                }
                            }
                        }
                    }
                }
                for (int n = 1; n < 4; ++n) {
                    for (Id se : eventsIn(voiceNumbered(dst, n), t, t + Fraction(1, 1000000))) {
                        const Object* src = m_ops.obj(se);
                        if (!src || m_ops.timeOf(se) != t || src->has(F::GRACE)) {
                            continue;
                        }
                        if (src->kind() == Kind::Chord) {
                            if (!(m_ops.endOf(se) - t == m_ops.endOf(e) - t)) {
                                continue;   // not the same length: kept
                            }
                            for (Id x : std::vector<Id>(src->children().ids())) {
                                const Object* note = m_ops.obj(x);
                                if (note->kind() != Kind::Note) {
                                    continue;
                                }
                                bool has = false;
                                for (Id y : m_ops.obj(e)->children()) {
                                    has = has || (m_ops.obj(y)->kind() == Kind::Note && m_ops.obj(y)->intOr(F::PITCH, -1) == note->intOr(F::PITCH, -2));
                                }
                                if (has) {
                                    continue;
                                }
                                const int pitch = int(note->intOr(F::PITCH, 60));
                                const int64_t tpc = note->intOr(F::TPC, -1);
                                const Id nn = m_ops.addNote(tx, e, pitch);
                                if (tpc >= 0) {
                                    m_ops.setPitch(tx, nn, pitch, tpc);
                                }
                                if (tied.isValid()) {
                                    for (Id tn : m_ops.obj(tied)->children()) {
                                        const Object* tno = m_ops.obj(tn);
                                        if (tno->kind() == Kind::Note && tno->intOr(F::PITCH, -1) == pitch
                                            && tno->intOr(F::TPC, -1) == m_ops.obj(nn)->intOr(F::TPC, -2) && !tno->has(F::TIE_TO)) {
                                            tx.set(tn, F::TIE_TO, nn);
                                        }
                                    }
                                }
                            }
                            // its articulations the chord doesn't have
                            for (Id a : std::vector<Id>(m_ops.obj(se)->children().ids())) {
                                const Object* at = m_ops.obj(a);
                                if (at->kind() != Kind::Attachment || model::articulationKind(at->text(F::SUBKIND)).empty()
                                    || at->text(F::SUBKIND) == "lyric") {
                                    continue;
                                }
                                bool has = false;
                                for (Id y : m_ops.obj(e)->children()) {
                                    has = has || (m_ops.obj(y)->kind() == Kind::Attachment
                                                  && model::articulationKind(m_ops.obj(y)->text(F::SUBKIND)) == model::articulationKind(at->text(F::SUBKIND)));
                                }
                                if (!has) {
                                    moveTo(tx, a, e, Placement::Append);
                                }
                            }
                        }
                        removeWithGraces(se);
                    }
                }
            } else {
                for (int n = 1; n < 4; ++n) {
                    for (Id se : eventsIn(voiceNumbered(dst, n), t, t + Fraction(1, 1000000))) {
                        if (m_ops.obj(se)->kind() == Kind::Rest && m_ops.timeOf(se) == t) {
                            m_ops.removeEvent(tx, se);
                        }
                    }
                }
            }
        }
        return true;
    }
    // several staves: the first four voices with music (in staff order) become the first staff's voices
    std::vector<std::pair<Id, int> > sources;
    for (size_t s = first; s < last && sources.size() < 4; ++s) {
        for (int n = 0; n < 4 && sources.size() < 4; ++n) {
            bool music = false;
            for (Id e : eventsIn(voiceNumbered(staves[s], n), t0, t1)) {
                music = music || m_ops.obj(e)->kind() != Kind::Rest;
            }
            if (music) {
                sources.push_back({ staves[s], n });
            }
        }
    }
    for (size_t i = 0; i < sources.size(); ++i) {
        if (!(sources[i].first == dst && sources[i].second == int(i))) {
            cloneVoice(tx, sources[i].first, sources[i].second, dst, int(i), t0, t1, false);
        }
    }
    return true;
}

// ---- slashes --------------------------------------------------------------------------------------------------
namespace {
//! The elements directly inside a details fragment (<x>…</x>, io/details.h): name and [begin, end) in the text
struct DetailChild {
    std::string name;
    size_t begin, end;
};

//! Finds the fragment's root content and its children; false if the text isn't a fragment
bool detailChildren(const std::string& d, size_t& contentBegin, size_t& contentEnd, std::vector<DetailChild>& out)
{
    size_t i = d.find('<');
    while (i != std::string::npos && (d.compare(i, 2, "<?") == 0 || d.compare(i, 4, "<!--") == 0)) {
        i = d.compare(i, 2, "<?") == 0 ? d.find("?>", i) : d.find("-->", i);
        i = i == std::string::npos ? i : d.find('<', i);
    }
    if (i == std::string::npos) {
        return false;
    }
    const size_t rootEnd = d.find('>', i);
    if (rootEnd == std::string::npos || d[rootEnd - 1] == '/') {
        return false;
    }
    size_t n = i + 1;
    while (n < rootEnd && d[n] != ' ' && d[n] != '>' && d[n] != '\t' && d[n] != '\n') {
        ++n;
    }
    const std::string root = d.substr(i + 1, n - i - 1);
    contentBegin = rootEnd + 1;
    contentEnd = d.rfind("</" + root);
    if (contentEnd == std::string::npos || contentEnd < contentBegin) {
        return false;
    }
    int depth = 0;
    for (size_t pos = d.find('<', contentBegin); pos != std::string::npos && pos < contentEnd; pos = d.find('<', pos)) {
        if (d.compare(pos, 4, "<!--") == 0) {
            const size_t e = d.find("-->", pos);
            pos = e == std::string::npos ? contentEnd : e + 3;
            continue;
        }
        const size_t e = d.find('>', pos);
        if (e == std::string::npos || e > contentEnd) {
            return false;
        }
        if (d[pos + 1] == '/') {
            if (--depth == 0 && !out.empty()) {
                out.back().end = e + 1;
            }
        } else {
            const bool empty = d[e - 1] == '/';
            if (depth == 0) {
                size_t k = pos + 1;
                while (k < e && d[k] != ' ' && d[k] != '/' && d[k] != '\t' && d[k] != '\n') {
                    ++k;
                }
                out.push_back({ d.substr(pos + 1, k - pos - 1), pos, empty ? e + 1 : std::string::npos });
            }
            if (!empty) {
                ++depth;
            }
        }
        pos = e + 1;
    }
    return depth == 0;
}

//! The text of a child element (empty if there is none)
std::string detailValue(const std::string& d, const std::string& tag)
{
    size_t b = 0, e = 0;
    std::vector<DetailChild> kids;
    if (!detailChildren(d, b, e, kids)) {
        return std::string();
    }
    for (const DetailChild& k : kids) {
        if (k.name == tag && k.end != std::string::npos) {
            const size_t open = d.find('>', k.begin) + 1;
            const size_t close = d.rfind("</", k.end);
            return close > open ? d.substr(open, close - open) : std::string();
        }
    }
    return std::string();
}

//! The fragment with these child elements set (an empty value: removed); empty when no child is left
std::string withDetailValues(std::string d, const std::vector<std::pair<std::string, std::string> >& values)
{
    size_t b = 0, e = 0;
    std::vector<DetailChild> kids;
    if (d.empty() || !detailChildren(d, b, e, kids)) {
        d = "<x>\n</x>\n";
        kids.clear();
        detailChildren(d, b, e, kids);
    }
    std::set<std::string> names;
    for (const auto& [name, value] : values) {
        names.insert(name);
    }
    for (auto it = kids.rbegin(); it != kids.rend(); ++it) {
        if (names.count(it->name) && it->end != std::string::npos) {
            size_t from = it->begin;
            while (from > b && (d[from - 1] == ' ' || d[from - 1] == '\t')) {
                --from;   // with its indentation
            }
            size_t to = it->end;
            if (to < d.size() && d[to] == '\n') {
                ++to;
            }
            d.erase(from, to - from);
            e -= to - from;
        }
    }
    std::string added;
    for (const auto& [name, value] : values) {
        if (!value.empty()) {
            added += "<" + name + ">" + value + "</" + name + ">\n";
        }
    }
    d.insert(e, added);
    size_t b2 = 0, e2 = 0;
    std::vector<DetailChild> left;
    return detailChildren(d, b2, e2, left) && left.empty() ? std::string() : d;
}

//! MuseScore's ClefInfo pitch offsets (the diatonic step of the top line) by clef name; G when unknown
int clefPitchOffset(const std::string& clef)
{
    static const std::map<std::string, int> offsets {
        { "G", 45 }, { "G15mb", 31 }, { "G8vb", 38 }, { "G8va", 52 }, { "G15ma", 59 }, { "G8vbo", 38 }, { "G8vbp", 45 },
        { "G1", 47 }, { "C1", 43 }, { "C2", 41 }, { "C3", 39 }, { "C4", 37 }, { "C5", 35 }, { "C_19C", 45 }, { "C1_F18C", 43 },
        { "C3_F18C", 39 }, { "C4_F18C", 37 }, { "C1_F20C", 43 }, { "C3_F20C", 39 }, { "C4_F20C", 37 }, { "F", 33 },
        { "F15mb", 19 }, { "F8vb", 26 }, { "F8va", 40 }, { "F15ma", 47 }, { "F3", 35 }, { "F5", 31 }, { "F_F18C", 33 },
        { "F_19C", 33 }, { "PERC", 45 }, { "PERC2", 45 }, { "TAB", 45 }, { "TAB4", 45 }, { "TAB2", 45 }, { "TAB4_SERIF", 45 },
        { "C4_8VB", 30 }, { "G8vbc", 38 },
    };
    auto it = offsets.find(clef);
    return it == offsets.end() ? 45 : it->second;
}

//! The first value of a tag anywhere in a fragment (a staff type's <lines>…)
std::string anyValue(const std::string& d, const std::string& tag)
{
    const std::string open = "<" + tag + ">";
    const size_t a = d.find(open);
    if (a == std::string::npos) {
        return std::string();
    }
    const size_t b = d.find("</" + tag + ">", a);
    return b == std::string::npos ? std::string() : d.substr(a + open.size(), b - a - open.size());
}
}

void Commands::setDetailValues(Transaction& tx, Id id, const std::vector<std::pair<std::string, std::string> >& values, Field addTo)
{
    // a value is taken out of both fragments (before and after an element's notes or pitch, as read) and added
    // to `addTo`, where MuseScore writes it
    const Object* o = m_ops.obj(id);
    const bool keyed = o->has(F::DETAILS_KEY);
    const bool stale = keyed && o->intOr(F::DETAILS_KEY, 0) != model::detailsKey(*o);
    std::vector<std::pair<std::string, std::string> > removed;
    for (const auto& [name, value] : values) {
        removed.push_back({ name, std::string() });
    }
    bool changed = false;
    for (Field f : { F::DETAILS, F::DETAILS_AFTER }) {
        const std::string before = m_ops.obj(id)->text(f);
        // already out of date: the writer would leave them out
        const std::string nd = withDetailValues(stale ? std::string() : before, f == addTo ? values : removed);
        if (nd != before) {
            tx.set(id, f, nd.empty() ? Value() : Value(Text(nd)));
            changed = true;
        }
    }
    if (!changed && !stale) {
        return;
    }
    const Object* now = m_ops.obj(id);
    const bool any = now->has(F::DETAILS) || now->has(F::DETAILS_AFTER) || now->has(F::DETAILS_2);
    // the details are kept in step with the music they were set for (D-E27); a new element has no fingerprint, so
    // its details stay with it through later edits
    tx.set(id, F::DETAILS_KEY, any && keyed ? Value(model::detailsKey(*now)) : Value());
}

std::string Commands::detailOf(Id id, const std::string& tag) const
{
    const Object* o = m_ops.obj(id);
    if (o->has(F::DETAILS_KEY) && o->intOr(F::DETAILS_KEY, 0) != model::detailsKey(*o)) {
        return std::string();
    }
    const std::string v = detailValue(o->text(F::DETAILS), tag);
    return v.empty() ? detailValue(o->text(F::DETAILS_AFTER), tag) : v;
}

std::vector<Id> Commands::notesOf(Id chord) const
{
    std::vector<Id> notes;
    for (Id n : m_ops.obj(chord)->children()) {
        if (m_ops.obj(n)->kind() == Kind::Note) {
            notes.push_back(n);
        }
    }
    // lowest first, as MuseScore keeps a chord's notes
    std::stable_sort(notes.begin(), notes.end(), [this](Id a, Id b) { return m_ops.obj(a)->intOr(F::PITCH, 0) < m_ops.obj(b)->intOr(F::PITCH, 0); });
    return notes;
}

bool Commands::isSlash(Id chord) const
{
    // Chord::slash: the top note is fixed to a line
    const std::vector<Id> notes = notesOf(chord);
    return !notes.empty() && detailOf(notes.back(), "fixed") == "1";
}

Commands::StaffLines Commands::staffLines(Id staff) const
{
    StaffLines l;
    const std::string& d = m_ops.obj(staff)->text(F::DETAILS);
    const std::string lines = anyValue(d, "lines"), step = anyValue(d, "stepOffset");
    if (!lines.empty()) {
        l.lines = std::atoi(lines.c_str());
    }
    if (!step.empty()) {
        l.stepOffset = std::atoi(step.c_str());
    }
    l.upsideDown = anyValue(d, "upsideDown") == "1";
    return l;
}

std::string Commands::clefAt(Id staff, const Fraction& t, bool concertPitch) const
{
    // the last clef of the staff at or before t; otherwise the staff's default clef
    const Object* found = nullptr;
    for (Id c : m_ops.obj(flowOf())->children()) {
        const Object* it = m_ops.obj(c);
        if (it->kind() == Kind::FlowItem && it->text(F::SUBKIND) == "clef" && it->idOr(F::ANCHOR) == staff) {
            if (m_ops.timeOf(c) > t) {
                break;
            }
            found = it;
        }
    }
    if (found) {
        const std::string transposing = concertPitch ? std::string() : detailValue(found->text(F::DETAILS), "transposingClefType");
        return transposing.empty() ? found->text(F::TEXT) : transposing;
    }
    const std::string& d = m_ops.obj(staff)->text(F::DETAILS);
    for (const char* tag : { concertPitch ? "defaultConcertClef" : "defaultTransposingClef", "defaultClef" }) {
        const std::string v = detailValue(d, tag);
        if (!v.empty()) {
            return v;
        }
    }
    // (MuseScore's Staff::clefType: the staff's own default, not its instrument's)
    const std::string group = m_ops.obj(staff)->text(F::SUBKIND);
    return group == "percussion" ? "PERC" : (group == "tablature" ? "TAB" : "G");
}

void Commands::setSlash(Transaction& tx, Id chord, bool on, bool stemless)
{
    // Chord::setSlash
    const Object* c = m_ops.obj(chord);
    const Id staff = staffOfEvent(chord);
    const int voice = int(m_ops.obj(c->parent())->intOr(F::NUMBER, 0));
    const bool drum = m_ops.obj(staff)->text(F::SUBKIND) == "percussion";
    const std::vector<Id> notes = notesOf(chord);
    if (!on) {
        // restored (MuseScore leaves the stem direction and beam mode as they are)
        setDetailValues(tx, chord, { { "noStem", "" }, { "small", "" }, { "offset", "" } });
        for (Id n : notes) {
            setDetailValues(tx, n, { { "head", "" }, { "fixed", "" }, { "fixedLine", "" }, { "play", "" }, { "visible", "" } }, F::DETAILS_AFTER);
        }
        return;
    }
    const StaffLines sl = staffLines(staff);
    const int middleLine = sl.lines - 1 - sl.stepOffset, bottomLine = (sl.lines - 1) * 2;
    std::vector<std::pair<std::string, std::string> > chordValues;
    if (stemless) {
        chordValues.push_back({ "noStem", "1" });
        const Fraction base = c->fractionOr(F::BASE_DURATION, Fraction(1, 4));
        if (base < Fraction(1, 4)) {
            chordValues.push_back({ "BeamMode", "no" });   // only a value with a flag has a beam mode (ChordRest)
        }
    }
    int line = middleLine;
    std::string head = "slash";
    if (voice < 2) {
        chordValues.push_back({ "StemDirection", "down" });
    } else {
        chordValues.push_back({ "small", "1" });
        if (voice % 2) {
            line = bottomLine + 1;
            chordValues.push_back({ "StemDirection", "down" });
        } else {
            line = -1;
            chordValues.push_back({ "StemDirection", "up" });
        }
        if (drum) {
            head = std::string();   // a drum staff keeps normal heads there
        }
    }
    setDetailValues(tx, chord, chordValues);
    for (size_t i = 0; i < notes.size(); ++i) {
        // all but the first notehead hidden
        // MuseScore writes a note's visibility before its pitch, the rest after it
        setDetailValues(tx, notes[i], { { "visible", i ? "0" : "" } });
        setDetailValues(tx, notes[i], { { "head", head }, { "play", "0" }, { "fixed", "1" }, { "fixedLine", std::to_string(line) } },
                        F::DETAILS_AFTER);
    }
}

std::vector<Id> Commands::linkedStavesOf(Id staff) const
{
    const Object* s = m_ops.obj(staff);
    const Id main = s->idOr(F::LINKED_STAFF).isValid() ? s->idOr(F::LINKED_STAFF) : staff;
    std::vector<Id> out;
    for (Id other : allStaves()) {
        const Object* o = m_ops.obj(other);
        if (other != staff && (other == main || o->idOr(F::LINKED_STAFF) == main)) {
            out.push_back(other);
        }
    }
    return out;
}

Id Commands::eventAt(Id staff, int voice, const Fraction& t, model::Kind kind) const
{
    for (Id e : eventsIn(voiceNumbered(staff, voice), t, t + Fraction(1, 1920 * 4))) {
        const Object* o = m_ops.obj(e);
        if (o->kind() == kind && !o->has(F::GRACE) && m_ops.timeOf(e) == t) {
            return e;
        }
    }
    return Id();
}

bool Commands::insideTuplet(Id voice, const Fraction& t) const
{
    if (!voice.isValid()) {
        return false;
    }
    for (Id e : m_ops.eventsOf(voice)) {
        Id tuplet = m_ops.obj(e)->idOr(F::TUPLET);
        while (tuplet.isValid() && m_ops.obj(tuplet) && m_ops.obj(tuplet)->idOr(F::TUPLET).isValid()) {
            tuplet = m_ops.obj(tuplet)->idOr(F::TUPLET);   // the outermost
        }
        if (tuplet.isValid() && m_ops.timeOf(tuplet) < t && t < m_ops.endOf(tuplet)) {
            return true;
        }
    }
    return false;
}

Commands::SlashPitch Commands::slashPitch(Id staff, const Fraction& t, bool concertPitch) const
{
    // Score::noteValForPosition on the staff's middle line (accidentals: the key's, or an earlier note's on that line
    // in the measure, as MuseScore's AccidentalState gives them)
    SlashPitch sp;
    const Object* st = m_ops.obj(staff);
    const Object* player = m_ops.obj(st->parent());
    const std::string group = st->text(F::SUBKIND);
    const StaffLines sl = staffLines(staff);
    const std::string instrument = player ? player->text(F::DETAILS_2) : std::string();
    if (group == "percussion") {   // pitch 0, as MuseScore enters it there
        sp.pitch = 0;
        sp.tpc = model::defaultTpc(0, 0);
        return sp;
    }
    if (anyValue(instrument, "useDrumset") == "1") {
        // a drum set on a five-line staff: the lowest drum on the middle line (Drumset::defaultPitchForLine); none
        // there, no slash
        const std::string middle = std::to_string(sl.lines - 1 - sl.stepOffset);
        sp.ok = false;
        int lowest = 128;
        for (size_t at = instrument.find("<Drum pitch=\""); at != std::string::npos; at = instrument.find("<Drum pitch=\"", at + 1)) {
            const size_t end = instrument.find("</Drum>", at);
            const int pitch = std::atoi(instrument.c_str() + at + 13);   // after <Drum pitch="
            if (anyValue(instrument.substr(at, end == std::string::npos ? std::string::npos : end - at), "line") == middle && pitch < lowest) {
                lowest = pitch;
            }
        }
        if (lowest < 128) {
            sp.ok = true;
            sp.pitch = lowest;
            sp.tpc = model::defaultTpc(lowest, 0);
        }
        return sp;
    }
    const Operations::Transposition tr = m_ops.transpositionAt(staff, t);
    const int64_t chrom = tr.chromatic, dia = tr.diatonic;
    if (group == "tablature") {
        const StringTuning tuning = StringTuning::of(*player);
        if (tuning.empty()) {
            sp.ok = false;
            return sp;
        }
        int string = sl.lines / 2;
        if (sl.upsideDown) {
            string = int(tuning.strings.size()) - 1 - string;
        }
        if (string < 0 || string >= int(tuning.strings.size())) {
            sp.ok = false;
            return sp;
        }
        sp.pitch = tuning.pitchOf(string, 0, -int(chrom));
        sp.tpc = model::defaultTpc(sp.pitch, m_ops.keyAt(staff, t));
        sp.string = string;
        sp.fret = 0;
        return sp;
    }
    const int line = sl.lines - 1 - sl.stepOffset;
    const int step = std::max(clefPitchOffset(clefAt(staff, t, concertPitch)) - line, 0);
    const int letter = step % 7, octave = step / 7;
    const bool transposing = !concertPitch && (chrom != 0 || dia != 0);
    int64_t key = m_ops.keyAt(staff, t);
    if (transposing) {
        key -= 7 * chrom - 12 * dia;   // the written key
        while (key > 7) {
            key -= 12;
        }
        while (key < -7) {
            key += 12;
        }
    }
    int acc = model::keyAccidental(letter, key);
    // a custom key signature: its own accidentals, on degrees from the key's tonic (MuseScore's CustDef)
    if (const Id ks = m_ops.keySigAt(staff, t); ks.isValid() && detailValue(m_ops.obj(ks)->text(F::DETAILS), "custom") == "1") {
        const std::string& d = m_ops.obj(ks)->text(F::DETAILS);
        const int tonic = model::letterOfTpc(14 + key);
        acc = 0;
        static const std::map<std::string, int> syms { { "accidentalFlat", -1 }, { "accidentalSharp", 1 }, { "accidentalNatural", 0 },
                                                       { "accidentalDoubleFlat", -2 }, { "accidentalDoubleSharp", 2 } };
        for (size_t at = d.find("<sym>"); at != std::string::npos; at = d.find("<sym>", at + 1)) {
            const size_t end = d.find("</sym>", at), def = d.find("degree=\"", at);
            if (end == std::string::npos || def == std::string::npos) {
                break;
            }
            const auto sym = syms.find(d.substr(at + 5, end - at - 5));
            if (sym != syms.end() && (tonic + std::atoi(d.c_str() + def + 8)) % 7 == letter) {
                acc = sym->second;
            }
        }
    }
    // an earlier note on this line in the measure
    const Id measure = m_ops.measureAt(t);
    const Fraction m0 = m_ops.measureStart(measure);
    Fraction latest(-1, 1);
    for (Id v : st->children()) {
        if (m_ops.obj(v)->kind() != Kind::Voice) {
            continue;
        }
        for (Id e : eventsIn(v, m0, t)) {
            if (m_ops.obj(e)->kind() != Kind::Chord || m_ops.timeOf(e) < latest) {
                continue;
            }
            for (Id n : notesOf(e)) {
                const Object* no = m_ops.obj(n);
                const int64_t tpc = transposing && no->has(F::TPC2) ? no->intOr(F::TPC2, 14)
                                                                     : (transposing ? model::transposeTpc(no->intOr(F::TPC, 14), -dia, -chrom)
                                                                                    : no->intOr(F::TPC, 14));
                const int64_t written = no->intOr(F::PITCH, 60) - (transposing ? chrom : 0);
                const int a = model::accidentalOfTpc(tpc), l = model::letterOfTpc(tpc);
                static const int natural[7] = { 0, 2, 4, 5, 7, 9, 11 };
                const int64_t oct = (written - a - natural[l]) / 12;
                if (l == letter && oct == octave) {
                    acc = a;
                    latest = m_ops.timeOf(e);
                }
            }
        }
    }
    static const int natural[7] = { 0, 2, 4, 5, 7, 9, 11 };
    const int writtenPitch = natural[letter] + octave * 12 + acc;
    const int64_t writtenTpc = model::tpcOf(letter, acc);
    sp.pitch = std::clamp(writtenPitch + (transposing ? int(chrom) : 0), 0, 127);
    sp.tpc = transposing ? model::transposeTpc(writtenTpc, dia, chrom) : writtenTpc;
    return sp;
}

int Commands::slashFill(Transaction& tx, size_t first, size_t last, const Fraction& t0, const Fraction& t1, bool concertPitch)
{
    // Score::cmdSlashFill: a slash on every beat (a dotted beat in compound time) of the range, in the first voice of
    // each measure that has only rests there (or none), voice 1 when every voice has music
    const std::vector<Id> staves = allStaves();
    int made = 0;
    std::vector<std::pair<Id, Id> > slashes;   // staff, chord
    for (size_t si = first; si < last && si < staves.size(); ++si) {
        const Id staff = staves[si];
        int voice = -1;
        for (Id measure : m_ops.measures()) {
            const Fraction ms = m_ops.measureStart(measure), me = m_ops.measureEnd(measure);
            if (me <= t0 || ms >= t1) {
                continue;
            }
            const Object* mo = m_ops.obj(measure);
            const int64_t num = mo->intOr(F::TIMESIG_NUM, 4), den = mo->intOr(F::TIMESIG_DEN, 4);
            const Fraction beat(den > 4 && num % 3 == 0 ? 3 : 1, int(den));
            const Fraction end = std::min(me, t1);
            if (const Id v0 = voiceNumbered(staff, 0); v0.isValid() && m_ops.stretchAt(v0, ms) != Fraction(1, 1)) {
                continue;   // a staff with its own time signature there: not yet (its beats aren't the measure's)
            }
            for (Fraction b = ms; b < end; b += beat) {
                if (b < t0) {
                    continue;
                }
                if (voice < 0 || b == ms) {
                    voice = -1;
                    for (int v = 0; v < 4 && voice < 0; ++v) {
                        if (eventAt(staff, v, b, Kind::Chord).isValid()) {
                            continue;
                        }
                        const Id rest = eventAt(staff, v, b, Kind::Rest);
                        if (rest.isValid() && m_ops.obj(rest)->has(F::MEASURE_REST)) {
                            voice = v;
                            break;
                        }
                        bool onlyRests = true;
                        for (Id e : eventsIn(voiceNumbered(staff, v), b, me)) {
                            onlyRests = onlyRests && !(m_ops.obj(e)->kind() == Kind::Chord && m_ops.timeOf(e) > b);
                        }
                        if (onlyRests) {
                            voice = v;
                        }
                    }
                    if (voice < 0) {
                        voice = 0;
                    }
                }
                const SlashPitch sp = slashPitch(staff, b, concertPitch);
                if (!sp.ok || insideTuplet(voiceNumbered(staff, voice), b)) {
                    continue;   // a beat inside a tuplet has no place for a slash (MuseScore finds no segment there)
                }
                const Id v = m_ops.voice(tx, staff, voice);
                // a beat cut short by the barline (a pickup, an irregular measure) gets a shorter slash; MuseScore
                // writes the whole beat, tied into the next measure, which shifts that measure's slashes
                Fraction at = b;
                for (const NoteValue& value : durationList(std::min(beat, me - b), 2)) {
                    const Id chord = m_ops.enterChord(tx, v, at, value, { sp.pitch });
                    at += value.length();
                    if (!chord.isValid()) {
                        continue;
                    }
                    for (Id n : notesOf(chord)) {
                        m_ops.setPitch(tx, n, sp.pitch, sp.tpc);
                        if (sp.string >= 0) {
                            tx.set(n, F::STRING, int64_t(sp.string));
                            tx.set(n, F::FRET, int64_t(sp.fret));
                        }
                    }
                    setSlash(tx, chord, true, true);
                    slashes.push_back({ staff, chord });
                    ++made;
                }
            }
        }
    }
    // linked staves (a tablature under its standard staff) show the slashes too, as MuseScore sets them on every
    // linked chord: their copies are made now, then given the slash look of their own staff
    bool linked = false;
    for (const auto& [staff, chord] : slashes) {
        linked = linked || !linkedStavesOf(staff).empty();
    }
    if (linked) {
        const ChangeSet soFar = tx.changes();
        syncLinkedStaves(tx, m_ops.m, m_ops.m_ids, &soFar);
    }
    for (const auto& [staff, chord] : slashes) {
        if (!m_ops.obj(chord)) {
            continue;
        }
        const int voice = int(m_ops.obj(m_ops.obj(chord)->parent())->intOr(F::NUMBER, 0));
        for (Id other : linkedStavesOf(staff)) {
            const Id copy = eventAt(other, voice, m_ops.timeOf(chord), Kind::Chord);
            if (copy.isValid()) {
                setSlash(tx, copy, true, true);
            }
        }
    }
    return made;
}

bool Commands::slashRhythm(Transaction& tx, const std::vector<Id>& events)
{
    // Score::cmdSlashRhythm: each chord's slash look toggled (stems kept); a rest in voice 3 or 4 becomes an accent
    // rest (small) or back
    bool changed = false;
    std::set<Id> done;
    for (Id e : events) {
        const Object* o = m_ops.obj(e);
        if (!o || !done.insert(e).second) {
            continue;
        }
        const Id staff = staffOfEvent(e);
        const int voice = int(m_ops.obj(o->parent())->intOr(F::NUMBER, 0));
        std::vector<Id> all { e };
        for (Id other : linkedStavesOf(staff)) {
            const Id copy = eventAt(other, voice, m_ops.timeOf(e), o->kind());
            if (copy.isValid() && done.insert(copy).second) {
                all.push_back(copy);
            }
        }
        if (o->kind() == Kind::Rest && voice >= 2) {
            const bool accent = detailOf(e, "small") == "1";
            for (Id x : all) {
                setDetailValues(tx, x, { { "small", accent ? "" : "1" } });
            }
            changed = true;
        } else if (o->kind() == Kind::Chord && !o->has(F::GRACE) && !notesOf(e).empty()) {
            const bool slash = isSlash(e);
            for (Id x : all) {
                setSlash(tx, x, !slash, false);
            }
            changed = true;
        }
    }
    return changed;
}

// ---- chord symbols -------------------------------------------------------------------------------------------
std::vector<Commands::ChordSymbol> Commands::chordSymbols() const
{
    const std::vector<Id> staves = allStaves();
    std::vector<ChordSymbol> out;
    for (Id c : m_ops.obj(flowOf())->children()) {
        const Object* o = m_ops.obj(c);
        if (o->kind() != Kind::FlowItem) {
            continue;
        }
        const bool fret = o->text(F::SUBKIND) == "detail" && o->text(F::TAG) == "FretDiagram";
        if (o->text(F::SUBKIND) != "harmony" && !fret) {
            continue;
        }
        const auto at = std::find(staves.begin(), staves.end(), o->idOr(F::ANCHOR));
        if (at == staves.end()) {
            continue;
        }
        ChordSymbol cs;
        cs.item = c;
        cs.time = m_ops.timeOf(c);
        cs.track = int(at - staves.begin()) * 4 + int(o->intOr(F::VOICE_INDEX, 0));
        cs.inFretDiagram = fret;
        cs.polychord = !fret && !o->has(F::INT_VALUE) && o->text(F::DETAILS).find("<harmonyInfo>") != std::string::npos;
        if (fret || cs.polychord) {
            // the diagram's chord symbol, or a polychord's first chord, as written
            const std::string& d = o->text(F::DETAILS);
            const size_t h = d.find(fret ? "<Harmony" : "<harmonyInfo>");
            if (h == std::string::npos) {
                continue;
            }
            const std::string harmony = d.substr(h, d.find(fret ? "</Harmony>" : "</harmonyInfo>", h) - h);
            const std::string root = anyValue(harmony, "root"), bass = anyValue(harmony, "bass");
            cs.root = root.empty() ? -9 : std::atoi(root.c_str());
            cs.bass = bass.empty() ? -9 : std::atoi(bass.c_str());
            cs.name = anyValue(harmony, "name");
        } else {
            cs.root = o->has(F::INT_VALUE) ? int(o->intOr(F::INT_VALUE, -9)) : -9;
            cs.bass = o->has(F::BASS) ? int(o->intOr(F::BASS, -9)) : -9;
            cs.name = o->text(F::TEXT);
        }
        out.push_back(cs);
    }
    std::stable_sort(out.begin(), out.end(), [](const ChordSymbol& a, const ChordSymbol& b) { return a.time < b.time; });
    return out;
}

int Commands::realizeChordSymbols(Transaction& tx, const std::vector<Id>& harmonies, const RealizeOptions& options, bool concertPitch)
{
    const std::vector<ChordSymbol> all = chordSymbols();
    const std::vector<Id> staves = allStaves();
    std::map<std::string, std::string> style;
    for (const auto& [name, value] : model::unpackPairs(m_ops.obj(m_ops.m.score())->text(F::STYLE))) {
        style[name] = value;
    }
    // the last time of the score
    Fraction scoreEnd;
    for (Id m : m_ops.measures()) {
        scoreEnd = std::max(scoreEnd, m_ops.measureEnd(m));
    }
    int made = 0;
    for (Id h : harmonies) {
        size_t index = all.size();
        for (size_t i = 0; i < all.size(); ++i) {
            if (all[i].item == h) {
                index = i;
            }
        }
        if (index == all.size() || all[index].root < -8) {
            continue;   // not a chord symbol that can be realized (no root: "N.C.")
        }
        const ChordSymbol& cs = all[index];
        const Object* o = m_ops.obj(h);
        auto setting = [&](const char* tag, const std::string& fallback) {
            // its own (a fret diagram's chord symbol: in the diagram), or the style's
            std::string own;
            if (cs.inFretDiagram) {
                const std::string& d = o->text(F::DETAILS);
                const size_t a = d.find("<Harmony");
                own = a == std::string::npos ? std::string() : anyValue(d.substr(a, d.find("</Harmony>", a) - a), tag);
            } else {
                own = detailValue(o->text(F::DETAILS), tag);
            }
            if (!own.empty()) {
                return own;
            }
            auto it = style.find(tag);
            return it != style.end() ? it->second : fallback;
        };
        const Fraction t = cs.time;
        const Id staff = staves[size_t(cs.track / 4)];
        const int voiceNumber = cs.track % 4;
        // settings
        const bool literal = options.override ? options.literal : setting("harmonyVoiceLiteral", "1") != "0";
        const int voicing = options.override ? options.voicing : std::atoi(setting("harmonyVoicing", "0").c_str());
        const int durationType = options.override ? options.duration : std::atoi(setting("harmonyDuration", "0").c_str());
        // the next chord symbol on the same staff and voice
        const ChordSymbol* next = nullptr;
        for (size_t j = index + 1; j < all.size() && !next; ++j) {
            if (all[j].track == cs.track && t < all[j].time) {
                next = &all[j];
            }
        }
        chords::Next n;
        if (next) {
            n.present = true;
            n.rootTpc = next->root;
            n.chord.parse(next->name);
        }
        chords::ParsedChord parsed;
        parsed.parse(cs.name);
        const Operations::Transposition tr = m_ops.transpositionAt(staff, t);
        const int offset = concertPitch ? 0 : int(tr.chromatic);
        const chords::PitchMap notes = chords::generateNotes(parsed, cs.root, cs.bass, literal, chords::Voicing(voicing), offset, n);
        // RealizedHarmony::getActualDuration
        const Id measure = m_ops.measureAt(t);
        const Fraction measureEnd = m_ops.measureEnd(measure);
        Fraction end = scoreEnd;
        if (durationType == int(chords::Duration::SegmentDuration)) {
            // until the next chord, rest or chord symbol of any staff (MuseScore's segment), or the barline
            end = measureEnd;
            for (Id st : staves) {
                for (Id v : m_ops.obj(st)->children()) {
                    if (m_ops.obj(v)->kind() != Kind::Voice) {
                        continue;
                    }
                    for (Id e : eventsIn(v, t + Fraction(1, 1920 * 4), measureEnd)) {
                        if (!m_ops.obj(e)->has(F::GRACE)) {
                            end = std::min(end, m_ops.timeOf(e));
                            break;
                        }
                    }
                }
            }
            for (const ChordSymbol& other : all) {
                if (t < other.time && other.time < end) {
                    end = other.time;
                }
            }
        } else {
            if (next) {
                end = next->time;
            }
            if (durationType == int(chords::Duration::StopAtMeasureEnd)) {
                end = std::min(end, measureEnd);
            }
        }
        if (!(t < end) || notes.empty() || m_ops.stretchAt(m_ops.voice(tx, staff, voiceNumber), t) != Fraction(1, 1)) {
            continue;   // (a staff with its own time signature: not yet)
        }
        // setChord: from t for the duration, each measure's part written in MuseScore's values and tied on, up to a
        // tuplet in the way
        const Id voice = m_ops.voice(tx, staff, voiceNumber);
        std::vector<Id> prevNotes;
        Fraction pos = t;
        bool first = true;
        while (pos < end) {
            const Fraction mEnd = m_ops.measureEnd(m_ops.measureAt(pos));
            Fraction partEnd = std::min(end, mEnd);
            for (Id e : m_ops.eventsOf(voice)) {
                if (m_ops.obj(e)->has(F::TUPLET) && m_ops.timeOf(e) < partEnd && pos < m_ops.endOf(e)) {
                    partEnd = std::min(partEnd, std::max(pos, m_ops.timeOf(e)));
                }
            }
            if (!(pos < partEnd)) {
                break;   // a tuplet: MuseScore writes into it; not yet
            }
            m_ops.clearRange(tx, voice, pos, partEnd);
            for (const NoteValue& value : durationList(partEnd - pos, 4)) {
                std::vector<std::pair<Field, Value> > f { { F::TIME, pos }, { F::DURATION, value.length() }, { F::BASE_DURATION, value.base } };
                if (value.dots) {
                    f.push_back({ F::DOTS, int64_t(value.dots) });
                }
                const Id chord = create(tx, m_ops.m_ids, voice, { Kind::Chord, f }, Placement::ByTime);
                std::vector<Id> chordNotes;
                for (const auto& [pitch, tpc] : notes) {
                    // the spelling given is the one shown: concert, or written on a transposing instrument
                    const int64_t concert = concertPitch ? tpc : model::transposeTpc(tpc, tr.diatonic, tr.chromatic);
                    std::vector<std::pair<Field, Value> > nf { { F::PITCH, int64_t(std::clamp(pitch, 0, 127)) }, { F::TPC, concert } };
                    if (const auto written = m_ops.writtenTpc(staff, concert, pos)) {
                        nf.push_back({ F::TPC2, *written });
                    }
                    chordNotes.push_back(create(tx, m_ops.m_ids, chord, { Kind::Note, nf }));
                }
                for (size_t k = 0; k < prevNotes.size() && k < chordNotes.size(); ++k) {
                    tx.set(prevNotes[k], F::TIE_TO, chordNotes[k]);
                }
                prevNotes = chordNotes;
                pos += value.length();
                first = false;
            }
        }
        made += first ? 0 : 1;
    }
    return made;
}
}
