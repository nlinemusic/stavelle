/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 */
#include "clipboard.h"

#include <algorithm>
#include <map>
#include <set>

namespace F = je::model::fields;
using je::model::IdList;
using je::model::Kind;
using je::model::Object;

namespace je::edit {
namespace {
//! The fields a copy keeps: the music, not identities, links, positions or the file's carried appearance (which
//! belongs to the original element; modelled appearance comes with S7)
std::vector<std::pair<Field, Value> > musicFields(const Object& o)
{
    static const std::set<Field> leave { F::KIND, F::TIME, F::CHILDREN, F::PARENT, F::TUPLET, F::TIE_TO, F::ORIGIN_EID, F::DETAILS,
                                         F::DETAILS_2, F::DETAILS_AFTER, F::DETAILS_KEY, F::SEQ, F::ANCHOR, F::HOME_STAFF,
                                         F::START_EVENT, F::END_STAFF, F::END_ANCHOR, F::LINK_EID, F::LINK_MARK, F::TIE_EID,
                                         F::TIE_LINK, F::TAG, F::AT_END, F::LINKED_TO, F::LINKED_STAFF, F::TIE_MARK, F::LINE_DETAILS };
    std::vector<std::pair<Field, Value> > out;
    o.forEachField([&out](Field f, const Value& v) {
        if (!leave.count(f)) {
            out.emplace_back(f, v);
        }
    });
    return out;
}

Fraction fieldFraction(const std::vector<std::pair<Field, Value> >& f, Field field)
{
    for (const auto& [k, v] : f) {
        if (k == field && std::holds_alternative<Fraction>(v)) {
            return std::get<Fraction>(v);
        }
    }
    return Fraction();
}

bool fieldHas(const std::vector<std::pair<Field, Value> >& f, Field field)
{
    return std::any_of(f.begin(), f.end(), [field](const auto& p) { return p.first == field; });
}
}

std::vector<Id> Clipboard::stavesInOrder() const
{
    std::vector<Id> out;
    for (Id c : m.find(m.score())->children()) {
        const Object* p = m.find(c);
        if (p->kind() == Kind::Player) {
            for (Id s : p->children()) {
                out.push_back(s);
            }
        }
    }
    return out;
}

RangeSelection Clipboard::whole(const RangeSelection& range) const
{
    RangeSelection r = range;
    for (bool grown = true; grown;) {
        grown = false;
        for (Id staff : r.staves) {
            for (Id v : m.find(staff)->children()) {
                for (Id c : m.find(v)->children()) {
                    const Object* t = m.find(c);
                    if (t->kind() != Kind::Tuplet) {
                        continue;
                    }
                    const Fraction s = t->fractionOr(F::TIME, Fraction()), e = s + t->fractionOr(F::DURATION, Fraction());
                    if (s < r.end && r.start < e && (s < r.start || r.end < e)) {
                        r.start = std::min(r.start, s);
                        r.end = std::max(r.end, e);
                        grown = true;
                    }
                }
            }
        }
    }
    return r;
}

Clip Clipboard::copy(const RangeSelection& selection, bool voiceOneRests) const
{
    const RangeSelection r = whole(selection);
    Clip clip;
    clip.length = r.end - r.start;
    std::map<Id, std::pair<int, int> > eventIndex;   // event → (staff, index in its voice's list)
    std::map<Id, int> voiceOf;
    std::map<Id, std::pair<int, int> > noteIndex;    // note → (event index, note index), per voice key below
    std::map<Id, int> noteVoice;
    std::vector<std::pair<Id, Clip::Note*> > ties;
    int staffNo = 0;
    std::vector<std::vector<Clip::Voice>*> voiceLists;
    for (Id staff : r.staves) {
        Clip::Staff cs;
        for (Id v : m.find(staff)->children()) {
            const Object* vo = m.find(v);
            if (vo->kind() != Kind::Voice) {
                continue;
            }
            Clip::Voice cv;
            cv.number = int(vo->intOr(F::NUMBER, 0));
            std::map<Id, int> tuplets;
            for (Id c : vo->children()) {
                const Object* t = m.find(c);
                const Fraction at = t->fractionOr(F::TIME, Fraction());
                if (t->kind() == Kind::Tuplet && r.start <= at && at < r.end) {
                    tuplets[c] = int(cv.tuplets.size());
                    cv.tuplets.push_back({ at - r.start, musicFields(*t), -1 });
                }
            }
            for (Id c : vo->children()) {
                const Object* t = m.find(c);
                if (t->kind() == Kind::Tuplet && tuplets.count(c) && tuplets.count(t->idOr(F::TUPLET))) {
                    cv.tuplets[size_t(tuplets[c])].parent = tuplets[t->idOr(F::TUPLET)];
                }
            }
            for (Id e : ops.eventsOf(v)) {
                const Object* o = m.find(e);
                const Fraction at = o->fractionOr(F::TIME, Fraction());
                if (at < r.start || !(at < r.end)) {
                    continue;
                }
                const bool rest = o->kind() == Kind::Rest;
                // voice 1's rests outside tuplets are made again by the paste
                if (rest && cv.number == 0 && !o->has(F::TUPLET) && !voiceOneRests) {
                    continue;
                }
                if (o->has(F::MEASURE_REST)) {
                    continue;   // an empty measure: the paste leaves rests (voice 1) or a gap
                }
                Clip::Event ce;
                ce.rest = rest;
                ce.rel = at - r.start;
                ce.fields = musicFields(*o);
                if (o->has(F::TUPLET) && tuplets.count(o->idOr(F::TUPLET))) {
                    ce.tuplet = tuplets[o->idOr(F::TUPLET)];
                }
                for (Id n : o->children()) {
                    const Object* x = m.find(n);
                    if (x->kind() == Kind::Note) {
                        Clip::Note cn;
                        cn.fields = musicFields(*x);
                        for (Id a : x->children()) {
                            cn.attachments.push_back(musicFields(*m.find(a)));
                        }
                        noteIndex[n] = { int(cv.events.size()), int(ce.notes.size()) };
                        ce.notes.push_back(cn);
                    } else if (x->kind() == Kind::Attachment) {
                        ce.attachments.push_back(musicFields(*x));
                    }
                }
                eventIndex[e] = { staffNo, int(cv.events.size()) };
                voiceOf[e] = int(cs.voices.size());
                cv.events.push_back(ce);
            }
            // ties inside the copy (in the same voice)
            for (Id e : ops.eventsOf(v)) {
                if (!eventIndex.count(e)) {
                    continue;
                }
                for (Id n : m.find(e)->children()) {
                    const Object* x = m.find(n);
                    if (x->kind() != Kind::Note || !x->has(F::TIE_TO)) {
                        continue;
                    }
                    auto to = noteIndex.find(x->idOr(F::TIE_TO));
                    auto from = noteIndex.find(n);
                    if (to != noteIndex.end() && from != noteIndex.end() && m.find(x->idOr(F::TIE_TO))
                        && m.find(m.find(x->idOr(F::TIE_TO))->parent())->parent() == v) {
                        Clip::Note& cn = cv.events[size_t(from->second.first)].notes[size_t(from->second.second)];
                        cn.tieTo = to->second.first;
                        cn.tieToNote = to->second.second;
                    }
                }
            }
            noteIndex.clear();
            cs.voices.push_back(std::move(cv));
        }
        clip.staves.push_back(std::move(cs));
        ++staffNo;
    }
    // lines starting and ending inside, and items on these staves
    auto staffIndex = [&r](Id staff) {
        auto it = std::find(r.staves.begin(), r.staves.end(), staff);
        return it == r.staves.end() ? -1 : int(it - r.staves.begin());
    };
    for (Id c : m.find(m.score())->children()) {
        const Object* sp = m.find(c);
        if (sp->kind() == Kind::Spanner) {
            const Fraction s = sp->fractionOr(F::TIME, Fraction()), e = s + sp->fractionOr(F::DURATION, Fraction());
            Id anchor = sp->idOr(F::ANCHOR);
            int startEvent = -1, startVoice = -1;
            if (const Object* an = m.find(anchor); an && an->kind() == Kind::Note) {
                // a line from note to note: copied when both of its notes are
                auto end = [&](Id note) {
                    Clip::Item::NoteEnd ne;
                    const Object* n = m.find(note);
                    if (!n || !eventIndex.count(n->parent())) {
                        return ne;
                    }
                    ne.voice = voiceOf[n->parent()];
                    ne.event = eventIndex[n->parent()].second;
                    int i = 0;
                    for (Id x : m.find(n->parent())->children()) {
                        if (x == note) {
                            ne.note = i;
                        }
                        i += m.find(x)->kind() == Kind::Note ? 1 : 0;
                    }
                    return ne;
                };
                const Clip::Item::NoteEnd a = end(anchor), b = end(sp->idOr(F::END_ANCHOR));
                if (a.event >= 0 && b.event >= 0 && a.note >= 0 && b.note >= 0) {
                    Clip::Item item { eventIndex[m.find(anchor)->parent()].first, Kind::Spanner, s - r.start, musicFields(*sp), -1, -1 };
                    item.fromNote = a;
                    item.toNote = b;
                    clip.items.push_back(item);
                }
                continue;
            }
            if (sp->has(F::START_EVENT) && eventIndex.count(sp->idOr(F::START_EVENT))) {
                startEvent = eventIndex[sp->idOr(F::START_EVENT)].second;
                startVoice = voiceOf[sp->idOr(F::START_EVENT)];
                anchor = r.staves[size_t(eventIndex[sp->idOr(F::START_EVENT)].first)];
            }
            if (staffIndex(anchor) < 0 || s < r.start || r.end < e || sp->has(F::END_STAFF)) {
                continue;
            }
            clip.items.push_back({ staffIndex(anchor), Kind::Spanner, s - r.start, musicFields(*sp), startEvent, startVoice });
        } else if (sp->kind() == Kind::Flow) {
            for (Id x : sp->children()) {
                const Object* it = m.find(x);
                const Fraction t = it->fractionOr(F::TIME, Fraction());
                const std::string sub = it->text(F::SUBKIND);
                if (it->kind() != Kind::FlowItem || sub == "frame" || sub == "measureDetail" || sub == "mmrMeasure" || sub == "detail"
                    || staffIndex(it->idOr(F::ANCHOR)) < 0 || t < r.start || !(t < r.end)) {
                    continue;
                }
                clip.items.push_back({ staffIndex(it->idOr(F::ANCHOR)), Kind::FlowItem, t - r.start, musicFields(*it) });
            }
        }
    }
    return clip;
}

Clip Clipboard::voiceOf(const Clip& clip, int fromNumber, int toNumber)
{
    Clip out;
    out.length = clip.length;
    if (clip.staves.empty()) {
        return out;
    }
    int index = -1;
    for (size_t i = 0; i < clip.staves[0].voices.size(); ++i) {
        if (clip.staves[0].voices[i].number == fromNumber) {
            index = int(i);
        }
    }
    out.staves.resize(1);
    if (index < 0) {
        return out;
    }
    out.staves[0].voices.push_back(clip.staves[0].voices[size_t(index)]);
    out.staves[0].voices[0].number = toNumber;
    auto voiceIndex = [](const Clip::Item& item) {
        for (const auto& [f, v] : item.fields) {
            if (f == F::VOICE_INDEX && std::holds_alternative<int64_t>(v)) {
                return int(std::get<int64_t>(v));
            }
        }
        return 0;
    };
    // MuseScore's cloneVoice: the voice's annotations (dynamics, texts, chord symbols…), its slurs, hammer-ons and
    // hairpins, and the lines between its notes
    static const std::set<std::string> lines { "slur", "hammerOnPullOff", "hairPin", "hairpin" };
    static const std::set<std::string> notItems { "clef", "keySig", "timeSig", "breath", "frame", "measureDetail", "mmrMeasure",
                                                  "detail" };
    for (Clip::Item item : clip.items) {
        if (item.staff != 0) {
            continue;
        }
        std::string sub;
        for (const auto& [f, v] : item.fields) {
            if (f == F::SUBKIND && std::holds_alternative<model::Text>(v)) {
                sub = std::get<model::Text>(v).str();
            }
        }
        if (item.kind == Kind::FlowItem) {
            if (notItems.count(sub) || voiceIndex(item) != fromNumber) {
                continue;
            }
            for (auto& [f, v] : item.fields) {
                if (f == F::VOICE_INDEX) {
                    v = int64_t(toNumber);
                }
            }
            out.items.push_back(item);
            continue;
        }
        if (item.kind != Kind::Spanner || (item.fromNote.event < 0 && !lines.count(sub))) {
            continue;
        }
        if (item.fromNote.event >= 0) {
            if (item.fromNote.voice != index || item.toNote.voice != index) {
                continue;
            }
            item.fromNote.voice = item.toNote.voice = 0;
        } else {
            if (voiceIndex(item) != fromNumber) {
                continue;
            }
            for (auto& [f, v] : item.fields) {
                if (f == F::VOICE_INDEX) {
                    v = int64_t(toNumber);
                }
            }
            if (item.startVoice >= 0) {
                if (item.startVoice != index) {
                    continue;
                }
                item.startVoice = 0;
            }
        }
        out.items.push_back(item);
    }
    return out;
}

RangeSelection Clipboard::paste(Transaction& tx, const Clip& clip, Id staff, const Fraction& at, bool onlyItsVoices)
{
    m_left = 0;
    RangeSelection pasted;
    const std::vector<Id> all = stavesInOrder();
    auto first = std::find(all.begin(), all.end(), staff);
    const std::vector<Id> ms = ops.measures();
    if (first == all.end() || ms.empty() || clip.empty()) {
        return pasted;
    }
    const Fraction scoreEnd = ops.measureEnd(ms.back());
    const Fraction end = std::min(at + clip.length, scoreEnd);
    pasted.start = at;
    pasted.end = end;
    std::vector<std::vector<Id> > made(clip.staves.size());   // per staff, per voice: the events made (for lines)
    std::vector<std::vector<std::vector<Id> > > madeEvents(clip.staves.size());
    for (size_t i = 0; i < clip.staves.size() && first + long(i) != all.end(); ++i) {
        const Id target = *(first + long(i));
        pasted.staves.push_back(target);
        // the target range is replaced in every voice
        for (Id v : m.find(target)->children()) {
            if (m.find(v)->kind() != Kind::Voice) {
                continue;
            }
            bool its = !onlyItsVoices;
            for (const Clip::Voice& cv : clip.staves[i].voices) {
                its = its || cv.number == int(m.find(v)->intOr(F::NUMBER, 0));
            }
            if (its) {
                ops.clearRange(tx, v, at, end);
            }
        }
        for (const Clip::Voice& cv : clip.staves[i].voices) {
            const Id voice = ops.voice(tx, target, cv.number);
            // tuplets: made where they fit their target measure
            std::vector<Id> tuplets(cv.tuplets.size());
            for (size_t k = 0; k < cv.tuplets.size(); ++k) {
                const Clip::Tuplet& ct = cv.tuplets[k];
                const Fraction t = at + ct.rel;
                const Fraction len = fieldFraction(ct.fields, F::DURATION);
                const Id measure = ops.measureAt(t);
                if (t + len > end || ops.measureEnd(measure) < t + len || (ct.parent >= 0 && !tuplets[size_t(ct.parent)].isValid())) {
                    ++m_left;
                    continue;   // across a barline here: left out (its place gets rests)
                }
                auto f = ct.fields;
                f.push_back({ F::TIME, t });
                if (ct.parent >= 0) {
                    f.push_back({ F::TUPLET, tuplets[size_t(ct.parent)] });
                }
                tuplets[k] = create(tx, m_ids, voice, { Kind::Tuplet, f }, Placement::ByTime);
            }
            std::vector<Id> events(cv.events.size());
            for (size_t k = 0; k < cv.events.size(); ++k) {
                const Clip::Event& ce = cv.events[k];
                const Fraction t = at + ce.rel;
                if (!(t < end)) {
                    continue;   // past the end of the score
                }
                if (ce.tuplet >= 0 && !tuplets[size_t(ce.tuplet)].isValid()) {
                    continue;
                }
                const bool grace = fieldHas(ce.fields, F::GRACE);
                const Fraction len = fieldFraction(ce.fields, F::DURATION);
                const Fraction mEnd = ops.measureEnd(ops.measureAt(t));
                Id e;
                if (ce.tuplet >= 0 || grace || t + len <= std::min(mEnd, end)) {
                    auto f = ce.fields;
                    f.push_back({ F::TIME, t });
                    if (ce.tuplet >= 0) {
                        f.push_back({ F::TUPLET, tuplets[size_t(ce.tuplet)] });
                    }
                    // by time: after what is already at its time, so grace notes keep the copy's order
                    e = create(tx, m_ids, voice, { ce.rest ? Kind::Rest : Kind::Chord, f }, Placement::ByTime);
                    for (const Clip::Note& cn : ce.notes) {
                        // the written spelling is the target instrument's
                        auto nf = cn.fields;
                        nf.erase(std::remove_if(nf.begin(), nf.end(), [](const auto& p) { return p.first == F::TPC2; }), nf.end());
                        for (const auto& [k2, val] : cn.fields) {
                            if (k2 == F::TPC) {
                                if (const auto w = ops.writtenTpc(target, std::get<int64_t>(val), m.find(e)->fractionOr(F::TIME, Fraction()))) {
                                    nf.push_back({ F::TPC2, *w });
                                }
                            }
                        }
                        const Id n = create(tx, m_ids, e, { Kind::Note, nf });
                        for (const auto& a : cn.attachments) {
                            create(tx, m_ids, n, { Kind::Attachment, a });
                        }
                    }
                } else {
                    // across a barline (or the end): entered as values in this measure, continued tied
                    NoteValue v { fieldFraction(ce.fields, F::BASE_DURATION), 0 };
                    for (const auto& [k2, val] : ce.fields) {
                        if (k2 == F::DOTS) {
                            v.dots = int(std::get<int64_t>(val));
                        }
                    }
                    const Fraction length = std::min(len, end - t);
                    std::vector<int> pitches;
                    for (const Clip::Note& cn : ce.notes) {
                        for (const auto& [k2, val] : cn.fields) {
                            if (k2 == F::PITCH) {
                                pitches.push_back(int(std::get<int64_t>(val)));
                            }
                        }
                    }
                    if (length == len) {
                        e = ce.rest ? ops.enterRest(tx, voice, t, v) : ops.enterChord(tx, voice, t, v, pitches);
                    } else {
                        // cut at the end of the score: the values that fill what is left
                        const auto values = valuesFor(t - ops.measureStart(ops.measureAt(t)), length, ce.rest);
                        Fraction p = t;
                        Id prev;
                        for (const NoteValue& piece : values) {
                            const Id x = ce.rest ? ops.enterRest(tx, voice, p, piece) : ops.enterChord(tx, voice, p, piece, pitches);
                            if (!e.isValid()) {
                                e = x;
                            }
                            if (prev.isValid() && x.isValid() && !ce.rest) {   // the pieces tied
                                Id last = prev;
                                for (int guard = 0; guard < 64; ++guard) {
                                    Id nextPiece;
                                    for (Id n : m.find(last)->children()) {
                                        if (m.find(n)->has(F::TIE_TO)) {
                                            nextPiece = m.find(m.find(n)->idOr(F::TIE_TO))->parent();
                                        }
                                    }
                                    if (!nextPiece.isValid()) {
                                        break;
                                    }
                                    last = nextPiece;
                                }
                                const auto a = m.find(last)->children();
                                const auto b = m.find(x)->children();
                                for (size_t j = 0; j < a.size() && j < b.size(); ++j) {
                                    if (m.find(a[j])->kind() == Kind::Note && m.find(b[j])->kind() == Kind::Note) {
                                        tx.set(a[j], F::TIE_TO, b[j]);
                                    }
                                }
                            }
                            prev = x;
                            p += piece.length();
                        }
                    }
                    if (e.isValid() && !ce.rest) {
                        // the copied spellings on the first piece
                        size_t idx = 0;
                        for (Id n : m.find(e)->children()) {
                            if (m.find(n)->kind() == Kind::Note && idx < ce.notes.size()) {
                                for (const auto& [k2, val] : ce.notes[idx].fields) {
                                    if (k2 == F::TPC) {
                                        tx.set(n, k2, val);
                                        const auto w = ops.writtenTpc(target, std::get<int64_t>(val), m.find(e)->fractionOr(F::TIME, Fraction()));
                                        tx.set(n, F::TPC2, w ? Value(*w) : Value());
                                    }
                                }
                                ++idx;
                            }
                        }
                    }
                }
                if (e.isValid()) {
                    for (const auto& a : ce.attachments) {
                        create(tx, m_ids, e, { Kind::Attachment, a });
                    }
                }
                events[k] = e;
            }
            // ties inside the copy
            for (size_t k = 0; k < cv.events.size(); ++k) {
                if (!events[k].isValid()) {
                    continue;
                }
                std::vector<Id> notes;
                for (Id n : m.find(events[k])->children()) {
                    if (m.find(n)->kind() == Kind::Note) {
                        notes.push_back(n);
                    }
                }
                for (size_t j = 0; j < cv.events[k].notes.size() && j < notes.size(); ++j) {
                    const Clip::Note& cn = cv.events[k].notes[j];
                    if (cn.tieTo < 0 || !events[size_t(cn.tieTo)].isValid()) {
                        continue;
                    }
                    std::vector<Id> to;
                    for (Id n : m.find(events[size_t(cn.tieTo)])->children()) {
                        if (m.find(n)->kind() == Kind::Note) {
                            to.push_back(n);
                        }
                    }
                    // a chord split at a barline: the tie goes from its last piece
                    Id from = notes[j];
                    for (int guard = 0; guard < 64 && m.find(from)->has(F::TIE_TO); ++guard) {
                        from = m.find(from)->idOr(F::TIE_TO);
                    }
                    if (size_t(cn.tieToNote) < to.size() && from != to[size_t(cn.tieToNote)]) {
                        tx.set(from, F::TIE_TO, to[size_t(cn.tieToNote)]);
                    }
                }
            }
            madeEvents[i].push_back(events);
            // voice 1 full again: rests in the gaps
            if (cv.number == 0) {
                Fraction cursor = at;
                for (Id e : ops.eventsOf(voice)) {
                    const Object* o = m.find(e);
                    const Fraction s = o->fractionOr(F::TIME, Fraction());
                    if (o->has(F::GRACE) || s < at || !(s < end)) {
                        continue;
                    }
                    if (cursor < s) {
                        ops.fillGap(tx, voice, cursor, s);
                    }
                    cursor = std::max(cursor, s + o->fractionOr(F::DURATION, Fraction()));
                }
                if (cursor < end) {
                    ops.fillGap(tx, voice, cursor, end);
                }
            }
        }
        // a staff without a voice 1 in the copy still keeps its voice 1 full (unless only the copy's voices were replaced)
        bool hasVoice1 = std::any_of(clip.staves[i].voices.begin(), clip.staves[i].voices.end(), [](const Clip::Voice& v) { return v.number == 0; });
        if (!hasVoice1 && !onlyItsVoices) {
            ops.fillGap(tx, ops.voice(tx, target, 0), at, end);
        }
    }
    // lines and items, on the target staves
    for (const Clip::Item& item : clip.items) {
        if (size_t(item.staff) >= pasted.staves.size()) {
            continue;
        }
        const Fraction t = at + item.rel;
        auto f = item.fields;
        f.push_back({ F::TIME, t });
        if (item.kind == Kind::Spanner && item.fromNote.event >= 0) {
            // between the pasted notes
            auto noteOf = [&](const Clip::Item::NoteEnd& ne) {
                if (size_t(ne.voice) >= madeEvents[size_t(item.staff)].size()) {
                    return Id();
                }
                const auto& evs = madeEvents[size_t(item.staff)][size_t(ne.voice)];
                if (size_t(ne.event) >= evs.size() || !m.find(evs[size_t(ne.event)])) {
                    return Id();
                }
                int i = 0;
                for (Id x : m.find(evs[size_t(ne.event)])->children()) {
                    if (m.find(x)->kind() == Kind::Note && i++ == ne.note) {
                        return x;
                    }
                }
                return Id();
            };
            const Id a = noteOf(item.fromNote), b = noteOf(item.toNote);
            if (a.isValid() && b.isValid()) {
                f.push_back({ F::ANCHOR, a });
                f.push_back({ F::END_ANCHOR, b });
                create(tx, m_ids, m.score(), { Kind::Spanner, f });
            }
            continue;
        }
        if (item.kind == Kind::Spanner) {
            if (t + fieldFraction(item.fields, F::DURATION) > end) {
                continue;
            }
            f.push_back({ F::ANCHOR, pasted.staves[size_t(item.staff)] });
            if (item.startEvent >= 0 && size_t(item.startVoice) < madeEvents[size_t(item.staff)].size()) {
                const auto& evs = madeEvents[size_t(item.staff)][size_t(item.startVoice)];
                if (size_t(item.startEvent) < evs.size() && evs[size_t(item.startEvent)].isValid()) {
                    f.push_back({ F::START_EVENT, evs[size_t(item.startEvent)] });
                }
            }
            create(tx, m_ids, m.score(), { Kind::Spanner, f });
        } else {
            if (!(t < end)) {
                continue;
            }
            f.push_back({ F::ANCHOR, pasted.staves[size_t(item.staff)] });
            const Id flow = m.find(ms.front())->parent();
            create(tx, m_ids, flow, { Kind::FlowItem, f }, Placement::ByTime);
        }
    }
    return pasted;
}

void Clipboard::erase(Transaction& tx, const RangeSelection& selection)
{
    const RangeSelection r = whole(selection);
    for (Id staff : r.staves) {
        for (Id v : m.find(staff)->children()) {
            if (m.find(v)->kind() != Kind::Voice) {
                continue;
            }
            ops.clearRange(tx, v, r.start, r.end);
            if (m.find(v)->intOr(F::NUMBER, 0) == 0) {
                ops.fillGap(tx, v, r.start, r.end);
            }
        }
    }
    // items on these staves in the range go too
    std::vector<Id> gone;
    for (Id c : m.find(m.score())->children()) {
        const Object* f = m.find(c);
        if (f->kind() != Kind::Flow) {
            continue;
        }
        for (Id x : f->children()) {
            const Object* it = m.find(x);
            const Fraction t = it->fractionOr(F::TIME, Fraction());
            if (it->kind() == Kind::FlowItem && std::find(r.staves.begin(), r.staves.end(), it->idOr(F::ANCHOR)) != r.staves.end()
                && r.start <= t && t < r.end) {
                gone.push_back(x);
            }
        }
    }
    for (Id x : gone) {
        removeTree(tx, x);
    }
}
}
