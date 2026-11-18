/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G5: Score Proofreading. See proofread.h.

#include "proofread.h"
#include "pageturns.h"

#include "engraving/dom/hairpin.h"
#include "engraving/dom/page.h"
#include "engraving/dom/clef.h"
#include "engraving/dom/keysig.h"
#include "engraving/dom/tempo.h"
#include "engraving/dom/instrchange.h"
#include "engraving/dom/lyrics.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

#include "translation.h"

#include "engraving/dom/accidental.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/chordrest.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/pitchspelling.h"
#include "engraving/dom/utils.h"
#include "engraving/dom/rehearsalmark.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftype.h"
#include "engraving/dom/spanner.h"
#include "engraving/dom/system.h"
#include "engraving/dom/text.h"
#include "engraving/dom/tuplet.h"
#include "engraving/infrastructure/eid.h"
#include "engraving/editing/editscoreproperties.h"
#include "engraving/rendering/score/jubelle/overlapchecker.h"

using namespace mu::engraving;
using muse::String;

namespace mu::notation::jubelle {
namespace {
constexpr const char* CTX = "notation/proofread";

std::string eidKey(EngravingItem* item)
{
    if (!item) {
        return {};
    }
    EID eid = item->eid();
    if (!eid.isValid()) {
        eid = item->assignNewEID();
    }
    return eid.toStdString();
}

String pitchName(int p)
{
    static const char16_t* NAMES[] = { u"C", u"C♯", u"D", u"E♭", u"E", u"F", u"F♯", u"G", u"A♭", u"A", u"B♭", u"B" };
    return String(NAMES[((p % 12) + 12) % 12]) + String::number(p / 12 - 1);
}

String groupOf(const Part* part)
{
    if (const Instrument* i = part ? part->instrument() : nullptr) {
        if (const InstrumentTemplate* t = searchTemplate(i->id())) {
            return t->groupId;
        }
    }
    return String();
}

// leaps worth a second look, in semitones, by instrument group
int leapLimit(const String& group)
{
    if (group == u"brass" || group == u"vocals") {
        return 12;    // more than an octave
    }
    if (group == u"woodwinds") {
        return 19;    // more than an octave and a fifth
    }
    if (group == u"strings" || group == u"plucked-strings") {
        return 24;
    }
    return 0;         // keyboards, percussion…: no leap check
}

struct Context {
    Score* score = nullptr;
    std::vector<Finding>* out = nullptr;
    std::map<const Measure*, int> barNo;

    void add(Severity sev, CheckCategory cat, const String& msg, EngravingItem* item, const Fraction& tick, size_t staff,
             const std::string& keyTail)
    {
        Finding f;
        f.severity = sev;
        f.category = cat;
        f.message = msg;
        f.item = item;
        f.tick = tick;
        f.staffIdx = staff;
        const Measure* m = score->tick2measure(tick);
        f.bar = m && barNo.count(m) ? barNo[m] : 0;
        std::ostringstream k;
        k << int(cat) << ':' << keyTail;
        f.key = k.str();
        out->push_back(f);
    }
};

void checkRange(Context& c, Note* n)
{
    const Instrument* ins = n->part() ? n->part()->instrument(n->tick()) : nullptr;
    if (!ins || n->staff()->isDrumStaff(n->tick())) {
        return;
    }
    const int p = n->pitch();
    const String where = pitchName(p - n->transposition());   // as the player reads it
    if (p < ins->minPitchP() || p > ins->maxPitchP()) {
        c.add(Severity::Error, CheckCategory::Range,
              muse::mtrc(CTX, "%1 is outside the range of %2").arg(where, n->part()->partName()), n, n->tick(), n->staffIdx(), eidKey(n));
    } else if (p < ins->minPitchA() || p > ins->maxPitchA()) {
        c.add(Severity::Warning, CheckCategory::Range,
              muse::mtrc(CTX, "%1 is difficult for many %2 players (outside the amateur range)").arg(where, n->part()->partName()),
              n, n->tick(), n->staffIdx(), eidKey(n));
    }
}

void checkSpelling(Context& c, Note* n)
{
    const int alter = int(tpc2alter(n->tpc()));
    if (std::abs(alter) >= 2) {
        c.add(Severity::Suggestion, CheckCategory::Spelling,
              alter > 0 ? muse::mtrc(CTX, "Double sharp: is a simpler spelling clearer here?")
              : muse::mtrc(CTX, "Double flat: is a simpler spelling clearer here?"),
              n, n->tick(), n->staffIdx(), eidKey(n));
    }
}

void checkStaff(Context& c, const ProofreadOptions& o, size_t staffIdx)
{
    Score* score = c.score;
    const Staff* staff = score->staff(staffIdx);
    if (!staff || !staff->part()) {
        return;
    }
    const bool drum = staff->isDrumStaff(Fraction(0, 1));
    const String group = groupOf(staff->part());
    const int leap = leapLimit(group);
    const track_idx_t track0 = staffIdx * VOICES;
    const bool courtesyAuto = score->style().styleB(Sid::jubelleCourtesyAccidentals);

    const Note* previous = nullptr;   // top note of the previous chord, voice 1
    bool seenNote = false;
    bool seenDynamic = false;
    const Dynamic* lastDynamic = nullptr;
    bool changeSinceDynamic = true;
    std::map<int, int> lastBarAlter;   // step -> alter shown by an accidental in the previous bar
    bool staffHasLyrics = false;
    for (Measure* lm = score->firstMeasure(); lm && !staffHasLyrics; lm = lm->nextMeasure()) {
        for (Segment* ls = lm->first(SegmentType::ChordRest); ls && !staffHasLyrics; ls = ls->next(SegmentType::ChordRest)) {
            for (track_idx_t t = track0; t < track0 + VOICES; ++t) {
                const ChordRest* cr = toChordRest(ls->element(t));
                if (cr && !cr->lyrics().empty()) {
                    staffHasLyrics = true;
                    break;
                }
            }
        }
    }
    int restBars = 0;            // bars this staff has been resting
    bool cueInRest = false;      // small (cue) notes seen while resting
    const bool firstStaffOfPart = staff->part()->staff(0) == staff;
    std::set<Fraction> hairpinStarts;
    for (const auto& [tick, sp] : score->spanner()) {
        if (sp && sp->isHairpin() && sp->staffIdx() == staffIdx) {
            hairpinStarts.insert(sp->tick());
        }
    }

    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        std::map<int, int> thisBarAlter;
        std::set<int> stepsSeenThisBar;
        Fraction voice1 = Fraction(0, 1);
        bool voice1Present = false;

        for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
            if (hairpinStarts.count(seg->tick())) {
                changeSinceDynamic = true;   // a crescendo or diminuendo changes the level
            }
            for (EngravingItem* a : seg->annotations()) {
                if (a && a->isDynamic() && a->staffIdx() == staffIdx) {
                    const Dynamic* d = toDynamic(a);
                    seenDynamic = true;
                    if (o.categories.count(CheckCategory::Dynamics) && lastDynamic && !changeSinceDynamic
                        && d->dynamicType() == lastDynamic->dynamicType() && d->dynamicType() != DynamicType::OTHER) {
                        c.add(Severity::Suggestion, CheckCategory::Dynamics,
                              muse::mtrc(CTX, "The same dynamic again, with no change in between"), a, seg->tick(), staffIdx, eidKey(a));
                    }
                    lastDynamic = d;
                    changeSinceDynamic = false;
                }
            }

            ChordRest* v1 = toChordRest(seg->element(track0));
            ChordRest* v2 = toChordRest(seg->element(track0 + 1));

            // Rhythm grouping: in 4/4 the middle of the bar stays visible
            if (o.categories.count(CheckCategory::MetricGrouping) && m->timesig() == Fraction(4, 4) && m->ticks() == Fraction(4, 4)) {
                for (track_idx_t t = track0; t < track0 + VOICES; ++t) {
                    ChordRest* cr = toChordRest(seg->element(t));
                    if (!cr || cr->isFullMeasureRest() || (cr->isChord() && toChord(cr)->isGrace()) || cr->tuplet()) {
                        continue;
                    }
                    const Fraction start = seg->rtick();
                    const Fraction end = start + cr->actualTicks();
                    const Fraction half(1, 2);
                    const bool crosses = start > Fraction(0, 1) && start < half && end > half;
                    const bool plainSyncopation = start == Fraction(1, 4) && cr->actualTicks() == half;   // quarter, half, quarter
                    if (crosses && !plainSyncopation) {
                        c.add(Severity::Suggestion, CheckCategory::MetricGrouping,
                              cr->isRest() ? muse::mtrc(CTX, "This rest hides beat 3: split it so the middle of the bar shows")
                              : muse::mtrc(CTX, "This note hides beat 3: tie it across the middle of the bar so the beat shows"),
                              cr->isChord() ? static_cast<EngravingItem*>(toChord(cr)->upNote()) : cr, seg->tick(), staffIdx,
                              "beat3:" + eidKey(cr));
                    }
                }
            }

            // Placement: dynamics below instruments, above voices with lyrics
            if (o.categories.count(CheckCategory::Placement) && !drum) {
                for (EngravingItem* a : seg->annotations()) {
                    if (!a || !a->isDynamic() || a->staffIdx() != staffIdx || !a->autoplace()) {
                        continue;
                    }
                    const bool grandStaff = staff->part()->nstaves() > 1;   // between the staves is right there
                    if (staffHasLyrics && !a->placeAbove()) {
                        c.add(Severity::Suggestion, CheckCategory::Placement,
                              muse::mtrc(CTX, "Vocal dynamics usually go above the staff, clear of the words"), a, seg->tick(), staffIdx,
                              "place:" + eidKey(a));
                    } else if (!staffHasLyrics && !grandStaff && a->placeAbove()) {
                        c.add(Severity::Suggestion, CheckCategory::Placement,
                              muse::mtrc(CTX, "Instrumental dynamics usually go below the staff"), a, seg->tick(), staffIdx,
                              "place:" + eidKey(a));
                    }
                }
            }
            if (v1) {
                voice1Present = true;
                voice1 += v1->actualTicks();
            }

            for (track_idx_t t = track0; t < track0 + VOICES; ++t) {
                EngravingItem* e = seg->element(t);
                if (!e || !e->isChord()) {
                    continue;
                }
                Chord* chord = toChord(e);
                if (!seenNote && o.categories.count(CheckCategory::Dynamics) && !seenDynamic && !drum) {
                    c.add(Severity::Suggestion, CheckCategory::Dynamics,
                          muse::mtrc(CTX, "%1 starts without a dynamic").arg(staff->part()->partName()), chord->upNote(),
                          seg->tick(), staffIdx, "first:" + std::to_string(staffIdx));
                }
                seenNote = true;
                if (drum) {
                    continue;
                }
                for (Note* n : chord->notes()) {
                    if (o.categories.count(CheckCategory::Range)) {
                        checkRange(c, n);
                    }
                    if (o.categories.count(CheckCategory::Spelling)) {
                        checkSpelling(c, n);
                        const int step = tpc2step(n->tpc());
                        const int alter = int(tpc2alter(n->tpc()));
                        if (n->accidental()) {
                            thisBarAlter[step] = alter;
                        } else if (!stepsSeenThisBar.count(step) && lastBarAlter.count(step) && lastBarAlter[step] != alter
                                   && !courtesyAuto && !n->tieBack()) {
                            c.add(Severity::Suggestion, CheckCategory::Spelling,
                                  muse::mtrc(CTX, "A courtesy accidental here would remind the player the previous bar was altered"),
                                  n, n->tick(), staffIdx, "courtesy:" + eidKey(n));
                        }
                        stepsSeenThisBar.insert(step);
                    }
                }
                if (t == track0 && !chord->isGrace()) {
                    const Note* top = chord->upNote();
                    if (previous && leap > 0 && o.categories.count(CheckCategory::Leaps) && !top->tieBack()) {
                        const int interval = std::abs(top->pitch() - previous->pitch());
                        if (interval > leap) {
                            c.add(Severity::Suggestion, CheckCategory::Leaps,
                                  muse::mtrc(CTX, "A leap of %n semitone(s): check it's intended and playable", nullptr, interval),
                                  const_cast<Note*>(top), seg->tick(), staffIdx, eidKey(const_cast<Note*>(top)));
                        }
                    }
                    // lesson 8: intervals must look the way they sound (a second written as a third, or the reverse)
                    if (previous && o.categories.count(CheckCategory::Spelling) && !top->tieBack()) {
                        const int semis = std::abs(top->pitch() - previous->pitch());
                        const int letters = std::abs(absStep(top->tpc(), top->epitch()) - absStep(previous->tpc(), previous->epitch()));
                        if ((semis == 1 || semis == 2) && letters == 2) {
                            c.add(Severity::Suggestion, CheckCategory::Spelling,
                                  muse::mtrc(CTX, "This step looks like a third but sounds like a second: respell it so it reads as it sounds"),
                                  const_cast<Note*>(top), seg->tick(), staffIdx, "looks:" + eidKey(const_cast<Note*>(top)));
                        } else if ((semis == 3 || semis == 4) && letters == 1) {
                            c.add(Severity::Suggestion, CheckCategory::Spelling,
                                  muse::mtrc(CTX, "This interval looks like a second but sounds like a third: respell it so it reads as it sounds"),
                                  const_cast<Note*>(top), seg->tick(), staffIdx, "looks:" + eidKey(const_cast<Note*>(top)));
                        }
                    }
                    previous = top;
                }
            }
            if (v1 && v1->isRest()) {
                // a rest between notes keeps the leap check from linking across long breaks
                if (toRest(v1)->actualTicks() >= m->ticks()) {
                    previous = nullptr;
                }
            }

            if (o.categories.count(CheckCategory::VoiceCrossings) && v1 && v2 && v1->isChord() && v2->isChord() && !drum) {
                const Note* lowest1 = toChord(v1)->downNote();
                const Note* highest2 = toChord(v2)->upNote();
                if (highest2->pitch() > lowest1->pitch()) {
                    c.add(Severity::Suggestion, CheckCategory::VoiceCrossings,
                          muse::mtrc(CTX, "Voice 2 is above voice 1 here"), const_cast<Note*>(highest2), seg->tick(), staffIdx,
                          eidKey(const_cast<Note*>(highest2)));
                }
            }
        }

        if (o.categories.count(CheckCategory::IncompleteBars) && voice1Present && voice1 < m->ticks() && !m->irregular()) {
            c.add(Severity::Error, CheckCategory::IncompleteBars, muse::mtrc(CTX, "This bar isn't full in voice 1"),
                  m->first(SegmentType::ChordRest) ? m->first(SegmentType::ChordRest)->element(track0) : nullptr, m->tick(), staffIdx,
                  "bar:" + std::to_string(staffIdx) + ":" + std::to_string(m->tick().ticks()));
        }
        lastBarAlter = thisBarAlter;

        // Preparation: an entry after a long rest wants a cue (one-staff instruments)
        if (o.categories.count(CheckCategory::Preparation) && firstStaffOfPart && staff->part()->nstaves() == 1 && !drum) {
            bool plays = false;
            bool cue = false;
            Chord* firstChord = nullptr;
            for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
                for (track_idx_t t = track0; t < track0 + VOICES; ++t) {
                    EngravingItem* e = seg->element(t);
                    if (e && e->isChord()) {
                        Chord* ch = toChord(e);
                        if (ch->isSmall() || ch->upNote()->isSmall() || !ch->upNote()->play()) {
                            cue = true;
                        } else {
                            plays = true;
                            if (!firstChord) {
                                firstChord = ch;
                            }
                        }
                    }
                }
            }
            if (plays) {
                if (restBars >= 16 && !cueInRest && !cue) {
                    c.add(Severity::Suggestion, CheckCategory::Preparation,
                          muse::mtrc(CTX, "%1 enters after %n bar(s) of rest with no cue: a short cue helps the player come in", nullptr,
                                     restBars).arg(staff->part()->partName()),
                          firstChord ? firstChord->upNote() : nullptr, m->tick(), staffIdx,
                          "cue:" + std::to_string(staffIdx) + ":" + std::to_string(m->tick().ticks()));
                }
                restBars = 0;
                cueInRest = false;
            } else {
                ++restBars;
                cueInRest = cueInRest || cue;
            }
        }
    }
}

//! a dynamic in this voice's staff at the tick (or the hairpin's own begin/end text)
bool dynamicNear(Score* score, const Fraction& tick, track_idx_t track)
{
    Segment* seg = score->tick2segment(tick, true, SegmentType::ChordRest);
    for (; seg && seg->tick() == tick; seg = seg->next1(SegmentType::ChordRest)) {
        for (EngravingItem* a : seg->annotations()) {
            if (a && a->isDynamic() && track2staff(a->track()) == track2staff(track)) {
                return true;
            }
        }
    }
    return false;
}

void checkPreparation(Context& c)
{
    Score* score = c.score;
    // rehearsal marks: no more than 16 bars apart
    int sinceMark = 0;
    int bar = 0;
    bool reported = false;
    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        ++bar;
        bool mark = bar == 1;
        for (Segment* seg = m->first(SegmentType::ChordRest); seg && !mark; seg = seg->next(SegmentType::ChordRest)) {
            for (EngravingItem* a : seg->annotations()) {
                if (a && a->isRehearsalMark()) {
                    mark = true;
                    break;
                }
            }
        }
        if (mark) {
            sinceMark = 0;
            reported = false;
        } else if (++sinceMark > 16 && !reported) {
            c.add(Severity::Suggestion, CheckCategory::Preparation,
                  muse::mtrc(CTX, "More than 16 bars without a rehearsal mark: rehearsals go faster with one every 8 to 16 bars"),
                  m->first(SegmentType::ChordRest) ? m->first(SegmentType::ChordRest)->element(0) : nullptr, m->tick(), 0,
                  "mark:" + std::to_string(m->tick().ticks()));
            reported = true;
        }
    }

    // a tempo marking at the start
    bool tempoAtStart = false;
    if (Measure* first = score->firstMeasure()) {
        for (Segment* seg = first->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
            for (EngravingItem* a : seg->annotations()) {
                tempoAtStart = tempoAtStart || (a && a->isTempoText() && a->visible());
            }
        }
        if (!tempoAtStart) {
            c.add(Severity::Suggestion, CheckCategory::Preparation, muse::mtrc(CTX, "No tempo marking at the start"),
                  first->first(SegmentType::ChordRest) ? first->first(SegmentType::ChordRest)->element(0) : nullptr, first->tick(), 0,
                  "tempo:start");
        }
    }

    // title and composer on the first page
    const Text* title = score->getText(TextStyleType::TITLE);
    if (!title || title->plainText().trimmed().isEmpty()) {
        c.add(Severity::Suggestion, CheckCategory::Preparation, muse::mtrc(CTX, "The score has no title"), nullptr, Fraction(0, 1), 0,
              "title");
    }
    const Text* composer = score->getText(TextStyleType::COMPOSER);
    if (!composer || composer->plainText().trimmed().isEmpty()) {
        c.add(Severity::Suggestion, CheckCategory::Preparation, muse::mtrc(CTX, "No composer or arranger is named on the first page"),
              nullptr, Fraction(0, 1), 0, "composer");
    }

    // bar numbers
    if (!score->style().styleB(Sid::showMeasureNumber) && score->nmeasures() > 8) {
        c.add(Severity::Suggestion, CheckCategory::Preparation,
              muse::mtrc(CTX, "Bar numbers are switched off: players and conductors rely on them in rehearsal"), nullptr,
              Fraction(0, 1), 0, "barnumbers");
    }

    // parts: long rests are counted as one multi-measure rest, and pages turn where there is time
    if (!score->isMaster()) {
        if (!score->style().styleB(Sid::createMultiMeasureRests)) {
            c.add(Severity::Suggestion, CheckCategory::Preparation,
                  muse::mtrc(CTX, "Multi-measure rests are off in this part: players count long rests more easily with them on"),
                  nullptr, Fraction(0, 1), 0, "mmrest");
        }
        TurnSettings turns;
        for (const PageTurn& t : analyseTurns(score, turns)) {
            if (t.quality == TurnQuality::Poor) {
                Measure* m = t.lastMeasure;
                c.add(Severity::Warning, CheckCategory::Preparation,
                      muse::mtrc(CTX, "Page %1 turns in the middle of the music (%2 s of rest): Format › Page turns… can move it")
                      .arg(t.page).arg(String::number(t.rest(), 1)),
                      m, m ? m->endTick() : Fraction(0, 1), 0, "turn:" + std::to_string(t.page));
            }
        }
    }

    // hairpins should start or end at a dynamic, so the player knows where from and where to
    for (auto it = score->spanner().cbegin(); it != score->spanner().cend(); ++it) {
        Spanner* sp = it->second;
        if (!sp || !sp->isHairpin() || !sp->visible()) {
            continue;
        }
        const bool atStart = dynamicNear(score, sp->tick(), sp->track());
        const bool atEnd = dynamicNear(score, sp->tick2(), sp->track());
        if (!atStart && !atEnd && toHairpin(sp)->hairpinType() != HairpinType::INVALID) {
            c.add(Severity::Suggestion, CheckCategory::Preparation,
                  muse::mtrc(CTX, "This crescendo or diminuendo has no dynamic at either end: from what, to what?"),
                  sp, sp->tick(), sp->staffIdx(), "hairpin:" + eidKey(sp));
        }
    }
}

void checkRehearsalMarks(Context& c)
{
    std::vector<std::pair<RehearsalMark*, Fraction> > marks;
    for (Measure* m = c.score->firstMeasure(); m; m = m->nextMeasure()) {
        for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
            for (EngravingItem* a : seg->annotations()) {
                if (a && a->isRehearsalMark() && a->staffIdx() == 0) {
                    marks.push_back({ toRehearsalMark(a), seg->tick() });
                }
            }
        }
    }
    std::set<String> seen;
    int expected = -1;
    for (auto& [mark, tick] : marks) {
        const String text = mark->plainText().trimmed();
        if (seen.count(text)) {
            c.add(Severity::Warning, CheckCategory::RehearsalMarks, muse::mtrc(CTX, "Rehearsal mark %1 appears twice").arg(text), mark,
                  tick, 0, eidKey(mark));
        }
        seen.insert(text);
        // letters A, B, C… or numbers 1, 2, 3…
        int value = -1;
        bool ok = false;
        const int num = text.toInt(&ok);
        if (ok) {
            value = num;
        } else if (text.size() == 1 && text.at(0).unicode() >= u'A' && text.at(0).unicode() <= u'Z') {
            value = text.at(0).unicode() - u'A' + 1;
        }
        if (value > 0 && expected > 0 && value != expected && !seen.empty()) {
            c.add(Severity::Warning, CheckCategory::RehearsalMarks,
                  muse::mtrc(CTX, "Rehearsal mark %1 doesn't follow the one before it").arg(text), mark, tick, 0, "gap:" + eidKey(mark));
        }
        if (value > 0) {
            expected = value + 1;
        }
    }
}

void checkNames(Context& c)
{
    std::map<String, std::vector<Part*> > byName;
    for (Part* p : c.score->parts()) {
        if (!p->show()) {
            continue;
        }
        byName[p->longName().trimmed()].push_back(p);
    }
    for (auto& [name, parts] : byName) {
        if (parts.size() > 1 && !name.isEmpty()) {
            c.add(Severity::Warning, CheckCategory::InstrumentNames,
                  muse::mtrc(CTX, "%n parts are all called “%1”: number them (1, 2…) so players can tell them apart", nullptr,
                             int(parts.size())).arg(name),
                  nullptr, Fraction(0, 1), parts.front()->staff(0)->idx(), "dup:" + name.toStdString());
        }
    }
    if (c.score->systems().size() > 1) {
        for (Part* p : c.score->parts()) {
            if (p->show() && p->shortName().trimmed().isEmpty() && !p->longName().trimmed().isEmpty()) {
                c.add(Severity::Suggestion, CheckCategory::InstrumentNames,
                      muse::mtrc(CTX, "%1 has no short name for the systems after the first").arg(p->longName()), nullptr, Fraction(0, 1),
                      p->staff(0)->idx(), "short:" + p->id().toStdString());
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Voicing (18 Nov 2026): the section players of a big band or a brass section
// ---------------------------------------------------------------------------

//! The section a part plays in ("" when it isn't one of the horn sections)
String sectionOf(const Part* part)
{
    const Instrument* i = part ? part->instrument() : nullptr;
    if (!i) {
        return String();
    }
    const String id = i->id();
    if (id.contains(u"saxophone")) {
        return u"saxophones";
    }
    if (id.contains(u"trumpet") || id.contains(u"flugelhorn") || id.contains(u"cornet")) {
        return u"trumpets";
    }
    if (id.contains(u"trombone")) {
        return u"trombones";
    }
    if (id == u"horn" || id.startsWith(u"horn-") || id.contains(u"french-horn")) {
        return u"horns";
    }
    return String();
}

//! The highest sounding note a part starts at this segment (voice 1 of its first staff), or -1
int topPitchAt(const Segment* seg, const Part* part)
{
    const track_idx_t track = part->startTrack();
    const EngravingItem* e = seg->element(track);
    if (!e || !e->isChord()) {
        return -1;
    }
    const Chord* ch = toChord(e);
    return ch->upNote() ? ch->upNote()->ppitch() : -1;
}

void checkVoicing(Context& c)
{
    Score* score = c.score;
    // sections in score order (top to bottom), only where two or more players share one
    std::map<String, std::vector<Part*> > sections;
    std::vector<String> order;
    for (Part* p : score->parts()) {
        const String s = sectionOf(p);
        if (s.isEmpty() || !p->show()) {
            continue;
        }
        if (!sections.count(s)) {
            order.push_back(s);
        }
        sections[s].push_back(p);
    }
    std::set<std::string> reported;   // one finding per kind, section and bar
    for (const String& name : order) {
        const std::vector<Part*>& players = sections[name];
        if (players.size() < 2) {
            continue;
        }
        for (Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
            // the players who start a note here, top to bottom
            std::vector<std::pair<Part*, int> > voices;
            for (Part* p : players) {
                const int pitch = topPitchAt(seg, p);
                if (pitch >= 0) {
                    voices.push_back({ p, pitch });
                }
            }
            if (voices.size() < 2) {
                continue;
            }
            const Measure* m = seg->measure();
            const int bar = c.barNo.count(m) ? c.barNo[m] : 0;
            auto once = [&](const std::string& kind, Part* part, const String& msg) {
                const std::string key = kind + ":" + name.toStdString() + ":" + std::to_string(bar);
                if (reported.insert(key).second) {
                    EngravingItem* at = seg->element(part->startTrack());
                    c.add(Severity::Suggestion, CheckCategory::Voicing, msg, at, seg->tick(), score->staffIdx(part), key);
                }
            };
            for (size_t i = 0; i + 1 < voices.size(); ++i) {
                const auto& [upper, hi] = voices[i];
                const auto& [lower, lo] = voices[i + 1];
                if (lo > hi) {
                    once("cross", lower, muse::mtrc(CTX, "%1 is above %2 here: section players usually stay in score order")
                         .arg(lower->partName(), upper->partName()));
                }
                const bool bottomPair = i + 2 == voices.size() && voices.size() >= 3;
                const int widest = bottomPair ? 19 : 12;
                if (hi - lo > widest) {
                    once("gap", lower, muse::mtrc(CTX, "More than %1 between %2 and %3: the section may not sound as one")
                         .arg(bottomPair ? muse::mtrc(CTX, "an octave and a fifth") : muse::mtrc(CTX, "an octave"),
                              upper->partName(), lower->partName()));
                }
            }
            const int lead = voices.front().second;
            const int second = voices[1].second;
            if (lead > second && lead - second < 3) {
                once("close", voices[1].first, muse::mtrc(CTX, "Less than a minor third between %1 and %2: the lead line loses clarity")
                     .arg(voices.front().first->partName(), voices[1].first->partName()));
            }
            if (voices.size() >= 4) {
                for (size_t i = 1; i + 1 < voices.size(); ++i) {
                    if (voices[i].second == lead - 12) {
                        once("double", voices[i].first,
                             muse::mtrc(CTX, "%1 doubles the lead an octave down: in a closed voicing only the bottom voice doubles the lead")
                             .arg(voices[i].first->partName()));
                    }
                }
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Orchestration: time to change instruments
// ---------------------------------------------------------------------------

void checkInstrumentChanges(Context& c)
{
    Score* score = c.score;
    static constexpr double MIN_SECONDS = 5.0;
    for (Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (EngravingItem* a : seg->annotations()) {
            if (!a || !a->isInstrumentChange()) {
                continue;
            }
            const track_idx_t track0 = a->track() - (a->track() % VOICES);
            // the last note before the change (the old instrument) and the first note after it (the new one)
            Fraction lastEnd(-1, 1);
            for (Segment* s = seg->prev1(SegmentType::ChordRest); s && lastEnd < Fraction(0, 1); s = s->prev1(SegmentType::ChordRest)) {
                for (track_idx_t t = track0; t < track0 + VOICES; ++t) {
                    if (EngravingItem* e = s->element(t); e && e->isChord()) {
                        lastEnd = std::max(lastEnd, s->tick() + toChord(e)->actualTicks());
                    }
                }
            }
            Fraction firstNote(-1, 1);
            for (Segment* s = seg; s && firstNote < Fraction(0, 1); s = s->next1(SegmentType::ChordRest)) {
                for (track_idx_t t = track0; t < track0 + VOICES; ++t) {
                    if (EngravingItem* e = s->element(t); e && e->isChord()) {
                        firstNote = s->tick();
                    }
                }
            }
            if (lastEnd < Fraction(0, 1) || firstNote < Fraction(0, 1)) {
                continue;
            }
            const double secs = score->tempomap()->tick2time(firstNote.ticks()) - score->tempomap()->tick2time(lastEnd.ticks());
            if (secs < MIN_SECONDS) {
                const InstrumentChange* ic = toInstrumentChange(a);
                const String to = ic->instrument() ? ic->instrument()->trackName() : String();
                c.add(Severity::Warning, CheckCategory::Orchestration,
                      muse::mtrc(CTX, "Only %1 s to change to %2: players usually need at least %3 s")
                      .arg(String::number(std::max(0.0, secs), 1), to, String::number(MIN_SECONDS, 0)),
                      a, seg->tick(), a->staffIdx(), "change:" + eidKey(a));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Copying: sizes, melisma lines, changes after page turns
// ---------------------------------------------------------------------------

void checkCopying(Context& c)
{
    Score* score = c.score;
    // cue notes: about two-thirds of the normal size
    const double cueMag = score->style().styleD(Sid::smallNoteMag);
    if (cueMag < 0.6 || cueMag > 0.8) {
        bool anyCue = false;
        for (Segment* seg = score->firstSegment(SegmentType::ChordRest); seg && !anyCue; seg = seg->next1(SegmentType::ChordRest)) {
            for (EngravingItem* e : seg->elist()) {
                anyCue = anyCue || (e && e->isChordRest() && toChordRest(e)->isSmall());
            }
        }
        if (anyCue) {
            c.add(Severity::Suggestion, CheckCategory::Copying,
                  muse::mtrc(CTX, "Small (cue) notes are %1% of normal size: cues are usually about two-thirds size (Format › Style › Sizes)")
                  .arg(int(std::lround(cueMag * 100))), nullptr, Fraction(0, 1), 0, "cuesize");
        }
    }
    const double staffMag = score->style().styleD(Sid::smallStaffMag);
    for (size_t s = 0; s < score->nstaves(); ++s) {
        const Staff* st = score->staff(s);
        if (st && st->show() && st->staffType(Fraction(0, 1)) && st->staffType(Fraction(0, 1))->isSmall() && (staffMag < 0.6 || staffMag > 0.8)) {
            c.add(Severity::Suggestion, CheckCategory::Copying,
                  muse::mtrc(CTX, "Small staves (ossias, cue staves) are %1% of normal size: about two-thirds is usual")
                  .arg(int(std::lround(staffMag * 100))), nullptr, Fraction(0, 1), s, "staffsize");
            break;
        }
    }

    // a word sung over several notes wants its extender line
    for (size_t staffIdx = 0; staffIdx < score->nstaves(); ++staffIdx) {
        for (track_idx_t track = staffIdx * VOICES; track < (staffIdx + 1) * VOICES; ++track) {
            for (Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
                EngravingItem* e = seg->element(track);
                if (!e || !e->isChord()) {
                    continue;
                }
                for (Lyrics* l : toChord(e)->lyrics()) {
                    const LyricsSyllabic syl = l->syllabic();
                    if ((syl != LyricsSyllabic::SINGLE && syl != LyricsSyllabic::END) || l->ticks().isNotZero()) {
                        continue;
                    }
                    // is the next chord in this voice without a syllable for this verse (a melisma)?
                    Segment* next = seg->next1(SegmentType::ChordRest);
                    while (next && !next->element(track)) {
                        next = next->next1(SegmentType::ChordRest);
                    }
                    EngravingItem* ne = next ? next->element(track) : nullptr;
                    if (!ne || !ne->isChord()) {
                        continue;
                    }
                    bool hasSyllable = false;
                    for (const Lyrics* nl : toChord(ne)->lyrics()) {
                        hasSyllable = hasSyllable || nl->verse() == l->verse();
                    }
                    if (!hasSyllable) {
                        c.add(Severity::Suggestion, CheckCategory::Copying,
                              muse::mtrc(CTX, "“%1” is sung over more notes but has no extender line").arg(l->plainText()),
                              l, seg->tick(), staffIdx, "melisma:" + eidKey(l));
                    }
                }
            }
        }
    }

    // parts: a key or clef change straight after a page turn is easily missed
    if (!score->isMaster() && score->pages().size() > 1) {
        const bool twoSided = score->style().styleB(Sid::pageTwosided);
        for (size_t i = 1; i < score->pages().size(); ++i) {
            // with two-sided pages the player turns before pages 3, 5, 7…; otherwise before every page
            if (twoSided && (i % 2) != 0) {
                continue;
            }
            const Page* page = score->pages()[i];
            Measure* m = page ? page->firstMeasure() : nullptr;
            if (!m) {
                continue;
            }
            bool change = false;
            EngravingItem* at = nullptr;
            for (Segment* seg = m->first(); seg && seg->tick() == m->tick(); seg = seg->next()) {
                if (!seg->isKeySigType() && !seg->isClefType() && !seg->isHeaderClefType()) {
                    continue;
                }
                for (EngravingItem* e : seg->elist()) {
                    if (e && e->isKeySig() && !toKeySig(e)->generated()) {
                        change = true;
                        at = e;
                    } else if (e && e->isClef() && !toClef(e)->generated()) {
                        change = true;
                        at = e;
                    }
                }
            }
            if (change) {
                c.add(Severity::Suggestion, CheckCategory::Copying,
                      muse::mtrc(CTX, "A key or clef change comes straight after the turn to page %1: a reminder at the end of page %2, "
                                      "or a different page turn, helps the player").arg(int(i + 1)).arg(int(i)),
                      at, m->tick(), at ? at->staffIdx() : 0, "turnchange:" + std::to_string(i + 1));
            }
        }
    }
}

void checkCollisions(Context& c)
{
    for (const engraving::jubelle::Overlap& o : engraving::jubelle::findOverlaps(c.score)) {
        if (!o.moves || !o.stays) {
            continue;
        }
        c.add(Severity::Warning, CheckCategory::Collisions,
              muse::mtrc(CTX, "%1 collides with %2").arg(o.moves->translatedTypeUserName(), o.stays->translatedTypeUserName()),
              o.moves, o.moves->tick(), o.moves->staffIdx(), eidKey(o.moves) + "/" + eidKey(o.stays));
    }
}
}

String categoryTitle(CheckCategory cat)
{
    switch (cat) {
    case CheckCategory::Range: return muse::mtrc(CTX, "Range");
    case CheckCategory::Leaps: return muse::mtrc(CTX, "Large leaps");
    case CheckCategory::Spelling: return muse::mtrc(CTX, "Accidentals and spelling");
    case CheckCategory::IncompleteBars: return muse::mtrc(CTX, "Incomplete bars");
    case CheckCategory::Dynamics: return muse::mtrc(CTX, "Dynamics");
    case CheckCategory::RehearsalMarks: return muse::mtrc(CTX, "Rehearsal marks");
    case CheckCategory::InstrumentNames: return muse::mtrc(CTX, "Instrument names");
    case CheckCategory::VoiceCrossings: return muse::mtrc(CTX, "Voice crossings");
    case CheckCategory::Voicing: return muse::mtrc(CTX, "Section voicing");
    case CheckCategory::Orchestration: return muse::mtrc(CTX, "Instrument changes");
    case CheckCategory::Copying: return muse::mtrc(CTX, "Copying");
    case CheckCategory::Collisions: return muse::mtrc(CTX, "Collisions");
    case CheckCategory::MetricGrouping: return muse::mtrc(CTX, "Rhythm grouping");
    case CheckCategory::Placement: return muse::mtrc(CTX, "Placement");
    case CheckCategory::Preparation: return muse::mtrc(CTX, "Score and part preparation");
    }
    return String();
}

std::set<std::string> intentionalKeys(const Score* score)
{
    std::set<std::string> keys;
    if (!score) {
        return keys;
    }
    const String tag = score->masterScore()->metaTag(String(INTENTIONAL_TAG));
    for (const String& k : tag.split(u'|')) {
        if (!k.isEmpty()) {
            keys.insert(k.toStdString());
        }
    }
    return keys;
}

void setIntentional(Score* score, const std::string& key, bool intentional)
{
    if (!score || key.empty()) {
        return;
    }
    MasterScore* master = score->masterScore();
    std::set<std::string> keys = intentionalKeys(master);
    intentional ? (void)keys.insert(key) : (void)keys.erase(key);
    String joined;
    for (const std::string& k : keys) {
        joined += (joined.isEmpty() ? u"" : u"|") + String::fromStdString(k);
    }
    std::map<String, String> tags = master->metaTags();
    if (joined.isEmpty()) {
        tags.erase(String(INTENTIONAL_TAG));
    } else {
        tags[String(INTENTIONAL_TAG)] = joined;
    }
    master->undo(new ChangeMetaTags(master, tags));
}

std::vector<Finding> proofread(Score* score, const ProofreadOptions& options)
{
    std::vector<Finding> out;
    if (!score) {
        return out;
    }
    Context c;
    c.score = score;
    c.out = &out;
    int bar = 0;
    for (const Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        c.barNo[m] = ++bar;
    }

    for (size_t s = 0; s < score->nstaves(); ++s) {
        if (score->staff(s)->show()) {
            checkStaff(c, options, s);
        }
    }
    if (options.categories.count(CheckCategory::RehearsalMarks)) {
        checkRehearsalMarks(c);
    }
    if (options.categories.count(CheckCategory::InstrumentNames)) {
        checkNames(c);
    }
    if (options.categories.count(CheckCategory::Collisions)) {
        checkCollisions(c);
    }
    if (options.categories.count(CheckCategory::Preparation)) {
        checkPreparation(c);
    }
    if (options.categories.count(CheckCategory::Voicing)) {
        checkVoicing(c);
    }
    if (options.categories.count(CheckCategory::Orchestration)) {
        checkInstrumentChanges(c);
    }
    if (options.categories.count(CheckCategory::Copying)) {
        checkCopying(c);
    }

    if (!options.includeIntentional) {
        const std::set<std::string> keys = intentionalKeys(score);
        out.erase(std::remove_if(out.begin(), out.end(), [&keys](const Finding& f) { return keys.count(f.key) > 0; }), out.end());
    }
    std::stable_sort(out.begin(), out.end(), [](const Finding& a, const Finding& b) {
        if (a.tick != b.tick) {
            return a.tick < b.tick;
        }
        return a.staffIdx < b.staffIdx;
    });
    return out;
}
}
