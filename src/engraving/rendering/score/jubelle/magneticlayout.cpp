/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G7: magnetic layout and collision avoidance. See magneticlayout.h.

#include "magneticlayout.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

#include "overlapchecker.h"

#include "dom/engravingitem.h"
#include "dom/measure.h"
#include "dom/segment.h"
#include "dom/system.h"
#include "infrastructure/shape.h"
#include "infrastructure/skyline.h"
#include "style/style.h"

#include "../alignmentlayout.h"
#include "../systemlayout.h"

using namespace muse;
using mu::engraving::rendering::score::AlignmentLayout;
using mu::engraving::rendering::score::SystemLayout;

namespace mu::engraving::jubelle {
namespace {
// moves smaller than this aren't made (steadiness: no jitter while editing)
constexpr double STAY_MARGIN_SP = 0.1;
// extra room left when an item is moved out of an overlap
constexpr double CLEARANCE_SP = 0.2;
// sideways nudging is only worth it if it saves at least this much
constexpr double NUDGE_SAVING_SP = 0.25;
constexpr int NUDGE_STEPS = 2;   // candidate positions on each side (half and full limit)
// the collision pass never moves an item further than this
constexpr double MAX_VERTICAL_MOVE_SP = 6.0;

bool rowKind(const EngravingItem* item)
{
    switch (item->type()) {
    case ElementType::DYNAMIC:
    case ElementType::EXPRESSION:
    case ElementType::HAIRPIN_SEGMENT:
    case ElementType::REHEARSAL_MARK:
    case ElementType::TEMPO_TEXT:
    case ElementType::GRADUAL_TEMPO_CHANGE_SEGMENT:
        return true;
    default:
        return false;
    }
}

bool nudgeKind(const EngravingItem* item)
{
    switch (item->type()) {
    case ElementType::DYNAMIC:
    case ElementType::EXPRESSION:
    case ElementType::STAFF_TEXT:
        return true;
    default:
        return false;   // chord symbols stay over their beat; MuseScore lines them up across the system
    }
}

// kinds the collision pass may move (lyrics are left to MuseScore, which
// keeps a verse on one line; notes, articulations, slurs and lines never move)
bool collisionMovableKind(const EngravingItem* item)
{
    switch (item->type()) {
    case ElementType::DYNAMIC:
    case ElementType::EXPRESSION:
    case ElementType::STAFF_TEXT:
    case ElementType::SYSTEM_TEXT:
    case ElementType::HARMONY:
    case ElementType::FINGERING:
    case ElementType::REHEARSAL_MARK:
    case ElementType::TEMPO_TEXT:
        return true;
    default:
        return false;
    }
}

// distance from the staff of an item's row position (bigger = further out)
double outwardDistance(const EngravingItem* item, bool above)
{
    const double y = AlignmentLayout::yOpticalCenter(item);
    return above ? -y : y;
}
}

bool magneticLayoutOn(const MStyle& style)
{
    return style.styleB(Sid::jubelleMagneticLayout);
}

bool magneticRowsOn(const MStyle& style)
{
    return magneticLayoutOn(style) && style.styleB(Sid::jubelleMagneticRows);
}

bool sidewaysNudgeOn(const MStyle& style)
{
    return magneticLayoutOn(style) && style.styleB(Sid::jubelleSidewaysNudge) && nudgeLimitSp(style) > 0.0;
}

bool collisionAvoidanceOn(const MStyle& style)
{
    return style.styleB(Sid::jubelleCollisionAvoidance);
}

double nudgeLimitSp(const MStyle& style)
{
    return std::clamp(style.styleS(Sid::jubelleNudgeLimit).val(), 0.0, 2.0);
}

bool mayMove(const EngravingItem* item)
{
    if (!item || !item->autoplace() || !item->visible()) {
        return false;
    }
    if (item->ldata()->autoplace.offsetChanged != OffsetChange::NONE) {
        return false;   // being dragged right now
    }
    const PropertyValue def = item->propertyDefault(Pid::OFFSET);
    if (def.isValid() && item->offset() != def.value<PointF>()) {
        return false;   // the user moved it
    }
    return true;
}

void alignRows(const std::vector<EngravingItem*>& items, const System* system)
{
    if (!system || items.empty() || !magneticRowsOn(system->style())) {
        return;
    }

    // per staff and side; rehearsal marks and tempo form one row per side of their staff
    std::map<std::pair<staff_idx_t, bool>, std::vector<EngravingItem*> > rows;
    for (EngravingItem* item : items) {
        if (!item || !rowKind(item) || !item->addToSkyline() || item->excludeVerticalAlign() || !mayMove(item)) {
            continue;
        }
        rows[{ item->staffIdx(), AlignmentLayout::isAbove(item) }].push_back(item);
    }

    const double margin = STAY_MARGIN_SP * system->spatium();
    for (auto& [key, row] : rows) {
        if (row.size() < 2) {
            continue;
        }
        const bool above = key.second;
        std::vector<double> distances;
        distances.reserve(row.size());
        for (const EngravingItem* item : row) {
            distances.push_back(outwardDistance(item, above));
        }
        std::vector<double> sorted = distances;
        std::sort(sorted.begin(), sorted.end());
        // the row: where most items are (upper median, so two items always meet)
        const double rowDistance = sorted[sorted.size() / 2];
        const double rowY = above ? -rowDistance : rowDistance;

        for (size_t i = 0; i < row.size(); ++i) {
            // only items closer to the staff move out to the row; items pushed
            // further out (round notes) stay where MuseScore put them
            if (rowDistance - distances[i] > margin) {
                AlignmentLayout::moveItemToY(row[i], rowY, system);
            }
        }
    }
}

double bestSidewaysMove(const EngravingItem* item, const Shape& shape, const SkylineLine& skyline, bool above,
                        double minHorizontalClearance, double minDistance, double pushNow)
{
    // cheapest checks first: this is asked for every marking MuseScore pushes
    if (!item || !nudgeKind(item) || !sidewaysNudgeOn(item->style()) || !mayMove(item)) {
        return 0.0;
    }
    const double sp = item->spatium();
    const double limit = nudgeLimitSp(item->style()) * sp;
    if (limit <= 0.0 || pushNow <= NUDGE_SAVING_SP * sp) {
        return 0.0;
    }

    double bestDx = 0.0;
    double bestPush = pushNow;
    for (int step = 1; step <= NUDGE_STEPS; ++step) {
        for (double sign : { -1.0, 1.0 }) {
            const double dx = sign * limit * step / NUDGE_STEPS;
            const Shape moved = shape.translated(PointF(dx, 0.0));
            const double d = above ? skyline.minDistanceToShapeAbove(moved, minHorizontalClearance)
                             : skyline.minDistanceToShapeBelow(moved, minHorizontalClearance);
            const double push = std::max(0.0, d + minDistance);
            // prefer the smallest move among equally good ones
            if (push < bestPush - 1e-6) {
                bestPush = push;
                bestDx = dx;
            }
        }
    }
    return (pushNow - bestPush >= NUDGE_SAVING_SP * sp) ? bestDx : 0.0;
}

int resolveCollisions(System* system)
{
    if (!system || !collisionAvoidanceOn(system->style())) {
        return 0;
    }
    const double sp = system->spatium();
    const bool nudge = sidewaysNudgeOn(system->style());
    const double limit = nudgeLimitSp(system->style()) * sp;
    const double clearance = CLEARANCE_SP * sp;

    // nothing here that collision avoidance may move: nothing to read or do
    bool anyCandidate = false;
    for (const MeasureBase* mb : system->measures()) {
        if (!mb->isMeasure()) {
            continue;
        }
        for (const Segment* seg = toMeasure(mb)->first(); seg && !anyCandidate; seg = seg->next()) {
            for (const EngravingItem* a : seg->annotations()) {
                if (a && collisionMovableKind(a) && mayMove(a)) {
                    anyCandidate = true;
                    break;
                }
            }
        }
        if (anyCandidate) {
            break;
        }
    }
    if (!anyCandidate) {
        return 0;
    }

    // the system's outlines are read once; moving an item re-reads only that item
    OverlapIndex index(system, [](const EngravingItem* e) { return collisionMovableKind(e) && mayMove(e); });
    auto overlapCount = [&index](const EngravingItem* item) { return index.overlapCountOf(item); };

    int moved = 0;
    std::set<EngravingItem*> done;
    for (int round = 0; round < 3; ++round) {
        bool changed = false;
        for (const Overlap& o : index.overlapsToResolve()) {
            EngravingItem* item = o.moves;
            if (done.count(item) || !collisionMovableKind(item) || !mayMove(item)) {
                continue;
            }
            const size_t before = overlapCount(item);

            // outlines in page coordinates
            const RectF mine = item->shape().translated(item->pagePos()).bbox();
            const RectF other = o.stays->shape().translated(o.stays->pagePos()).bbox();

            // 1. sideways, within the nudging limit
            bool solved = false;
            if (nudge && nudgeKind(item)) {
                const double right = other.right() + clearance - mine.left();   // move right to clear
                const double left = other.left() - clearance - mine.right();    // move left to clear
                const double first = std::abs(left) < std::abs(right) ? left : right;
                const double second = (first == left) ? right : left;
                for (double dx : { first, second }) {
                    if (std::abs(dx) > limit) {
                        continue;
                    }
                    item->mutldata()->moveX(dx);
                    index.itemMoved(item);
                    if (overlapCount(item) < before) {
                        solved = true;
                        break;
                    }
                    item->mutldata()->moveX(-dx);
                    index.itemMoved(item);
                }
            }

            // 2. away from the staff, just clear of the other item (at most a few spaces)
            if (!solved) {
                const bool above = AlignmentLayout::isAbove(item);
                const double dy = above ? other.top() - clearance - mine.bottom() : other.bottom() + clearance - mine.top();
                if ((above ? dy < 0.0 : dy > 0.0) && std::abs(dy) <= MAX_VERTICAL_MOVE_SP * sp) {
                    item->mutldata()->moveY(dy);
                    index.itemMoved(item);
                    if (overlapCount(item) < before) {
                        SystemLayout::updateSkylineForElement(item, system, dy);
                        solved = true;
                    } else {
                        item->mutldata()->moveY(-dy);
                        index.itemMoved(item);
                    }
                }
            }

            if (solved) {
                ++moved;
                changed = true;
                done.insert(item);
            }
        }
        if (!changed) {
            break;
        }
    }
    return moved;
}
}
