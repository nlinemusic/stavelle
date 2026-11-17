/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 * Music Composition & Notation
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "condensedscore.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>

#include "io/buffer.h"

#include "engraving/compat/mscxcompat.h"
#include "engraving/compat/scoreaccess.h"
#include "engraving/engravingproject.h"
#include "engraving/infrastructure/mscwriter.h"
#include "engraving/infrastructure/localfileinfoprovider.h"

#include "log.h"

#include "engraving/dom/chord.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/hairpin.h"
#include "engraving/dom/instrchange.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/slur.h"
#include "engraving/dom/spanner.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftext.h"
#include "engraving/dom/system.h"
#include "engraving/dom/textline.h"
#include "engraving/dom/tie.h"
#include "engraving/editing/editscoreproperties.h"

#include "condensingplanner.h"
#include "linkedpassages.h"

using namespace mu::engraving;
using muse::String;

namespace mu::notation::jubelle {
namespace {
using condensing::Decision;

//! What one player plays in one bar
struct BarMusic {
    condensing::PlayerPassage events;
    bool otherVoices = false;   //!< notes in voices 2–4
    bool tuplets = false;
    bool graceNotes = false;
    bool measureRepeat = false;
};

BarMusic readBar(const Measure* m, size_t staffIdx)
{
    BarMusic b;
    const track_idx_t track = staffIdx * VOICES;
    for (const Segment* s = m->first(); s; s = s->next()) {
        if (s->isType(SegmentType::ChordRest)) {
            for (voice_idx_t v = 1; v < VOICES; ++v) {
                const EngravingItem* e = s->element(track + v);
                if (e && e->isChord()) {
                    b.otherVoices = true;
                }
            }
            const EngravingItem* e = s->element(track);
            if (!e) {
                continue;
            }
            if (e->isMeasureRepeat()) {
                b.measureRepeat = true;
                continue;
            }
            const ChordRest* cr = toChordRest(e);
            if (cr->tuplet()) {
                b.tuplets = true;
            }
            condensing::Event ev;
            ev.tick = (s->tick() - m->tick()).ticks();
            ev.duration = cr->actualTicks().ticks();
            if (cr->isChord()) {
                const Chord* c = toChord(cr);
                if (!c->graceNotes().empty()) {
                    b.graceNotes = true;
                }
                for (const Note* n : c->notes()) {
                    ev.pitches.push_back(n->pitch());
                }
                std::sort(ev.pitches.begin(), ev.pitches.end());
            }
            b.events.push_back(ev);
        }
    }
    return b;
}

bool barHasNotes(const BarMusic& b)
{
    return std::any_of(b.events.begin(), b.events.end(), [](const condensing::Event& e) { return !e.pitches.empty(); });
}

//! How one bar of a group is shown
struct BarPlan {
    bool condensed = false;
    Decision decision = Decision::Separate;
    int soloPlayer = -1;
    String label;          //!< "a2", "1.", "2." or empty
    String reason;         //!< why it stays separate
};

bool movesSecondPlayer(const BarPlan& p)
{
    return p.decision == Decision::TwoVoices || (p.decision == Decision::OnlyOne && p.soloPlayer == 1);
}

const CondenseOverride* overrideFor(const std::vector<CondenseOverride>& overrides, size_t staffIdx, int bar)
{
    const CondenseOverride* found = nullptr;
    for (const CondenseOverride& o : overrides) {
        if (o.staffIdx == staffIdx && bar >= o.fromBar && bar <= o.toBar) {
            found = &o;   // the last one set wins
        }
    }
    return found;
}

BarPlan planBar(const Measure* m, const CondenseGroup& g, int bar, const std::vector<CondenseOverride>& overrides)
{
    BarPlan p;
    const BarMusic a = readBar(m, g.first);
    const BarMusic b = readBar(m, g.second);
    const CondenseOverride* o = overrideFor(overrides, g.first, bar);

    if (o && o->mode == CondenseOverride::Mode::Separate) {
        p.reason = u"kept separate by your override";
        return p;
    }
    if (a.otherVoices || b.otherVoices) {
        p.reason = u"a player uses more than one voice";
        return p;
    }
    if (a.measureRepeat || b.measureRepeat) {
        p.reason = u"a measure repeat sign";
        return p;
    }

    const condensing::Result r = condensing::decide({ a.events, b.events });
    p.decision = r.decision;
    p.soloPlayer = r.soloPlayer;
    p.label = String::fromStdString(r.label);
    if (r.decision == Decision::Separate) {
        p.reason = String::fromStdString(r.reason);
        if (o && o->mode == CondenseOverride::Mode::Condense) {
            // Your override: two voices always fit (crossing parts just cross)
            p.decision = Decision::TwoVoices;
            p.label.clear();
        }
    }
    if (p.decision == Decision::Separate) {
        return p;
    }
    if (movesSecondPlayer(p) && b.tuplets) {
        p.decision = Decision::Separate;
        p.reason = u"the second player has tuplets";
        return p;
    }
    if (!movesSecondPlayer(p) && b.graceNotes && barHasNotes(b)) {
        p.decision = Decision::Separate;
        p.reason = u"the second player has grace notes";
        return p;
    }
    p.condensed = true;
    return p;
}

std::vector<std::pair<Measure*, Measure*> > systemsOf(MasterScore* score)
{
    std::vector<std::pair<Measure*, Measure*> > out;
    for (System* sys : score->systems()) {
        Measure* first = nullptr;
        Measure* last = nullptr;
        for (MeasureBase* mb : sys->measures()) {
            if (mb->isMeasure()) {
                if (!first) {
                    first = toMeasure(mb);
                }
                last = toMeasure(mb);
            }
        }
        if (first) {
            out.push_back({ first, last });
        }
    }
    return out;
}

int barIndex(const Measure* m)
{
    int i = 0;
    for (const Measure* x = m->score()->firstMeasure(); x && x != m; x = x->nextMeasure()) {
        ++i;
    }
    return i;
}

ChordRest* crAt(Measure* m, track_idx_t track, const Fraction& tick)
{
    Segment* s = m->findSegment(SegmentType::ChordRest, tick);
    EngravingItem* e = s ? s->element(track) : nullptr;
    return e && e->isChordRest() ? toChordRest(e) : nullptr;
}

//! The bars of one group, as shown in the copy
struct GroupState {
    CondenseGroup group;
    std::map<const Measure*, BarPlan> bars;   //!< condensed bars only
    std::map<Note*, Note*> notes;              //!< second player's note → the note that shows it
    std::map<Chord*, Chord*> chords;           //!< second player's chord → the chord that shows it
};

track_idx_t targetTrack(const GroupState& g, const BarPlan& p)
{
    const track_idx_t base = g.group.first * VOICES;
    return p.decision == Decision::TwoVoices ? base + 1 : base;
}

bool inCondensedBar(const GroupState& g, const Fraction& tick)
{
    for (const auto& [m, plan] : g.bars) {
        if (tick >= m->tick() && tick < m->endTick()) {
            return true;
        }
    }
    return false;
}

const BarPlan* planAt(const GroupState& g, const Fraction& tick)
{
    for (const auto& [m, plan] : g.bars) {
        if (tick >= m->tick() && tick < m->endTick()) {
            return &plan;
        }
    }
    return nullptr;
}

void mapChord(GroupState& g, Chord* from, Chord* to)
{
    g.chords[from] = to;
    for (Note* n : from->notes()) {
        for (Note* t : to->notes()) {
            if (t->pitch() == n->pitch()) {
                g.notes[n] = t;
                break;
            }
        }
    }
}

//! A cloned note keeps a copy of its tie with no end; the ties are made again
//! afterwards from the original ones
void dropUnconnectedTies(Chord* chord)
{
    std::vector<Chord*> chords { chord };
    for (Chord* g : chord->graceNotes()) {
        chords.push_back(g);
    }
    for (Chord* c : chords) {
        for (Note* n : c->notes()) {
            Tie* t = n->tieFor();
            if (t && !t->endNote() && !t->isLaissezVib() && !t->isPartialTie()) {
                n->setTieFor(nullptr);
                delete t;
            }
        }
    }
}

//! Puts the second player's music for one bar where the plan shows it
void writeBar(MasterScore* score, GroupState& g, Measure* m, const BarPlan& p)
{
    const track_idx_t a = g.group.first * VOICES;
    const track_idx_t b = g.group.second * VOICES;

    std::vector<ChordRest*> second;
    for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
        if (EngravingItem* e = s->element(b)) {
            second.push_back(toChordRest(e));
        }
    }

    switch (p.decision) {
    case Decision::Unison:
        for (ChordRest* cr : second) {
            ChordRest* mine = crAt(m, a, cr->tick());
            if (cr->isChord() && mine && mine->isChord()) {
                mapChord(g, toChord(cr), toChord(mine));
            }
        }
        break;
    case Decision::Chords:
        for (ChordRest* cr : second) {
            if (!cr->isChord()) {
                continue;
            }
            ChordRest* mine = crAt(m, a, cr->tick());
            if (!mine || !mine->isChord()) {
                continue;
            }
            Chord* chord = toChord(mine);
            for (Note* n : toChord(cr)->notes()) {
                Note* shown = nullptr;
                for (Note* x : chord->notes()) {
                    if (x->pitch() == n->pitch()) {
                        shown = x;
                    }
                }
                if (!shown) {
                    NoteVal nv(n->pitch());
                    nv.tpc1 = n->tpc1();
                    nv.tpc2 = n->tpc2();
                    shown = score->addNote(chord, nv);
                }
                if (shown) {
                    g.notes[n] = shown;
                }
            }
            g.chords[toChord(cr)] = chord;
        }
        break;
    case Decision::TwoVoices:
    case Decision::OnlyOne:
        if (!movesSecondPlayer(p)) {
            break;   // the first player plays alone: nothing to move
        }
        if (p.decision == Decision::OnlyOne) {
            // The first player rests all bar: their rests make room
            std::vector<ChordRest*> mine;
            for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
                if (EngravingItem* e = s->element(a)) {
                    mine.push_back(toChordRest(e));
                }
            }
            for (ChordRest* cr : mine) {
                score->undoRemoveElement(cr);
            }
        }
        for (ChordRest* cr : second) {
            ChordRest* copy = toChordRest(cr->clone());
            if (copy->isChord()) {
                dropUnconnectedTies(toChord(copy));
            }
            copy->setTrack(targetTrack(g, p));
            copy->setStaffMove(0);
            score->undoAddCR(copy, m, cr->tick());
            if (cr->isChord() && copy->isChord()) {
                Chord* from = toChord(cr);
                Chord* to = toChord(copy);
                g.chords[from] = to;
                for (size_t i = 0; i < from->notes().size() && i < to->notes().size(); ++i) {
                    g.notes[from->notes()[i]] = to->notes()[i];
                }
            }
        }
        break;
    case Decision::AllRest:
    case Decision::Separate:
        break;
    }

    // The second player's dynamics and texts, where the first player has none there
    for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
        std::vector<EngravingItem*> toCopy;
        for (EngravingItem* e : s->annotations()) {
            if (e->track() < b || e->track() >= b + VOICES) {
                continue;
            }
            if (!(e->isDynamic() || e->isExpression() || e->isStaffText())) {
                continue;
            }
            const bool firstHasOne = std::any_of(s->annotations().begin(), s->annotations().end(), [&](EngravingItem* x) {
                return x->type() == e->type() && x->track() >= a && x->track() < a + VOICES
                       && (!x->isTextBase() || !e->isTextBase() || toTextBase(x)->xmlText() == toTextBase(e)->xmlText()
                           || x->isDynamic());
            });
            if (!firstHasOne) {
                toCopy.push_back(e);
            }
        }
        for (EngravingItem* e : toCopy) {
            EngravingItem* c = e->clone();
            c->setTrack(targetTrack(g, p));
            c->setParent(s);
            score->undoAddElement(c);
        }
    }
}

//! Ties, slurs and hairpins of the second player, moved with the notes
void moveSpanners(MasterScore* score, GroupState& g, CondenseReport* report)
{
    const track_idx_t b = g.group.second * VOICES;

    // Ties
    std::vector<Tie*> ties;
    for (const auto& [m, plan] : g.bars) {
        (void)plan;
        for (Segment* s = const_cast<Measure*>(m)->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            EngravingItem* e = s->element(b);
            if (!e || !e->isChord()) {
                continue;
            }
            for (Note* n : toChord(e)->notes()) {
                if (n->tieFor() && n->tieFor()->endNote()) {
                    ties.push_back(n->tieFor());
                }
                if (n->tieBack() && n->tieBack()->startNote()
                    && !inCondensedBar(g, n->tieBack()->startNote()->tick())) {
                    ties.push_back(n->tieBack());   // a tie from a separate bar
                }
            }
        }
    }
    int lostTies = 0;
    for (Tie* t : ties) {
        Note* start = t->startNote();
        Note* end = t->endNote();
        auto ms = g.notes.find(start);
        auto me = g.notes.find(end);
        score->undoRemoveElement(t);
        if (ms == g.notes.end() || me == g.notes.end()) {
            ++lostTies;
            continue;
        }
        Note* ns = ms->second;
        Note* ne = me->second;
        if (ns->tieFor() || ne->tieBack() || ns->pitch() != ne->pitch()) {
            continue;   // already tied (for example in unison)
        }
        Tie* tie = Factory::createTie(ns);
        tie->setStartNote(ns);
        tie->setEndNote(ne);
        tie->setTrack(ns->track());
        tie->setTick(ns->tick());
        tie->setTick2(ne->tick());
        score->undoAddElement(tie);
    }

    // Slurs, hairpins and other lines on the second player's staff
    std::vector<Spanner*> spanners;
    for (const auto& [tick, sp] : score->spanner()) {
        (void)tick;
        if (sp->staffIdx() != g.group.second || sp->systemFlag()) {
            continue;
        }
        if (inCondensedBar(g, sp->tick()) || (sp->tick2() > sp->tick() && inCondensedBar(g, sp->tick2() - Fraction(1, 1920)))) {
            spanners.push_back(sp);
        }
    }
    int lostLines = 0;
    for (Spanner* sp : spanners) {
        if (sp->isSlur()) {
            ChordRest* sc = sp->startCR();
            ChordRest* ec = sp->endCR();
            auto ms = sc && sc->isChord() ? g.chords.find(toChord(sc)) : g.chords.end();
            auto me = ec && ec->isChord() ? g.chords.find(toChord(ec)) : g.chords.end();
            score->undoRemoveElement(sp);
            if (ms == g.chords.end() || me == g.chords.end()) {
                ++lostLines;
                continue;
            }
            Chord* a = ms->second;
            Chord* e = me->second;
            const bool already = std::any_of(score->spanner().begin(), score->spanner().end(), [&](const auto& kv) {
                return kv.second->isSlur() && kv.second->startElement() == a && kv.second->endElement() == e;
            });
            if (already || a == e) {
                continue;
            }
            Slur* slur = Factory::createSlur(score->dummy());
            slur->setTrack(a->track());
            slur->setTrack2(e->track());
            slur->setTick(a->tick());
            slur->setTick2(e->tick());
            slur->setStartElement(a);
            slur->setEndElement(e);
            score->undoAddElement(slur);
        } else if (sp->isHairpin()) {
            const BarPlan* p = planAt(g, sp->tick());
            const track_idx_t to = p ? targetTrack(g, *p) : g.group.first * VOICES;
            const bool firstHasOne = std::any_of(score->spanner().begin(), score->spanner().end(), [&](const auto& kv) {
                return kv.second->isHairpin() && kv.second->staffIdx() == g.group.first && kv.second->tick() == sp->tick();
            });
            score->undoRemoveElement(sp);
            if (!firstHasOne && p) {
                Spanner* c = toSpanner(sp->clone());
                c->setTrack(to);
                c->setTrack2(to);
                score->undoAddElement(c);
            }
        } else {
            score->undoRemoveElement(sp);
            ++lostLines;
        }
    }
    if (lostTies) {
        report->notes.push_back(String(u"%1 tie(s) of a second player that cross between a separate and a shared system aren't drawn.")
                                .arg(lostTies));
    }
    if (lostLines) {
        report->notes.push_back(String(u"%1 slur(s) or line(s) of a second player on shared systems couldn't be moved and were left out.")
                                .arg(lostLines));
    }
}

//! Empties the second player's staff in the shared bars (it then hides on those systems)
void clearSecondPlayer(MasterScore* score, const GroupState& g)
{
    std::vector<Measure*> bars;
    for (const auto& [m, plan] : g.bars) {
        (void)plan;
        bars.push_back(const_cast<Measure*>(m));
    }
    std::sort(bars.begin(), bars.end(), [](Measure* x, Measure* y) { return x->tick() < y->tick(); });
    size_t i = 0;
    while (i < bars.size()) {
        size_t j = i;
        while (j + 1 < bars.size() && bars[j + 1] == bars[j]->nextMeasure()) {
            ++j;
        }
        score->deselectAll();
        score->select(bars[i], SelectType::RANGE, g.group.second);
        score->select(bars[j], SelectType::RANGE, g.group.second);
        score->cmdDeleteSelection();
        i = j + 1;
    }
    score->deselectAll();
}

void addLabel(MasterScore* score, Measure* m, size_t staffIdx, const String& text)
{
    Segment* s = m->first(SegmentType::ChordRest);
    if (!s) {
        return;
    }
    StaffText* t = Factory::createStaffText(s);
    t->setTrack(staffIdx * VOICES);
    t->setParent(s);
    t->setXmlText(text);
    score->undoAddElement(t);
}

//! Names of the first player's staff on shared systems ("Trumpet 1.2") and
//! back to its own name on separate ones, by hidden instrument changes
void setNames(MasterScore* score, const CondenseGroup& g, const std::vector<std::pair<Measure*, bool> >& systemStarts)
{
    Part* first = score->staff(g.first)->part();
    Part* second = score->staff(g.second)->part();
    const Instrument base = *first->instrument(Fraction(0, 1));
    Instrument shared = base;
    shared.setLongName(combinedName(first->longName(Fraction(0, 1)), second->longName(Fraction(0, 1))));
    shared.setShortName(combinedName(first->shortName(Fraction(0, 1)), second->shortName(Fraction(0, 1))));

    bool current = false;   // names at the start: the part's own
    for (size_t k = 0; k < systemStarts.size(); ++k) {
        const auto& [m, condensed] = systemStarts[k];
        if (k == 0) {
            if (condensed) {
                first->instrument(Fraction(0, 1))->setLongNames(shared.longNames());
                first->instrument(Fraction(0, 1))->setShortNames(shared.shortNames());
            }
            current = condensed;
            continue;
        }
        if (condensed == current) {
            continue;
        }
        Segment* s = m->undoGetSegment(SegmentType::ChordRest, m->tick());
        InstrumentChange* ic = Factory::createInstrumentChange(s, condensed ? shared : base);
        ic->setTrack(g.first * VOICES);
        ic->setParent(s);
        ic->setVisible(false);
        ic->setXmlText(condensed ? shared.trackName() : base.trackName());
        score->undoAddElement(ic);
        current = condensed;
    }
}

/// Makes a fresh copy of the score (MuseScore's own clone: write and read
/// back) in a project of its own, which owns it
std::shared_ptr<EngravingProject> cloneScore(MasterScore* original)
{
    MasterScore* copy = original->clone();
    if (!copy) {
        return nullptr;
    }
    std::shared_ptr<EngravingProject> project = EngravingProject::create(original->style(), original->iocContext());
    delete project->masterScore();
    project->setMasterScore(copy);
    return project;
}

//! Restores the linked-passages hook when generation is done
struct HookPause {
    Score::JubelleCmdHook* saved = Score::jubelleCmdHook;
    HookPause() { Score::jubelleCmdHook = nullptr; }
    ~HookPause() { Score::jubelleCmdHook = saved; }
};
}

std::vector<CondenseGroup> condenseGroups(const Score* score)
{
    std::vector<CondenseGroup> groups;
    const std::vector<Part*>& parts = score->parts();
    auto key = [](const Part* p) -> std::optional<String> {
        if (p->nstaves() != 1) {
            return std::nullopt;
        }
        const Staff* st = p->staves().front();
        const Instrument* in = p->instrument(Fraction(0, 1));
        if (!st->isPitchedStaff(Fraction(0, 1)) || in->useDrumset()) {
            return std::nullopt;
        }
        // A real change of instrument (say flute to piccolo) makes the pairing
        // unreliable. Changes to the same instrument are fine: a condensed copy
        // uses them to rename the staff "Trumpet 1.2" on shared systems.
        for (const auto& [tick, other] : p->instruments()) {
            (void)tick;
            if (other->id() != in->id() || other->musicXmlId() != in->musicXmlId() || !(other->transpose() == in->transpose())) {
                return std::nullopt;
            }
        }
        // Woodwinds and brass only: strings use divisi on their own staves,
        // and keyboards, voices and percussion aren't condensed
        const String family = in->musicXmlId();
        if (!family.startsWith(u"wind.") && !family.startsWith(u"brass.")) {
            return std::nullopt;
        }
        const String id = in->id().empty() ? in->trackName() : in->id();
        return id + u"|" + String::number(in->transpose().chromatic) + u"|" + String::number(in->transpose().diatonic);
    };
    for (size_t i = 0; i + 1 < parts.size();) {
        const std::optional<String> a = key(parts[i]);
        const std::optional<String> b = key(parts[i + 1]);
        if (a && b && *a == *b) {
            groups.push_back({ parts[i]->staves().front()->idx(), parts[i + 1]->staves().front()->idx() });
            i += 2;
        } else {
            ++i;
        }
    }
    return groups;
}

bool isCondensedCopy(const Score* score)
{
    return score && score->masterScore()->metaTag(CONDENSED_COPY_TAG) == u"1";
}

String overridesText(const std::vector<CondenseOverride>& overrides)
{
    String out;
    for (const CondenseOverride& o : overrides) {
        if (!out.empty()) {
            out += u";";
        }
        out += String(u"%1:%2-%3:%4").arg(int(o.staffIdx)).arg(o.fromBar).arg(o.toBar)
               .arg(o.mode == CondenseOverride::Mode::Separate ? u"separate" : u"condense");
    }
    return out;
}

std::vector<CondenseOverride> parseOverrides(const String& text)
{
    std::vector<CondenseOverride> out;
    for (const String& item : text.split(u';')) {
        const std::vector<String> parts = item.split(u':');
        if (parts.size() != 3) {
            continue;
        }
        const std::vector<String> bars = parts[1].split(u'-');
        if (bars.size() != 2) {
            continue;
        }
        bool ok1 = false;
        bool ok2 = false;
        bool ok3 = false;
        CondenseOverride o;
        o.staffIdx = size_t(parts[0].toInt(&ok1));
        o.fromBar = bars[0].toInt(&ok2);
        o.toBar = bars[1].toInt(&ok3);
        if (!ok1 || !ok2 || !ok3 || o.toBar < o.fromBar) {
            continue;
        }
        if (parts[2] == u"separate") {
            o.mode = CondenseOverride::Mode::Separate;
        } else if (parts[2] == u"condense") {
            o.mode = CondenseOverride::Mode::Condense;
        } else {
            continue;
        }
        out.push_back(o);
    }
    return out;
}

std::vector<CondenseOverride> readOverrides(const Score* copy)
{
    return copy ? parseOverrides(copy->masterScore()->metaTag(CONDENSE_OVERRIDES_TAG)) : std::vector<CondenseOverride>();
}

void setOverride(Score* copy, size_t staffIdx, int fromBar, int toBar, std::optional<CondenseOverride::Mode> mode)
{
    MasterScore* master = copy->masterScore();
    std::vector<CondenseOverride> list;
    // Bars covered by the new choice drop out of older overrides
    for (const CondenseOverride& o : readOverrides(master)) {
        if (o.staffIdx != staffIdx || o.toBar < fromBar || o.fromBar > toBar) {
            list.push_back(o);
            continue;
        }
        if (o.fromBar < fromBar) {
            CondenseOverride left = o;
            left.toBar = fromBar - 1;
            list.push_back(left);
        }
        if (o.toBar > toBar) {
            CondenseOverride right = o;
            right.fromBar = toBar + 1;
            list.push_back(right);
        }
    }
    if (mode) {
        list.push_back({ staffIdx, fromBar, toBar, *mode });
    }
    std::map<String, String> tags = master->metaTags();
    tags[CONDENSE_OVERRIDES_TAG] = overridesText(list);
    master->undo(new ChangeMetaTags(master, tags));
}

String combinedName(const String& a, const String& b)
{
    if (a.empty()) {
        return b;
    }
    if (a == b) {
        return a + u" 1.2";
    }
    size_t i = 0;
    while (i < a.size() && i < b.size() && a.at(i) == b.at(i)) {
        ++i;
    }
    // Back up to the start of the differing word ("Trumpet 1" / "Trumpet 2")
    while (i > 0 && a.at(i - 1) != u' ') {
        --i;
    }
    const String ra = a.mid(i);
    const String rb = b.mid(i);
    if (i > 0 && !ra.empty() && !rb.empty() && ra.size() <= 4 && rb.size() <= 4) {
        return a.left(i) + ra + u"." + rb;
    }
    return a + u" / " + b;
}

std::shared_ptr<EngravingProject> makeCondensedCopy(MasterScore* original, const std::vector<CondenseOverride>& overrides,
                                                    CondenseReport* report)
{
    CondenseReport dummy;
    CondenseReport* rep = report ? report : &dummy;
    *rep = CondenseReport();

    if (!original || isCondensedCopy(original)) {
        rep->notes.push_back(u"This is already a condensed copy. Update it from the original score.");
        return nullptr;
    }
    const std::vector<CondenseGroup> groups = condenseGroups(original);
    if (groups.empty()) {
        rep->notes.push_back(u"There are no players to condense: condensing needs two neighbouring single-staff parts "
                             u"of the same woodwind or brass instrument (for example Trumpet 1 and Trumpet 2). Strings, keyboards, "
                             u"voices and percussion are never condensed.");
        return nullptr;
    }

    HookPause pause;   // the copy is never kept in step with anything
    std::shared_ptr<EngravingProject> project = cloneScore(original);
    if (!project) {
        rep->notes.push_back(u"The score couldn't be copied.");
        return nullptr;
    }
    MasterScore* copy = project->masterScore();
    rep->groups = int(groups.size());

    copy->startCmd(muse::TranslatableString::untranslatable("Jubelle: condense"));

    // Linked passages make no sense in a copy
    for (const LinkGroup& lg : linkGroups(copy)) {
        copy->undoRemoveElement(toEngravingItem(lg.source.line));
        for (const LinkMember& t : lg.targets) {
            copy->undoRemoveElement(toEngravingItem(t.line));
        }
    }
    // Multimeasure rests would hide the bars to be condensed
    if (copy->style().styleB(Sid::createMultiMeasureRests)) {
        copy->undoChangeStyleVal(Sid::createMultiMeasureRests, false);
        rep->notes.push_back(u"Multimeasure rests are switched off in the condensed copy.");
    }
    copy->endCmd();
    copy->doLayout();

    // Fix the systems as they are now, so shared staves never reflow the music
    const std::vector<std::pair<Measure*, Measure*> > systems = systemsOf(copy);
    rep->systems = int(systems.size());

    copy->startCmd(muse::TranslatableString::untranslatable("Jubelle: condense"));
    for (size_t k = 0; k + 1 < systems.size(); ++k) {
        Measure* last = systems[k].second;
        if (!last->lineBreak() && !last->pageBreak() && !last->sectionBreak()) {
            last->undoSetBreak(true, LayoutBreakType::LINE);
        }
    }

    std::vector<GroupState> states;
    std::map<size_t, std::vector<std::pair<Measure*, bool> > > namesPlan;
    for (const CondenseGroup& g : groups) {
        GroupState state;
        state.group = g;
        std::vector<std::pair<Measure*, bool> > starts;
        for (const auto& [first, last] : systems) {
            std::vector<std::pair<Measure*, BarPlan> > plans;
            bool allCondensed = true;
            String why;
            for (Measure* m = first; m; m = m->nextMeasure()) {
                BarPlan p = planBar(m, g, barIndex(m), overrides);
                if (!p.condensed) {
                    allCondensed = false;
                    if (why.empty()) {
                        why = p.reason;
                    }
                }
                plans.push_back({ m, p });
                if (m == last) {
                    break;
                }
            }
            starts.push_back({ first, allCondensed });
            if (allCondensed) {
                ++rep->systemsCondensed;
                for (auto& [m, p] : plans) {
                    state.bars[m] = p;
                }
            } else {
                ++rep->systemsSeparate;
            }
        }
        namesPlan[g.first] = starts;
        states.push_back(state);
    }

    for (GroupState& st : states) {
        // Bars in time order
        std::vector<std::pair<Measure*, BarPlan> > bars;
        for (const auto& [m, p] : st.bars) {
            bars.push_back({ const_cast<Measure*>(m), p });
        }
        std::sort(bars.begin(), bars.end(), [](const auto& x, const auto& y) { return x.first->tick() < y.first->tick(); });

        for (const auto& [m, p] : bars) {
            writeBar(copy, st, m, p);
        }
        moveSpanners(copy, st, rep);
        clearSecondPlayer(copy, st);

        // Labels: when what is shown changes. A bar where both rest keeps the label
        String shown;
        Measure* prevBar = nullptr;
        for (const auto& [m, p] : bars) {
            if (prevBar && m != prevBar->nextMeasure()) {
                shown.clear();   // separate bars came between
            }
            prevBar = m;
            if (p.decision == Decision::AllRest) {
                continue;
            }
            if (!p.label.empty() && p.label != shown) {
                addLabel(copy, m, st.group.first, p.label);
            }
            shown = p.label;
        }

        // The second player's staff hides on the shared systems
        for (const auto& [m, p] : bars) {
            (void)p;
            m->setHideStaffIfEmpty(st.group.second, AutoOnOff::ON);
        }
        setNames(copy, st.group, namesPlan[st.group.first]);
    }

    std::map<String, String> tags = copy->metaTags();
    tags[CONDENSED_COPY_TAG] = u"1";
    tags[CONDENSED_FROM_TAG] = original->fileInfo() ? original->fileInfo()->fileName(true).toString() : String();
    tags[CONDENSE_OVERRIDES_TAG] = overridesText(overrides);
    copy->undo(new ChangeMetaTags(copy, tags));

    copy->endCmd();
    copy->doLayout();
    return project;
}

muse::io::path_t condensedPathFor(const muse::io::path_t& originalPath)
{
    const muse::io::path_t dir = muse::io::dirpath(originalPath);
    const muse::io::path_t base = muse::io::completeBasename(originalPath);
    return dir.appendingComponent(base.toString() + u" (condensed).mscz");
}

ExistingCopy readExistingCopy(const muse::io::path_t& path, const muse::modularity::ContextPtr& iocCtx)
{
    ExistingCopy e;
    std::error_code ec;
    e.exists = std::filesystem::exists(std::filesystem::u8path(path.toStdString()), ec);
    if (!e.exists) {
        return e;
    }
    MasterScore* score = compat::ScoreAccess::createMasterScoreWithBaseStyle(iocCtx);
    score->setFileInfoProvider(std::make_shared<LocalFileInfoProvider>(path));
    const muse::Ret ret = compat::loadMsczOrMscx(score, path, true);
    if (!ret) {
        LOGW() << "Jubelle: couldn't read " << path << ": " << ret.toString();
    }
    if (ret) {
        e.isCondensedCopy = isCondensedCopy(score);
        e.overrides = readOverrides(score);
    }
    delete score;
    return e;
}

bool writeCondensedFile(EngravingProject& project, const muse::io::path_t& path, String* error)
{
    auto fail = [&](const String& why) {
        if (error) {
            *error = why;
        }
        return false;
    };
    const ExistingCopy existing = readExistingCopy(path, project.masterScore()->iocContext());
    if (existing.exists && !existing.isCondensedCopy) {
        return fail(u"A file with the condensed copy's name already exists and isn't a condensed copy, so it was left alone. "
                    u"Rename or move that file and try again.");
    }

    muse::io::Buffer buffer;
    buffer.open(muse::io::IODevice::WriteOnly);
    MscWriter::Params params;
    params.device = &buffer;
    params.filePath = path;
    params.mainFileName = muse::io::completeBasename(path).toString() + u".mscx";
    params.mode = MscIoMode::Zip;
    MscWriter writer(params);
    if (!writer.open()) {
        return fail(u"The condensed copy couldn't be prepared for saving.");
    }
    const bool ok = project.writeMscz(writer, false);
    writer.close();
    if (!ok || writer.hasError()) {
        return fail(u"The condensed copy couldn't be written.");
    }

    const std::filesystem::path target = std::filesystem::u8path(path.toStdString());
    std::filesystem::path temp = target;
    temp += ".jubelle-tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        const muse::ByteArray& bytes = buffer.data();
        out.write(reinterpret_cast<const char*>(bytes.constData()), std::streamsize(bytes.size()));
        out.flush();
        if (!out) {
            std::error_code ec;
            std::filesystem::remove(temp, ec);
            return fail(u"The condensed copy couldn't be saved (is the disk full or the folder read-only?).");
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        std::filesystem::remove(temp, ec);
        return fail(u"The condensed copy couldn't be put in place. If it is open in Jubelle or another program, close it and try again.");
    }
    return true;
}
}
