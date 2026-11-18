/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "harmonyrunner.h"

#include "../chordidentifier.h"

#include <algorithm>
#include <map>
#include <set>

#include "engraving/dom/chord.h"
#include "engraving/dom/chordlist.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/pitchspelling.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/select.h"
#include "engraving/dom/selectionfilter.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/tempo.h"
#include "engraving/dom/tie.h"
#include "engraving/dom/utils.h"
#include "engraving/editing/transpose.h"
#include "engraving/rw/xmlreader.h"

using namespace mu::engraving;

namespace mu::notation::jubelle::harmony {
namespace {
int mod12(int v)
{
    return ((v % 12) + 12) % 12;
}

std::string lowerId(const Part* p, const Fraction& tick)
{
    const Instrument* ins = p->instrument(tick);
    return ins ? ins->id().toLower().toStdString() : std::string();
}

bool contains(const std::string& s, const char* what)
{
    return s.find(what) != std::string::npos;
}

std::string staffName(const Score* score, staff_idx_t idx)
{
    const Staff* st = idx < score->nstaves() ? score->staff(idx) : nullptr;
    return st && st->part() ? st->part()->partName().toStdString() : std::string();
}

//! Concert tpc of a tpc shown on a staff (chord symbols follow the Concert pitch button)
int concertTpcOnStaff(const Score* score, staff_idx_t staffIdx, const Fraction& tick, int tpc)
{
    if (!tpcIsValid(tpc) || score->style().styleB(Sid::concertPitch)) {
        return tpc;
    }
    const Interval v = score->staff(staffIdx)->transpose(tick);
    return v.isZero() ? tpc : Transpose::transposeTpc(tpc, v, true);
}

//! The written tpc a player reads, from the concert tpc
int writtenTpc(const Staff* staff, const Fraction& tick, int concertTpc)
{
    Interval v = staff->transpose(tick);
    if (v.isZero() || !tpcIsValid(concertTpc)) {
        return concertTpc;
    }
    v.flip();
    return Transpose::transposeTpc(concertTpc, v, true);
}

int concertFromWritten(const Staff* staff, const Fraction& tick, int written)
{
    const Interval v = staff->transpose(tick);
    return v.isZero() ? written : Transpose::transposeTpc(written, v, true);
}

//! One chord symbol of the passage, with every copy of it at that point (piano and guitar, say)
struct ChordAt {
    Fraction tick;
    ChordSymbol before;
    std::vector<Harmony*> copies;
    bool identified = false;   //!< named from the rhythm section's notes (no chord symbol here)
    bool noChord = false;      //!< N.C., or a symbol Jubelle can't read: the melody is left in unison there
    std::string where;         //!< "Xyz (bar 5)"
};

ChordSymbol fromHarmony(const Score* score, Harmony* h)
{
    ChordSymbol c;
    if (!h || h->chords().empty() || h->chords().size() > 1) {
        return c;   // a polychord: the lessons give no rule for it, so it isn't guessed at
    }
    HarmonyInfo* info = h->chords().front();
    const Fraction tick = h->tick();
    const int root = concertTpcOnStaff(score, h->staffIdx(), tick, info->rootTpc());
    if (!tpcIsValid(root)) {
        return c;   // N.C. and the like
    }
    const int bass = concertTpcOnStaff(score, h->staffIdx(), tick, info->bassTpc());
    const int bassPc = tpcIsValid(bass) ? tpcToPc(bass) : -1;
    // MuseScore's own reading of the symbol, from its chord list
    const ChordDescription* d = info->getDescription();
    if (d && !d->generated) {
        c = chordFromIntervals(root, unsigned(d->chord.getKeys()), bassPc);
    }
    if (!c.valid) {
        // not in the chord list: MuseScore's root with the quality as written ("m7", "7♭9", "°7")
        const ChordSymbol q = parseChordSymbol("C" + info->textName().toStdString());
        if (q.valid) {
            c = q;
            c.rootTpc = root;
            c.rootPc = tpcToPc(root);
            c.bassPc = bassPc;
        }
    }
    c.text = h->harmonyName().toStdString();
    return c;
}

//! Every standard chord symbol that sounds under [start, end): the one in force at the start, then each change
std::vector<ChordAt> chordsFor(Score* score, const Fraction& start, const Fraction& end, staff_idx_t preferred)
{
    std::map<Fraction, ChordAt> byTick;
    for (Segment* seg = score->firstSegment(SegmentType::ChordRest); seg && seg->tick() < end; seg = seg->next1(SegmentType::ChordRest)) {
        for (EngravingItem* a : seg->annotations()) {
            if (!a->isHarmony() || toHarmony(a)->harmonyType() != HarmonyType::STANDARD) {
                continue;
            }
            Harmony* h = toHarmony(a);
            ChordAt& at = byTick[seg->tick()];
            at.tick = seg->tick();
            at.copies.push_back(h);
        }
    }
    std::vector<ChordAt> out;
    for (auto& [tick, at] : byTick) {
        // read the copy on the chord-symbol staff when there is one, else the top one
        std::sort(at.copies.begin(), at.copies.end(), [preferred](Harmony* a, Harmony* b) {
            const bool pa = a->staffIdx() == preferred;
            const bool pb = b->staffIdx() == preferred;
            return pa != pb ? pa : a->staffIdx() < b->staffIdx();
        });
        at.before = fromHarmony(score, at.copies.front());
        if (!at.before.valid) {
            at.noChord = true;
            const Measure* m = score->tick2measure(tick);
            const std::string text = at.copies.front()->harmonyName().toStdString();
            at.where = (text.empty() ? std::string("N.C.") : text) + " (bar " + std::to_string(m ? m->no() + 1 : 0) + ")";
        }
        if (tick <= start) {
            out.clear();   // only the last one at or before the start sounds
        }
        out.push_back(at);
    }
    return out;
}

//! Where no chord symbol is in force, the chord the other parts sound (the rhythm section's written voicings),
//! named by Jubelle's Chord Identifier, which is reused here rather than duplicated
ChordSymbol chordSoundingAt(Score* score, const Fraction& tick, const std::set<staff_idx_t>& skip)
{
    std::vector<chordidentifier::InputNote> notes;
    for (staff_idx_t st = 0; st < score->nstaves(); ++st) {
        if (skip.count(st)) {
            continue;
        }
        const Staff* staff = score->staff(st);
        const Instrument* ins = staff->part() ? staff->part()->instrument(tick) : nullptr;
        if (!ins || ins->useDrumset()) {
            continue;
        }
        for (voice_idx_t v = 0; v < VOICES; ++v) {
            ChordRest* cr = score->findCR(tick, st * VOICES + v);
            if (cr && cr->isChord() && cr->tick() <= tick && cr->endTick() > tick) {
                for (const Note* n : toChord(cr)->notes()) {
                    notes.push_back({ n->pitch(), n->tpc1() });
                }
            }
        }
    }
    const chordidentifier::Result r = chordidentifier::identify(notes);
    if (!r.ok) {
        return ChordSymbol();
    }
    unsigned mask = 0;
    for (int iv : r.intervals) {
        mask |= 1u << (((iv % 12) + 12) % 12);
    }
    ChordSymbol c = chordFromIntervals(r.rootTpc, mask, r.bassPc);
    c.text = r.name;
    return c;
}

bool hasNotes(Score* score, staff_idx_t staffIdx, const Fraction& start, const Fraction& end)
{
    for (Segment* seg = score->tick2segment(start, true, SegmentType::ChordRest); seg && seg->tick() < end;
         seg = seg->next1(SegmentType::ChordRest)) {
        for (voice_idx_t v = 0; v < VOICES; ++v) {
            EngravingItem* e = seg->element(staffIdx * VOICES + v);
            if (e && e->isChord()) {
                return true;
            }
        }
    }
    return false;
}

//! The melody, one entry per attack (a tied note is one entry)
struct Melody {
    std::vector<MelodyNote> notes;
    std::vector<Fraction> ticks;
    std::map<Fraction, size_t> indexAt;   //!< every chord or rest in the melody staff → its entry
};

Melody readMelody(Score* score, staff_idx_t staffIdx, const Fraction& start, const Fraction& end, const std::vector<ChordAt>& chords)
{
    Melody m;
    const TempoMap* tm = score->tempomap();
    auto seconds = [tm](const Fraction& a, const Fraction& b) {
        return tm->tick2time(b.ticks()) - tm->tick2time(a.ticks());
    };
    auto chordIndexAt = [&chords](const Fraction& t) {
        int idx = -1;
        for (size_t i = 0; i < chords.size(); ++i) {
            if (chords[i].tick <= t) {
                idx = int(i);
            }
        }
        return idx;
    };
    const track_idx_t track = staffIdx * VOICES;
    for (Segment* seg = score->tick2segment(start, true, SegmentType::ChordRest); seg && seg->tick() < end;
         seg = seg->next1(SegmentType::ChordRest)) {
        EngravingItem* e = seg->element(track);
        if (!e || !e->isChordRest()) {
            continue;
        }
        ChordRest* cr = toChordRest(e);
        if (cr->isRest() || (cr->isChord() && toChord(cr)->notes().empty())) {
            MelodyNote n;
            n.rest = true;
            n.seconds = seconds(cr->tick(), cr->endTick());
            m.indexAt[cr->tick()] = m.notes.size();
            m.notes.push_back(n);
            m.ticks.push_back(cr->tick());
            continue;
        }
        Chord* c = toChord(cr);
        Note* top = c->upNote();
        if (top->tieBack() && !m.notes.empty() && top->firstTiedNote() != top) {
            // a tied continuation belongs to the note it continues
            const Fraction first = top->firstTiedNote()->chord()->tick();
            auto it = m.indexAt.find(first);
            if (it != m.indexAt.end()) {
                m.indexAt[cr->tick()] = it->second;
                continue;
            }
        }
        MelodyNote n;
        n.pitch = top->pitch();
        n.tpc = top->tpc1();
        Note* last = top->lastTiedNote();
        n.seconds = seconds(c->tick(), std::max(last->chord()->endTick(), c->endTick()));
        n.chordIndex = chordIndexAt(c->tick());
        m.indexAt[cr->tick()] = m.notes.size();
        m.notes.push_back(n);
        m.ticks.push_back(cr->tick());
    }
    return m;
}

//! Copies the melody's rhythm (and its articulations, slurs, ties, dynamics, breaths) to another staff,
//! the way Copy and Paste does
muse::ByteArray melodyRhythm(Score* score, staff_idx_t staffIdx, const Fraction& start, const Fraction& end)
{
    SelectionFilter& f = score->selectionFilter();
    const SelectionFilter saved = f;
    f.setFiltered(VoicesSelectionFilterTypes::ALL, false);
    f.setFiltered(VoicesSelectionFilterTypes::FIRST_VOICE, true);
    f.setFiltered(NotesInChordSelectionFilterTypes::ALL, false);
    f.setFiltered(NotesInChordSelectionFilterTypes::TOP_NOTE, true);
    f.setIncludeSingleNotes(true);
    f.setFiltered(ElementsSelectionFilterTypes::ALL, false);
    for (ElementsSelectionFilterTypes keep : { ElementsSelectionFilterTypes::DYNAMIC, ElementsSelectionFilterTypes::HAIRPIN,
                                               ElementsSelectionFilterTypes::ARTICULATION, ElementsSelectionFilterTypes::ORNAMENT,
                                               ElementsSelectionFilterTypes::SLUR, ElementsSelectionFilterTypes::TIE,
                                               ElementsSelectionFilterTypes::BREATH }) {
        f.setFiltered(keep, true);
    }
    Selection& sel = score->selection();
    Segment* s1 = score->tick2segment(start, true, SegmentType::ChordRest);
    Segment* s2 = end < score->endTick() ? score->tick2segment(end, true, SegmentType::ChordRest) : nullptr;
    sel.setRange(s1, s2, staffIdx, staffIdx + 1);
    sel.updateSelectedElements();
    muse::ByteArray data = sel.mimeData();
    f = saved;
    return data;
}
}

SectionStaves findSections(const Score* score)
{
    SectionStaves s;
    size_t tpt = 0;
    std::vector<staff_idx_t> tenorTrombones;
    std::vector<staff_idx_t> bassTrombones;
    size_t alto = 0;
    size_t tenor = 0;
    staff_idx_t piano = muse::nidx;
    staff_idx_t guitar = muse::nidx;
    const Fraction tick(0, 1);
    for (const Part* p : score->parts()) {
        if (p->staves().empty()) {
            continue;
        }
        const staff_idx_t idx = p->staves().front()->idx();
        const std::string id = lowerId(p, tick);
        if ((contains(id, "trumpet") || contains(id, "flugelhorn")) && !contains(id, "piccolo") && tpt < 4) {
            s.trumpets[tpt++] = idx;
        } else if (contains(id, "bass-trombone")) {
            bassTrombones.push_back(idx);
        } else if (contains(id, "trombone")) {
            tenorTrombones.push_back(idx);
        } else if (contains(id, "alto-saxophone") && alto < 2) {
            s.saxes[alto++] = idx;
        } else if (contains(id, "tenor-saxophone") && tenor < 2) {
            s.saxes[2 + tenor++] = idx;
        } else if (contains(id, "baritone-saxophone") && s.saxes[4] == muse::nidx) {
            s.saxes[4] = idx;
        } else if (contains(id, "piano") && piano == muse::nidx) {
            piano = idx;
        } else if (contains(id, "guitar") && guitar == muse::nidx) {
            guitar = idx;
        }
    }
    // trombones 1–3 (or 1–4), with the bass trombone at the bottom
    size_t k = 0;
    for (staff_idx_t t : tenorTrombones) {
        if (k < 4) {
            s.trombones[k++] = t;
        }
    }
    for (staff_idx_t t : bassTrombones) {
        if (k < 4) {
            s.trombones[k++] = t;
        }
    }
    s.chordSymbols = piano != muse::nidx ? piano : guitar != muse::nidx ? guitar : (score->nstaves() ? 0 : muse::nidx);
    return s;
}

namespace {
struct Work {
    bool ok = false;
    std::string problem;
    staff_idx_t melodyStaff = muse::nidx;
    Fraction start;
    Fraction end;
    SectionStaves staves;
    std::vector<ChordAt> chords;
    Melody melody;
    int identifiedChords = 0;
};

Work workFor(Score* score, const RunOptions& options)
{
    Work w;
    if (!score) {
        w.problem = "No score is open.";
        return w;
    }
    const Selection& sel = score->selection();
    if (!sel.isRange()) {
        w.problem = "Select the melody first: a range of bars or notes on the staff that has the melody.";
        return w;
    }
    w.melodyStaff = sel.staffStart();
    w.start = sel.tickStart();
    w.end = sel.tickEnd();
    const Staff* st = score->staff(w.melodyStaff);
    if (!st || !st->part() || !st->part()->instrument(w.start) || st->part()->instrument(w.start)->useDrumset()) {
        w.problem = "The top staff of the selection has no pitched melody.";
        return w;
    }
    w.staves = options.useStaves ? options.staves : findSections(score);
    w.chords = chordsFor(score, w.start, w.end, w.staves.chordSymbols);
    w.melody = readMelody(score, w.melodyStaff, w.start, w.end, w.chords);
    // no chord symbol in force: name the chord the rhythm section sounds (horns and the melody are not counted)
    {
        std::set<staff_idx_t> skip { w.melodyStaff };
        for (auto arr : { std::vector<staff_idx_t>(w.staves.trumpets.begin(), w.staves.trumpets.end()),
                          std::vector<staff_idx_t>(w.staves.trombones.begin(), w.staves.trombones.end()),
                          std::vector<staff_idx_t>(w.staves.saxes.begin(), w.staves.saxes.end()) }) {
            skip.insert(arr.begin(), arr.end());
        }
        bool added = false;
        ChordSymbol last;
        for (size_t i = 0; i < w.melody.notes.size(); ++i) {
            const MelodyNote& n = w.melody.notes[i];
            if (n.rest || n.chordIndex >= 0) {
                continue;
            }
            const ChordSymbol c = chordSoundingAt(score, w.melody.ticks[i], skip);
            if (!c.valid || (last.valid && last.rootPc == c.rootPc && last.family == c.family && last.alterations == c.alterations)) {
                continue;
            }
            ChordAt at;
            at.tick = w.melody.ticks[i];
            at.before = c;
            at.before.fixed = true;   // the rhythm section's own notes: no alterations or substitutions
            at.identified = true;
            w.chords.push_back(at);
            last = c;
            added = true;
        }
        if (added) {
            std::stable_sort(w.chords.begin(), w.chords.end(), [](const ChordAt& a, const ChordAt& b) { return a.tick < b.tick; });
            w.melody = readMelody(score, w.melodyStaff, w.start, w.end, w.chords);
            for (const ChordAt& c : w.chords) {
                w.identifiedChords += c.identified ? 1 : 0;
            }
        }
    }
    bool anyNote = false;
    for (const MelodyNote& n : w.melody.notes) {
        anyNote = anyNote || !n.rest;
    }
    if (!anyNote) {
        w.problem = "The selection has no melody notes.";
        return w;
    }
    w.ok = true;
    return w;
}

Ranges rangesFor(const Score* score, const SectionStaves& staves, const Fraction& tick)
{
    Ranges rg;
    for (size_t j = 0; j < 5; ++j) {
        const staff_idx_t idx = staves.saxes[j];
        if (idx == muse::nidx) {
            continue;
        }
        const Instrument* ins = score->staff(idx)->part()->instrument(tick);
        if (ins && ins->minPitchP() < ins->maxPitchP()) {
            // the instrument's own professional range (the same data Proofread checks)
            rg.saxLow[j] = ins->minPitchP();
            rg.saxHigh[j] = ins->maxPitchP();
        }
    }
    return rg;
}
}

namespace {
//! Everything the dialog should say before you write: what will happen that you might not expect, and the
//! choices that can't apply to this score
std::vector<std::string> noticesFor(Score* score, const Work& w, const RunOptions& o)
{
    std::vector<std::string> out;
    // the melody staff
    bool chords = false;
    bool graces = false;
    bool otherVoices = false;
    for (Segment* seg = score->tick2segment(w.start, true, SegmentType::ChordRest); seg && seg->tick() < w.end;
         seg = seg->next1(SegmentType::ChordRest)) {
        for (voice_idx_t v = 0; v < VOICES; ++v) {
            EngravingItem* e = seg->element(w.melodyStaff * VOICES + v);
            if (!e || !e->isChord()) {
                continue;
            }
            if (v > 0) {
                otherVoices = true;
                continue;
            }
            chords = chords || toChord(e)->notes().size() > 1;
            graces = graces || !toChord(e)->graceNotes().empty();
        }
    }
    if (chords) {
        out.push_back("The melody has chords in places: the top note is harmonised.");
    }
    if (otherVoices) {
        out.push_back("Only voice 1 of the melody staff is harmonised; voices 2–4 are ignored.");
    }
    if (graces) {
        out.push_back("Grace notes stay in the melody only; they are not copied to the new parts.");
    }
    // chords that can't be read
    std::vector<std::string> unreadable;
    for (const ChordAt& c : w.chords) {
        if (c.noChord) {
            unreadable.push_back(c.where);
        }
    }
    if (!unreadable.empty()) {
        std::string list;
        for (size_t i = 0; i < unreadable.size() && i < 6; ++i) {
            list += (i ? ", " : "") + unreadable[i];
        }
        out.push_back("No chord, or a chord symbol Jubelle can't read (" + list + (unreadable.size() > 6 ? ", …" : "")
                      + "): the melody is played in unison there rather than guessed.");
    }
    // the sections this score has
    auto count = [](const auto& arr) {
        int k = 0;
        for (auto s : arr) {
            k += s != muse::nidx ? 1 : 0;
        }
        return k;
    };
    const int tpts = count(w.staves.trumpets);
    const int tbns = count(w.staves.trombones);
    const int saxes = count(w.staves.saxes);
    const bool bari = w.staves.saxes[4] != muse::nidx;
    for (const staff_idx_t s : w.staves.trumpets) {
        if (s == w.melodyStaff) {
            out.push_back(staffName(score, s) + " has the melody and keeps it; the other parts are written around it.");
        }
    }
    if (o.orchestration.trumpets != TrumpetTechnique::LeaveOut && tpts > 0 && tpts < 4) {
        out.push_back("This score has " + std::to_string(tpts) + " trumpet(s): only the top " + std::to_string(tpts)
                      + " voice(s) of each trumpet voicing are written.");
    }
    if (o.orchestration.trumpets != TrumpetTechnique::LeaveOut && tpts == 0) {
        out.push_back("No trumpets were found, so the trumpet choice has no effect.");
    }
    if (o.orchestration.trombones != TromboneTechnique::LeaveOut && tbns == 0) {
        out.push_back("No trombones were found, so the trombone choice has no effect.");
    }
    if (o.orchestration.saxes != SaxTechnique::LeaveOut && saxes == 0) {
        out.push_back("No saxophones were found, so the saxophone choice has no effect.");
    }
    if (!bari && (o.orchestration.saxes == SaxTechnique::DoubleTrombones || o.orchestration.saxes == SaxTechnique::Drop2BariRoots)) {
        out.push_back(tbns >= 4 ? "There is no baritone saxophone: the bass trombone takes the roots (lesson 1)."
                      : "There is no baritone saxophone, so nobody takes the roots in this choice.");
    }
    if (o.orchestration.saxes == SaxTechnique::FourPartDrop2) {
        out.push_back("Four-part Drop 2 leaves Tenor 2 free (the player who is busy elsewhere).");
    }
    if (o.orchestration.trumpets == TrumpetTechnique::TrumpetTriads) {
        out.push_back("Trumpet triads are meant for a lead above about concert A5 (lesson 4); below that, closed voicing is usual.");
    }
    if (o.orchestration.altosFillHighTriads && o.orchestration.saxes != SaxTechnique::DoubleTrombones) {
        out.push_back("The altos fill the trumpet triads only with the full-band saxophone choice.");
    }
    if (o.harmony.halfStepSlide && !o.harmony.addAlterations) {
        // fine on its own; nothing to say
    }
    return out;
}
}

Scope scope(Score* score, const RunOptions& options)
{
    Scope sc;
    const Work w = workFor(score, options);
    sc.ok = w.ok;
    sc.problem = w.problem;
    if (w.melodyStaff == muse::nidx) {
        return sc;
    }
    sc.melodyStaff = w.melodyStaff;
    sc.melodyStaffName = staffName(score, w.melodyStaff);
    if (const Measure* m = score->tick2measure(w.start)) {
        sc.firstBar = m->no() + 1;
        sc.lastBar = sc.firstBar;
        for (const Measure* x = m; x && x->tick() < w.end; x = x->nextMeasure()) {
            sc.lastBar = x->no() + 1;
        }
    }
    for (const MelodyNote& n : w.melody.notes) {
        if (n.rest) {
            continue;
        }
        ++sc.melodyNotes;
        sc.notesWithoutChord += n.chordIndex < 0 ? 1 : 0;
        sc.fastNotes += n.seconds < FAST_NOTE_SECONDS ? 1 : 0;
        sc.longestNoteSeconds = std::max(sc.longestNoteSeconds, n.seconds);
    }
    for (const ChordAt& c : w.chords) {
        if (c.identified) {
            ++sc.identifiedChords;
        } else {
            ++sc.chordSymbols;
        }
        sc.chordNames.push_back(c.before.text);
    }
    sc.saxWarning = techniqueUnsuitable(options.orchestration.saxes, w.melody.notes);
    sc.notices = noticesFor(score, w, options);
    return sc;
}

Report run(Score* score, const RunOptions& options)
{
    Report report;
    Work w = workFor(score, options);
    if (!w.ok) {
        report.problem = w.problem;
        return report;
    }
    report.notices = noticesFor(score, w, options);
    const staff_idx_t selStaffStart = score->selection().staffStart();
    const staff_idx_t selStaffEnd = score->selection().staffEnd();

    // 1. voice the melody with the lesson rules
    std::vector<ChordSymbol> chords;
    for (const ChordAt& c : w.chords) {
        chords.push_back(c.before);
    }
    const std::vector<ChordSymbol> chordsBefore = chords;
    const std::vector<VoicedNote> voiced = voiceMelody(w.melody.notes, chords, options.harmony);
    OrchestrationOptions orch = options.orchestration;
    if (w.staves.saxes[4] == muse::nidx) {
        orch.bariAvailable = false;   // no baritone in this band: the bass trombone takes the roots (lesson 1)
    }
    const Ranges rg = rangesFor(score, w.staves, w.start);
    const std::vector<SectionNotes> parts = orchestrate(w.melody.notes, voiced, orch, rg);
    for (size_t i = 0; i < voiced.size(); ++i) {
        if (!w.melody.notes[i].rest && !voiced[i].voicing.ok) {
            // no chord, an unreadable one, or no voicing that keeps the rules: unison, and said so
            ++report.unisonNotes;
            const Measure* m = score->tick2measure(w.melody.ticks[i]);
            const int bar = m ? m->no() + 1 : 0;
            if (report.unisonBars.empty() || report.unisonBars.back() != bar) {
                report.unisonBars.push_back(bar);
            }
            continue;
        }
        if (w.melody.notes[i].rest || voiced[i].chordTone || !voiced[i].voicing.ok) {
            continue;
        }
        ++report.nonChordTones;
        report.planed += voiced[i].tool == NctTool::ChromaticPlaning ? 1 : 0;
        report.diatonic += voiced[i].tool == NctTool::Diatonic ? 1 : 0;
        report.diminished += voiced[i].tool == NctTool::Diminished ? 1 : 0;
        if (!voiced[i].note.empty() && std::find(report.notes.begin(), report.notes.end(), voiced[i].note) == report.notes.end()) {
            report.notes.push_back(voiced[i].note);
        }
    }

    // 2. the spelling of each generated pitch (lesson 8), at concert pitch
    const Staff* melodyStaff = score->staff(w.melodyStaff);
    auto concertSpelling = [&](size_t i, int p) {
        const MelodyNote& n = w.melody.notes[i];
        const VoicedNote& vn = voiced[i];
        if (mod12(p) == mod12(n.pitch)) {
            return n.tpc;   // a doubling of the melody is spelled as the melody
        }
        if (!vn.chordTone && vn.voicing.ok) {
            if (vn.tool == NctTool::Diminished && mod12(n.pitch - p) % 3 == 0) {
                return spellDiminished(n.pitch - mod12(n.pitch - p), n.pitch, n.tpc);
            }
            if (vn.tool == NctTool::ChromaticPlaning && i + 1 < w.melody.notes.size()) {
                const int step = n.pitch - w.melody.notes[i + 1].pitch;
                const int target = p - step;
                return spellChromatic(p, target, spellChordTone(target, vn.chord));
            }
        }
        if (vn.chord.valid && isChordTone(p, vn.chord)) {
            return spellChordTone(p, vn.chord);
        }
        if (n.chordIndex >= 0 && chords[size_t(n.chordIndex)].valid && isChordTone(p, chords[size_t(n.chordIndex)])) {
            return spellChordTone(p, chords[size_t(n.chordIndex)]);
        }
        // no chord to spell from: the key signature in force at this note
        return pitch2tpc(p, melodyStaff->concertKey(w.melody.ticks[i]), Prefer::NEAREST);
    };

    // 3. the target staves and which part each one plays
    struct Target {
        staff_idx_t staff;
        int section;   // 0 trumpets, 1 trombones, 2 saxes
        size_t slot;
    };
    std::vector<Target> targets;
    auto add = [&](staff_idx_t s, int section, size_t slot, bool wanted) {
        if (s != muse::nidx && s != w.melodyStaff && wanted) {
            targets.push_back({ s, section, slot });
        }
    };
    for (size_t j = 0; j < 4; ++j) {
        add(w.staves.trumpets[j], 0, j, orch.trumpets != TrumpetTechnique::LeaveOut);
        add(w.staves.trombones[j], 1, j, orch.trombones != TromboneTechnique::LeaveOut);
    }
    for (size_t j = 0; j < 5; ++j) {
        add(w.staves.saxes[j], 2, j, orch.saxes != SaxTechnique::LeaveOut);
    }
    auto pitchFor = [&](const Target& t, size_t i) {
        const SectionNotes& s = parts[i];
        return t.section == 0 ? s.trumpets[t.slot] : t.section == 1 ? s.trombones[t.slot] : s.saxes[t.slot];
    };

    // 4. write: the melody's rhythm on each target staff, then the pitches
    const muse::ByteArray rhythm = melodyRhythm(score, w.melodyStaff, w.start, w.end);
    for (const Target& t : targets) {
        const std::string name = staffName(score, t.staff);
        bool plays = false;
        for (size_t i = 0; i < parts.size() && !plays; ++i) {
            plays = !w.melody.notes[i].rest && pitchFor(t, i) >= 0;
        }
        if (!plays) {
            continue;   // e.g. the baritone in a unison too high for it, or the busy sax in four-part Drop 2
        }
        if (options.existing == ExistingNotes::KeepExisting && hasNotes(score, t.staff, w.start, w.end)) {
            report.stavesKept.push_back(name);
            continue;
        }
        Segment* dst = score->tick2segment(w.start, true, SegmentType::ChordRest);
        if (!dst) {
            continue;
        }
        XmlReader xml(rhythm);
        if (!score->pasteStaff(xml, dst, t.staff)) {
            report.notes.push_back("Couldn't copy the melody's rhythm to " + name + ".");
            continue;
        }
        ++report.stavesWritten;
        Staff* staff = score->staff(t.staff);
        const Instrument* ins = staff->part()->instrument(w.start);
        const track_idx_t track = t.staff * VOICES;
        std::vector<Note*> silence;
        struct Written {
            Note* note;
            int pitch;
            int writtenPitch;
            int base;        // the written tpc from the chord (lesson 8's first rules)
            bool tied;
            Fraction tick;
        };
        std::vector<Written> line;
        for (Segment* seg = score->tick2segment(w.start, true, SegmentType::ChordRest); seg && seg->tick() < w.end;
             seg = seg->next1(SegmentType::ChordRest)) {
            EngravingItem* e = seg->element(track);
            if (!e || !e->isChord()) {
                continue;
            }
            Chord* c = toChord(e);
            auto it = w.melody.indexAt.find(c->tick());
            if (it == w.melody.indexAt.end()) {
                continue;
            }
            const size_t i = it->second;
            const int p = pitchFor(t, i);
            // one note per player
            std::vector<Note*> notes = c->notes();
            Note* keep = c->upNote();
            for (Note* n : notes) {
                if (n != keep) {
                    score->undoRemoveElement(n);
                }
            }
            if (p < 0) {
                silence.push_back(keep);
                continue;
            }
            // spelled as the player reads it: no B♯, C♭, E♯, F♭ or double accidentals in the written part, except
            // where a planed half step needs one to look like the second it is (lesson 8)
            int tpc1 = concertSpelling(i, p);
            int tpc2 = writtenTpc(staff, c->tick(), tpc1);
            const bool planedStep = !voiced[i].chordTone && voiced[i].tool == NctTool::ChromaticPlaning;
            const int simpler = simplerSpelling(tpc2);
            if (simpler != tpc2 && (!planedStep || tpc2 < 6 || tpc2 > 26)) {
                tpc2 = simpler;
            }
            const bool tied = keep->tieBack() && keep->tieBack()->startNote() && !line.empty();
            line.push_back({ keep, p, p - staff->transpose(c->tick()).chromatic, tpc2, tied, c->tick() });
            if (!tied) {
                ++report.notesWritten;
            }
            if (ins && (p < ins->minPitchP() || p > ins->maxPitchP())) {
                ++report.outOfRange;
                const Measure* m = c->measure();
                report.outOfRangeWhere.push_back(name + ", bar " + std::to_string(m ? m->no() + 1 : 0));
            }
        }

        // "Most important of all: intervals must look the way they sound" (lesson 8). The whole line is respelled
        // together: each note may take its enharmonic (never a double accidental), and the spelling with the fewest
        // steps that look wrong is chosen; then the fewest B♯, C♭, E♯ and F♭; then the chord's own spelling.
        if (!line.empty()) {
            auto wrongLook = [](int fromTpc, int fromPitch, int toTpc, int toPitch) {
                const int semis = std::abs(toPitch - fromPitch);
                const int letters = std::abs(absStep(toTpc, toPitch) - absStep(fromTpc, fromPitch));
                const int oct = semis / 12;
                const int r = semis % 12;
                static const int expected[12] = { 0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6 };
                const int l = letters - 7 * oct;
                if (l == expected[r]) {
                    return false;
                }
                // a chromatic half step (C to C♯), the tritone either way, and the augmented 5th are ordinary spellings
                return !((r == 1 && l == 0) || (r == 6 && l == 4) || (r == 8 && l == 4));
            };
            auto valid = [&](int tpc2, const Fraction& tick) {
                const int tpc1 = concertFromWritten(staff, tick, tpc2);
                return tpc2 >= 6 && tpc2 <= 26 && tpc1 >= 6 && tpc1 <= 26;
            };
            const int INF = 1 << 28;
            const size_t n = line.size();
            std::vector<std::array<int, 3>> cand(n);
            std::vector<std::array<int, 3>> cost(n);
            std::vector<std::array<int, 3>> from(n);
            for (size_t j = 0; j < n; ++j) {
                const Written& x = line[j];
                cand[j] = { x.base, x.base - 12, x.base + 12 };
                for (size_t k = 0; k < 3; ++k) {
                    const int tp = cand[j][k];
                    const bool ok = (k == 0 || valid(tp, x.tick)) && tp >= 6 && tp <= 26;
                    int nodeCost = ok ? (k == 0 ? 0 : 1) + (tp >= 8 && tp <= 24 ? 0 : 4) : INF;
                    if (j == 0) {
                        cost[j][k] = nodeCost;
                        from[j][k] = 0;
                        continue;
                    }
                    int best = INF;
                    int arg = 0;
                    for (size_t q = 0; q < 3; ++q) {
                        if (cost[j - 1][q] >= INF || nodeCost >= INF) {
                            continue;
                        }
                        int edge = 0;
                        if (x.tied) {
                            edge = cand[j - 1][q] == tp ? 0 : INF;   // a tie holds one spelling
                        } else {
                            edge = wrongLook(cand[j - 1][q], line[j - 1].writtenPitch, tp, x.writtenPitch) ? 10 : 0;
                        }
                        if (edge < INF && cost[j - 1][q] + edge + nodeCost < best) {
                            best = cost[j - 1][q] + edge + nodeCost;
                            arg = int(q);
                        }
                    }
                    cost[j][k] = best;
                    from[j][k] = arg;
                }
            }
            int k = 0;
            for (int q = 1; q < 3; ++q) {
                if (cost[n - 1][size_t(q)] < cost[n - 1][size_t(k)]) {
                    k = q;
                }
            }
            std::vector<int> chosen(n);
            for (size_t j = n; j-- > 0;) {
                chosen[j] = cand[j][size_t(k)];
                k = from[j][size_t(k)];
            }
            for (size_t j = 1; j < n; ++j) {
                if (!line[j].tied && wrongLook(chosen[j - 1], line[j - 1].writtenPitch, chosen[j], line[j].writtenPitch)) {
                    ++report.awkwardSpellings;   // no spelling of this line makes every step look right
                    const Measure* m = score->tick2measure(line[j].tick);
                    report.awkwardWhere.push_back(name + ", bar " + std::to_string(m ? m->no() + 1 : 0));
                }
            }
            for (size_t j = 0; j < n; ++j) {
                const Written& x = line[j];
                const int tpc2 = cost[n - 1][0] >= INF && cost[n - 1][1] >= INF && cost[n - 1][2] >= INF ? x.base : chosen[j];
                const int tpc1 = concertFromWritten(staff, x.tick, tpc2);
                if (x.note->pitch() != x.pitch || x.note->tpc1() != tpc1 || x.note->tpc2() != tpc2) {
                    score->undoChangePitch(x.note, x.pitch, tpc1, tpc2);
                }
            }
        }
        for (Note* n : silence) {
            score->deleteItem(n);   // a rest where this player has nothing
        }
    }

    // 5. chord symbols the passage changed (alterations, the half-step slide): updated where they are
    for (size_t c = 0; c < chords.size() && c < w.chords.size(); ++c) {
        if (chords[c].rootPc == chordsBefore[c].rootPc && chords[c].alterations == chordsBefore[c].alterations) {
            continue;
        }
        const ChordAt& at = w.chords[c];
        bool onSymbolStaff = false;
        auto textFor = [&](staff_idx_t staffIdx) {
            ChordSymbol shown = chords[c];
            if (!score->style().styleB(Sid::concertPitch)) {
                shown.rootTpc = writtenTpc(score->staff(staffIdx), at.tick, shown.rootTpc);
            }
            return muse::String::fromStdString(chordText(shown));
        };
        for (Harmony* h : at.copies) {
            onSymbolStaff = onSymbolStaff || h->staffIdx() == w.staves.chordSymbols;
            Segment* seg = h->explicitParent() && h->explicitParent()->isSegment() ? toSegment(h->explicitParent()) : nullptr;
            if (!seg) {
                continue;
            }
            const staff_idx_t staffIdx = h->staffIdx();
            const track_idx_t trk = h->track();
            score->undoRemoveElement(h);
            Harmony* nh = Factory::createHarmony(seg);
            nh->setTrack(trk);
            nh->setParent(seg);
            nh->setHarmonyType(HarmonyType::STANDARD);
            nh->setHarmony(textFor(staffIdx));
            score->undoAddElement(nh);
            ++report.chordSymbolsChanged;
        }
        if (!onSymbolStaff && options.writeChordSymbols && w.staves.chordSymbols != muse::nidx && at.tick >= w.start) {
            Segment* seg = score->tick2segment(at.tick, true, SegmentType::ChordRest);
            if (seg) {
                Harmony* nh = Factory::createHarmony(seg);
                nh->setTrack(w.staves.chordSymbols * VOICES);
                nh->setParent(seg);
                nh->setHarmonyType(HarmonyType::STANDARD);
                nh->setHarmony(textFor(w.staves.chordSymbols));
                score->undoAddElement(nh);
                ++report.chordSymbolsWritten;
            }
        }
    }

    // the selection as it was (found again by its ticks: the notes in it may have been replaced)
    {
        Segment* s1 = score->tick2segment(w.start, true, SegmentType::ChordRest);
        Segment* s2 = w.end < score->endTick() ? score->tick2segment(w.end, true, SegmentType::ChordRest) : nullptr;
        score->selection().setRange(s1, s2, selStaffStart, selStaffEnd);
        score->selection().updateSelectedElements();
    }
    report.ok = true;
    return report;
}
}
