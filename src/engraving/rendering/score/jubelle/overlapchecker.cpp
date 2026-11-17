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

struct OverlapIndex::Impl {
    std::vector<Item> items;
    double tol = 0.0;
};

OverlapIndex::OverlapIndex(const System* system, double toleranceSp)
    : m_impl(new Impl)
{
    if (system) {
        m_impl->items = collect(system);
        m_impl->tol = toleranceSp * system->spatium();
    }
}

OverlapIndex::~OverlapIndex()
{
    delete m_impl;
}

std::vector<Overlap> OverlapIndex::overlaps() const
{
    return overlapsIn(m_impl->items, m_impl->tol);
}

size_t OverlapIndex::overlapCountOf(const EngravingItem* item) const
{
    const Item* mine = nullptr;
    for (const Item& it : m_impl->items) {
        if (it.item == item) {
            mine = &it;
            break;
        }
    }
    if (!mine) {
        return 0;
    }
    size_t n = 0;
    for (const Item& other : m_impl->items) {
        RectF area;
        if (&other != mine && other.box.right() > mine->box.left() && other.box.left() < mine->box.right()
            && overlapBetween(*mine, other, m_impl->tol, &area)) {
            ++n;
        }
    }
    return n;
}

void OverlapIndex::itemMoved(const EngravingItem* item)
{
    for (Item& it : m_impl->items) {
        if (it.item == item) {
            it.shape = item->shape().translated(item->pagePos());
            it.box = it.shape.bbox();
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
