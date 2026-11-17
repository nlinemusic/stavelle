/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "pagealignment.h"

#include <algorithm>
#include <cmath>
#include <map>

#include "engraving/dom/box.h"
#include "engraving/dom/engravingitem.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/page.h"
#include "engraving/dom/score.h"
#include "engraving/dom/textbase.h"

using namespace mu::engraving;
using muse::String;
using muse::PointF;
using muse::RectF;

namespace mu::notation::jubelle {
namespace {
constexpr double TOLERANCE = 0.5;   // raster units; far below a printed point

const Page* pageOf(const EngravingItem* item)
{
    for (const EngravingObject* p = item; p; p = p->explicitParent()) {
        if (p->isPage()) {
            return toPage(p);
        }
    }
    return nullptr;
}

String describe(const EngravingItem* item)
{
    if (item->isMeasure()) {
        return String(u"Bar %1").arg(toMeasure(item)->no() + 1);
    }
    return item->typeUserName().translated();
}

bool isAllowedType(const EngravingItem* item)
{
    switch (item->type()) {
    case ElementType::TEXT:           // frame text: title, subtitle, composer, lyricist, part name…
    case ElementType::IMAGE:
    case ElementType::STAFF_TEXT:
    case ElementType::SYSTEM_TEXT:
    case ElementType::TEMPO_TEXT:
    case ElementType::REHEARSAL_MARK:
    case ElementType::EXPRESSION:
        return true;
    default:
        return false;
    }
}

bool inFrame(const EngravingItem* item)
{
    const EngravingObject* p = item->explicitParent();
    return p && p->isBox();
}

RectF referenceRect(const Page* page, AlignReference reference)
{
    // The sheet's size from the page settings (the laid-out page rectangle
    // can be shorter than the sheet, e.g. on a last page)
    const RectF sheet(0, 0, page->style().styleD(Sid::pageWidth) * DPI, page->style().styleD(Sid::pageHeight) * DPI);
    if (reference == AlignReference::Page) {
        return RectF(0, 0, sheet.width(), sheet.height());
    }
    return RectF(page->lm(), page->tm(), sheet.width() - page->lm() - page->rm(), sheet.height() - page->tm() - page->bm());
}

//! The rectangle around all of them (computed from the edges, so that empty
//! rectangles, e.g. an empty text, still count)
RectF united(const std::vector<RectF>& rects)
{
    double l = rects.front().left();
    double t = rects.front().top();
    double r = rects.front().right();
    double b = rects.front().bottom();
    for (const RectF& x : rects) {
        l = std::min(l, x.left());
        t = std::min(t, x.top());
        r = std::max(r, x.right());
        b = std::max(b, x.bottom());
    }
    return RectF(l, t, r - l, b - t);
}

//! How far a rectangle must move to satisfy the command against `ref`
PointF delta(const RectF& r, const RectF& ref, AlignCommand c)
{
    double dx = 0;
    double dy = 0;
    switch (c) {
    case AlignCommand::Left: dx = ref.left() - r.left();
        break;
    case AlignCommand::Right: dx = ref.right() - r.right();
        break;
    case AlignCommand::HCenter: dx = ref.center().x() - r.center().x();
        break;
    case AlignCommand::Top: dy = ref.top() - r.top();
        break;
    case AlignCommand::Bottom: dy = ref.bottom() - r.bottom();
        break;
    case AlignCommand::VCenter: dy = ref.center().y() - r.center().y();
        break;
    case AlignCommand::Center:
        dx = ref.center().x() - r.center().x();
        dy = ref.center().y() - r.center().y();
        break;
    default:
        break;
    }
    return PointF(dx, dy);
}

bool isVertical(AlignCommand c)
{
    return c == AlignCommand::Top || c == AlignCommand::Bottom || c == AlignCommand::VCenter || c == AlignCommand::Center
           || c == AlignCommand::DistributeVertically;
}

void move(EngravingItem* item, const PointF& d)
{
    if (std::abs(d.x()) < TOLERANCE && std::abs(d.y()) < TOLERANCE) {
        return;
    }
    item->undoChangeProperty(Pid::OFFSET, item->offset() + d);
}
}

String alignRefusal(const EngravingItem* item, bool vertical)
{
    if (!item) {
        return String();
    }
    if (!isAllowedType(item)) {
        return String(u"%1 can't be aligned: its position comes from the music layout.").arg(describe(item));
    }
    if (!pageOf(item)) {
        return String(u"%1 can't be aligned: it isn't on a page in this view.").arg(describe(item));
    }
    if (vertical && !inFrame(item) && item->autoplace()) {
        return String(u"%1 can't be moved vertically: automatic placement keeps it clear of the music and would move it back. "
                      "Switch off Automatic placement for it in Properties first.").arg(describe(item));
    }
    return String();
}

AlignReport alignItems(Score* score, const std::vector<EngravingItem*>& items, AlignCommand command, AlignReference reference)
{
    AlignReport report;
    if (!score) {
        return report;
    }
    const bool vertical = isVertical(command);
    const bool distribute = command == AlignCommand::DistributeHorizontally || command == AlignCommand::DistributeVertically;

    // Group the alignable items by page
    std::map<const Page*, std::vector<EngravingItem*> > byPage;
    for (EngravingItem* item : items) {
        const String why = alignRefusal(item, vertical);
        if (!why.isEmpty()) {
            report.refused.push_back(why);
            continue;
        }
        byPage[pageOf(item)].push_back(item);
    }

    struct Planned {
        EngravingItem* item;
        RectF target;   // where its page rectangle should end up
    };
    std::vector<Planned> planned;

    for (auto& [page, group] : byPage) {
        std::vector<RectF> rects;
        for (EngravingItem* item : group) {
            rects.push_back(item->pageBoundingRect());
        }

        if (distribute) {
            if (group.size() < 3) {
                report.warnings.push_back(String(u"Distributing needs three or more items on the same page."));
                continue;
            }
            const bool h = command == AlignCommand::DistributeHorizontally;
            std::vector<size_t> order(group.size());
            for (size_t i = 0; i < order.size(); ++i) {
                order[i] = i;
            }
            std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
                return h ? rects[a].left() < rects[b].left() : rects[a].top() < rects[b].top();
            });
            const RectF& first = rects[order.front()];
            const RectF& last = rects[order.back()];
            double sizes = 0;
            for (const RectF& r : rects) {
                sizes += h ? r.width() : r.height();
            }
            const double span = h ? last.right() - first.left() : last.bottom() - first.top();
            const double gap = (span - sizes) / double(group.size() - 1);
            double pos = h ? first.left() : first.top();
            for (size_t k = 0; k < order.size(); ++k) {
                const RectF& r = rects[order[k]];
                const PointF d = h ? PointF(pos - r.left(), 0) : PointF(0, pos - r.top());
                planned.push_back({ group[order[k]], r.translated(d) });
                pos += (h ? r.width() : r.height()) + gap;
            }
            continue;
        }

        if (reference == AlignReference::EachOther) {
            if (group.size() < 2) {
                report.warnings.push_back(String(u"Aligning to each other needs two or more items on the same page."));
                continue;
            }
            // The group's own edges or centre stay where they are
            const RectF ref = united(rects);
            for (size_t i = 0; i < group.size(); ++i) {
                planned.push_back({ group[i], rects[i].translated(delta(rects[i], ref, command)) });
            }
            continue;
        }

        // Page or margins: the selection moves as a group, keeping its spacing
        const RectF ref = referenceRect(page, reference);
        const PointF d = delta(united(rects), ref, command);
        for (size_t i = 0; i < group.size(); ++i) {
            planned.push_back({ group[i], rects[i].translated(d) });
        }
    }

    // Frame text centred alone on the margins: use MuseScore's own centre
    // alignment, so it stays centred when the text is edited
    const bool useCentreAlign = reference == AlignReference::Margins && planned.size() == 1
                                && (command == AlignCommand::HCenter || command == AlignCommand::Center)
                                && planned.front().item->isTextBase() && inFrame(planned.front().item);
    if (useCentreAlign) {
        EngravingItem* item = planned.front().item;
        Align a = item->getProperty(Pid::ALIGN).value<Align>();
        if (a.horizontal != AlignH::HCENTER) {
            a.horizontal = AlignH::HCENTER;
            item->undoChangeProperty(Pid::ALIGN, PropertyValue::fromValue(a));
        }
        item->undoChangeProperty(Pid::OFFSET, PointF(0.0, item->offset().y()));
        score->doLayout();
    }

    // Move, lay out, then correct whatever is left (two passes at most)
    for (int pass = 0; pass < 3 && !planned.empty(); ++pass) {
        bool allThere = true;
        for (const Planned& p : planned) {
            const RectF now = p.item->pageBoundingRect();
            const PointF d(p.target.left() - now.left(), p.target.top() - now.top());
            if (std::abs(d.x()) >= TOLERANCE || std::abs(d.y()) >= TOLERANCE) {
                allThere = false;
                if (pass < 2) {
                    move(p.item, d);
                }
            }
        }
        if (allThere) {
            break;
        }
        if (pass < 2) {
            score->doLayout();
        } else {
            report.warnings.push_back(String(u"Some items couldn't reach the exact position: automatic placement moved them."));
        }
    }

    for (const Planned& p : planned) {
        if (inFrame(p.item)) {
            const RectF frame = toEngravingItem(p.item->explicitParent())->pageBoundingRect();
            if (!frame.contains(p.item->pageBoundingRect())) {
                report.warnings.push_back(String(u"%1 now extends outside its frame and may overlap the music.").arg(describe(p.item)));
            }
        }
        ++report.moved;
    }
    return report;
}
}
