/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "pageturns.h"

#include <algorithm>
#include <map>

#include "engraving/dom/chord.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/layoutbreak.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/page.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/system.h"
#include "engraving/dom/tempo.h"
#include "engraving/style/style.h"

using namespace mu::engraving;

namespace mu::notation::jubelle {
namespace {
//! Jubelle's own page breaks carry this drawing-order value (breaks are never printed, so it changes nothing)
int jubelleZ()
{
    return int(ElementType::LAYOUT_BREAK) * 100 + 7;
}

LayoutBreak* pageBreakOf(const MeasureBase* mb)
{
    for (EngravingItem* e : mb->el()) {
        if (e && e->isLayoutBreak() && toLayoutBreak(e)->layoutBreakType() == LayoutBreakType::PAGE) {
            return toLayoutBreak(e);
        }
    }
    return nullptr;
}

bool isJubelleBreak(const LayoutBreak* lb)
{
    return lb && lb->z() == jubelleZ();
}

//! Sounding stretches of the whole score (all staves), merged and sorted: [start, end) in ticks
std::vector<std::pair<int, int> > soundingStretches(Score* score)
{
    std::vector<std::pair<int, int> > raw;
    for (Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (EngravingItem* e : seg->elist()) {
            if (e && e->isChord()) {
                const int start = seg->tick().ticks();
                raw.push_back({ start, start + toChord(e)->actualTicks().ticks() });
            }
        }
    }
    std::sort(raw.begin(), raw.end());
    std::vector<std::pair<int, int> > merged;
    for (const auto& r : raw) {
        if (!merged.empty() && r.first <= merged.back().second) {
            merged.back().second = std::max(merged.back().second, r.second);
        } else {
            merged.push_back(r);
        }
    }
    return merged;
}

double seconds(Score* score, int fromTick, int toTick)
{
    if (toTick <= fromTick) {
        return 0.0;
    }
    const TempoMap* tm = score->tempomap();
    return tm->tick2time(toTick) - tm->tick2time(fromTick);
}

struct Rests {
    Score* score = nullptr;
    std::vector<std::pair<int, int> > sound;
    int end = 0;

    explicit Rests(Score* s)
        : score(s), sound(soundingStretches(s)), end(s->endTick().ticks()) {}

    double before(int tick) const
    {
        // the last sound that starts before `tick`
        auto it = std::lower_bound(sound.begin(), sound.end(), std::make_pair(tick, 0));
        if (it == sound.begin()) {
            return seconds(score, 0, tick);
        }
        --it;
        return it->second >= tick ? 0.0 : seconds(score, it->second, tick);
    }

    double after(int tick) const
    {
        auto it = std::lower_bound(sound.begin(), sound.end(), std::make_pair(tick, 0));
        if (it != sound.begin() && std::prev(it)->second > tick) {
            return 0.0;   // something is still sounding across the turn
        }
        return it == sound.end() ? seconds(score, tick, end) : seconds(score, tick, it->first);
    }
};

TurnQuality qualityOf(double rest, const TurnSettings& s)
{
    return rest >= s.neededSeconds ? TurnQuality::Good : rest >= s.neededSeconds / 2.0 ? TurnQuality::Tight : TurnQuality::Poor;
}

bool isTurnPage(const Page* page, size_t index, const TurnSettings& s)
{
    TurnLayout layout = s.layout;
    if (layout == TurnLayout::Auto) {
        layout = page->style().styleB(Sid::pageTwosided) ? TurnLayout::AfterRightHandPages : TurnLayout::EveryPage;
    }
    switch (layout) {
    case TurnLayout::EveryPage: return true;
    case TurnLayout::AfterRightHandPages: return index % 2 == 0;
    case TurnLayout::AfterLeftHandPages: return index % 2 == 1;
    default: return true;
    }
}

//! the last written bar of a laid-out measure (a multi-measure rest stands for several)
Measure* writtenLast(MeasureBase* mb)
{
    if (!mb || !mb->isMeasure()) {
        return nullptr;
    }
    Measure* m = toMeasure(mb);
    return m->isMMRest() ? m->mmRestLast() : m;
}

Measure* lastMeasureOf(const System* system)
{
    const std::vector<MeasureBase*>& mbs = system->measures();
    for (size_t i = mbs.size(); i-- > 0;) {
        if (Measure* m = writtenLast(mbs[i])) {
            return m;
        }
    }
    return nullptr;
}

Measure* lastMeasureOf(const Page* page)
{
    const std::vector<System*>& systems = page->systems();
    for (size_t i = systems.size(); i-- > 0;) {
        if (Measure* m = lastMeasureOf(systems[i])) {
            return m;
        }
    }
    return nullptr;
}

PageTurn describe(const Rests& rests, Measure* last, int pageNo, const TurnSettings& s)
{
    PageTurn t;
    t.page = pageNo;
    t.lastMeasure = last;
    const Measure* next = last->nextMeasure();
    t.nextBar = next ? next->no() + 1 : last->no() + 2;
    const int tick = last->endTick().ticks();
    t.restBefore = rests.before(tick);
    t.restAfter = rests.after(tick);
    t.quality = qualityOf(t.rest(), s);
    const LayoutBreak* lb = pageBreakOf(last);
    t.jubelleBreak = isJubelleBreak(lb);
    t.userBreak = lb && !t.jubelleBreak;
    return t;
}

std::vector<PageTurn> turnsWith(Score* score, const Rests& rests, const TurnSettings& s)
{
    std::vector<PageTurn> out;
    const std::vector<Page*>& pages = score->pages();
    for (size_t i = 0; i + 1 < pages.size(); ++i) {   // the last page is never turned
        if (!isTurnPage(pages[i], i, s)) {
            continue;
        }
        if (Measure* last = lastMeasureOf(pages[i])) {
            out.push_back(describe(rests, last, int(i) + 1, s));
        }
    }
    return out;
}

void addJubelleBreak(Score* score, Measure* m)
{
    LayoutBreak* lb = Factory::createLayoutBreak(m);
    lb->setLayoutBreakType(LayoutBreakType::PAGE);
    lb->setZ(jubelleZ());
    lb->setTrack(0);
    lb->setParent(m);
    score->undoAddElement(lb);
}
}

double restBefore(Score* score, int tick)
{
    return score ? Rests(score).before(tick) : 0.0;
}

double restAfter(Score* score, int tick)
{
    return score ? Rests(score).after(tick) : 0.0;
}

std::vector<PageTurn> analyseTurns(Score* score, const TurnSettings& settings)
{
    if (!score) {
        return {};
    }
    return turnsWith(score, Rests(score), settings);
}

int clearJubelleTurns(Score* score)
{
    if (!score) {
        return 0;
    }
    std::vector<LayoutBreak*> mine;
    for (MeasureBase* mb = score->first(); mb; mb = mb->next()) {
        LayoutBreak* lb = pageBreakOf(mb);
        if (isJubelleBreak(lb)) {
            mine.push_back(lb);
        }
    }
    for (LayoutBreak* lb : mine) {
        score->undoRemoveElement(lb);
    }
    if (!mine.empty()) {
        score->doLayout();
    }
    return int(mine.size());
}

TurnResult optimiseTurns(Score* score, const TurnSettings& settings)
{
    TurnResult result;
    if (!score) {
        return result;
    }
    // start again from your own breaks only
    clearJubelleTurns(score);
    score->doLayout();
    const Rests rests(score);   // the music doesn't change, so this is read once

    const std::vector<PageTurn> before = turnsWith(score, rests, settings);
    std::map<int, PageTurn> beforeByPage;
    for (const PageTurn& t : before) {
        beforeByPage[t.page] = t;
    }

    // page by page, front to back: a moved turn changes every later page, so each is read after the one before it is settled
    for (size_t pageIndex = 0; pageIndex + 1 < score->pages().size(); ++pageIndex) {
        const Page* page = score->pages()[pageIndex];
        if (!isTurnPage(page, pageIndex, settings)) {
            continue;
        }
        Measure* last = lastMeasureOf(page);
        if (!last) {
            continue;
        }
        const PageTurn now = describe(rests, last, int(pageIndex) + 1, settings);
        TurnChange change;
        change.page = now.page;
        change.nextBarBefore = change.nextBarAfter = now.nextBar;
        change.restWas = change.restNow = now.rest();
        change.was = change.now = now.quality;
        change.userBreak = now.userBreak;

        if (!now.userBreak && now.quality != TurnQuality::Good) {
            // candidates: the ends of the bars on this page, from the back, no further than the fill limit
            const std::vector<System*>& systems = page->systems();
            const size_t firstAllowed = std::min(systems.size() - 1, size_t(double(systems.size()) * settings.minPageFill));
            Measure* best = nullptr;
            double bestRest = now.rest();
            bool found = false;
            for (size_t si = systems.size(); si-- > firstAllowed && !found;) {
                const std::vector<MeasureBase*>& mbs = systems[si]->measures();
                for (size_t mi = mbs.size(); mi-- > 0;) {
                    Measure* m = writtenLast(mbs[mi]);
                    if (!m || m == last) {
                        continue;
                    }
                    const int tick = m->endTick().ticks();
                    const double rest = rests.before(tick) + rests.after(tick);
                    if (rest >= settings.neededSeconds) {
                        best = m;      // the nearest good place: take it
                        bestRest = rest;
                        found = true;
                        break;
                    }
                    // otherwise the best of the rest, if it is clearly better than now
                    if (rest > bestRest + 0.25) {
                        best = m;
                        bestRest = rest;
                    }
                }
            }
            if (best) {
                addJubelleBreak(score, best);
                score->doLayout();
                const Measure* next = best->nextMeasure();
                change.nextBarAfter = next ? next->no() + 1 : best->no() + 2;
                change.restNow = bestRest;
                change.now = qualityOf(bestRest, settings);
                change.moved = true;
                ++result.moved;
            }
        }
        if (change.now == TurnQuality::Poor) {
            ++result.stillPoor;
        }
        result.turns.push_back(change);
    }
    return result;
}

bool isJubelleTurnBreak(const EngravingItem* item)
{
    return item && item->isLayoutBreak() && toLayoutBreak(item)->layoutBreakType() == LayoutBreakType::PAGE
           && isJubelleBreak(toLayoutBreak(item));
}

void keepTurnBreakAsOwn(Score* score, EngravingItem* item)
{
    if (!score || !isJubelleTurnBreak(item)) {
        return;
    }
    // Jubelle's own breaks are told apart by their stacking order only; the default makes it yours
    item->undoChangeProperty(Pid::Z, item->propertyDefault(Pid::Z));
}
}
