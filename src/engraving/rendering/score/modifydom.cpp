/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2023 MuseScore Limited and others
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
#include "modifydom.h"

#include <algorithm>
#include <array>
#include <map>

#include "dom/measure.h"
#include "dom/staff.h"
#include "dom/spannermap.h"
#include "dom/timesig.h"
#include "dom/trill.h"
#include "dom/ornament.h"
#include "dom/note.h"
#include "dom/pitchspelling.h"
#include "dom/key.h"
#include "dom/utils.h"
#include "dom/chord.h"
#include "dom/keysig.h"
#include "dom/hook.h"
#include "dom/part.h"
#include "dom/score.h"
#include "editing/addremoveelement.h"
#include "rendering/score/chordlayout.h"

using namespace mu::engraving::rendering::score;

void ModifyDom::setCrossMeasure(const Measure* measure, LayoutContext& ctx)
{
    bool crossMeasure = ctx.conf().styleB(Sid::crossMeasureValues);
    const DomAccessor& dom = ctx.dom();
    for (staff_idx_t staffIdx = 0; staffIdx < dom.nstaves(); ++staffIdx) {
        const Staff* staff = dom.staff(staffIdx);
        if (!staff->show()) {
            continue;
        }

        track_idx_t startTrack = staffIdx * VOICES;
        track_idx_t endTrack  = startTrack + VOICES;

        for (const Segment& segment : measure->segments()) {
            if (!segment.isJustType(SegmentType::ChordRest)) {
                continue;
            }

            for (track_idx_t t = startTrack; t < endTrack; ++t) {
                ChordRest* cr = segment.cr(t);
                if (!cr) {
                    continue;
                }
                if (cr->isChord()) {
                    Chord* chord = toChord(cr);
                    if (!chord->isGrace()) {
                        ChordLayout::crossMeasureSetup(chord, crossMeasure, ctx);
                    }
                }
            }
        }
    }
}

void ModifyDom::connectTremolo(Measure* m)
{
    m->connectTremolo();
}

//! Jubelle: automatic courtesy (cautionary) accidentals.
//! A staff line whose pitch a note altered in the previous measure, and which the
//! key of this measure spells differently, has its accidental restated on its first
//! note in this measure (for example F♯ in bar 1, then F in bar 2 shows ♮).
//! Uses the engine's own "force restate" mechanism, so the accidental is an
//! ordinary automatic one: it updates and disappears by itself as notes change.
//!
//! G2 rules, each its own style setting (defaults = the behaviour above):
//! - jubelleCourtesyAccidentalsBars: look back up to N bars, not just one
//!   (F♯ in bar 1, nothing on that line in bar 2, F in bar 3 shows ♮ with N ≥ 2);
//! - jubelleCourtesyAccidentalsOtherStaves: also alterations on the other staves
//!   of the same instrument, in earlier bars and earlier in the same bar
//!   (piano: F♯ in the right hand, then F in the left hand shows ♮);
//! - jubelleCourtesyAccidentalsParentheses: courtesy accidentals are drawn in
//!   parentheses (see Accidental::displayBracket; nothing stored changes).
namespace mu::engraving::rendering::score {
//! Restates `line` (or every octave of it) where the reader last saw `seen`
static void forceJubelleCourtesy(AccidentalState& as, int line, AccidentalVal seen, bool allOctaves)
{
    auto mark = [&](int l) {
        if (seen != as.accidentalVal(l)) {
            as.setForceRestateAccidental(l, true);
            as.setJubelleCourtesy(l, true);
        } else if (as.jubelleCourtesy(l)) {
            as.setForceRestateAccidental(l, false);
        }
    };
    if (!allOctaves) {
        mark(line);
        return;
    }
    for (int other = line % 7; other < MAX_ACC_STATE; other += 7) {
        mark(other);
    }
}

//! The staves whose notes count for courtesy accidentals on `staffIdx`
static std::vector<staff_idx_t> jubelleCourtesyStaves(const Score* score, staff_idx_t staffIdx, const Staff* staff)
{
    std::vector<staff_idx_t> staves { staffIdx };
    if (score->style().styleB(Sid::jubelleCourtesyAccidentalsOtherStaves)) {
        for (const Staff* s : staff->part()->staves()) {
            if (s->idx() != staffIdx && !s->isTabStaff(Fraction(0, 1))) {
                staves.push_back(s->idx());
            }
        }
    }
    return staves;
}

//! Calls f(chord) for every chord (grace notes included) in a ChordRest segment
//! that is drawn on staff `onStaff`, in the given part
template<typename F>
static void forEachJubelleChord(const Segment& segment, const Staff* staff, staff_idx_t onStaff, F f)
{
    for (track_idx_t t = staff->part()->startTrack(); t < staff->part()->endTrack(); ++t) {
        const EngravingItem* e = segment.element(t);
        if (!e || !e->isChord()) {
            continue;
        }
        const Chord* chord = toChord(e);
        auto one = [&](const Chord* ch) {
            if (ch->vStaffIdx() == onStaff) {
                f(ch);
            }
        };
        for (const Chord* grace : chord->graceNotesBefore()) {
            one(grace);
        }
        one(chord);
        for (const Chord* grace : chord->graceNotesAfter()) {
            one(grace);
        }
    }
}

static void markCourtesyAccidentals(const Measure* measure, staff_idx_t staffIdx, const Staff* staff, AccidentalState& as)
{
    const Score* score = measure->score();
    if (!score->style().styleB(Sid::jubelleCourtesyAccidentals)) {
        return;
    }

    const Measure* prev = measure->prevMeasure();
    if (!prev) {
        return;
    }

    // Only pitched, non-tablature staves use accidentals this way.
    if (staff->isTabStaff(prev->tick()) || staff->part()->instrument(prev->tick())->useDrumset()) {
        return;
    }

    // The bars to look back over, oldest first
    const int bars = std::clamp(score->style().styleI(Sid::jubelleCourtesyAccidentalsBars), 1, 4);
    std::vector<const Measure*> window;
    for (const Measure* m = prev; m && int(window.size()) < bars; m = m->prevMeasure()) {
        window.insert(window.begin(), m);
    }

    // Per line: the last altered value a reader saw, and when
    struct Pending {
        bool on = false;
        AccidentalVal val = AccidentalVal::NATURAL;
        int tick = -1;
    };
    std::array<Pending, MAX_ACC_STATE> pending;

    for (staff_idx_t s : jubelleCourtesyStaves(score, staffIdx, staff)) {
        const Staff* st = score->staff(s);
        std::array<Pending, MAX_ACC_STATE> mine;
        for (const Measure* m : window) {
            AccidentalState state;
            state.init(st->keySigEvent(m->tick()));
            std::array<bool, MAX_ACC_STATE> altered;
            altered.fill(false);
            std::array<bool, MAX_ACC_STATE> played;
            played.fill(false);
            std::array<int, MAX_ACC_STATE> lastTick;
            lastTick.fill(-1);

            for (const Segment& segment : m->segments()) {
                if (segment.isJustType(SegmentType::KeySig)) {
                    if (segment.element(s * VOICES)) {
                        // A key change cancels what came before it
                        state.init(st->keySigEvent(segment.tick()));
                        altered.fill(false);
                        mine.fill(Pending());
                    }
                } else if (segment.isJustType(SegmentType::ChordRest)) {
                    forEachJubelleChord(segment, st, s, [&](const Chord* ch) {
                        for (const Note* note : ch->notes()) {
                            const int line = absStep(note->tpc(), note->epitch());
                            if (line < MIN_ACC_STATE || line >= MAX_ACC_STATE) {
                                continue;
                            }
                            const AccidentalVal val = tpc2alter(note->tpc());
                            if (val != state.accidentalVal(line)) {
                                altered[line] = true;
                            }
                            state.setAccidentalVal(line, val);
                            played[line] = true;
                            lastTick[line] = segment.tick().ticks();
                        }
                    });
                }
            }
            for (int line = MIN_ACC_STATE; line < MAX_ACC_STATE; ++line) {
                if (played[line]) {
                    // A bar that plays this line settles it: pending only if it was altered
                    mine[line] = altered[line] ? Pending { true, state.accidentalVal(line), lastTick[line] } : Pending();
                }
            }
        }
        for (int line = MIN_ACC_STATE; line < MAX_ACC_STATE; ++line) {
            if (mine[line].on && (!pending[line].on || mine[line].tick > pending[line].tick)) {
                pending[line] = mine[line];
            }
        }
    }

    const bool allOctaves = score->style().styleB(Sid::jubelleCourtesyAccidentalsAllOctaves);
    for (int line = MIN_ACC_STATE; line < MAX_ACC_STATE; ++line) {
        if (pending[line].on) {
            forceJubelleCourtesy(as, line, pending[line].val, allOctaves);
        }
    }
}

//! Altered notes on the other staves of the instrument in this measure, in time
//! order, for courtesy accidentals within the bar (empty unless switched on)
struct JubelleStaffEvent {
    int tick = 0;
    int line = 0;
    AccidentalVal val = AccidentalVal::NATURAL;
};

static std::vector<JubelleStaffEvent> jubelleOtherStaffEvents(const Measure* measure, staff_idx_t staffIdx, const Staff* staff)
{
    std::vector<JubelleStaffEvent> events;
    const Score* score = measure->score();
    if (!score->style().styleB(Sid::jubelleCourtesyAccidentals)
        || !score->style().styleB(Sid::jubelleCourtesyAccidentalsOtherStaves)
        || staff->isTabStaff(measure->tick()) || staff->part()->instrument(measure->tick())->useDrumset()) {
        return events;
    }
    for (staff_idx_t s : jubelleCourtesyStaves(score, staffIdx, staff)) {
        if (s == staffIdx) {
            continue;
        }
        const Staff* st = score->staff(s);
        AccidentalState state;
        state.init(st->keySigEvent(measure->tick()));
        for (const Segment& segment : measure->segments()) {
            if (segment.isJustType(SegmentType::KeySig) && segment.element(s * VOICES)) {
                state.init(st->keySigEvent(segment.tick()));
            } else if (segment.isJustType(SegmentType::ChordRest)) {
                forEachJubelleChord(segment, st, s, [&](const Chord* ch) {
                    for (const Note* note : ch->notes()) {
                        const int line = absStep(note->tpc(), note->epitch());
                        if (line < MIN_ACC_STATE || line >= MAX_ACC_STATE) {
                            continue;
                        }
                        const AccidentalVal val = tpc2alter(note->tpc());
                        if (val != state.accidentalVal(line)) {
                            events.push_back({ segment.tick().ticks(), line, val });
                        }
                        state.setAccidentalVal(line, val);
                    }
                });
            }
        }
    }
    std::stable_sort(events.begin(), events.end(), [](const JubelleStaffEvent& a, const JubelleStaffEvent& b) {
        return a.tick < b.tick;
    });
    return events;
}

//! Applies the other staves' alterations up to and including `tick`
static void applyJubelleOtherStaffEvents(const std::vector<JubelleStaffEvent>& events, size_t& next, int tick, AccidentalState& as,
                                         bool allOctaves)
{
    std::map<int, AccidentalVal> latest;
    while (next < events.size() && events[next].tick <= tick) {
        latest[events[next].line] = events[next].val;
        ++next;
    }
    for (const auto& [line, val] : latest) {
        forceJubelleCourtesy(as, line, val, allOctaves);
    }
}
}

void ModifyDom::cmdUpdateNotes(const Measure* measure, const DomAccessor& dom)
{
    for (staff_idx_t staffIdx = 0; staffIdx < dom.nstaves(); ++staffIdx) {
        const Staff* staff = dom.staff(staffIdx);
        if (!staff->show()) {
            continue;
        }

        AccidentalState as;          // list of already set accidentals for this measure
        // initAccidentalState
        {
            as.init(staff->keySigEvent(measure->tick()));

            // Trills may carry an accidental into this measure that requires a force-restate
            int ticks = measure->tick().ticks();
            auto spanners = dom.spannerMap().findOverlapping(ticks, ticks, true);
            for (auto iter : spanners) {
                Spanner* spanner = iter.value;
                if (spanner->staffIdx() != staffIdx || !spanner->isTrill()
                    || spanner->tick() == measure->tick() || spanner->tick2() == measure->tick()) {
                    continue;
                }
                Ornament* ornament = toTrill(spanner)->ornament();
                Note* trillNote = ornament ? ornament->noteAbove() : nullptr;
                if (trillNote && trillNote->accidental() && ornament->showAccidental() == OrnamentShowAccidental::DEFAULT) {
                    int line = absStep(trillNote->tpc(), trillNote->epitch());
                    as.setForceRestateAccidental(line, true);
                }
            }

            markCourtesyAccidentals(measure, staffIdx, staff, as);
        }

        track_idx_t startTrack = staff->part()->startTrack();
        track_idx_t mainTrack = staffIdx * VOICES;
        track_idx_t endTrack = staff->part()->endTrack();

        // Jubelle: alterations earlier in this bar on the instrument's other staves
        const std::vector<JubelleStaffEvent> otherStaffEvents = jubelleOtherStaffEvents(measure, staffIdx, staff);
        const bool courtesyAllOctaves = measure->score()->style().styleB(Sid::jubelleCourtesyAccidentalsAllOctaves);
        size_t nextOtherStaffEvent = 0;

        for (const Segment& segment : measure->segments()) {
            if (segment.isJustType(SegmentType::KeySig)) {
                KeySig* ks = item_cast<KeySig*>(segment.element(mainTrack));
                if (ks) {
                    Fraction tick = segment.tick();
                    as.init(staff->keySigEvent(tick));
                }
            } else if (segment.isJustType(SegmentType::ChordRest)) {
                if (!otherStaffEvents.empty()) {
                    applyJubelleOtherStaffEvents(otherStaffEvents, nextOtherStaffEvent, segment.tick().ticks(), as, courtesyAllOctaves);
                }
                for (track_idx_t t = startTrack; t < endTrack; ++t) {
                    Chord* chord = item_cast<Chord*>(segment.element(t), CastMode::MAYBE_BAD); // maybe Rest
                    if (chord) {
                        chord->cmdUpdateNotes(&as, staffIdx);
                    }
                }
            }
        }
    }
}

void ModifyDom::createStems(const Measure* measure, LayoutContext& ctx)
{
    const DomAccessor& dom = ctx.dom();
    for (staff_idx_t staffIdx = 0; staffIdx < dom.nstaves(); ++staffIdx) {
        const Staff* staff = dom.staff(staffIdx);
        if (!staff->show()) {
            continue;
        }

        track_idx_t startTrack = staffIdx * VOICES;
        track_idx_t endTrack  = startTrack + VOICES;

        for (const Segment& segment : measure->segments()) {
            if (!segment.isJustType(SegmentType::ChordRest)) {
                continue;
            }

            for (track_idx_t t = startTrack; t < endTrack; ++t) {
                ChordRest* cr = segment.cr(t);
                if (!cr) {
                    continue;
                }

                auto createStems = [](Chord* chord) {
                    if (!chord->shouldHaveStem()) {
                        chord->removeStem();
                        return;
                    }

                    if (!chord->stem()) {
                        chord->createStem();
                    }
                };

                if (cr->isChord()) {
                    Chord* chord = toChord(cr);

                    for (Chord* c : chord->graceNotes()) {
                        createStems(c);
                    }

                    createStems(chord);     // create stems needed to calculate spacing
                    // stem direction can change later during beam processing
                }
            }
        }
    }
}

void ModifyDom::setTrackForChordGraceNotes(Measure* measure, const DomAccessor& dom)
{
    for (staff_idx_t staffIdx = 0; staffIdx < dom.nstaves(); ++staffIdx) {
        track_idx_t startTrack = staffIdx * VOICES;
        track_idx_t endTrack  = startTrack + VOICES;

        for (const Segment& segment : measure->segments()) {
            if (!segment.isJustType(SegmentType::ChordRest)) {
                continue;
            }

            for (track_idx_t t = startTrack; t < endTrack; ++t) {
                ChordRest* cr = segment.cr(t);
                if (!cr) {
                    continue;
                }

                if (cr->isChord()) {
                    Chord* chord = toChord(cr);
                    for (Chord* c : chord->graceNotes()) {
                        c->setTrack(t);
                    }
                }
            }
        }
    }
}

void ModifyDom::sortMeasureSegments(Measure* measure, LayoutContext& ctx)
{
    measure = measure->coveringMMRestOrThis();

    // Move segments between measure which need to move

    // Move key and time signature segments to the correct measure.
    // Depending on configuration, this could mean moving segments at the end of this measure to the start of the following measure
    // or moving segments from the start of the following measure to the end of this measure

    // Loop through clef, key, time sigs in this bar.  If any are found and shouldn't be in this bar, move to the next

    auto changeAppliesToRepeatAndContinuation = [&](const Segment& seg) -> bool {
        // Check if the change applies to the beginning of the repeat section as well as the continuation
        const std::vector<Measure*> measures = findFollowingRepeatMeasures(measure);
        for (const Measure* repeatMeasure : measures) {
            if (repeatMeasure == measure->nextMeasureMM()) {
                continue;
            }
            const Fraction startTick = repeatMeasure->tick();
            for (track_idx_t track = 0; track < ctx.dom().nstaves(); track += VOICES) {
                const Staff* staff = ctx.dom().staff(track2staff(track));

                if (seg.isKeySigType()) {
                    const Key continuationKey = staff->key(seg.tick());
                    const Key repeatKey = staff->key(startTick);
                    const Key keyBeforeContinuation = staff->key(seg.tick() - Fraction::eps());
                    return keyBeforeContinuation != continuationKey && continuationKey == repeatKey;
                }

                if (seg.isTimeSigType()) {
                    const TimeSig* continuationTimeSig = staff->timeSig(seg.tick());
                    const TimeSig* repeatTimeSig = staff->timeSig(startTick);
                    const TimeSig* timeSigBeforeContinuation = staff->timeSig(seg.tick() - Fraction::eps());
                    return continuationTimeSig && repeatTimeSig && timeSigBeforeContinuation
                           && timeSigBeforeContinuation->sig() != continuationTimeSig->sig()
                           && continuationTimeSig->sig() == repeatTimeSig->sig();
                }

                if (seg.isClefType()) {
                    const ClefType continuationClef = staff->clef(seg.tick());
                    const ClefType repeatClef = staff->clef(startTick);
                    const ClefType clefBeforeContinuation = staff->clef(seg.tick() - Fraction::eps());
                    if (clefBeforeContinuation != continuationClef && continuationClef == repeatClef) {
                        return true;
                    }
                }
            }
        }
        return false;
    };

    auto clefSegBarlinePosition = [&](const Segment& seg) -> ClefToBarlinePosition
    {
        for (EngravingItem* el : seg.elist()) {
            if (!el || !el->isClef()) {
                continue;
            }
            const Clef* clef = toClef(el);
            if (clef->clefToBarlinePosition() != ClefToBarlinePosition::AUTO) {
                return clef->clefToBarlinePosition();
            }
        }
        return ClefToBarlinePosition::AUTO;
    };

    MeasureBase* nextMb = measure ? measure->nextMM() : nullptr;

    if (!nextMb || !nextMb->isMeasure()) {
        return;
    }

    Measure* nextMeasure = toMeasure(nextMb);

    const bool sigsShouldBeInThisMeasure = ((measure->repeatEnd() && ctx.conf().styleB(Sid::changesBeforeBarlineRepeats))
                                            || (measure->repeatJump() && ctx.conf().styleB(Sid::changesBeforeBarlineOtherJumps)));
    std::vector<Segment*> segsToRemove;

    std::vector<Segment*> segsToMoveToNextMeasure;
    for (Segment& seg : measure->segments()) {
        if (seg.tick() != measure->endTick() || seg.isChordRestType()) {
            continue;
        }

        // Move clefs at the end of this measure into the next measure
        if (seg.isClefType()) {
            ClefToBarlinePosition pos = clefSegBarlinePosition(seg);
            // Clef position explicitly set by user
            if (pos == ClefToBarlinePosition::BEFORE) {
                continue;
            } else if (pos == ClefToBarlinePosition::AFTER) {
                segsToMoveToNextMeasure.push_back(&seg);
                continue;
            }

            // AUTO on normal barline
            if (!measure->repeatEnd()) {
                continue;
            }

            // Otherwise, pos is AUTO
            // Place changes before barline & clefs before repeats style settings
            if ((!sigsShouldBeInThisMeasure && !changeAppliesToRepeatAndContinuation(seg))
                || (measure->repeatEnd() && !ctx.conf().styleB(Sid::placeClefsBeforeRepeats))) {
                segsToMoveToNextMeasure.push_back(&seg);
                continue;
            }
        }

        // Move key sigs and time sigs at the end of this measure into the next measure
        if ((seg.isKeySigType() || seg.isTimeSigType()) && !seg.header() && !seg.trailer()
            && (!sigsShouldBeInThisMeasure || !changeAppliesToRepeatAndContinuation(seg))) {
            if (nextMeasure && nextMeasure->findSegmentR(seg.segmentType(), Fraction(0, 1))) {
                segsToRemove.push_back(&seg);
                continue;
            }
            segsToMoveToNextMeasure.push_back(&seg);
        }
    }

    // No next measure, so these segments are useless
    if (!nextMeasure) {
        for (Segment* seg : segsToMoveToNextMeasure) {
            ctx.mutDom().doUndoRemoveElement(seg);
        }
        return;
    }

    std::vector<Segment*> segsToMoveToThisMeasure;
    for (Segment& seg : nextMeasure->segments()) {
        if (seg.tick() != nextMeasure->tick() || seg.isChordRestType()) {
            continue;
        }

        // Move clefs at the beginning of the next measure into this measure
        if (seg.isClefType()) {
            ClefToBarlinePosition pos = clefSegBarlinePosition(seg);
            // Clef position explicitly set by user
            if (pos == ClefToBarlinePosition::BEFORE) {
                segsToMoveToThisMeasure.push_back(&seg);
                continue;
            } else if (pos == ClefToBarlinePosition::AFTER) {
                continue;
            }

            if (!measure->repeatEnd()) {
                segsToMoveToThisMeasure.push_back(&seg);
                continue;
            }

            // Otherwise, pos is AUTO
            // Place changes before barline & clefs before repeats style settings
            if ((sigsShouldBeInThisMeasure && changeAppliesToRepeatAndContinuation(seg))
                || !measure->repeatEnd() || ctx.conf().styleB(Sid::placeClefsBeforeRepeats)) {
                segsToMoveToThisMeasure.push_back(&seg);
            }
        }

        // Move key sigs and time sigs at the start of the next measure to the end of this measure
        if ((seg.isTimeSigType() || seg.isKeySigType()) && !seg.header() && !seg.trailer()
            && sigsShouldBeInThisMeasure && changeAppliesToRepeatAndContinuation(seg)) {
            if (measure->findSegmentR(seg.segmentType(), measure->ticks())) {
                segsToRemove.push_back(&seg);
                continue;
            }
            segsToMoveToThisMeasure.push_back(&seg);
        }
    }

    for (Segment* seg : segsToRemove) {
        // Don't add duplicate segs to a measure
        ctx.mutDom().doUndoRemoveElement(seg);

        // Remove from underlying measures
        Measure* segMeasure = seg->measure();
        if (segMeasure && segMeasure->isMMRest()) {
            Measure* target = nullptr;
            // Segments at the start of the measure -> mmRestFirst, end -> mmRestLast
            if (seg->rtick() == Fraction(0, 1) || seg->tick() == segMeasure->tick()) {
                target = segMeasure->mmRestFirst();
            } else {
                target = segMeasure->mmRestLast();
            }
            if (target && target != segMeasure) {
                Segment* counterpart = target->findSegmentR(seg->segmentType(), seg->rtick());
                if (counterpart) {
                    ctx.mutDom().doUndoRemoveElement(counterpart);
                }
            }
        }
    }

    for (Segment* seg : segsToMoveToNextMeasure) {
        Fraction origRtick = seg->rtick();

        seg->undoChangeProperty(Pid::END_OF_MEASURE_CHANGE, false);
        measure->score()->undo(new ChangeSegmentParent(seg, nextMeasure, Fraction(0, 1)));

        // Move in underlying segments
        if (measure->isMMRest()) {
            Measure* fromMmLast = measure->mmRestLast();
            if (fromMmLast) {
                Segment* underlyingSeg = fromMmLast->findSegmentR(seg->segmentType(), origRtick);
                if (underlyingSeg) {
                    underlyingSeg->undoChangeProperty(Pid::END_OF_MEASURE_CHANGE, false);
                    if (nextMeasure->isMMRest() && nextMeasure->mmRestFirst()) {
                        nextMeasure->score()->undo(new ChangeSegmentParent(underlyingSeg, nextMeasure->mmRestFirst(), Fraction(0, 1)));
                    } else {
                        ctx.mutDom().doUndoRemoveElement(underlyingSeg);
                    }
                }
            }
        }

        // If the destination is an MMRest but the source is not, create underlying segment
        if (nextMeasure->isMMRest() && !measure->isMMRest()) {
            Measure* mmFirst = nextMeasure->mmRestFirst();
            if (mmFirst && mmFirst != nextMeasure) {
                Segment* mmSeg = mmFirst->undoGetSegmentR(seg->segmentType(), Fraction(0, 1));
                mmSeg->undoChangeProperty(Pid::END_OF_MEASURE_CHANGE, false);
                for (track_idx_t track = 0; track < ctx.dom().ntracks(); track += VOICES) {
                    EngravingItem* e = seg->element(track);
                    if (!e) {
                        continue;
                    }
                    if (!mmSeg->element(track)) {
                        bool generated = e->generated();
                        EngravingItem* eClone = generated ? e->clone() : e->linkedClone();
                        eClone->setGenerated(generated);
                        eClone->setParent(mmSeg);
                        ctx.mutDom().doUndoAddElement(eClone);
                    }
                }
            }
        }
    }

    for (Segment* seg : segsToMoveToThisMeasure) {
        Fraction origRtick = seg->rtick();

        seg->undoChangeProperty(Pid::END_OF_MEASURE_CHANGE, true);
        measure->score()->undo(new ChangeSegmentParent(seg, measure, measure->ticks()));

        // Move in underlying segments
        if (nextMeasure->isMMRest()) {
            Measure* fromMmFirst = nextMeasure->mmRestFirst();
            if (fromMmFirst) {
                Segment* underlyingSeg = fromMmFirst->findSegmentR(seg->segmentType(), origRtick);
                if (underlyingSeg) {
                    underlyingSeg->undoChangeProperty(Pid::END_OF_MEASURE_CHANGE, true);
                    if (measure->isMMRest() && measure->mmRestLast()) {
                        measure->score()->undo(new ChangeSegmentParent(underlyingSeg, measure->mmRestLast(),
                                                                       measure->mmRestLast()->ticks()));
                    } else {
                        ctx.mutDom().doUndoRemoveElement(underlyingSeg);
                    }
                }
            }
        }

        // If the destination is an MMRest but the source is not, create underlying segment
        if (measure->isMMRest() && !nextMeasure->isMMRest()) {
            Measure* mmLast = measure->mmRestLast();
            if (mmLast && mmLast != measure) {
                Segment* mmSeg = mmLast->undoGetSegmentR(seg->segmentType(), measure->ticks());
                mmSeg->undoChangeProperty(Pid::END_OF_MEASURE_CHANGE, true);
                // Clone per-track elements from the moved segment into the underlying mmrest segment
                for (track_idx_t track = 0; track < ctx.dom().ntracks(); track += VOICES) {
                    EngravingItem* e = seg->element(track);
                    if (!e) {
                        continue;
                    }
                    if (!mmSeg->element(track)) {
                        bool generated = e->generated();
                        EngravingItem* eClone = generated ? e->clone() : e->linkedClone();
                        eClone->setGenerated(generated);
                        eClone->setParent(mmSeg);
                        ctx.mutDom().doUndoAddElement(eClone);
                    }
                }
            }
        }
    }

    // Check end of measure changes for top and underlying measures
    measure->checkEndOfMeasureChange();
    if (measure->isMMRest()) {
        Measure* mmLast = measure->mmRestLast();
        if (mmLast) {
            mmLast->checkEndOfMeasureChange();
        }
    }
    if (nextMeasure->isMMRest()) {
        Measure* mmFirst = nextMeasure->mmRestFirst();
        if (mmFirst) {
            mmFirst->checkEndOfMeasureChange();
        }
    }

    // Sort segments at start of next measure
    removeAndAddBeginSegments(nextMeasure);
    if (nextMeasure->isMMRest()) {
        Measure* mmFirst = nextMeasure->mmRestFirst();
        if (mmFirst) {
            removeAndAddBeginSegments(mmFirst);
        }
    }

    // Sort segments at start of first measure
    Measure* prevMeasure = measure->prevMeasure();
    if (prevMeasure && prevMeasure == ctx.dom().firstMeasure()) {
        removeAndAddBeginSegments(prevMeasure);
        if (prevMeasure->isMMRest()) {
            Measure* mmFirst = prevMeasure->mmRestFirst();
            if (mmFirst) {
                removeAndAddBeginSegments(mmFirst);
            }
        }
    }
}

void ModifyDom::removeAndAddBeginSegments(Measure* measure)
{
    std::vector<Segment*> segsToSort;
    for (Segment& seg : measure->segments()) {
        if (seg.rtick() != Fraction(0, 1) || seg.isChordRestType() || seg.isEndBarLineType()) {
            continue;
        }

        segsToSort.push_back(&seg);
    }

    // Re-add the segments. They will be placed in their correct positions
    for (Segment* seg : segsToSort) {
        measure->segments().remove(seg);
    }
    for (Segment* seg : segsToSort) {
        measure->add(seg);
    }
}
