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
};

ChordSymbol fromHarmony(const Score* score, Harmony* h)
{
    ChordSymbol c;
    if (!h || h->chords().empty()) {
        return c;
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
    const Key key = score->staff(w.melodyStaff)->concertKey(w.start);
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
        return pitch2tpc(p, key, Prefer::NEAREST);
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
        int prevWrittenTpc = Tpc::TPC_INVALID;   // the part's previous note, as the player reads it
        int prevWrittenPitch = -1;
        Note* prevNote = nullptr;
        int prevPrevWrittenTpc = Tpc::TPC_INVALID;
        int prevPrevWrittenPitch = -1;
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
            // where a planed half step needs one to look like the second it is ("most important of all", lesson 8)
            int tpc1 = concertSpelling(i, p);
            int tpc2 = writtenTpc(staff, c->tick(), tpc1);
            const bool planedStep = !voiced[i].chordTone && voiced[i].tool == NctTool::ChromaticPlaning;
            const int simpler = simplerSpelling(tpc2);
            if (simpler != tpc2 && (!planedStep || tpc2 < 6 || tpc2 > 26)) {
                tpc2 = simpler;
                tpc1 = concertFromWritten(staff, c->tick(), tpc2);
            }
            const int writtenPitch = p - staff->transpose(c->tick()).chromatic;
            if (keep->tieBack() && keep->tieBack()->startNote()) {
                // a tied note keeps the spelling of the note it continues
                tpc1 = keep->tieBack()->startNote()->tpc1();
                tpc2 = keep->tieBack()->startNote()->tpc2();
            } else if (tpcIsValid(prevWrittenTpc)) {
                // "most important of all": the step from the previous note must look the way it sounds (lesson 8).
                auto looksWrong = [](int fromTpc, int fromPitch, int toTpc, int toPitch) {
                    if (!tpcIsValid(fromTpc)) {
                        return false;
                    }
                    const int semis = std::abs(toPitch - fromPitch);
                    const int letters = std::abs(absStep(toTpc, toPitch) - absStep(fromTpc, fromPitch));
                    return ((semis == 1 || semis == 2) && letters == 2) || ((semis == 3 || semis == 4) && letters == 1)
                           || (semis == 0 && letters != 0);
                };
                auto nice = [](int tpc) { return tpc >= 8 && tpc <= 24; };   // no B♯, C♭, E♯, F♭, no doubles
                if (looksWrong(prevWrittenTpc, prevWrittenPitch, tpc2, writtenPitch)) {
                    bool fixed = false;
                    // 1. this note, enharmonically, without B♯, C♭, E♯ or F♭
                    for (int alt : { tpc2 - 12, tpc2 + 12 }) {
                        if (!fixed && nice(alt) && !looksWrong(prevWrittenTpc, prevWrittenPitch, alt, writtenPitch)) {
                            tpc2 = alt;
                            fixed = true;
                        }
                    }
                    // 2. respell the phrase: the previous note instead, if that keeps its own step right
                    if (!fixed && prevNote && !prevNote->tieFor() && !prevNote->tieBack()) {
                        for (int alt : { prevWrittenTpc - 12, prevWrittenTpc + 12 }) {
                            if (!fixed && nice(alt) && !looksWrong(alt, prevWrittenPitch, tpc2, writtenPitch)
                                && !looksWrong(prevPrevWrittenTpc, prevPrevWrittenPitch, alt, prevWrittenPitch)) {
                                score->undoChangePitch(prevNote, prevNote->pitch(), concertFromWritten(staff, prevNote->tick(), alt), alt);
                                prevWrittenTpc = alt;
                                fixed = true;
                            }
                        }
                    }
                    // 3. this note with B♯, C♭, E♯ or F♭ (but never a double accidental)
                    for (int alt : { tpc2 - 12, tpc2 + 12 }) {
                        if (!fixed && alt >= 6 && alt <= 26 && !looksWrong(prevWrittenTpc, prevWrittenPitch, alt, writtenPitch)) {
                            tpc2 = alt;
                            fixed = true;
                        }
                    }
                    tpc1 = concertFromWritten(staff, c->tick(), tpc2);
                }
            }
            prevPrevWrittenTpc = prevWrittenTpc;
            prevPrevWrittenPitch = prevWrittenPitch;
            prevNote = keep;
            prevWrittenTpc = tpc2;
            prevWrittenPitch = writtenPitch;
            if (keep->pitch() != p || keep->tpc1() != tpc1 || keep->tpc2() != tpc2) {
                score->undoChangePitch(keep, p, tpc1, tpc2);
            }
            if (keep->tieBack() == nullptr || keep->firstTiedNote() == keep) {
                ++report.notesWritten;
            }
            if (ins && (p < ins->minPitchP() || p > ins->maxPitchP())) {
                ++report.outOfRange;
                const Measure* m = c->measure();
                report.outOfRangeWhere.push_back(name + ", bar " + std::to_string(m ? m->no() + 1 : 0));
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
