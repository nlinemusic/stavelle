/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G4 (Phase 6.1, approved): custom timeline markers.
//
// A custom marker is a hidden staff text on the top staff ("Jubelle marker:
// <name>"). Being part of the score, it moves with its bar when bars are
// inserted or deleted, saves and reopens with the project, and is never
// printed or exported (hidden items aren't). MuseScore 4.7.7 opens it as an
// ordinary hidden staff text. Rehearsal marks stay the score's own markers.

#pragma once

#include <vector>

#include "global/types/string.h"
#include "engraving/types/fraction.h"

namespace mu::engraving {
class EngravingItem;
class Score;
class StaffText;
}

namespace mu::notation::jubelle {
constexpr const char16_t* MARKER_PREFIX = u"Jubelle marker:";

struct TimelineMarker {
    engraving::StaffText* text = nullptr;
    muse::String name;
    engraving::Fraction tick;
};

bool isTimelineMarker(const engraving::EngravingItem* item);
muse::String markerName(const engraving::EngravingItem* item);

//! All custom markers, in score order
std::vector<TimelineMarker> timelineMarkers(const engraving::Score* score);

//! These change the score as undoable edits: call them inside a command
//! (startCmd/endCmd, or the notation's undo stack).
engraving::StaffText* addTimelineMarker(engraving::Score* score, const engraving::Fraction& tick, const muse::String& name);
void renameTimelineMarker(engraving::Score* score, engraving::EngravingItem* marker, const muse::String& name);
void removeTimelineMarker(engraving::Score* score, engraving::EngravingItem* marker);
//! Moves a marker to another position (returns the marker at its new place)
engraving::StaffText* moveTimelineMarker(engraving::Score* score, engraving::EngravingItem* marker, const engraving::Fraction& tick);

//! "Marker 1", "Marker 2", …: the first unused default name
muse::String nextMarkerName(const engraving::Score* score);
}
