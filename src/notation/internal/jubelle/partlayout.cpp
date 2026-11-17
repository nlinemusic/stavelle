/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "partlayout.h"

#include <algorithm>
#include <map>
#include <tuple>
#include <unordered_map>

#include "engraving/dom/factory.h"
#include "engraving/dom/layoutbreak.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/score.h"
#include "engraving/dom/spacer.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/systemlock.h"
#include "engraving/editing/editsystemlocks.h"
#include "engraving/style/style.h"

using namespace mu::engraving;
using muse::String;

namespace mu::notation::jubelle {
namespace {
//! Bars match by tick; frames by (tick, n-th frame at that tick, type)
using Key = std::tuple<int, int, int>;

std::map<Key, MeasureBase*> keyedMeasures(const Score* score)
{
    std::map<Key, MeasureBase*> out;
    std::map<int, int> framesAtTick;
    for (MeasureBase* mb = score->first(); mb; mb = mb->next()) {
        const int tick = mb->tick().ticks();
        if (mb->isMeasure()) {
            out[{ tick, -1, int(mb->type()) }] = mb;
        } else {
            const int n = framesAtTick[tick]++;
            out[{ tick, n, int(mb->type()) }] = mb;
        }
    }
    return out;
}

std::unordered_map<const MeasureBase*, MeasureBase*> counterparts(const Score* source, const Score* destination)
{
    std::unordered_map<const MeasureBase*, MeasureBase*> out;
    const std::map<Key, MeasureBase*> dest = keyedMeasures(destination);
    for (const auto& [key, mb] : keyedMeasures(source)) {
        auto it = dest.find(key);
        out[mb] = it == dest.end() ? nullptr : it->second;
    }
    return out;
}

//! The style values copied with "Page and spacing settings"
const std::vector<Sid>& pageAndSpacingSids()
{
    static const std::vector<Sid> sids = {
        Sid::pageWidth, Sid::pageHeight, Sid::pagePrintableWidth,
        Sid::pageEvenTopMargin, Sid::pageEvenBottomMargin, Sid::pageEvenLeftMargin,
        Sid::pageOddTopMargin, Sid::pageOddBottomMargin, Sid::pageOddLeftMargin, Sid::pageTwosided,
        Sid::spatium, Sid::staffUpperBorder, Sid::staffLowerBorder,
        Sid::minSystemDistance, Sid::maxSystemDistance, Sid::staffDistance, Sid::akkoladeDistance,
        Sid::systemFrameDistance, Sid::frameSystemDistance, Sid::lastSystemFillLimit,
        Sid::minMeasureWidth, Sid::measureSpacing,
        Sid::enableVerticalSpread, Sid::spreadSystem, Sid::spreadSquareBracket, Sid::spreadCurlyBracket,
        Sid::minSystemSpread, Sid::maxSystemSpread, Sid::minStaffSpread, Sid::maxStaffSpread,
        Sid::maxAkkoladeDistance, Sid::maxPageFillSpread,
        Sid::createMultiMeasureRests, Sid::minEmptyMeasures, Sid::minMMRestWidth,
    };
    return sids;
}

String barText(const MeasureBase* mb)
{
    if (mb->isMeasure()) {
        return String(u"bar %1").arg(toMeasure(mb)->no() + 1);
    }
    return String(u"a frame");
}

//! The multi-measure rest a bar belongs to in its part, if any (needs a laid-out score)
const Measure* mmRestOf(const MeasureBase* mb)
{
    if (!mb || !mb->isMeasure()) {
        return nullptr;
    }
    const Measure* m = toMeasure(mb)->coveringMMRestOrThis();
    return m && m != mb && m->isMMRest() ? m : nullptr;
}

//! True when a break on this bar has no effect, because the bar is inside
//! (not at the end of) a multi-measure rest
bool breakIneffective(const MeasureBase* mb)
{
    const Measure* mm = mmRestOf(mb);
    return mm && mm->mmRestLast() != mb;
}

bool hasLayoutBreak(const MeasureBase* mb)
{
    return mb->lineBreak() || mb->pageBreak() || mb->noBreak();
}
}

PartLayoutCheck checkPartLayout(const Score* source, const Score* destination, const PartLayoutOptions& options)
{
    PartLayoutCheck check;
    if (!source || !destination || source == destination) {
        check.compatible = false;
        return check;
    }
    const auto map = counterparts(source, destination);

    for (const MeasureBase* smb = source->first(); smb; smb = smb->next()) {
        if (!hasLayoutBreak(smb)) {
            continue;
        }
        const MeasureBase* dmb = map.at(smb);
        if (!dmb) {
            check.compatible = false;
            check.issues.push_back(String(u"The break on %1 can't be placed: this part has no matching frame.").arg(barText(smb)));
            continue;
        }
        if (breakIneffective(dmb)) {
            check.compatible = false;
            const Measure* mm = mmRestOf(dmb);
            check.issues.push_back(String(u"Bars %1–%2 are a multi-measure rest here, so the break after bar %3 %4.")
                                   .arg(mm->mmRestFirst()->no() + 1).arg(mm->mmRestLast()->no() + 1).arg(toMeasure(dmb)->no() + 1)
                                   .arg(options.breaksInsideMMRests ? String(u"has no effect") : String(u"is skipped")));
        }
    }

    // Multi-measure rests in the source where this part plays: those bars
    // take more room here
    for (const MeasureBase* smb = source->first(); smb; smb = smb->next()) {
        const Measure* mm = mmRestOf(smb);
        if (!mm || mm->mmRestFirst() != smb) {
            continue;
        }
        bool destPlays = false;
        for (const Measure* m = mm->mmRestFirst(); m; m = m->nextMeasure()) {
            const MeasureBase* dmb = map.at(m);
            if (dmb && !mmRestOf(dmb)) {
                destPlays = true;
            }
            if (m == mm->mmRestLast()) {
                break;
            }
        }
        if (destPlays) {
            check.compatible = false;
            check.issues.push_back(String(u"The source has a multi-measure rest at bars %1–%2 where this part plays, so those bars may need more systems.")
                                   .arg(mm->mmRestFirst()->no() + 1).arg(mm->mmRestLast()->no() + 1));
        }
    }

    if (options.spacers && source->nstaves() != destination->nstaves()) {
        check.issues.push_back(String(u"This part has %1 staves and the source %2, so staff spacers are skipped.")
                               .arg(destination->nstaves()).arg(source->nstaves()));
    }

    if (!options.pageAndSpacing) {
        const MStyle& a = source->style();
        const MStyle& b = destination->style();
        if (a.styleV(Sid::pageWidth) != b.styleV(Sid::pageWidth) || a.styleV(Sid::pageHeight) != b.styleV(Sid::pageHeight)
            || a.styleV(Sid::spatium) != b.styleV(Sid::spatium)) {
            check.compatible = false;
            check.issues.push_back(String(u"The page or staff size differs and page settings aren't copied, so the bars per system may differ."));
        }
    }
    return check;
}

static void applyOne(const Score* source, Score* dest, const PartLayoutOptions& options, PartLayoutReport& report)
{
    report.destination = dest;
    const auto map = counterparts(source, dest);

    // 1. Page and spacing settings first, as they decide the multi-measure rests
    if (options.pageAndSpacing) {
        std::unordered_map<Sid, PropertyValue> values;
        for (Sid sid : pageAndSpacingSids()) {
            if (dest->style().styleV(sid) != source->style().styleV(sid)) {
                values[sid] = source->style().styleV(sid);
            }
        }
        if (!values.empty()) {
            dest->undoChangeStyleValues(values);
        }
        report.pageAndSpacing = true;
    }

    // 2. Breaks: the destination's own system/page/keep-together breaks are
    //    replaced (section breaks are shared with the score and left alone)
    for (MeasureBase* mb = dest->first(); mb; mb = mb->next()) {
        if (mb->lineBreak()) {
            mb->undoSetBreak(false, LayoutBreakType::LINE);
        }
        if (mb->pageBreak()) {
            mb->undoSetBreak(false, LayoutBreakType::PAGE);
        }
        if (mb->noBreak()) {
            mb->undoSetBreak(false, LayoutBreakType::NOBREAK);
        }
    }
    for (const MeasureBase* smb = source->first(); smb; smb = smb->next()) {
        if (!hasLayoutBreak(smb)) {
            continue;
        }
        MeasureBase* dmb = map.at(smb);
        if (!dmb) {
            ++report.skippedNoFrame;
            continue;
        }
        if (!options.breaksInsideMMRests && breakIneffective(dmb)) {
            ++report.skippedInMMRests;
            continue;
        }
        // Set on the bar itself, never redirected to the end of a
        // multi-measure rest, so it is where the source has it
        auto setBreak = [&](LayoutBreakType type) {
            LayoutBreak* lb = Factory::createLayoutBreak(dmb);
            lb->setLayoutBreakType(type);
            lb->setTrack(0);
            lb->setParent(dmb);
            dest->undoAddElement(lb, false);
        };
        if (smb->pageBreak()) {
            setBreak(LayoutBreakType::PAGE);
            ++report.pageBreaks;
        } else if (smb->lineBreak()) {
            setBreak(LayoutBreakType::LINE);
            ++report.systemBreaks;
        }
        if (smb->noBreak()) {
            setBreak(LayoutBreakType::NOBREAK);
            ++report.keepTogether;
        }
    }

    // 3. System locks
    if (options.systemLocks) {
        EditSystemLocks::undoRemoveAllLocks(dest);
        for (const SystemLock* lock : source->systemLocks()->allLocks()) {
            MeasureBase* s = map.at(lock->startMB());
            MeasureBase* e = map.at(lock->endMB());
            if (s && e && (s->isMeasure() || s->isHBox()) && (e->isMeasure() || e->isHBox())) {
                SystemLock* copy = new SystemLock(s, e);
                EditSystemLocks::undoAddSystemLock(dest, copy);
                ++report.systemLocks;
            }
        }
    }

    // 4. Measure widths (per part; MuseScore doesn't share them)
    if (options.measureWidths) {
        for (const MeasureBase* smb = source->first(); smb; smb = smb->next()) {
            MeasureBase* dmb = map.at(smb);
            if (!smb->isMeasure() || !dmb || !dmb->isMeasure()) {
                continue;
            }
            const double stretch = toMeasure(smb)->userStretch();
            if (toMeasure(dmb)->userStretch() != stretch) {
                dmb->undoChangeProperty(Pid::USER_STRETCH, stretch);
                ++report.measureWidths;
            }
        }
    }

    // 5. Staff spacers, only when the staves correspond
    if (options.spacers) {
        if (source->nstaves() != dest->nstaves()) {
            report.spacersSkipped = true;
        } else {
            for (const MeasureBase* smb = source->first(); smb; smb = smb->next()) {
                MeasureBase* dmb = map.at(smb);
                if (!smb->isMeasure() || !dmb || !dmb->isMeasure()) {
                    continue;
                }
                const Measure* sm = toMeasure(smb);
                Measure* dm = toMeasure(dmb);
                for (staff_idx_t i = 0; i < dest->nstaves(); ++i) {
                    for (Spacer* old : { dm->vspacerUp(i), dm->vspacerDown(i) }) {
                        if (old) {
                            dest->undoRemoveElement(old);
                        }
                    }
                    for (const Spacer* sp : { sm->vspacerUp(i), sm->vspacerDown(i) }) {
                        if (!sp) {
                            continue;
                        }
                        Spacer* copy = Factory::createSpacer(dm);
                        copy->setSpacerType(sp->spacerType());
                        copy->setGap(sp->gap());
                        copy->setTrack(i * VOICES);
                        copy->setParent(dm);
                        dest->undoAddElement(copy, false);
                        ++report.spacers;
                    }
                }
            }
        }
    }

    dest->setLayoutAll();
}

std::vector<PartLayoutReport> applyPartLayout(const Score* source, const std::vector<Score*>& destinations,
                                              const PartLayoutOptions& options)
{
    std::vector<PartLayoutReport> reports;
    if (!source) {
        return reports;
    }
    for (Score* dest : destinations) {
        if (!dest || dest == source) {
            continue;
        }
        PartLayoutReport r;
        applyOne(source, dest, options, r);
        reports.push_back(r);
    }
    return reports;
}
}
