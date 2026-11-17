/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "overlapchecker.h"

#include <algorithm>

#include "dom/articulation.h"
#include "dom/chord.h"
#include "dom/chordrest.h"
#include "dom/lyrics.h"
#include "dom/measure.h"
#include "dom/note.h"
#include "dom/page.h"
#include "dom/score.h"
#include "dom/segment.h"
#include "dom/spanner.h"
#include "dom/stem.h"
#include "dom/system.h"
#include "infrastructure/shape.h"

using namespace muse;

namespace mu::engraving::jubelle {
LayoutPriority priorityOf(const EngravingItem* item)
{
    switch (item->type()) {
    case ElementType::NOTE:
    case ElementType::STEM:
    case ElementType::HOOK:
    case ElementType::BEAM:
    case ElementType::REST:
    case ElementType::MMREST:
    case ElementType::ACCIDENTAL:
    case ElementType::NOTEDOT:
        return LayoutPriority::Notes;
    case ElementType::ARTICULATION:
    case ElementType::ORNAMENT:
    case ElementType::FERMATA:
        return LayoutPriority::Articulations;
    case ElementType::SLUR_SEGMENT:
    case ElementType::TIE_SEGMENT:
    case ElementType::LAISSEZ_VIB_SEGMENT:
    case ElementType::PARTIAL_TIE_SEGMENT:
        return LayoutPriority::SlursTies;
    case ElementType::DYNAMIC:
    case ElementType::HAIRPIN_SEGMENT:
        return LayoutPriority::Dynamics;
    case ElementType::LYRICS:
    case ElementType::LYRICSLINE_SEGMENT:
        return LayoutPriority::Lyrics;
    case ElementType::STAFF_TEXT:
    case ElementType::SYSTEM_TEXT:
    case ElementType::EXPRESSION:
    case ElementType::HARMONY:
    case ElementType::FINGERING:
    case ElementType::TEXTLINE_SEGMENT:
    case ElementType::PLAYTECH_ANNOTATION:
    case ElementType::CAPO:
    case ElementType::INSTRUMENT_CHANGE:
    case ElementType::STRING_TUNINGS:
        return LayoutPriority::Text;
    case ElementType::REHEARSAL_MARK:
    case ElementType::TEMPO_TEXT:
    case ElementType::TRIPLET_FEEL:
        return LayoutPriority::RehearsalTempo;
    default:
        return LayoutPriority::Other;
    }
}

namespace {
struct Item {
    EngravingItem* item = nullptr;
    Shape shape;            // page coordinates
    RectF box;              // page coordinates
    LayoutPriority priority = LayoutPriority::Other;
    const EngravingItem* owner = nullptr;   // the chord or rest it belongs to, if any
};

const EngravingItem* ownerOf(const EngravingItem* e)
{
    for (const EngravingItem* p = e; p; p = p->parentItem()) {
        if (p->isChordRest()) {
            // grace notes belong to their main chord
            const EngravingItem* up = p->parentItem();
            if (p->isChord() && toChord(p)->isGrace() && up && up->isChord()) {
                return up;
            }
            return p;
        }
        if (p->isSegment() || p->isSystem() || p->isMeasure()) {
            return nullptr;
        }
    }
    return nullptr;
}

void add(std::vector<Item>& out, EngravingItem* e)
{
    if (!e || !e->visible() || e->ldata()->isSkipDraw()) {
        return;
    }
    const LayoutPriority prio = priorityOf(e);
    if (prio == LayoutPriority::Other) {
        return;
    }
    Shape sh = e->shape().translated(e->pagePos());
    if (sh.empty()) {
        return;
    }
    const RectF box = sh.bbox();
    if (box.width() <= 0 || box.height() <= 0) {
        return;
    }
    out.push_back({ e, sh, box, prio, ownerOf(e) });
}

void addChordRest(std::vector<Item>& out, ChordRest* cr)
{
    if (cr->isChord()) {
        Chord* c = toChord(cr);
        for (Chord* g : c->graceNotes()) {
            addChordRest(out, g);
        }
        for (Note* n : c->notes()) {
            add(out, n);
        }
        add(out, c->stem());
        for (Articulation* a : c->articulations()) {
            add(out, a);
        }
    } else {
        add(out, cr);
    }
    for (Lyrics* l : cr->lyrics()) {
        add(out, l);
    }
}

std::vector<Item> collect(const System* system)
{
    std::vector<Item> items;
    for (MeasureBase* mb : system->measures()) {
        if (!mb->isMeasure()) {
            continue;
        }
        for (Segment* seg = toMeasure(mb)->first(); seg; seg = seg->next()) {
            if (seg->isChordRestType()) {
                for (EngravingItem* e : seg->elist()) {
                    if (e && e->isChordRest()) {
                        addChordRest(items, toChordRest(e));
                    }
                }
            }
            for (EngravingItem* a : seg->annotations()) {
                add(items, a);
            }
        }
    }
    for (SpannerSegment* ss : system->spannerSegments()) {
        add(items, ss);
    }
    return items;
}
}

size_t checkedItemCount(const System* system)
{
    return system ? collect(system).size() : 0;
}

namespace {
bool overlapBetween(const Item& a, const Item& b, double tol, RectF* area)
{
    if (a.priority == b.priority) {
        return false;   // same kind: MuseScore stacks these itself
    }
    if (a.owner && a.owner == b.owner) {
        return false;   // a marking against its own chord
    }
    *area = a.box.intersected(b.box);
    if (area->width() <= tol || area->height() <= tol) {
        return false;
    }
    return a.shape.intersects(b.shape);   // boxes can touch where outlines don't
}

std::vector<Overlap> overlapsIn(std::vector<Item>& items, double tol)
{
    std::vector<Overlap> result;
    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.box.left() < b.box.left(); });
    for (size_t i = 0; i < items.size(); ++i) {
        const Item& a = items[i];
        for (size_t j = i + 1; j < items.size() && items[j].box.left() < a.box.right() - tol; ++j) {
            const Item& b = items[j];
            RectF area;
            if (!overlapBetween(a, b, tol, &area)) {
                continue;
            }
            const bool aStays = int(a.priority) < int(b.priority);
            result.push_back({ aStays ? a.item : b.item, aStays ? b.item : a.item, area });
        }
    }
    return result;
}
}

std::vector<Overlap> findOverlaps(const System* system, double toleranceSp)
{
    if (!system) {
        return {};
    }
    std::vector<Item> items = collect(system);
    return overlapsIn(items, toleranceSp * system->spatium());
}

namespace {
struct Entry {
    EngravingItem* item = nullptr;
    RectF coarse;                 // page coordinates, a little larger than the outline
    LayoutPriority priority = LayoutPriority::Other;
    const EngravingItem* owner = nullptr;
    // the exact outline, read when first needed
    mutable bool exact = false;
    mutable Shape shape;
    mutable RectF box;

    void ensureExact() const
    {
        if (!exact) {
            shape = item->shape().translated(item->pagePos());
            box = shape.bbox();
            exact = true;
        }
    }
};

bool exactOverlap(const Entry& a, const Entry& b, double tol, RectF* area)
{
    if (a.priority == b.priority || (a.owner && a.owner == b.owner) || !a.coarse.intersects(b.coarse)) {
        return false;
    }
    // one outline at a time: most near misses are settled by the first, so the second is never read
    a.ensureExact();
    if (a.shape.empty() || !a.box.intersects(b.coarse)) {
        return false;
    }
    b.ensureExact();
    if (b.shape.empty() || !a.box.intersects(b.box)) {
        return false;
    }
    *area = a.box.intersected(b.box);
    if (area->width() <= tol || area->height() <= tol) {
        return false;
    }
    return a.shape.intersects(b.shape);
}

void addEntry(std::vector<Entry>& out, EngravingItem* e, double pad)
{
    if (!e || !e->visible() || e->ldata()->isSkipDraw()) {
        return;
    }
    const LayoutPriority prio = priorityOf(e);
    if (prio == LayoutPriority::Other) {
        return;
    }
    Entry en;
    en.item = e;
    en.coarse = e->ldata()->bbox(LD_ACCESS::MAYBE_NOTINITED).translated(e->pagePos()).adjusted(-pad, -pad, pad, pad);
    en.priority = prio;
    en.owner = ownerOf(e);
    out.push_back(en);
}

void addChordRestEntries(std::vector<Entry>& out, ChordRest* cr, double pad)
{
    if (cr->isChord()) {
        Chord* c = toChord(cr);
        for (Chord* g : c->graceNotes()) {
            addChordRestEntries(out, g, pad);
        }
        for (Note* n : c->notes()) {
            addEntry(out, n, pad);
        }
        addEntry(out, c->stem(), pad);
        for (Articulation* a : c->articulations()) {
            addEntry(out, a, pad);
        }
    } else {
        addEntry(out, cr, pad);
    }
    for (Lyrics* l : cr->lyrics()) {
        addEntry(out, l, pad);
    }
}
}

struct OverlapIndex::Impl {
    std::vector<Entry> obstacles;      // never move; sorted by coarse.left
    std::vector<double> maxRight;      // largest coarse.right among obstacles[0..i]
    std::vector<Entry> candidates;
    double tol = 0.0;

    //! calls f(other) for every entry whose coarse box meets `c`'s
    template<typename F>
    void forNear(const Entry& c, F f) const
    {
        // obstacles left of c.coarse.right(), walking back while any earlier one can still reach c
        auto end = std::upper_bound(obstacles.begin(), obstacles.end(), c.coarse.right(), [](double x, const Entry& e) {
            return x < e.coarse.left();
        });
        for (size_t i = size_t(end - obstacles.begin()); i-- > 0;) {
            if (maxRight[i] < c.coarse.left()) {
                break;
            }
            f(obstacles[i]);
        }
        for (const Entry& other : candidates) {
            if (&other != &c) {
                f(other);
            }
        }
    }

    const Entry* find(const EngravingItem* item) const
    {
        for (const Entry& e : candidates) {
            if (e.item == item) {
                return &e;
            }
        }
        return nullptr;
    }
};

OverlapIndex::OverlapIndex(const System* system, IsCandidate isCandidate, double toleranceSp)
    : m_impl(new Impl)
{
    if (!system) {
        return;
    }
    m_impl->tol = toleranceSp * system->spatium();
    const double pad = 2.0 * system->spatium();   // parentheses and the like can reach past the plain box
    std::vector<Entry> all;
    for (MeasureBase* mb : system->measures()) {
        if (!mb->isMeasure()) {
            continue;
        }
        for (Segment* seg = toMeasure(mb)->first(); seg; seg = seg->next()) {
            if (seg->isChordRestType()) {
                for (EngravingItem* e : seg->elist()) {
                    if (e && e->isChordRest()) {
                        addChordRestEntries(all, toChordRest(e), pad);
                    }
                }
            }
            for (EngravingItem* a : seg->annotations()) {
                addEntry(all, a, pad);
            }
        }
    }
    for (SpannerSegment* ss : system->spannerSegments()) {
        addEntry(all, ss, pad);
    }
    for (Entry& e : all) {
        (isCandidate && isCandidate(e.item) ? m_impl->candidates : m_impl->obstacles).push_back(std::move(e));
    }
    std::sort(m_impl->obstacles.begin(), m_impl->obstacles.end(), [](const Entry& a, const Entry& b) {
        return a.coarse.left() < b.coarse.left();
    });
    m_impl->maxRight.reserve(m_impl->obstacles.size());
    double reach = -1e300;
    for (const Entry& e : m_impl->obstacles) {
        reach = std::max(reach, e.coarse.right());
        m_impl->maxRight.push_back(reach);
    }
}

OverlapIndex::~OverlapIndex()
{
    delete m_impl;
}

std::vector<Overlap> OverlapIndex::overlapsToResolve() const
{
    std::vector<Overlap> result;
    for (const Entry& c : m_impl->candidates) {
        m_impl->forNear(c, [&](const Entry& other) {
            // only pairs in which the candidate is the one that moves (lower priority)
            if (int(c.priority) <= int(other.priority)) {
                return;
            }
            RectF area;
            if (exactOverlap(c, other, m_impl->tol, &area)) {
                result.push_back({ other.item, c.item, area });
            }
        });
    }
    return result;
}

size_t OverlapIndex::overlapCountOf(const EngravingItem* candidate) const
{
    const Entry* c = m_impl->find(candidate);
    if (!c) {
        return 0;
    }
    size_t n = 0;
    m_impl->forNear(*c, [&](const Entry& other) {
        RectF area;
        if (exactOverlap(*c, other, m_impl->tol, &area)) {
            ++n;
        }
    });
    return n;
}

void OverlapIndex::itemMoved(const EngravingItem* candidate)
{
    for (Entry& e : m_impl->candidates) {
        if (e.item == candidate) {
            const double pad = 2.0 * e.item->spatium();
            e.coarse = e.item->ldata()->bbox(LD_ACCESS::MAYBE_NOTINITED).translated(e.item->pagePos()).adjusted(-pad, -pad, pad, pad);
            e.exact = false;
            return;
        }
    }
}

std::vector<Overlap> findOverlaps(const Score* score, double toleranceSp)
{
    std::vector<Overlap> result;
    for (const Page* page : score->pages()) {
        for (const System* system : page->systems()) {
            std::vector<Overlap> o = findOverlaps(system, toleranceSp);
            result.insert(result.end(), o.begin(), o.end());
        }
    }
    return result;
}
}
