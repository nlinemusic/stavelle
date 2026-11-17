/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 * Music Composition & Notation
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "chordidentifierrunner.h"

#include <algorithm>
#include <map>
#include <set>
#include <vector>

#include "chordidentifier.h"

#include "engraving/dom/chord.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/key.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/select.h"
#include "engraving/dom/staff.h"
#include "engraving/editing/transpose.h"

using namespace mu::engraving;

namespace mu::notation::chordidentifier {
bool HighlightColors::isHighlight(const muse::Color& c)
{
    return c == root() || c == third() || c == fifth() || c == seventh() || c == extension();
}

namespace {
//! What a run works on: pitched staves and a tick range, or a set of notes.
struct Work {
    bool wholeScore = false;
    bool list = false;
    std::vector<staff_idx_t> staves;  //!< pitched staves, top to bottom
    Fraction startTick;
    Fraction endTick;
    std::vector<Note*> listNotes;     //!< for a list selection
};

bool isPitchedStaff(const Score* score, staff_idx_t idx, const Fraction& tick)
{
    const Staff* staff = score->staff(idx);
    if (!staff || !staff->part()) {
        return false;
    }
    const Instrument* instrument = staff->part()->instrument(tick);
    return instrument && !instrument->useDrumset();
}

Work workFor(const Score* score)
{
    Work w;
    const Selection& sel = score->selection();

    if (sel.isList()) {
        std::set<staff_idx_t> staves;
        for (EngravingItem* e : sel.elements()) {
            std::vector<Note*> notes;
            if (e->isNote()) {
                notes.push_back(toNote(e));
            } else if (e->isChord()) {
                notes = toChord(e)->notes();
            }
            for (Note* n : notes) {
                if (n->chord() && !n->chord()->isGrace() && isPitchedStaff(score, n->staffIdx(), n->tick())) {
                    w.listNotes.push_back(n);
                    staves.insert(n->chord()->vStaffIdx());
                }
            }
        }
        if (!w.listNotes.empty()) {
            w.list = true;
            w.staves.assign(staves.begin(), staves.end());
            w.startTick = w.listNotes.front()->tick();
            w.endTick = w.startTick;
            for (Note* n : w.listNotes) {
                w.startTick = std::min(w.startTick, n->tick());
                w.endTick = std::max(w.endTick, n->chord()->endTick());
            }
            return w;
        }
        // A selection without notes (e.g. a rest) analyses nothing.
        w.list = true;
        return w;
    }

    staff_idx_t first = 0;
    staff_idx_t last = score->nstaves();
    if (sel.isRange()) {
        first = sel.staffStart();
        last = sel.staffEnd();
        w.startTick = sel.tickStart();
        w.endTick = sel.tickEnd();
    } else {
        w.wholeScore = true;
        w.startTick = Fraction(0, 1);
        w.endTick = score->endTick();
    }
    for (staff_idx_t s = first; s < last; ++s) {
        if (isPitchedStaff(score, s, w.startTick)) {
            w.staves.push_back(s);
        }
    }
    return w;
}

//! The notes sounding at one attack, and which of them start there.
struct Attack {
    Segment* segment = nullptr;
    std::vector<Note*> sounding;
    std::vector<Note*> attacked;
};

std::vector<Attack> attacksFor(Score* score, const Work& w)
{
    std::vector<Attack> attacks;

    if (w.list) {
        std::map<Segment*, Attack> bySegment;
        std::vector<Segment*> order;
        for (Note* n : w.listNotes) {
            Segment* seg = n->chord()->segment();
            auto it = bySegment.find(seg);
            if (it == bySegment.end()) {
                order.push_back(seg);
                it = bySegment.emplace(seg, Attack()).first;
                it->second.segment = seg;
            }
            it->second.sounding.push_back(n);
            it->second.attacked.push_back(n);
        }
        std::sort(order.begin(), order.end(), [](Segment* a, Segment* b) { return a->tick() < b->tick(); });
        for (Segment* s : order) {
            attacks.push_back(bySegment[s]);
        }
        return attacks;
    }

    std::vector<track_idx_t> tracks;
    for (staff_idx_t s : w.staves) {
        for (voice_idx_t v = 0; v < VOICES; ++v) {
            tracks.push_back(s * VOICES + v);
        }
    }
    if (tracks.empty()) {
        return attacks;
    }

    // Walk every chord/rest position, remembering what each voice is playing,
    // so notes still held from an earlier beat are part of later chords.
    std::map<track_idx_t, ChordRest*> current;
    for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        const Fraction t = s->tick();
        if (t >= w.endTick) {
            break;
        }
        bool attack = false;
        for (track_idx_t track : tracks) {
            EngravingItem* e = s->element(track);
            if (e && e->isChordRest()) {
                current[track] = toChordRest(e);
                if (e->isChord()) {
                    attack = true;
                }
            }
        }
        if (t < w.startTick || !attack) {
            continue;
        }

        Attack a;
        a.segment = s;
        for (track_idx_t track : tracks) {
            auto it = current.find(track);
            if (it == current.end() || !it->second->isChord()) {
                continue;
            }
            Chord* chord = toChord(it->second);
            if (chord->endTick() <= t) {
                continue; // finished before this attack
            }
            for (Note* n : chord->notes()) {
                a.sounding.push_back(n);
                if (chord->segment() == s) {
                    a.attacked.push_back(n);
                }
            }
        }
        if (!a.sounding.empty()) {
            attacks.push_back(a);
        }
    }
    return attacks;
}

Harmony* existingSymbol(Segment* seg, staff_idx_t staff, HarmonyType type)
{
    for (EngravingItem* a : seg->annotations()) {
        if (a->isHarmony() && a->staffIdx() == staff && toHarmony(a)->harmonyType() == type) {
            return toHarmony(a);
        }
    }
    return nullptr;
}

void placeSymbol(Score* score, Segment* seg, staff_idx_t staff, HarmonyType type, const std::string& text,
                 ExistingSymbols existing, Report& report)
{
    const muse::String newText = muse::String::fromStdString(text);
    Harmony* old = existingSymbol(seg, staff, type);
    if (old) {
        if (existing == ExistingSymbols::Keep || old->harmonyName() == newText) {
            ++report.keptExisting;
            return;
        }
        score->undoRemoveElement(old);
        ++report.replaced;
    } else {
        ++report.added;
    }

    Harmony* h = Factory::createHarmony(seg);
    h->setTrack(staff * VOICES);
    h->setParent(seg);
    h->setHarmonyType(type);
    h->setHarmony(newText);
    score->undoAddElement(h);
}

muse::Color colourFor(ToneRole role)
{
    switch (role) {
    case ToneRole::Root: return HighlightColors::root();
    case ToneRole::Third: return HighlightColors::third();
    case ToneRole::Fifth: return HighlightColors::fifth();
    case ToneRole::Seventh: return HighlightColors::seventh();
    case ToneRole::Extension: return HighlightColors::extension();
    case ToneRole::None: break;
    }
    return muse::Color();
}

void setColour(Note* n, const muse::Color& c)
{
    if (n->color() != c) {
        n->undoChangeProperty(Pid::COLOR, PropertyValue::fromValue(c));
    }
}

muse::Color defaultColour(Note* n)
{
    return n->propertyDefault(Pid::COLOR).value<muse::Color>();
}

int writtenTpc(const Staff* staff, const Fraction& tick, int concertTpc, bool concertPitch)
{
    Interval v = staff->transpose(tick);
    if (concertPitch || v.isZero()) {
        return concertTpc;
    }
    v.flip();
    return Transpose::transposeTpc(concertTpc, v, true);
}
}

Scope scope(const Score* score)
{
    Scope sc;
    const Work w = workFor(score);
    sc.wholeScore = w.wholeScore;
    sc.listSelection = w.list;
    sc.noteCount = static_cast<int>(w.listNotes.size());
    sc.staffCount = static_cast<int>(w.staves.size());
    if (!w.staves.empty()) {
        sc.firstStaffName = score->staff(w.staves.front())->part()->partName().toStdString();
        sc.lastStaffName = score->staff(w.staves.back())->part()->partName().toStdString();
    }
    const Measure* m = score->tick2measure(w.startTick);
    if (m) {
        sc.firstBar = m->no() + 1;
        sc.lastBar = sc.firstBar;
        for (const Measure* x = m; x && x->tick() < w.endTick; x = x->nextMeasure()) {
            sc.lastBar = x->no() + 1;
        }
    }
    return sc;
}

Report run(Score* score, const Options& options)
{
    Report report;
    if (!score || (!options.chordSymbols && !options.romanNumerals && !options.highlightChordTones)) {
        return report;
    }

    const Work w = workFor(score);
    if (w.staves.empty()) {
        return report;
    }
    const staff_idx_t symbolStaff = w.staves.front(); // chord symbols above the top staff
    const staff_idx_t romanStaff = w.staves.back();   // Roman numerals below the bottom staff
    const bool concertPitch = score->style().styleB(Sid::concertPitch);

    std::string previous;
    for (const Attack& a : attacksFor(score, w)) {
        std::vector<InputNote> input;
        for (const Note* n : a.sounding) {
            input.push_back({ n->pitch(), n->tpc1() }); // concert pitch and spelling
        }
        const Result chord = identify(input);

        if (options.highlightChordTones) {
            for (Note* n : a.attacked) {
                const ToneRole role = roleOf(chord, n->pitch());
                if (role != ToneRole::None) {
                    setColour(n, colourFor(role));
                    ++report.notesColoured;
                } else if (HighlightColors::isHighlight(n->color())) {
                    setColour(n, defaultColour(n)); // an old highlight that no longer applies
                    ++report.coloursRemoved;
                }
            }
        }

        if (!chord.ok) {
            ++report.unrecognised;
            continue;
        }

        const Fraction tick = a.segment->tick();
        std::string symbol;
        if (options.chordSymbols) {
            const Staff* staff = score->staff(symbolStaff);
            symbol = symbolText(chord, writtenTpc(staff, tick, chord.rootTpc, concertPitch),
                                writtenTpc(staff, tick, chord.bassTpc, concertPitch), options.slashBass);
        }
        std::string roman;
        if (options.romanNumerals) {
            const KeySigEvent ks = score->staff(romanStaff)->keySigEvent(tick);
            const bool minor = ks.mode() == KeyMode::MINOR || ks.mode() == KeyMode::AEOLIAN;
            roman = romanNumeral(chord, keyFromSignature(static_cast<int>(ks.concertKey()), minor), options.figuredBass);
        }

        const std::string signature = symbol + "|" + roman;
        if (options.onlyWhenChordChanges && signature == previous) {
            continue;
        }
        previous = signature;

        if (options.chordSymbols) {
            placeSymbol(score, a.segment, symbolStaff, HarmonyType::STANDARD, symbol, options.existing, report);
        }
        if (options.romanNumerals) {
            placeSymbol(score, a.segment, romanStaff, HarmonyType::ROMAN, roman, options.existing, report);
        }
    }
    return report;
}

Report removeHighlight(Score* score)
{
    Report report;
    if (!score) {
        return report;
    }
    const Work w = workFor(score);
    for (const Attack& a : attacksFor(score, w)) {
        for (Note* n : a.attacked) {
            if (HighlightColors::isHighlight(n->color())) {
                setColour(n, defaultColour(n));
                ++report.coloursRemoved;
            }
        }
    }
    return report;
}
}
