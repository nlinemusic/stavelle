/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle engine (je::edit), based on MuseScore Studio's lyric entry (see lyrics.h), Copyright (C) MuseScore
 * Limited and others, GPL-3.0-only. The steps follow MuseScore's functions in order; MuseScore's "segments" are the
 * times at which any staff has a chord or rest, its "tracks" a staff's voices.
 */
#include "lyrics.h"

#include <algorithm>
#include <set>

#include "structure.h"
#include "model/fields.h"
#include "model/fragment.h"
#include "model/model.h"
#include "model/packing.h"

namespace F = je::model::fields;
using je::model::Kind;
using je::model::Object;
using je::model::Text;

namespace je::edit {
namespace {
const Fraction EPS(1, 1920);   // MuseScore's Fraction::eps() and Lyrics::TEMP_MELISMA_TICKS
}

LyricsEntry::LyricsEntry(Operations& ops)
    : m_ops(ops)
{
    const Object* score = m_ops.obj(m_ops.m.score());
    if (score && score->has(F::STYLE)) {
        for (const auto& [name, value] : model::unpackPairs(score->text(F::STYLE))) {
            if (name == "lyricsPlacement") {
                m_stylePlacement = value;
            }
        }
    }
}

// ---- the music, as segments and tracks ---------------------------------------------------------------------------
void LyricsEntry::scan()
{
    // the chords and rests don't change during lyric entry: read once per entry session
    if (m_scanned) {
        return;
    }
    m_scanned = true;
    m_tracks.clear();
    m_voices.clear();
    m_trackOf.clear();
    std::set<Fraction> times;
    std::vector<Id> voices;
    m_ops.m.snapshot().forEachObject([&](const Object& o) {
        if (o.kind() == Kind::Voice) {
            voices.push_back(o.id());
        }
    });
    for (Id v : voices) {
        const Object* vo = m_ops.obj(v);
        const Id staff = vo->parent();
        const int number = int(vo->intOr(F::NUMBER, 0));
        m_voices[staff].push_back(number);
        auto& track = m_tracks[{ staff, number }];
        for (Id e : m_ops.eventsOf(v)) {
            const Object* eo = m_ops.obj(e);
            if (eo->has(F::GRACE)) {
                continue;
            }
            const Fraction t = eo->fractionOr(F::TIME, Fraction());
            track[t] = e;
            m_trackOf[e] = { staff, number };
            times.insert(t);
        }
    }
    for (auto& [staff, numbers] : m_voices) {
        std::sort(numbers.begin(), numbers.end());
    }
    m_times.assign(times.begin(), times.end());
    // the staves in score order (players, then their staves): MuseScore's tracks
    m_staffOrder.clear();
    if (const Object* score = m_ops.obj(m_ops.m.score())) {
        for (Id p : score->children()) {
            const Object* player = m_ops.obj(p);
            if (player && player->kind() == Kind::Player) {
                for (Id st : player->children()) {
                    if (m_ops.obj(st)->kind() == Kind::Staff) {
                        m_staffOrder.push_back(st);
                    }
                }
            }
        }
    }
    m_repeats = perform::RepeatList::build(m_ops.m.snapshot());
    const std::vector<Id> ms = m_ops.measures();
    m_end = ms.empty() ? Fraction() : m_ops.measureEnd(ms.back());
}

Id LyricsEntry::element(const Fraction& t, Id staff, int voice) const
{
    auto it = m_tracks.find({ staff, voice });
    if (it == m_tracks.end()) {
        return Id();
    }
    auto e = it->second.find(t);
    return e == it->second.end() ? Id() : e->second;
}

bool LyricsEntry::isChord(Id event) const
{
    const Object* o = m_ops.obj(event);
    return o && o->kind() == Kind::Chord;
}

//! Segment::next1(ChordRest): the next time any staff has a chord or rest
bool LyricsEntry::next1(Fraction& t) const
{
    auto it = std::upper_bound(m_times.begin(), m_times.end(), t);
    if (it == m_times.end()) {
        return false;
    }
    t = *it;
    return true;
}

bool LyricsEntry::prev1(Fraction& t) const
{
    auto it = std::lower_bound(m_times.begin(), m_times.end(), t);
    if (it == m_times.begin()) {
        return false;
    }
    t = *--it;
    return true;
}

//! Segment::nextCR: the next segment with an element in the voice (any voice of the staff: sameStaff; any at
//! all: voice -1)
bool LyricsEntry::nextCR(Fraction& t, Id staff, int voice, bool sameStaff) const
{
    Fraction s = t;
    while (next1(s)) {
        if (voice < 0) {
            t = s;
            return true;
        }
        if (sameStaff) {
            auto vs = m_voices.find(staff);
            if (vs != m_voices.end()) {
                for (int v : vs->second) {
                    if (element(s, staff, v).isValid()) {
                        t = s;
                        return true;
                    }
                }
            }
        } else if (element(s, staff, voice).isValid()) {
            t = s;
            return true;
        }
    }
    return false;
}

Fraction LyricsEntry::endOf(Id event) const
{
    return m_ops.endOf(event);
}

//! ChordRest::ticks(): the written length (a tuplet's notes without its ratio)
Fraction LyricsEntry::nominal(Id event) const
{
    const Object* o = m_ops.obj(event);
    const Fraction actual = o->fractionOr(F::DURATION, Fraction());
    return o->has(F::TUPLET) ? actual / m_ops.tupletFactor(o->idOr(F::TUPLET)) : actual;
}

size_t LyricsEntry::measureOf(const Fraction& t) const
{
    return m_repeats.measureAt(t);
}

Id LyricsEntry::lastChordRest(size_t measure, Id staff, int voice) const
{
    auto it = m_tracks.find({ staff, voice });
    if (it == m_tracks.end() || measure == size_t(-1)) {
        return Id();
    }
    const Id m = m_repeats.measures()[measure];
    const Fraction a = m_ops.measureStart(m), b = m_ops.measureEnd(m);
    auto e = it->second.lower_bound(b);
    if (e == it->second.begin()) {
        return Id();
    }
    --e;
    return e->first >= a ? e->second : Id();
}

//! Score::findChordRestEndingBeforeTickInTrack: in the measure before t, the last chord or rest of the voice that
//! ends by t
Id LyricsEntry::endingBefore(const Fraction& t, Id staff, int voice) const
{
    const size_t m = measureOf(t - EPS);
    auto it = m_tracks.find({ staff, voice });
    if (m == size_t(-1) || it == m_tracks.end()) {
        return Id();
    }
    const Id mid = m_repeats.measures()[m];
    const Fraction a = m_ops.measureStart(mid);
    auto e = it->second.lower_bound(m_ops.measureEnd(mid));
    while (e != it->second.begin()) {
        --e;
        if (e->first < a) {
            break;
        }
        if (endOf(e->second) <= t) {
            return e->second;
        }
    }
    return Id();
}

//! segmentsAreAdjacent (dom/utils.cpp)
bool LyricsEntry::adjacent(const Fraction& a, const Fraction& b) const
{
    return m_repeats.adjacent(measureOf(a), measureOf(b));
}

bool LyricsEntry::hasFollowingJumpItem(Id event) const
{
    if (!event.isValid()) {
        return false;
    }
    const size_t m = measureOf(m_ops.timeOf(event));
    if (m == size_t(-1) || endOf(event) != m_ops.measureEnd(m_repeats.measures()[m])) {
        return false;
    }
    return !m_repeats.followingMeasures(m).empty();
}

bool LyricsEntry::hasPrecedingJumpItem(Id event) const
{
    const size_t m = measureOf(m_ops.timeOf(event));
    if (m == size_t(-1) || m_ops.timeOf(event) != m_ops.measureStart(m_repeats.measures()[m])) {
        return false;
    }
    return !m_repeats.precedingMeasures(m).empty();
}

// ---- lyrics ------------------------------------------------------------------------------------------------------
bool LyricsEntry::isAbove(const Object& lyric) const
{
    const std::string p = model::fragment::value(lyric.text(F::DETAILS), "placement");
    if (!p.empty()) {
        return p == "above";
    }
    return m_stylePlacement == "0" || m_stylePlacement == "above";
}

LyricsEntry::Where LyricsEntry::whereOf(Id lyric) const
{
    Where w;
    const Object* l = m_ops.obj(lyric);
    if (!l) {
        return w;
    }
    w.lyric = lyric;
    w.event = l->parent();
    auto t = m_trackOf.find(w.event);
    if (t != m_trackOf.end()) {
        w.staff = t->second.first;
        w.voice = t->second.second;
    }
    // a lyric made in another voice's chord keeps the voice it was typed in (MuseScore's lyric track) while editing
    if (auto v = m_lyricVoice.find(lyric); v != m_lyricVoice.end()) {
        w.voice = v->second;
    }
    w.verse = int(l->intOr(F::VERSE, 0));
    w.above = isAbove(*l);
    w.time = m_ops.timeOf(w.event);
    return w;
}

Id LyricsEntry::lyricOf(Id event, int verse, bool above) const
{
    const Object* e = m_ops.obj(event);
    if (!e) {
        return Id();
    }
    for (Id c : e->children()) {
        const Object* a = m_ops.obj(c);
        if (a->kind() == Kind::Attachment && a->text(F::SUBKIND) == "lyric" && a->intOr(F::VERSE, 0) == verse
            && isAbove(*a) == above) {
            return c;
        }
    }
    return Id();
}

LyricsEntry::Syllabic LyricsEntry::syllabic(Id lyric) const
{
    const std::string& s = m_ops.obj(lyric)->text(F::SYLLABIC);
    return s == "begin" ? Syllabic::Begin : s == "middle" ? Syllabic::Middle : s == "end" ? Syllabic::End : Syllabic::Single;
}

Fraction LyricsEntry::ticks(Id lyric) const
{
    return m_ops.obj(lyric)->fractionOr(F::DURATION, Fraction());
}

void LyricsEntry::setSyllabic(Transaction& tx, Id lyric, Syllabic s)
{
    if (syllabic(lyric) == s) {
        return;
    }
    static const char* names[] = { "single", "begin", "middle", "end" };
    tx.set(lyric, F::SYLLABIC, s == Syllabic::Single ? Value() : Value(Text(std::string(names[int(s)]))));
}

void LyricsEntry::setTicks(Transaction& tx, Id lyric, const Fraction& t, bool asProperty)
{
    if (ticks(lyric) == t) {
        return;
    }
    tx.set(lyric, F::DURATION, t.isZero() ? Value() : Value(t));
    if (asProperty) {
        ticksChanged(lyric);
    }
}

//! Lyrics::setProperty(LYRIC_TICKS): a line no longer than its own chord is checked at the next layout
void LyricsEntry::ticksChanged(Id lyric)
{
    const Where w = whereOf(lyric);
    const Id cr = findCR(w.time, w.staff, w.voice);
    if (cr.isValid() && ticks(lyric) <= nominal(cr)) {
        flagInvalidSegments(lyric);
    }
}

//! Score::findCR: the voice's chord or rest at or before t, in t's measure
Id LyricsEntry::findCR(const Fraction& t, Id staff, int voice) const
{
    const size_t m = measureOf(t);
    auto it = m_tracks.find({ staff, voice });
    if (m == size_t(-1) || it == m_tracks.end()) {
        return Id();
    }
    const Fraction a = m_ops.measureStart(m_repeats.measures()[m]);
    auto e = it->second.upper_bound(t);
    if (e == it->second.begin()) {
        return Id();
    }
    --e;
    return e->first >= a ? e->second : Id();
}

//! A new lyric, set up as MuseScore sets one up from the lyric `like` (placement and font copied; the text style
//! copied, or the verse's own with styleByVerse)
Id LyricsEntry::makeLyric(Transaction& tx, Id event, int verse, Syllabic s, const Where& like, bool styleByVerse)
{
    std::vector<std::pair<std::string, std::string> > values;
    if (const Object* l = m_ops.obj(like.lyric)) {
        const std::string& d = l->text(F::DETAILS);
        for (const char* tag : { "placement", "fontStyle" }) {
            const std::string v = model::fragment::value(d, tag);
            if (!v.empty()) {
                values.push_back({ tag, v });
            }
        }
        if (!styleByVerse) {
            // the text style: the lyric's own, or its verse's default (odd and even lines); written when it isn't the
            // new verse's default
            std::string style = model::fragment::value(d, "style");
            if (style.empty()) {
                style = like.verse % 2 ? "lyrics_even" : "lyrics_odd";
            }
            if (style != (verse % 2 ? "lyrics_even" : "lyrics_odd")) {
                values.push_back({ "style", style });
            }
        }
    } else if (like.above != (m_stylePlacement == "0" || m_stylePlacement == "above")) {
        values.push_back({ "placement", like.above ? "above" : "below" });
    }
    std::vector<std::pair<Field, Value> > f { { F::SUBKIND, Text(std::string("lyric")) }, { F::VERSE, int64_t(verse) },
                                              { F::TEXT, Text(std::string()) } };
    if (s != Syllabic::Single) {
        static const char* names[] = { "single", "begin", "middle", "end" };
        f.push_back({ F::SYLLABIC, Text(std::string(names[int(s)])) });
    }
    if (!values.empty()) {
        f.push_back({ F::DETAILS, Text(model::fragment::withValues(std::string(), values)) });
    }
    const Id made = create(tx, m_ops.m_ids, event, { Kind::Attachment, f });
    auto t = m_trackOf.find(event);
    if (t != m_trackOf.end() && t->second.second != like.voice) {
        m_lyricVoice[made] = like.voice;
    }
    return made;
}

//! prevLyrics (dom/navigate.cpp): the lyric of the verse on the nearest earlier chord of the staff, in any voice
Id LyricsEntry::prevLyrics(const Where& w) const
{
    Fraction t = w.time;
    auto vs = m_voices.find(w.staff);
    if (vs == m_voices.end()) {
        return Id();
    }
    while (prev1(t)) {
        for (int v : vs->second) {
            const Id e = element(t, w.staff, v);
            if (e.isValid() && isChord(e)) {
                if (Id l = lyricOf(e, w.verse, w.above); l.isValid()) {
                    return l;
                }
            }
        }
    }
    return Id();
}

//! Lyrics::isMelisma: an extension line, or a dash with no syllable of the verse on the next chord of the staff
bool LyricsEntry::isMelisma(Id lyric) const
{
    if (ticks(lyric) > Fraction()) {
        return true;
    }
    const Syllabic s = syllabic(lyric);
    const auto sep = m_separators.find(lyric);
    if (sep == m_separators.end() || !sep->second.exists || (s != Syllabic::Begin && s != Syllabic::Middle)) {
        return false;
    }
    const Where w = whereOf(lyric);
    auto nextOn = [&](int v) {
        auto it = m_tracks.find({ w.staff, v });
        if (it == m_tracks.end()) {
            return Id();
        }
        auto e = it->second.upper_bound(w.time);
        return e == it->second.end() ? Id() : e->second;
    };
    const Id lyrVoiceNext = nextOn(w.voice);
    auto vs = m_voices.find(w.staff);
    for (int v : vs->second) {
        const Id trackNext = nextOn(v);
        if (!trackNext.isValid()) {
            continue;
        }
        if (v != w.voice && lyrVoiceNext.isValid() && !lyricOf(lyrVoiceNext, w.verse, w.above).isValid()
            && m_ops.timeOf(lyrVoiceNext) < m_ops.timeOf(trackNext)) {
            return true;   // a note in another voice in between: a melisma
        }
        if (lyricOf(trackNext, w.verse, w.above).isValid()) {
            return false;   // the next note has a syllable: just a dash
        }
    }
    return true;
}

//! Lyrics::setNeedRemoveInvalidSegments: checked at the next layout, unless its line ends on the measure's last
//! chord before a repeat (a dash that continues after the repeat)
void LyricsEntry::flagInvalidSegments(Id lyric)
{
    const Where w = whereOf(lyric);
    const auto sep = m_separators.find(lyric);
    const Id endChord = sep != m_separators.end() && sep->second.exists ? sep->second.endEvent : Id();
    const Id last = lastChordRest(measureOf(w.time), w.staff, w.voice);
    if (endChord == last && hasFollowingJumpItem(last)) {
        return;
    }
    m_flagged.push_back(lyric);
}

// ---- partial lines ----------------------------------------------------------------------------------------------
namespace {
struct Partial {
    Id staff;
    int voice = 0, verse = 0;
    bool melisma = false, above = false;
    Fraction time, end;
};

Partial partialOf(const Object& o)
{
    Partial p;
    const std::string& d = o.text(F::DETAILS);
    p.staff = o.idOr(F::ANCHOR);
    p.voice = int(o.intOr(F::VOICE_INDEX, 0));
    p.verse = std::atoi(model::fragment::value(d, "no").c_str());
    const std::string m = model::fragment::value(d, "isEndMelisma");
    p.melisma = m == "1" || m == "true";
    p.above = model::fragment::value(d, "placement") == "above";
    p.time = o.fractionOr(F::TIME, Fraction());
    p.end = p.time + o.fractionOr(F::DURATION, Fraction());
    return p;
}

template<typename Fn>
void forEachPartial(const Operations& ops, const model::Model& m, Fn&& fn)
{
    (void)ops;
    const Object* score = m.find(m.score());
    for (Id c : score->children()) {
        const Object* o = m.find(c);
        if (o && o->kind() == Kind::Spanner && o->text(F::SUBKIND) == "partialLyricsLine") {
            if (fn(c, partialOf(*o))) {
                return;
            }
        }
    }
}
}

//! findPrevPartialLyricsLineDash: a partial dash of the verse over the lyric's time, in its voice
Id LyricsEntry::prevPartialDash(const Where& w) const
{
    Id found;
    forEachPartial(m_ops, m_ops.m, [&](Id id, const Partial& p) {
        if (p.time <= w.time && w.time <= p.end && p.staff == w.staff && p.voice == w.voice && !p.melisma && p.verse == w.verse
            && p.above == w.above) {
            found = id;
            return true;
        }
        return false;
    });
    return found;
}

//! A partial extension line of the verse over the event (addMelisma), in any voice of its staff
Id LyricsEntry::partialMelismaAt(Id event, int verse, bool above) const
{
    const auto t = m_trackOf.find(event);
    if (t == m_trackOf.end()) {
        return Id();
    }
    const Fraction a = m_ops.timeOf(event), b = endOf(event);
    Id found;
    forEachPartial(m_ops, m_ops.m, [&](Id id, const Partial& p) {
        if (p.time <= b && a <= p.end && p.staff == t->second.first && p.verse == verse && p.above == above && p.melisma) {
            found = id;
            return true;
        }
        return false;
    });
    return found;
}

Id LyricsEntry::makePartial(Transaction& tx, const Where& at, Id event, bool melisma, const Fraction& length)
{
    const auto t = m_trackOf.at(event);
    std::vector<std::pair<std::string, std::string> > values;
    if (at.verse != 0) {
        values.push_back({ "no", std::to_string(at.verse) });
    }
    values.push_back({ "isEndMelisma", melisma ? "1" : "0" });
    if (at.above) {
        values.push_back({ "placement", "above" });
    }
    int64_t seq = 0;
    m_ops.m.snapshot().forEachObject([&](const Object& o) { seq = std::max(seq, o.intOr(F::SEQ, 0)); });
    return create(tx, m_ops.m_ids, m_ops.m.score(),
                  { Kind::Spanner, { { F::SUBKIND, Text(std::string("partialLyricsLine")) }, { F::ANCHOR, t.first },
                                     { F::TIME, m_ops.timeOf(event) }, { F::DURATION, length },
                                     { F::VOICE_INDEX, int64_t(t.second) }, { F::SEQ, seq + 1 },
                                     { F::DETAILS, Text(model::fragment::withValues(std::string(), values)) } } });
}

void LyricsEntry::moveEnd(Transaction& tx, Id partial, const Fraction& delta)
{
    const Object* o = m_ops.obj(partial);
    const Fraction d = o->fractionOr(F::DURATION, Fraction()) + delta;
    tx.set(partial, F::DURATION, d < Fraction() ? Fraction() : d);
}

// ---- the edit session --------------------------------------------------------------------------------------------
void LyricsEntry::startEditing(Id lyric, bool newlyAdded, size_t creationMark, Transaction& tx)
{
    m_editing = true;
    m_cur = whereOf(lyric);
    m_newlyAdded = newlyAdded;
    m_text = m_ops.obj(lyric)->text(F::TEXT);
    m_creation.clear();
    if (newlyAdded) {
        const auto& records = tx.changes().records;
        m_creation.assign(records.begin() + std::ptrdiff_t(creationMark), records.end());
    }
}

void LyricsEntry::setText(const std::string& text)
{
    m_text = text;
}

//! MuseScore's endEdit for the step that made the lyric (the "add element" command rolled back): what it created
//! removed, what it changed set back
void LyricsEntry::rollback(Transaction& tx)
{
    std::set<Id> created;
    for (const ChangeRecord& r : m_creation) {
        if (r.field == F::EXISTS && std::holds_alternative<bool>(r.after) && !std::holds_alternative<bool>(r.before)) {
            created.insert(r.object);
        }
    }
    for (auto it = m_creation.rbegin(); it != m_creation.rend(); ++it) {
        if (created.count(it->object) || it->field == F::EXISTS || it->field == F::PARENT || it->field == F::CHILDREN
            || it->field == F::KIND) {
            continue;
        }
        if (m_ops.obj(it->object)) {
            tx.set(it->object, it->field, it->before);
            if (it->field == F::DURATION && m_ops.obj(it->object)->text(F::SUBKIND) == "lyric") {
                ticksChanged(it->object);   // undone as a property change
            }
        }
    }
    for (Id id : created) {
        if (m_ops.obj(id)) {
            removeTree(tx, id);
        }
    }
    m_creation.clear();
}

//! TextBase::endEdit for a lyric: the typed text committed (and Lyrics::adjustPrevious), or the lyric removed when
//! it was left empty
void LyricsEntry::endEditText(Transaction& tx)
{
    if (!m_editing) {
        return;
    }
    m_editing = false;
    const Where w = m_cur;
    // plain text: tags aside
    std::string plain;
    bool inTag = false;
    for (char c : m_text) {
        if (c == '<') {
            inTag = true;
        } else if (c == '>') {
            inTag = false;
        } else if (!inTag) {
            plain += c;
        }
    }
    if (!plain.empty() && m_ops.obj(w.lyric)) {
        if (m_ops.obj(w.lyric)->text(F::TEXT) != m_text) {
            tx.set(w.lyric, F::TEXT, Text(m_text));
        }
        // Lyrics::adjustPrevious: an extension line of the previous syllable that reaches this one stops before it
        if (const Id prev = prevLyrics(w); prev.isValid()) {
            const Fraction pt = m_ops.timeOf(m_ops.obj(prev)->parent());
            if (pt + ticks(prev) >= w.time) {
                Fraction s = w.time;
                if (prev1(s)) {
                    setTicks(tx, prev, s > pt ? s - pt : EPS);
                }
                flagInvalidSegments(prev);
            }
        }
        m_creation.clear();
        layout(tx);
        return;
    }
    // empty: removed (a lyric made for typing: the step that made it is taken back)
    if (m_newlyAdded) {
        rollback(tx);
    } else if (m_ops.obj(w.lyric)) {
        if (const Id dash = prevPartialDash(w); dash.isValid()) {
            removeTree(tx, dash);
        }
        removeTree(tx, w.lyric);
    }
    if (const Id prev = prevLyrics(w); prev.isValid()) {
        flagInvalidSegments(prev);
    }
    layout(tx);
}

void LyricsEntry::finish(Transaction& tx)
{
    if (m_editing) {
        scan();
        endEditText(tx);
    }
}

//! What MuseScore's layout changes in the lyrics (LyricsLayout::createOrRemoveLyricsLine): flagged syllables lose a
//! line that ends on their own chord (Lyrics::removeInvalidSegments), extension lines that don't end on a chord of
//! their voice are shortened ("FIXUP"), and each lyric's dash or line is noted for the next step
void LyricsEntry::layout(Transaction& tx)
{
    std::set<Id> flagged(m_flagged.begin(), m_flagged.end());
    m_flagged.clear();
    // the whole score the first time (as it was laid out when opened), then the staves an edit can change: lyrics
    // only look at their own staff
    std::set<Id> staves;
    if (m_laidOut) {
        staves.insert(m_cur.staff);
        for (Id l : flagged) {
            if (m_ops.obj(l)) {
                staves.insert(whereOf(l).staff);
            }
        }
    }
    std::map<Id, Separator> separators;
    for (auto it = m_separators.begin(); it != m_separators.end(); ++it) {
        const auto t = m_ops.obj(it->first) ? m_trackOf.find(m_ops.obj(it->first)->parent()) : m_trackOf.end();
        if (m_laidOut && t != m_trackOf.end() && !staves.count(t->second.first)) {
            separators.insert(*it);   // kept: a staff not laid out again
        }
    }
    for (const auto& [key, events] : m_tracks) {
        const Id staff = key.first;
        if (m_laidOut && !staves.count(staff)) {
            continue;
        }
        const int eventVoice = key.second;
        for (const auto& [t, event] : events) {
            const Object* e = m_ops.obj(event);
            if (!e) {
                continue;
            }
            for (Id c : std::vector<Id>(e->children().ids().begin(), e->children().ids().end())) {
                const Object* a = m_ops.obj(c);
                if (!a || a->kind() != Kind::Attachment || a->text(F::SUBKIND) != "lyric") {
                    continue;
                }
                const Id lyric = c;
                const int voice = m_lyricVoice.count(lyric) ? m_lyricVoice[lyric] : eventVoice;
                if (flagged.count(lyric) && m_separators.count(lyric) && m_separators[lyric].exists && isMelisma(lyric)
                    && ticks(lyric) < nominal(event)) {
                    setTicks(tx, lyric, Fraction(), false);
                    const Syllabic s = syllabic(lyric);
                    setSyllabic(tx, lyric, s == Syllabic::Begin || s == Syllabic::Single ? Syllabic::Single : Syllabic::End);
                }
                Fraction lineEnd = t + EPS;
                bool haveLineEnd = false;
                if (!ticks(lyric).isZero()) {
                    const Fraction startTick = t;
                    const Fraction itemEndTick = ticks(lyric) == EPS ? startTick : startTick + ticks(lyric);
                    Fraction endTick = itemEndTick;
                    Fraction endSeg = startTick;
                    bool have = true;
                    while (have && endSeg < endTick) {
                        have = nextCR(endSeg, staff, voice, true);
                    }
                    bool atEnd = false;   // MuseScore's last segment (the end of the score)
                    if (!have) {
                        endSeg = m_end;
                        atEnd = true;
                        endTick = Fraction(-1, 1);
                    }
                    const Id endEl = atEnd ? Id() : element(endSeg, staff, voice);
                    bool haveEnd = true;
                    if (endSeg == endTick && endEl.isValid() && isChord(endEl)) {
                        const Fraction endChordSeg = endSeg;
                        const Fraction chordEnd = endOf(endEl);
                        haveEnd = nextCR(endSeg, staff, voice, false);
                        if (!haveEnd || endSeg > chordEnd) {
                            endSeg = endChordSeg;
                            haveEnd = true;
                            while (haveEnd && endSeg < chordEnd) {
                                haveEnd = nextCR(endSeg, staff, -1, true);
                            }
                        }
                    } else {
                        // FIXUP: the end isn't a chord of this voice any more
                        const Fraction ns = endSeg;
                        Fraction ps = endSeg;
                        bool havePs = atEnd ? !m_times.empty() : prev1(ps);
                        if (atEnd && havePs) {
                            ps = m_times.back();
                        }
                        while (havePs && ps != startTick) {
                            const Id pe = element(ps, staff, voice);
                            if (pe.isValid() && isChord(pe)) {
                                break;
                            }
                            endSeg = ps;
                            atEnd = false;
                            havePs = prev1(ps);
                        }
                        if (!havePs || ps == startTick) {
                            ps = ns;
                            endSeg = ps;
                            haveEnd = !(atEnd && ns == m_end) && nextCR(endSeg, staff, voice, false);
                            const Id ee = haveEnd ? element(endSeg, staff, voice) : Id();
                            if (!ee.isValid() || !isChord(ee) || ps > itemEndTick) {
                                setTicks(tx, lyric, Fraction());
                            } else {
                                setTicks(tx, lyric, ps - startTick);
                            }
                        } else {
                            setTicks(tx, lyric, ps - startTick);
                        }
                    }
                    lineEnd = haveEnd ? endSeg : m_end;
                    haveLineEnd = true;
                }
                const Syllabic s = syllabic(lyric);
                Separator sep;
                sep.exists = !ticks(lyric).isZero() || s == Syllabic::Begin || s == Syllabic::Middle;
                if (sep.exists && ticks(lyric).isZero()) {
                    // a dash ends on the next syllable of the verse (LyricsLayout::layout(LyricsLine),
                    // searchNextLyrics), or on the measure's last chord or rest before a repeat, or on its own chord
                    const Where w = whereOf(lyric);
                    Fraction nt = t;
                    while (next1(nt) && adjacent(t, nt)) {
                        for (int v = 0; v < 4 && !sep.endEvent.isValid(); ++v) {
                            const Id cr = element(nt, staff, v);
                            if (cr.isValid() && lyricOf(cr, w.verse, w.above).isValid()) {
                                sep.endEvent = cr;
                            }
                        }
                        if (sep.endEvent.isValid()) {
                            break;
                        }
                    }
                    if (!sep.endEvent.isValid()) {
                        Id endCR;   // the first chord or rest (any staff) of the measure's last segment
                        const size_t mi = measureOf(t);
                        if (mi != size_t(-1)) {
                            const Id mid = m_repeats.measures()[mi];
                            auto it = std::lower_bound(m_times.begin(), m_times.end(), m_ops.measureEnd(mid));
                            if (it != m_times.begin() && *(it - 1) >= m_ops.measureStart(mid)) {
                                const Fraction last = *(it - 1);
                                for (Id st : m_staffOrder) {
                                    for (int v = 0; v < 4 && !endCR.isValid(); ++v) {
                                        endCR = element(last, st, v);
                                    }
                                    if (endCR.isValid()) {
                                        break;
                                    }
                                }
                            }
                        }
                        const Fraction end = endCR.isValid() && hasFollowingJumpItem(endCR) ? m_ops.timeOf(endCR) : t;
                        sep.endEvent = element(end, staff, voice);
                        for (int v = 0; v < 4 && !sep.endEvent.isValid(); ++v) {
                            sep.endEvent = element(end, staff, v);
                        }
                    }
                }
                if (sep.exists && haveLineEnd && !ticks(lyric).isZero()) {
                    // an extension line ends on the last chord or rest that ends by its end, in the measure before it
                    // (LyricsLine::doComputeEndElement; a dash's end is no chord or rest)
                    sep.endEvent = endingBefore(lineEnd, staff, voice);
                    if (!sep.endEvent.isValid()) {
                        for (int v = 0; v < 4 && !sep.endEvent.isValid(); ++v) {
                            sep.endEvent = endingBefore(lineEnd, staff, v);
                        }
                    }
                }
                separators[lyric] = sep;
            }
        }
    }
    m_separators = separators;
    m_laidOut = true;
}

// ---- the commands -------------------------------------------------------------------------------------------------
bool LyricsEntry::start(Transaction& tx, Id event)
{
    scan();
    if (m_editing) {
        endEditText(tx);
    }
    const Object* e = m_ops.obj(event);
    if (!e || (e->kind() != Kind::Chord && e->kind() != Kind::Rest) || e->text(F::SUBKIND) == "measureRepeat") {
        return false;   // MuseScore adds lyrics to a note or a rest only (not a measure repeat)
    }
    if (e->has(F::GRACE)) {
        // a grace note's lyric goes on its main chord: the chord of its voice at its time
        const Object* v = m_ops.obj(e->parent());
        Id main;
        for (Id c : v->children()) {
            const Object* o = m_ops.obj(c);
            if ((o->kind() == Kind::Chord || o->kind() == Kind::Rest) && !o->has(F::GRACE)
                && o->fractionOr(F::TIME, Fraction()) == e->fractionOr(F::TIME, Fraction())) {
                main = c;
            }
        }
        if (!main.isValid()) {
            return false;
        }
        event = main;
        e = m_ops.obj(event);
    }
    if (!m_laidOut) {
        layout(tx);   // as the score was laid out when it was opened
    }
    // the next verse: after the event's lyrics, and after partial lines that start there
    int no = 0;
    for (Id c : e->children()) {
        const Object* a = m_ops.obj(c);
        no += a->kind() == Kind::Attachment && a->text(F::SUBKIND) == "lyric";
    }
    const Fraction t = m_ops.timeOf(event);
    forEachPartial(m_ops, m_ops.m, [&](Id, const Partial& p) {
        if (p.time == t) {
            no = std::max(no, p.verse + 1);
        }
        return false;
    });
    const size_t mark = tx.changes().records.size();
    Where like;
    like.above = m_stylePlacement == "0" || m_stylePlacement == "above";
    like.voice = m_trackOf.count(event) ? m_trackOf[event].second : 0;
    const Id lyric = makeLyric(tx, event, no, Syllabic::Single, like, true);
    layout(tx);
    startEditing(lyric, true, mark, tx);
    return true;
}

bool LyricsEntry::edit(Id lyric)
{
    const Object* l = m_ops.obj(lyric);
    if (!l || l->text(F::SUBKIND) != "lyric") {
        return false;
    }
    scan();
    m_editing = true;
    m_cur = whereOf(lyric);
    m_newlyAdded = false;
    m_text = l->text(F::TEXT);
    m_creation.clear();
    return true;
}

bool LyricsEntry::nextWord(Transaction& tx, bool back, bool moveOnly)
{
    if (!m_editing) {
        return false;
    }
    scan();
    if (!m_laidOut) {
        layout(tx);
    }
    const Where w = m_cur;
    Fraction next = w.time;
    bool found = false;
    while (back ? prev1(next) : next1(next)) {
        const Id el = element(next, w.staff, w.voice);
        if (!el.isValid()) {
            continue;
        }
        if (isChord(el) || lyricOf(el, w.verse, w.above).isValid()) {
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }
    endEditText(tx);

    // the lyric we are moving from: this one, or an earlier one when several chords are skipped with spaces
    Id from;
    if (!back) {
        Fraction seg = w.time;
        bool have = true;
        while (have) {
            const Id cr = element(seg, w.staff, w.voice);
            if (cr.isValid()) {
                from = lyricOf(cr, w.verse, w.above);
                if (from.isValid()) {
                    break;
                }
            }
            have = prev1(seg);
        }
    }
    const Id cr = element(next, w.staff, w.voice);
    Id to = lyricOf(cr, w.verse, w.above);
    const bool newLyrics = !to.isValid();
    const size_t mark = tx.changes().records.size();
    if (newLyrics) {
        to = makeLyric(tx, cr, w.verse, Syllabic::Single, w, false);
    }
    if (from.isValid() && !moveOnly) {
        // arrived by a space: it can start a word, but has no dash before it
        if (syllabic(to) == Syllabic::End) {
            setSyllabic(tx, to, Syllabic::Single);
        } else if (syllabic(to) == Syllabic::Middle) {
            setSyllabic(tx, to, Syllabic::Begin);
        }
        // left by a space: it can end a word, but has no dash after it
        if (syllabic(from) == Syllabic::Begin) {
            setSyllabic(tx, from, Syllabic::Single);
        } else if (syllabic(from) == Syllabic::Middle) {
            setSyllabic(tx, from, Syllabic::End);
        }
    }
    layout(tx);
    startEditing(to, newLyrics, mark, tx);
    return true;
}

bool LyricsEntry::nextSyllable(Transaction& tx)
{
    if (!m_editing) {
        return false;
    }
    scan();
    if (!m_laidOut) {
        layout(tx);
    }
    const Where w = m_cur;
    const Id initialCR = w.event;
    const bool hasPrecedingRepeat = hasPrecedingJumpItem(initialCR);
    const bool hasFollowingRepeat = hasFollowingJumpItem(initialCR);
    int toLyricVoice = w.voice;
    const std::vector<int>& voices = m_voices[w.staff];

    // the next chord: of this voice, or of another voice that has a lyric of the verse there
    Fraction next = w.time;
    bool haveNext = false;
    while (next1(next)) {
        Id el = element(next, w.staff, w.voice);
        if (!el.isValid() || !isChord(el)) {
            // all four voices, as MuseScore's tracks: when none has a lyric there, el ends as voice 4's
            for (int v = 0; v < 4; ++v) {
                el = element(next, w.staff, v);
                if (el.isValid() && isChord(el) && lyricOf(el, w.verse, w.above).isValid()) {
                    toLyricVoice = v;
                    break;
                }
            }
        }
        if (el.isValid() && isChord(el)) {
            haveNext = true;
            break;
        }
    }
    if (haveNext && !adjacent(w.time, next)) {
        haveNext = false;
    }
    if (!haveNext && !hasFollowingRepeat && !hasPrecedingRepeat) {
        return false;
    }
    // a partial dash before this lyric (kept to be continued; endEditText may take it away)
    const Id prevPartial = prevPartialDash(w);
    std::vector<std::pair<Field, Value> > prevPartialFields;
    Id prevPartialParent;
    if (const Object* p = m_ops.obj(prevPartial)) {
        prevPartialParent = p->parent();
        p->forEachField([&](Field field, const Value& value) {
            if (field != F::EXISTS && field != F::KIND && field != F::PARENT && field != F::CHILDREN) {
                prevPartialFields.push_back({ field, value });
            }
        });
    }
    const std::string typed = m_text;
    endEditText(tx);

    // the lyric we are moving from: this one, or an earlier one extended with several dashes
    Id from;
    {
        Fraction seg = w.time;
        bool have = true;
        while (have && adjacent(seg, w.time)) {
            const Id cr = element(seg, w.staff, w.voice);
            if (!cr.isValid()) {
                have = prev1(seg);
                continue;
            }
            from = lyricOf(cr, w.verse, w.above);
            if (from.isValid()) {
                break;
            }
            have = prev1(seg);
        }
    }

    if (!haveNext) {
        if (from.isValid() && hasFollowingRepeat) {
            // a dash with no syllable after it, before a repeat
            if (syllabic(from) == Syllabic::Single) {
                setSyllabic(tx, from, Syllabic::Begin);
            } else if (syllabic(from) == Syllabic::End) {
                setSyllabic(tx, from, Syllabic::Middle);
            }
            setTicks(tx, from, Fraction());
            layout(tx);
            return true;
        }
        if (hasPrecedingRepeat) {
            // no syllable to come from: a partial dash into this lyric, which is edited again
            const size_t mark = tx.changes().records.size();
            makePartial(tx, w, initialCR, false, Fraction());
            const Id to = makeLyric(tx, initialCR, w.verse, Syllabic::End, w, false);
            layout(tx);
            startEditing(to, true, mark, tx);
            return true;
        }
        return false;
    }

    const size_t mark = tx.changes().records.size();
    Id cr = element(next, w.staff, toLyricVoice);
    Id to = cr.isValid() ? lyricOf(cr, w.verse, w.above) : Id();
    if (!to.isValid() && !cr.isValid()) {
        return false;
    }
    if (!to.isValid()) {
        // no lyric in this voice there: look in the others
        for (int v : voices) {
            if (v == w.voice) {
                continue;
            }
            const Id other = element(next, w.staff, v);
            if (other.isValid()) {
                cr = other;
                to = lyricOf(cr, w.verse, w.above);
                if (to.isValid()) {
                    break;
                }
            }
        }
    }
    // a partial dash at the start of a measure after a repeat: no lyric to go to
    if (hasPrecedingRepeat && !from.isValid()) {
        to = Id();
    }
    if (!to.isValid()) {
        cr = element(next, w.staff, w.voice);
    }
    // no dash between sections that don't follow each other (a first and a second ending): partial dashes instead
    if (cr.isValid() && from.isValid()) {
        const size_t toM = measureOf(m_ops.timeOf(cr));
        const size_t fromM = measureOf(m_ops.timeOf(m_ops.obj(from)->parent()));
        const Id fromVoice = m_ops.obj(m_ops.obj(from)->parent())->parent();
        const int fromVoiceNo = int(m_ops.obj(fromVoice)->intOr(F::NUMBER, 0));
        if (toM != fromM && hasFollowingJumpItem(lastChordRest(fromM, w.staff, fromVoiceNo))) {
            const std::vector<size_t> previous = m_repeats.precedingMeasures(toM);
            if (!previous.empty() && std::find(previous.begin(), previous.end(), fromM) == previous.end()) {
                from = Id();
            }
        }
    }
    const bool startIncomingPartialDash = hasPrecedingRepeat && !from.isValid() && !prevPartial.isValid() && typed.empty();
    const bool newLyrics = !to.isValid() || startIncomingPartialDash;
    if (newLyrics) {
        const Id on = startIncomingPartialDash ? initialCR : cr;
        if (!on.isValid()) {
            return false;
        }
        to = Id();
        cr = on;
    } else {
        // arrived by a dash: it can't start a word or stand alone
        if (syllabic(to) == Syllabic::Begin) {
            setSyllabic(tx, to, Syllabic::Middle);
        } else if (syllabic(to) == Syllabic::Single) {
            setSyllabic(tx, to, Syllabic::End);
        }
    }
    if (from.isValid()) {
        // left by a dash: it can have dashes before and after, but can't stand alone or end a word, nor have a line
        if (syllabic(from) == Syllabic::Single) {
            setSyllabic(tx, from, Syllabic::Begin);
        } else if (syllabic(from) == Syllabic::End) {
            setSyllabic(tx, from, Syllabic::Middle);
        }
        setTicks(tx, from, Fraction());
    } else if (hasPrecedingRepeat && !prevPartial.isValid()) {
        makePartial(tx, w, initialCR, false, Fraction());
    } else if (prevPartial.isValid()) {
        if (!m_ops.obj(prevPartial)) {
            // taken away with the empty lyric: back again (MuseScore adds it again)
            createWithId(tx, prevPartial, prevPartialParent, { Kind::Spanner, prevPartialFields });
        }
        const Object* p = m_ops.obj(prevPartial);
        const Fraction pend = p->fractionOr(F::TIME, Fraction()) + p->fractionOr(F::DURATION, Fraction());
        moveEnd(tx, prevPartial, m_ops.timeOf(cr) - pend);
    }
    if (newLyrics) {
        to = makeLyric(tx, cr, w.verse, Syllabic::End, w, false);
    }
    layout(tx);
    startEditing(to, newLyrics, mark, tx);
    return true;
}

bool LyricsEntry::melisma(Transaction& tx)
{
    if (!m_editing) {
        return false;
    }
    scan();
    if (!m_laidOut) {
        layout(tx);
    }
    const Where w = m_cur;
    const Id initialCR = w.event;
    const bool hasPrecedingRepeat = hasPrecedingJumpItem(initialCR);
    const Fraction endTick = w.time;   // an earlier melisma can't go beyond this
    endEditText(tx);

    Fraction next = w.time;
    bool haveNext = false;
    while (next1(next)) {
        const Id el = element(next, w.staff, w.voice);
        if (el.isValid() && isChord(el)) {
            haveNext = true;
            break;
        }
    }
    if (haveNext && !adjacent(w.time, next)) {
        haveNext = false;
    }

    // the lyric we are moving from: this one, or an earlier one extended with several underscores
    Id from, prevPartial;
    Fraction segment = w.time;
    bool haveSegment = true;
    while (haveSegment && adjacent(segment, w.time)) {
        const Id cr = element(segment, w.staff, w.voice);
        if (cr.isValid()) {
            from = lyricOf(cr, w.verse, w.above);
            if (from.isValid()) {
                break;
            }
            prevPartial = partialMelismaAt(cr, w.verse, w.above);   // a partial line to extend
            if (prevPartial.isValid()) {
                break;
            }
        }
        haveSegment = prev1(segment);
        const Id e = haveSegment ? element(segment, w.staff, w.voice) : Id();
        if (e.isValid() && !isChord(e)) {
            break;   // a rest in this voice: stop going back
        }
    }

    // a one-chord melisma: still on its first chord, with a chord to come
    if (from.isValid() && from == w.lyric && haveNext) {
        setTicks(tx, w.lyric, EPS);
        layout(tx);
    }

    auto partialEnd = [&]() {
        Fraction endSegment = segment;
        bool have = haveSegment;
        while (have && endSegment < endTick) {
            have = nextCR(endSegment, w.staff, w.voice, true);
        }
        if (!have) {
            return m_end;
        }
        const Id el = element(endSegment, w.staff, w.voice);
        if (endSegment == endTick && el.isValid() && isChord(el)) {
            const Fraction endChordSeg = endSegment;
            const Fraction chordEnd = endOf(el);
            have = nextCR(endSegment, w.staff, w.voice, false);
            if (!have || endSegment > chordEnd) {
                endSegment = endChordSeg;
                have = true;
                while (have && endSegment < chordEnd) {
                    have = nextCR(endSegment, w.staff, -1, true);
                }
            }
        }
        return have ? endSegment : m_end;
    };

    if (!haveNext) {
        if (from.isValid()) {
            if (syllabic(from) != Syllabic::Single && syllabic(from) != Syllabic::End) {
                setSyllabic(tx, from, Syllabic::End);
            }
            const Fraction fromTime = m_ops.timeOf(m_ops.obj(from)->parent());
            const size_t segMeasure = measureOf(segment);
            const bool endsBeforeRepeat = segMeasure != size_t(-1)
                                          && endTick + endOf(initialCR) - m_ops.timeOf(initialCR)
                                          == m_ops.measureEnd(m_repeats.measures()[segMeasure])
                                          && hasFollowingJumpItem(initialCR);
            if (fromTime < endTick || endsBeforeRepeat) {
                setTicks(tx, from, std::max(endTick - fromTime, EPS));
            }
        }
        if (hasPrecedingRepeat && !prevPartial.isValid()) {
            makePartial(tx, w, initialCR, true, nominal(initialCR));
        } else if (prevPartial.isValid()) {
            const Object* p = m_ops.obj(prevPartial);
            moveEnd(tx, prevPartial, partialEnd() - (p->fractionOr(F::TIME, Fraction()) + p->fractionOr(F::DURATION, Fraction())));
        }
        layout(tx);
        return true;
    }

    const Id nextCRId = element(next, w.staff, w.voice);
    // no extension line between sections that don't follow each other: a partial line instead
    if (nextCRId.isValid() && from.isValid()) {
        const size_t toM = measureOf(next);
        const size_t fromM = measureOf(m_ops.timeOf(m_ops.obj(from)->parent()));
        if (toM != fromM && hasFollowingJumpItem(lastChordRest(fromM, w.staff, w.voice))) {
            const std::vector<size_t> previous = m_repeats.precedingMeasures(toM);
            if (!previous.empty() && std::find(previous.begin(), previous.end(), fromM) == previous.end()) {
                from = Id();
            }
        }
    }
    Id to = lyricOf(nextCRId, w.verse, w.above);
    const bool newLyrics = !to.isValid();
    if (!newLyrics) {
        // arrived by an underscore: no dash before it
        if (syllabic(to) == Syllabic::Middle) {
            setSyllabic(tx, to, Syllabic::Begin);
        } else if (syllabic(to) == Syllabic::End) {
            setSyllabic(tx, to, Syllabic::Single);
        }
    }
    if (from.isValid()) {
        // left by an underscore: it can stand alone or end a word, but has no dash after it, and its line ends here
        if (syllabic(from) != Syllabic::Single && syllabic(from) != Syllabic::End) {
            setSyllabic(tx, from, Syllabic::End);
        }
        const Fraction fromTime = m_ops.timeOf(m_ops.obj(from)->parent());
        if (fromTime < endTick) {
            setTicks(tx, from, endTick - fromTime);
        }
    } else if (hasPrecedingRepeat && !prevPartial.isValid()) {
        makePartial(tx, w, initialCR, true, nominal(initialCR));
    } else if (prevPartial.isValid()) {
        const Object* p = m_ops.obj(prevPartial);
        moveEnd(tx, prevPartial, partialEnd() - (p->fractionOr(F::TIME, Fraction()) + p->fractionOr(F::DURATION, Fraction())));
    }
    layout(tx);
    const size_t mark = tx.changes().records.size();   // the new lyric is a step of its own
    if (newLyrics) {
        to = makeLyric(tx, nextCRId, w.verse, Syllabic::Single, w, true);
        layout(tx);
    }
    startEditing(to, newLyrics, mark, tx);
    return true;
}

bool LyricsEntry::addVerse(Transaction& tx)
{
    if (!m_editing) {
        return false;
    }
    scan();
    if (!m_laidOut) {
        layout(tx);
    }
    const Where w = m_cur;
    endEditText(tx);
    if (!m_ops.obj(w.event)) {
        return false;
    }
    // intentional difference: MuseScore adds a second lyric of the verse if there is one; this goes to it
    if (const Id existing = lyricOf(w.event, w.verse + 1, w.above); existing.isValid()) {
        layout(tx);
        startEditing(existing, false, 0, tx);
        return true;
    }
    const size_t mark = tx.changes().records.size();
    Where like = w;
    like.lyric = m_ops.obj(w.lyric) ? w.lyric : Id();
    const Id lyric = makeLyric(tx, w.event, w.verse + 1, Syllabic::Single, like, true);
    layout(tx);
    startEditing(lyric, true, mark, tx);
    return true;
}

bool LyricsEntry::changeVerse(Transaction& tx, bool up)
{
    if (!m_editing) {
        return false;
    }
    scan();
    if (!m_laidOut) {
        layout(tx);
    }
    const Where w = m_cur;
    int verse = w.verse;
    if (up) {
        if (verse == 0) {
            return false;
        }
        --verse;
    } else {
        ++verse;
        int last = -1;   // ChordRest::lastVerse
        for (Id c : m_ops.obj(w.event)->children()) {
            const Object* a = m_ops.obj(c);
            if (a->kind() == Kind::Attachment && a->text(F::SUBKIND) == "lyric" && isAbove(*a) == w.above) {
                last = std::max(last, int(a->intOr(F::VERSE, 0)));
            }
        }
        if (verse > last) {
            return false;
        }
    }
    endEditText(tx);
    Id lyric = lyricOf(w.event, verse, w.above);
    bool made = false;
    const size_t mark = tx.changes().records.size();
    if (!lyric.isValid()) {
        Where like = w;
        like.lyric = m_ops.obj(w.lyric) ? w.lyric : Id();
        lyric = makeLyric(tx, w.event, verse, Syllabic::Single, like, false);
        made = true;
    }
    layout(tx);
    startEditing(lyric, made, mark, tx);
    return true;
}
}
