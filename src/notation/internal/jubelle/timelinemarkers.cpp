/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G4: custom timeline markers. See timelinemarkers.h.

#include "timelinemarkers.h"

#include "translation.h"

#include "engraving/dom/factory.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/score.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/stafftext.h"

using namespace mu::engraving;
using muse::String;

namespace mu::notation::jubelle {
bool isTimelineMarker(const EngravingItem* item)
{
    return item && item->isStaffText() && toStaffText(item)->plainText().startsWith(String(MARKER_PREFIX));
}

String markerName(const EngravingItem* item)
{
    if (!isTimelineMarker(item)) {
        return String();
    }
    return toStaffText(item)->plainText().mid(String(MARKER_PREFIX).size()).trimmed();
}

std::vector<TimelineMarker> timelineMarkers(const Score* score)
{
    std::vector<TimelineMarker> out;
    for (const Measure* m = score ? score->firstMeasure() : nullptr; m; m = m->nextMeasure()) {
        for (const Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            for (EngravingItem* a : s->annotations()) {
                if (isTimelineMarker(a)) {
                    out.push_back({ toStaffText(a), markerName(a), s->tick() });
                }
            }
        }
    }
    return out;
}

StaffText* addTimelineMarker(Score* score, const Fraction& tick, const String& name)
{
    if (!score) {
        return nullptr;
    }
    Measure* m = score->tick2measure(tick);
    Segment* seg = m ? m->undoGetSegment(SegmentType::ChordRest, tick) : nullptr;
    if (!seg) {
        return nullptr;
    }
    StaffText* t = Factory::createStaffText(seg);
    t->setParent(seg);
    t->setTrack(0);
    t->setPlainText(String(MARKER_PREFIX) + u" " + (name.isEmpty() ? nextMarkerName(score) : name.trimmed()));
    t->setVisible(false);              // never printed or exported
    score->undoAddElement(t);
    return t;
}

void renameTimelineMarker(Score* score, EngravingItem* marker, const String& name)
{
    if (!score || !isTimelineMarker(marker) || name.trimmed().isEmpty()) {
        return;
    }
    marker->undoChangeProperty(Pid::TEXT, TextBase::plainToXmlText(String(MARKER_PREFIX) + u" " + name.trimmed()));
}

void removeTimelineMarker(Score* score, EngravingItem* marker)
{
    if (score && isTimelineMarker(marker)) {
        score->undoRemoveElement(marker);
    }
}

StaffText* moveTimelineMarker(Score* score, EngravingItem* marker, const Fraction& tick)
{
    if (!score || !isTimelineMarker(marker)) {
        return nullptr;
    }
    const String name = markerName(marker);
    removeTimelineMarker(score, marker);
    return addTimelineMarker(score, tick, name);
}

String nextMarkerName(const Score* score)
{
    const std::vector<TimelineMarker> existing = timelineMarkers(score);
    for (int n = 1;; ++n) {
        const String candidate = muse::mtrc("notation/timeline", "Marker %1").arg(n);
        bool used = false;
        for (const TimelineMarker& m : existing) {
            used = used || m.name == candidate;
        }
        if (!used) {
            return candidate;
        }
    }
}
}
