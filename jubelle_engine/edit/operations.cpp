/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 */
#include "operations.h"

#include <algorithm>
#include <set>
#include <stdexcept>

#include "model/pitch.h"

namespace F = je::model::fields;
using je::model::IdList;
using je::model::Kind;
using je::model::Object;
using je::model::Text;

namespace je::edit {
Fraction NoteValue::length() const
{
    Fraction l = base, add = base;
    for (int i = 0; i < dots; ++i) {
        add = add * Fraction(1, 2);
        l += add;
    }
    return l;
}

namespace {
bool isWhole(const Fraction& f)
{
    return f.denominator() == 1;
}

const std::vector<Fraction>& bases()
{
    static const std::vector<Fraction> b { Fraction(2, 1), Fraction(1, 1), Fraction(1, 2), Fraction(1, 4), Fraction(1, 8),
                                           Fraction(1, 16), Fraction(1, 32), Fraction(1, 64), Fraction(1, 128),
                                           Fraction(1, 256), Fraction(1, 512), Fraction(1, 1024) };
    return b;
}
}

std::vector<NoteValue> valuesFor(const Fraction& position, const Fraction& length, bool forRest)
{
    std::vector<NoteValue> out;
    Fraction pos = position, rem = length;
    for (int guard = 0; Fraction() < rem && guard < 256; ++guard) {
        NoteValue pick;
        if (!forRest) {
            // a note: one value for the whole length if there is one (dots allowed), else the longest that fits
            for (const Fraction& b : bases()) {
                for (int dots = 0; dots <= 2 && !(pick.base == b); ++dots) {
                    if ((NoteValue { b, dots }).length() == rem) {
                        pick = { b, dots };
                    }
                }
            }
            if (pick.base == Fraction()) {
                for (const Fraction& b : bases()) {
                    for (int dots = 2; dots >= 0; --dots) {
                        const NoteValue v { b, dots };
                        if (pick.base == Fraction() && v.length() <= rem) {
                            pick = v;
                        }
                    }
                }
            }
        } else {
            // a rest: undotted, starting on a multiple of its own value (MuseScore's split)
            for (const Fraction& b : bases()) {
                if (pick.base == Fraction() && b <= rem && isWhole(pos / b)) {
                    pick = { b, 0 };
                }
            }
        }
        if (pick.base == Fraction()) {
            break;   // shorter than 1/1024 (an invalid length)
        }
        out.push_back(pick);
        pos += pick.length();
        rem -= pick.length();
    }
    return out;
}

// ---- reading ------------------------------------------------------------------------------------------------
Fraction Operations::timeOf(Id id) const
{
    const Object* o = obj(id);
    return o ? o->fractionOr(F::TIME, Fraction()) : Fraction();
}

Fraction Operations::endOf(Id id) const
{
    const Object* o = obj(id);
    if (!o || o->has(F::GRACE)) {
        return timeOf(id);
    }
    return timeOf(id) + o->fractionOr(F::DURATION, Fraction());
}

std::vector<Id> Operations::measures() const
{
    std::vector<Id> out;
    const Object* score = obj(m.score());
    if (!score) {
        return out;
    }
    for (Id c : score->children()) {
        const Object* f = obj(c);
        if (f && f->kind() == Kind::Flow) {
            for (Id x : f->children()) {
                if (obj(x)->kind() == Kind::Measure) {
                    out.push_back(x);
                }
            }
            break;
        }
    }
    return out;
}

Id Operations::measureAt(const Fraction& t) const
{
    // the flow's children are in time order: the last measure starting at or before t (a binary search, then back
    // over the items at the same time)
    const Object* score = obj(m.score());
    if (!score) {
        return Id();
    }
    for (Id c : score->children()) {
        const Object* f = obj(c);
        if (!f || f->kind() != Kind::Flow) {
            continue;
        }
        const std::vector<Id>& kids = f->children().ids();
        auto it = std::upper_bound(kids.begin(), kids.end(), t, [this](const Fraction& time, Id x) { return time < timeOf(x); });
        while (it != kids.begin()) {
            --it;
            if (obj(*it)->kind() == Kind::Measure) {
                return *it;
            }
        }
        // before the first measure: the first one
        for (Id x : kids) {
            if (obj(x)->kind() == Kind::Measure) {
                return t < timeOf(x) ? Id() : x;
            }
        }
        return Id();
    }
    return Id();
}

Fraction Operations::measureStart(Id measure) const
{
    return timeOf(measure);
}

Fraction Operations::measureEnd(Id measure) const
{
    return endOf(measure);
}

std::vector<Id> Operations::eventsOf(Id voice) const
{
    std::vector<Id> out;
    if (const Object* v = obj(voice)) {
        for (Id c : v->children()) {
            const Kind k = obj(c)->kind();
            if (k == Kind::Chord || k == Kind::Rest) {
                out.push_back(c);
            }
        }
    }
    return out;
}

Id Operations::staffOf(Id voice) const
{
    const Object* v = obj(voice);
    return v ? v->parent() : Id();
}

Id Operations::voice(Transaction& tx, Id staff, int number)
{
    Id flow;
    for (Id ms : measures()) {
        flow = obj(ms)->parent();
        break;
    }
    if (const Object* s = obj(staff)) {
        for (Id c : s->children()) {
            if (obj(c)->kind() == Kind::Voice && obj(c)->intOr(F::NUMBER, 0) == number) {
                return c;
            }
        }
    }
    return create(tx, m_ids, staff, { Kind::Voice, { { F::FLOW, flow }, { F::NUMBER, int64_t(number) } } });
}

int64_t Operations::keyAt(Id staff, const Fraction& t) const
{
    // back from t through the flow (in time order) to the nearest key signature for this staff or all staves
    const Object* score = obj(m.score());
    for (Id c : score->children()) {
        const Object* f = obj(c);
        if (!f || f->kind() != Kind::Flow) {
            continue;
        }
        const std::vector<Id>& kids = f->children().ids();
        auto it = std::upper_bound(kids.begin(), kids.end(), t, [this](const Fraction& time, Id x) { return time < timeOf(x); });
        auto fits = [&](const Object* item) {
            return item->kind() == Kind::FlowItem && item->text(F::SUBKIND) == "keySig"
                   && (!item->has(F::ANCHOR) || item->idOr(F::ANCHOR) == staff);
        };
        while (it != kids.begin()) {
            --it;
            if (fits(obj(*it))) {
                // of several at that time, the first (as the forward search took it)
                const Fraction at = timeOf(*it);
                auto found = it;
                while (found != kids.begin() && timeOf(*(found - 1)) == at) {
                    --found;
                    if (fits(obj(*found))) {
                        it = found;
                    }
                }
                return obj(*it)->intOr(F::INT_VALUE, 0);
            }
        }
    }
    return 0;
}

Fraction Operations::tupletFactor(Id tuplet) const
{
    Fraction f(1, 1);
    for (Id t = tuplet; t.isValid();) {
        const Object* o = obj(t);
        if (!o) {
            break;
        }
        f = f * Fraction(o->intOr(F::RATIO_DEN, 1), o->intOr(F::RATIO_NUM, 1));
        t = o->idOr(F::TUPLET);
    }
    return f;
}

//! A new note's fields: pitch, spelling for the key, and the written spelling on a transposing instrument
std::vector<std::pair<Field, Value> > Operations::noteFields(Id staff, const Fraction& t, int pitch) const
{
    const int64_t tpc = model::defaultTpc(pitch, keyAt(staff, t));
    std::vector<std::pair<Field, Value> > f { { F::PITCH, int64_t(pitch) }, { F::TPC, tpc } };
    if (const auto written = writtenTpc(staff, tpc)) {
        f.push_back({ F::TPC2, *written });
    }
    return f;
}

std::optional<int64_t> Operations::writtenTpc(Id staff, int64_t tpc) const
{
    if (const Object* st = obj(staff); st && obj(st->parent()) && obj(st->parent())->kind() == Kind::Player) {
        const Object* p = obj(st->parent());
        const int64_t c = p->intOr(F::TRANSPOSE_CHROMATIC, 0);
        const int64_t d = p->intOr(F::TRANSPOSE_DIATONIC, model::diatonicOf(c));
        const int64_t written = model::transposeTpc(tpc, -d, -c);
        if (written != tpc) {
            return written;
        }
    }
    return std::nullopt;
}

// ---- building blocks ----------------------------------------------------------------------------------------
void Operations::removeEvent(Transaction& tx, Id event)
{
    // ties into its notes and lines anchored to it go with it
    std::set<Id> notes;
    if (const Object* e = obj(event)) {
        for (Id c : e->children()) {
            if (obj(c)->kind() == Kind::Note) {
                notes.insert(c);
            }
        }
    }
    // ties from any voice of the instrument (a tie can go to another voice, or to the other staff of a piano); a tie
    // may come from any earlier note (the model doesn't require tied notes to be next to each other)
    std::vector<Id> tiedHere;
    const Fraction t = timeOf(event);
    const Object* staff = obj(obj(obj(event)->parent())->parent());
    const Object* player = staff ? obj(staff->parent()) : nullptr;
    for (Id st : player ? player->children() : model::IdList()) {
        for (Id v : obj(st)->children()) {
            if (obj(v)->kind() != Kind::Voice) {
                continue;
            }
            const std::vector<Id>& kids = obj(v)->children().ids();
            auto end = std::upper_bound(kids.begin(), kids.end(), t, [this](const Fraction& time, Id x) { return time < timeOf(x); });
            for (auto it = kids.begin(); it != end; ++it) {
                const Object* o = obj(*it);
                if (!o || o->kind() != Kind::Chord) {
                    continue;
                }
                for (Id n : o->children()) {
                    if (notes.count(obj(n)->idOr(F::TIE_TO))) {
                        tiedHere.push_back(n);
                    }
                }
            }
        }
    }
    for (Id n : tiedHere) {
        tx.set(n, F::TIE_TO, Value());
    }
    std::vector<Id> lines;
    for (Id c : obj(m.score())->children()) {
        const Object* sp = obj(c);
        if (sp->kind() == Kind::Spanner && (sp->idOr(F::ANCHOR) == event || sp->idOr(F::END_ANCHOR) == event
                                            || notes.count(sp->idOr(F::ANCHOR)) || notes.count(sp->idOr(F::END_ANCHOR)))) {
            lines.push_back(c);
        }
    }
    for (Id l : lines) {
        removeTree(tx, l);
    }
    removeTree(tx, event);
}

std::vector<Id> Operations::writeEvents(Transaction& tx, Id voice, const Fraction& t, const Fraction& length, bool rest,
                                        const std::vector<int>& pitches, Id tuplet)
{
    std::vector<Id> made;
    const Id staff = staffOf(voice);
    auto makeOne = [&](const Fraction& at, const NoteValue& v, const Fraction& factor) {
        std::vector<std::pair<Field, Value> > f { { F::TIME, at }, { F::DURATION, v.length() * factor }, { F::BASE_DURATION, v.base } };
        if (v.dots) {
            f.push_back({ F::DOTS, int64_t(v.dots) });
        }
        if (tuplet.isValid()) {
            f.push_back({ F::TUPLET, tuplet });
        }
        const Id e = create(tx, m_ids, voice, { rest ? Kind::Rest : Kind::Chord, f }, Placement::ByTime);
        if (!rest) {
            for (int p : pitches) {
                create(tx, m_ids, e, { Kind::Note, noteFields(staff, at, p) });
            }
            if (!made.empty() && obj(made.back())->kind() == Kind::Chord) {   // tied from the piece before
                const auto prev = obj(made.back())->children();
                const auto now = obj(e)->children();
                for (size_t i = 0; i < prev.size() && i < now.size(); ++i) {
                    tx.set(prev[i], F::TIE_TO, now[i]);
                }
            }
        }
        made.push_back(e);
    };
    if (tuplet.isValid()) {
        // inside a tuplet: written values on its own scale, from its start
        const Fraction factor = tupletFactor(tuplet);
        const Fraction start = timeOf(tuplet);
        Fraction at = t;
        for (const NoteValue& v : valuesFor((t - start) / factor, length / factor, rest)) {
            makeOne(at, v, factor);
            at += v.length() * factor;
        }
        return made;
    }
    Fraction at = t;
    const Fraction end = t + length;
    while (at < end) {
        const Id ms = measureAt(at);
        if (!ms.isValid()) {
            break;
        }
        const Fraction mEnd = measureEnd(ms);
        if (!(at < mEnd)) {
            break;   // past the last measure
        }
        const Fraction piece = std::min(end, mEnd) - at;
        if (rest && at == measureStart(ms) && piece == mEnd - at && obj(voice)->intOr(F::NUMBER, 0) == 0) {
            // a whole empty measure: one measure rest
            made.push_back(create(tx, m_ids, voice, { Kind::Rest, { { F::TIME, at }, { F::DURATION, piece }, { F::MEASURE_REST, true } } },
                                  Placement::ByTime));
        } else {
            for (const NoteValue& v : valuesFor(at - measureStart(ms), piece, rest)) {
                makeOne(at, v, Fraction(1, 1));
                at += v.length();
            }
        }
        at = std::min(end, mEnd);
    }
    return made;
}

void Operations::fillRests(Transaction& tx, Id voice, Fraction t0, const Fraction& t1, Id tuplet)
{
    if (t0 < t1) {
        writeEvents(tx, voice, t0, t1 - t0, true, {}, tuplet);
    }
}

void Operations::clearRange(Transaction& tx, Id voice, const Fraction& t0, const Fraction& t1)
{
    if (!(t0 < t1)) {
        return;
    }
    // the innermost tuplet holding the whole range, if any: the edit stays in its values
    Id context;
    for (Id e : eventsOf(voice)) {
        for (Id t = obj(e)->idOr(F::TUPLET); t.isValid(); t = obj(t)->idOr(F::TUPLET)) {
            if (timeOf(t) <= t0 && t1 <= endOf(t) && (!context.isValid() || (endOf(t) - timeOf(t)) < (endOf(context) - timeOf(context)))) {
                context = t;
            }
        }
    }
    // the tuplet an event belongs to directly below the context (the unit that is kept or removed whole)
    auto unitOf = [this, &context](Id e) {
        Id unit;
        for (Id t = obj(e)->idOr(F::TUPLET); t.isValid() && t != context; t = obj(t)->idOr(F::TUPLET)) {
            unit = t;
        }
        return unit;
    };
    // tuplets cut by the range are removed whole: the range grows to cover them, and what they held outside the
    // original range becomes rests
    Fraction e0 = t0, e1 = t1;
    for (bool grown = true; grown;) {
        grown = false;
        for (Id e : eventsOf(voice)) {
            const Id u = unitOf(e);
            if (u.isValid() && timeOf(u) < e1 && e0 < endOf(u) && (timeOf(u) < e0 || e1 < endOf(u))) {
                e0 = std::min(e0, timeOf(u));
                e1 = std::max(e1, endOf(u));
                grown = true;
            }
        }
    }
    std::vector<Id> remove;
    std::set<Id> tuplets;
    Id cutAtStart;          // an event that begins before the range and goes into it
    Fraction tailFrom, tailTo;   // the rest of an event cut at the end
    for (Id e : eventsOf(voice)) {
        if (context.isValid()) {
            bool inside = false;
            for (Id t = obj(e)->idOr(F::TUPLET); t.isValid(); t = obj(t)->idOr(F::TUPLET)) {
                inside = inside || t == context;
            }
            if (!inside) {
                continue;
            }
        }
        const Fraction s = timeOf(e), end = endOf(e);
        const bool grace = obj(e)->has(F::GRACE);
        if (grace) {
            if (e0 <= s && s < e1) {
                remove.push_back(e);   // goes with its main chord
            }
            continue;
        }
        if (!(s < e1 && e0 < end)) {
            continue;
        }
        if (unitOf(e).isValid()) {
            tuplets.insert(unitOf(e));
            remove.push_back(e);
        } else if (s < e0) {
            cutAtStart = e;
            if (e1 < end) {   // it also goes past the range: what is left after it becomes rests
                tailFrom = e1;
                tailTo = end;
            }
        } else {
            remove.push_back(e);
            if (e1 < end) {
                tailFrom = e1;
                tailTo = end;
            }
        }
    }
    for (Id e : remove) {
        if (obj(e)) {
            removeEvent(tx, e);
        }
    }
    for (Id t : tuplets) {
        // nested tuplets inside it too
        std::vector<Id> inner;
        for (Id c : obj(voice)->children()) {
            const Object* o = obj(c);
            if (o->kind() == Kind::Tuplet) {
                for (Id up = o->idOr(F::TUPLET); up.isValid(); up = obj(up) ? obj(up)->idOr(F::TUPLET) : Id()) {
                    if (up == t) {
                        inner.push_back(c);
                    }
                }
            }
        }
        for (Id i : inner) {
            removeTree(tx, i);
        }
        removeTree(tx, t);
    }
    if (cutAtStart.isValid()) {
        // shortened to end where the range starts: one value if one fits, else tied pieces (rests: more rests)
        const Object* e = obj(cutAtStart);
        const bool rest = e->kind() == Kind::Rest;
        const Id tuplet = e->idOr(F::TUPLET);
        const Fraction factor = tuplet.isValid() ? tupletFactor(tuplet) : Fraction(1, 1);
        const Fraction s = timeOf(cutAtStart);
        const Id ms = measureAt(s);
        const Fraction origin = tuplet.isValid() ? timeOf(tuplet) : measureStart(ms);
        const auto values = valuesFor((s - origin) / factor, (e0 - s) / factor, rest);
        std::vector<int> pitches;
        for (Id n : e->children()) {
            if (obj(n)->kind() == Kind::Note) {
                pitches.push_back(int(obj(n)->intOr(F::PITCH, 60)));
            }
        }
        if (!values.empty()) {
            tx.set(cutAtStart, F::MEASURE_REST, Value());
            tx.set(cutAtStart, F::BASE_DURATION, values[0].base);
            tx.set(cutAtStart, F::DOTS, values[0].dots ? Value(int64_t(values[0].dots)) : Value());
            tx.set(cutAtStart, F::DURATION, values[0].length() * factor);
            const Fraction after = s + values[0].length() * factor;
            if (after < e0) {
                const std::vector<Id> more = writeEvents(tx, voice, after, e0 - after, rest, pitches, tuplet);
                if (!rest && !more.empty()) {
                    const auto from = obj(cutAtStart)->children();
                    std::vector<Id> fromNotes;
                    for (Id c : from) {
                        if (obj(c)->kind() == Kind::Note) {
                            fromNotes.push_back(c);
                        }
                    }
                    const auto to = obj(more.front())->children();
                    for (size_t i = 0; i < fromNotes.size() && i < to.size(); ++i) {
                        tx.set(fromNotes[i], F::TIE_TO, to[i]);
                    }
                }
            }
        }
    }
    // what the removed music held outside the requested range becomes rests
    const Id contextTuplet = context;
    fillRests(tx, voice, e0, t0, contextTuplet);
    fillRests(tx, voice, t1, e1, contextTuplet);
    if (tailTo > tailFrom) {
        fillRests(tx, voice, std::max(tailFrom, e1), tailTo, contextTuplet);
    }
}

// ---- note entry ---------------------------------------------------------------------------------------------
Id Operations::enterChord(Transaction& tx, Id voice, const Fraction& t, const NoteValue& value, const std::vector<int>& pitches)
{
    if (pitches.empty()) {
        return enterRest(tx, voice, t, value);
    }
    // inside a tuplet the value is in the tuplet's terms
    Id tuplet;
    for (Id e : eventsOf(voice)) {
        if (timeOf(e) <= t && t < endOf(e) && obj(e)->has(F::TUPLET)) {
            tuplet = obj(e)->idOr(F::TUPLET);
        }
    }
    const Fraction factor = tuplet.isValid() ? tupletFactor(tuplet) : Fraction(1, 1);
    Fraction length = value.length() * factor;
    if (tuplet.isValid() && endOf(tuplet) < t + length) {
        length = endOf(tuplet) - t;   // a value longer than what is left of the tuplet is cut at its end
    }
    clearRange(tx, voice, t, t + length);
    // the range now holds nothing (clearRange refills only outside it)
    std::vector<Id> made;
    if (tuplet.isValid() || (t + length) <= measureEnd(measureAt(t))) {
        // one event of exactly the value asked for, when it fits its measure or tuplet
        std::vector<std::pair<Field, Value> > f { { F::TIME, t }, { F::DURATION, length }, { F::BASE_DURATION, value.base } };
        if (value.dots) {
            f.push_back({ F::DOTS, int64_t(value.dots) });
        }
        if (tuplet.isValid()) {
            f.push_back({ F::TUPLET, tuplet });
        }
        if (length == value.length() * factor) {
            const Id e = create(tx, m_ids, voice, { Kind::Chord, f }, Placement::ByTime);
            for (int p : pitches) {
                create(tx, m_ids, e, { Kind::Note, noteFields(staffOf(voice), t, p) });
            }
            return e;
        }
    }
    made = writeEvents(tx, voice, t, length, false, pitches, tuplet);
    return made.empty() ? Id() : made.front();
}

Id Operations::enterRest(Transaction& tx, Id voice, const Fraction& t, const NoteValue& value)
{
    Id tuplet;
    for (Id e : eventsOf(voice)) {
        if (timeOf(e) <= t && t < endOf(e) && obj(e)->has(F::TUPLET)) {
            tuplet = obj(e)->idOr(F::TUPLET);
        }
    }
    const Fraction factor = tuplet.isValid() ? tupletFactor(tuplet) : Fraction(1, 1);
    Fraction length = value.length() * factor;
    if (tuplet.isValid() && endOf(tuplet) < t + length) {
        length = endOf(tuplet) - t;
    }
    clearRange(tx, voice, t, t + length);
    if (length == value.length() * factor && (tuplet.isValid() || (t + length) <= measureEnd(measureAt(t)))) {
        std::vector<std::pair<Field, Value> > f { { F::TIME, t }, { F::DURATION, length }, { F::BASE_DURATION, value.base } };
        if (value.dots) {
            f.push_back({ F::DOTS, int64_t(value.dots) });
        }
        if (tuplet.isValid()) {
            f.push_back({ F::TUPLET, tuplet });
        }
        return create(tx, m_ids, voice, { Kind::Rest, f }, Placement::ByTime);
    }
    const std::vector<Id> made = writeEvents(tx, voice, t, length, true, {}, tuplet);
    return made.empty() ? Id() : made.front();
}

Id Operations::addNote(Transaction& tx, Id chord, int pitch)
{
    const Object* c = obj(chord);
    if (!c) {
        return Id();
    }
    if (c->kind() == Kind::Rest) {
        throw std::logic_error("je::edit::addNote: a rest has no notes (enter a chord instead)");
    }
    std::vector<Id> notes;
    for (Id n : c->children()) {
        if (obj(n)->kind() == Kind::Note) {
            if (obj(n)->intOr(F::PITCH, -1) == pitch) {
                return n;
            }
            notes.push_back(n);
        }
    }
    // notes are kept low to high
    std::vector<Id> kids = c->children().ids();
    const Id note = create(tx, m_ids, chord, { Kind::Note, noteFields(staffOf(c->parent()), timeOf(chord), pitch) });
    kids = obj(chord)->children().ids();
    kids.erase(std::remove(kids.begin(), kids.end(), note), kids.end());
    auto pos = kids.begin();
    while (pos != kids.end() && (obj(*pos)->kind() != Kind::Note || obj(*pos)->intOr(F::PITCH, 0) < pitch)) {
        ++pos;
    }
    // past attachments that come before the notes, the note goes before the first higher one
    kids.insert(pos, note);
    tx.set(chord, F::CHILDREN, IdList(std::move(kids)));
    return note;
}

void Operations::removeNote(Transaction& tx, Id note)
{
    const Object* n = obj(note);
    if (!n) {
        return;
    }
    const Id chord = n->parent();
    int notes = 0;
    for (Id c : obj(chord)->children()) {
        notes += obj(c)->kind() == Kind::Note;
    }
    if (notes > 1) {
        // ties into it go
        for (Id e : eventsOf(obj(chord)->parent())) {
            for (Id c : obj(e)->children()) {
                if (obj(c)->idOr(F::TIE_TO) == note) {
                    tx.set(c, F::TIE_TO, Value());
                }
            }
        }
        removeTree(tx, note);
        return;
    }
    // the last note: the chord becomes a rest (in any voice; deleting the rest leaves the gap)
    const Object* c = obj(chord);
    const Id voice = c->parent();
    std::vector<std::pair<Field, Value> > f { { F::TIME, c->get(F::TIME) }, { F::DURATION, c->get(F::DURATION) },
                                              { F::BASE_DURATION, c->get(F::BASE_DURATION) } };
    if (c->has(F::DOTS)) {
        f.push_back({ F::DOTS, c->get(F::DOTS) });
    }
    if (c->has(F::TUPLET)) {
        f.push_back({ F::TUPLET, c->get(F::TUPLET) });
    }
    const bool grace = c->has(F::GRACE);
    removeEvent(tx, chord);
    if (!grace) {
        create(tx, m_ids, voice, { Kind::Rest, f }, Placement::ByTime);
    }
}

void Operations::setPitch(Transaction& tx, Id note, int pitch, int64_t tpc)
{
    const Object* n = obj(note);
    if (!n) {
        return;
    }
    const Id chord = n->parent();
    auto f = noteFields(staffOf(obj(chord)->parent()), timeOf(chord), pitch);
    if (tpc >= 0) {
        f = { { F::PITCH, int64_t(pitch) }, { F::TPC, tpc } };
        const auto spelled = noteFields(staffOf(obj(chord)->parent()), timeOf(chord), pitch);
        for (const auto& [field, v] : spelled) {
            if (field == F::TPC2) {   // the same transposition from the spelling given
                const int64_t shift = std::get<int64_t>(v) - std::get<int64_t>(spelled[1].second);
                f.push_back({ F::TPC2, tpc + shift });
            }
        }
    }
    tx.set(note, F::TPC2, Value());
    for (const auto& [field, v] : f) {
        tx.set(note, field, v);
    }
    // a tie holds only between equal pitches
    if (const Object* to = obj(obj(note)->idOr(F::TIE_TO)); to && to->intOr(F::PITCH, -1) != pitch) {
        tx.set(note, F::TIE_TO, Value());
    }
    for (Id e : eventsOf(obj(chord)->parent())) {
        for (Id c : obj(e)->children()) {
            if (obj(c)->idOr(F::TIE_TO) == note && obj(c)->intOr(F::PITCH, -1) != pitch) {
                tx.set(c, F::TIE_TO, Value());
            }
        }
    }
}

bool Operations::tieToNext(Transaction& tx, Id note)
{
    const Object* n = obj(note);
    if (!n) {
        return false;
    }
    const Id chord = n->parent();
    const Fraction end = endOf(chord);
    for (Id e : eventsOf(obj(chord)->parent())) {
        const Object* o = obj(e);
        if (o->kind() == Kind::Chord && !o->has(F::GRACE) && timeOf(e) == end) {
            for (Id c : o->children()) {
                if (obj(c)->kind() == Kind::Note && obj(c)->intOr(F::PITCH, -1) == n->intOr(F::PITCH, -2)) {
                    tx.set(note, F::TIE_TO, c);
                    return true;
                }
            }
        }
    }
    return false;
}

void Operations::untie(Transaction& tx, Id note)
{
    tx.set(note, F::TIE_TO, Value());
}

// ---- changing what is there ---------------------------------------------------------------------------------
void Operations::deleteEvent(Transaction& tx, Id event)
{
    const Object* e = obj(event);
    if (!e) {
        return;
    }
    const Id voice = e->parent();
    if (e->has(F::GRACE)) {
        removeEvent(tx, event);
        return;
    }
    if (obj(voice)->intOr(F::NUMBER, 0) != 0) {
        // graces at its time go with it
        const Fraction t = timeOf(event);
        std::vector<Id> graces;
        for (Id g : eventsOf(voice)) {
            if (obj(g)->has(F::GRACE) && timeOf(g) == t) {
                graces.push_back(g);
            }
        }
        for (Id g : graces) {
            removeEvent(tx, g);
        }
        removeEvent(tx, event);
        return;
    }
    if (e->kind() == Kind::Rest) {
        return;   // a rest in voice 1 is already the deleted state
    }
    const Fraction t = timeOf(event), end = endOf(event);
    const Id tuplet = e->idOr(F::TUPLET);   // rests inside a tuplet stay in it
    clearRange(tx, voice, t, end);
    fillRests(tx, voice, t, end, tuplet);
}

void Operations::setValue(Transaction& tx, Id event, const NoteValue& value)
{
    const Object* e = obj(event);
    if (!e || e->has(F::GRACE)) {
        return;
    }
    const Id voice = e->parent();
    const Fraction t = timeOf(event), oldEnd = endOf(event);
    const Id tuplet = e->idOr(F::TUPLET);
    const Fraction factor = tuplet.isValid() ? tupletFactor(tuplet) : Fraction(1, 1);
    const Fraction limit = tuplet.isValid() ? endOf(tuplet) : measureEnd(measureAt(t));
    Fraction length = value.length() * factor;
    if (limit < t + length) {
        // longer than its measure (or tuplet) allows: entered again, which continues it, tied, into the next
        std::vector<int> pitches;
        for (Id n : e->children()) {
            if (obj(n)->kind() == Kind::Note) {
                pitches.push_back(int(obj(n)->intOr(F::PITCH, 60)));
            }
        }
        if (tuplet.isValid()) {
            length = limit - t;   // cut at the tuplet's end (written below in the values that fit)
        } else {
            if (e->kind() == Kind::Chord) {
                enterChord(tx, voice, t, value, pitches);
            } else {
                enterRest(tx, voice, t, value);
            }
            return;
        }
    }
    if (oldEnd < t + length) {
        clearRange(tx, voice, oldEnd, t + length);
    }
    // the value asked for, or (cut at a tuplet's end) the values that fill the length, tied for a chord
    NoteValue first = value;
    if (length != value.length() * factor) {
        const Fraction origin = tuplet.isValid() ? timeOf(tuplet) : measureStart(measureAt(t));
        const auto vs = valuesFor((t - origin) / factor, length / factor, e->kind() == Kind::Rest);
        first = vs.empty() ? value : vs.front();
    }
    const Fraction firstLength = first.length() * factor;
    tx.set(event, F::MEASURE_REST, Value());
    tx.set(event, F::BASE_DURATION, first.base);
    tx.set(event, F::DOTS, first.dots ? Value(int64_t(first.dots)) : Value());
    tx.set(event, F::DURATION, firstLength);
    if (firstLength < length) {
        const bool rest = obj(event)->kind() == Kind::Rest;
        std::vector<int> pitches;
        std::vector<Id> notes;
        for (Id n : obj(event)->children()) {
            if (obj(n)->kind() == Kind::Note) {
                pitches.push_back(int(obj(n)->intOr(F::PITCH, 60)));
                notes.push_back(n);
            }
        }
        const std::vector<Id> more = writeEvents(tx, voice, t + firstLength, length - firstLength, rest, pitches, tuplet);
        if (!rest && !more.empty()) {
            const auto to = obj(more.front())->children();
            for (size_t i = 0; i < notes.size() && i < to.size(); ++i) {
                tx.set(notes[i], F::TIE_TO, to[i]);
            }
        }
    }
    if (t + length < oldEnd && obj(voice)->intOr(F::NUMBER, 0) == 0) {
        fillRests(tx, voice, t + length, oldEnd, tuplet);
    }
}

Id Operations::makeTuplet(Transaction& tx, Id voice, const Fraction& t, int actual, int normal, const Fraction& base)
{
    if (actual <= 0 || normal <= 0) {
        throw std::logic_error("je::edit::makeTuplet: a tuplet needs a ratio");
    }
    const Fraction span = base * Fraction(normal, 1);
    if (measureEnd(measureAt(t)) < t + span) {
        throw std::logic_error("je::edit::makeTuplet: a tuplet can't cross a barline");
    }
    clearRange(tx, voice, t, t + span);
    const Id tuplet = create(tx, m_ids, voice, { Kind::Tuplet, { { F::TIME, t }, { F::DURATION, span }, { F::BASE_DURATION, base },
                                                     { F::RATIO_NUM, int64_t(actual) }, { F::RATIO_DEN, int64_t(normal) } } },
                             Placement::ByTime);
    fillRests(tx, voice, t, t + span, tuplet);
    return tuplet;
}

// ---- transposition ----------------------------------------------------------------------------------------
void Operations::transposeNotes(Transaction& tx, const std::vector<Id>& notes, int diatonic, int chromatic)
{
    for (Id n : notes) {
        const Object* note = obj(n);
        if (!note || note->kind() != Kind::Note) {
            continue;
        }
        const int64_t pitch = note->intOr(F::PITCH, 60) + chromatic;
        if (pitch < 0 || pitch > 127) {
            continue;   // out of range: left where it is
        }
        // (read before changing: a changed object is a new one)
        const int64_t tpc = note->has(F::TPC) ? note->intOr(F::TPC, 14) : model::defaultTpc(pitch - chromatic, 0);
        const bool hasTpc2 = note->has(F::TPC2);
        const int64_t tpc2 = note->intOr(F::TPC2, 14);
        tx.set(n, F::PITCH, pitch);
        tx.set(n, F::TPC, model::transposeTpc(tpc, diatonic, chromatic));
        if (hasTpc2) {
            tx.set(n, F::TPC2, model::transposeTpc(tpc2, diatonic, chromatic));
        }
    }
}

void Operations::transposeRange(Transaction& tx, const std::vector<Id>& staves, const Fraction& t0, const Fraction& t1, int diatonic,
                                int chromatic)
{
    std::vector<Id> notes;
    for (Id staff : staves) {
        for (Id v : obj(staff)->children()) {
            if (obj(v)->kind() != Kind::Voice) {
                continue;
            }
            for (Id e : eventsOf(v)) {
                const Fraction t = timeOf(e);
                if (t0 <= t && t < t1) {
                    for (Id n : obj(e)->children()) {
                        if (obj(n)->kind() == Kind::Note) {
                            notes.push_back(n);
                        }
                    }
                }
            }
        }
    }
    transposeNotes(tx, notes, diatonic, chromatic);
    // chord symbols on these staves: their roots
    const std::vector<Id> ms = measures();
    if (ms.empty()) {
        return;
    }
    for (Id x : obj(obj(ms.front())->parent())->children()) {
        const Object* it = obj(x);
        if (it->kind() != Kind::FlowItem || it->text(F::SUBKIND) != "harmony" || !it->has(F::INT_VALUE)) {
            continue;
        }
        const Fraction t = it->fractionOr(F::TIME, Fraction());
        if (t0 <= t && t < t1 && std::find(staves.begin(), staves.end(), it->idOr(F::ANCHOR)) != staves.end()) {
            tx.set(x, F::INT_VALUE, model::transposeTpc(it->intOr(F::INT_VALUE, 14), diatonic, chromatic));
        }
    }
}

// ---- measures -----------------------------------------------------------------------------------------------
void Operations::shiftFrom(Transaction& tx, const Fraction& from, const Fraction& delta)
{
    std::vector<Id> all;
    m.snapshot().forEachObject([&all](const Object& o) {
        if (o.has(F::TIME)) {
            all.push_back(o.id());
        }
    });
    for (Id id : all) {
        const Object* o = obj(id);
        const Fraction t = o->fractionOr(F::TIME, Fraction());
        if (from <= t) {
            tx.set(id, F::TIME, t + delta);
        } else if (o->kind() == Kind::Spanner && from < t + o->fractionOr(F::DURATION, Fraction())) {
            tx.set(id, F::DURATION, o->fractionOr(F::DURATION, Fraction()) + delta);   // a line across the change
        }
    }
}

void Operations::insertMeasures(Transaction& tx, size_t before, int count, int num, int den)
{
    const std::vector<Id> ms = measures();
    if (ms.empty() || count <= 0) {
        return;
    }
    const Id flow = obj(ms.front())->parent();
    const Fraction from = before < ms.size() ? measureStart(ms[before]) : measureEnd(ms.back());
    const Fraction len(num, den);
    shiftFrom(tx, from, len * Fraction(count, 1));
    for (int i = 0; i < count; ++i) {
        create(tx, m_ids, flow, { Kind::Measure, { { F::TIME, from + len * Fraction(i, 1) }, { F::DURATION, len },
                                                   { F::TIMESIG_NUM, int64_t(num) }, { F::TIMESIG_DEN, int64_t(den) } } },
               Placement::ByTime);
    }
    // every staff's voice 1: measure rests
    for (Id c : obj(m.score())->children()) {
        const Object* p = obj(c);
        if (p->kind() != Kind::Player) {
            continue;
        }
        for (Id staff : p->children()) {
            const Id v = voice(tx, staff, 0);
            for (int i = 0; i < count; ++i) {
                create(tx, m_ids, v, { Kind::Rest, { { F::TIME, from + len * Fraction(i, 1) }, { F::DURATION, len }, { F::MEASURE_REST, true } } },
                       Placement::ByTime);
            }
        }
    }
}

void Operations::removeMeasures(Transaction& tx, size_t first, size_t count)
{
    const std::vector<Id> ms = measures();
    if (first >= ms.size() || count == 0) {
        return;
    }
    const size_t last = std::min(ms.size(), first + count);
    if (first == 0 && last == ms.size()) {
        throw std::logic_error("je::edit::removeMeasures: a score keeps at least one measure");
    }
    const Fraction a = measureStart(ms[first]), b = measureEnd(ms[last - 1]);
    // the music inside: events, tuplets, items and lines wholly inside; ties into it
    std::vector<Id> events, others;
    m.snapshot().forEachObject([&](const Object& o) {
        if (!o.has(F::TIME) || o.kind() == Kind::Measure) {
            return;
        }
        const Fraction t = o.fractionOr(F::TIME, Fraction());
        if (o.kind() == Kind::Chord || o.kind() == Kind::Rest) {
            if (a <= t && t < b) {
                events.push_back(o.id());
            }
        } else if (o.kind() == Kind::Tuplet || o.kind() == Kind::FlowItem) {
            if (a <= t && t < b) {
                others.push_back(o.id());
            }
        } else if (o.kind() == Kind::Spanner) {
            const Fraction e = t + o.fractionOr(F::DURATION, Fraction());
            if (a <= t && e <= b) {
                others.push_back(o.id());
            } else if (a <= t && t < b) {
                // starting inside: it starts where the removed music ended (moved back below)
                tx.set(o.id(), F::TIME, b);
                tx.set(o.id(), F::DURATION, e - b);
            } else if (t < a && a < e && e <= b) {
                tx.set(o.id(), F::DURATION, a - t);   // ending inside: it ends where the removed music began
            }   // across the whole removed range: shortened by the shift below
        }
    });
    for (Id e : events) {
        if (obj(e)) {
            removeEvent(tx, e);
        }
    }
    for (Id o : others) {
        if (obj(o)) {
            removeTree(tx, o);
        }
    }
    for (size_t i = first; i < last; ++i) {
        removeTree(tx, ms[i]);
    }
    shiftFrom(tx, b, a - b);
}
}
