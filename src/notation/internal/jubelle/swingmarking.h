/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle Phase 5C: the automatic swing marking.
//
// When the whole score has a swing feel, a marking is placed just above the
// first tempo marking (or at the start when there is none): the feel's name,
// optionally with the rhythmic equation, or your own description. It is
// ordinary system text, so it prints, exports and opens in MuseScore. Jubelle
// remembers which text is its own (by element id), so it updates or removes
// that one and never adds a second.

#pragma once

#include "global/types/string.h"

#include "engraving/playback/jubelle/swingfeel.h"

namespace mu::engraving {
class Score;
class SystemText;
}

namespace mu::notation::jubelle {
struct SwingMarkingResult {
    bool added = false;
    bool updated = false;
    bool removed = false;
};

//! Brings the marking in line with `settings` (the score's feel and marking
//! style). `feelName` is the feel's name as shown to you. Updates
//! settings.markingEid; write the settings afterwards. Undoable: call inside
//! startCmd/endCmd.
SwingMarkingResult updateSwingMarking(engraving::Score* score, engraving::jubelle::SwingSettings& settings,
                                      const muse::String& feelName);

//! The marking Jubelle placed, if it is still in the score
engraving::SystemText* findSwingMarking(const engraving::Score* score, const engraving::jubelle::SwingSettings& settings);

//! The text the marking should have (empty = no marking)
muse::String swingMarkingXmlText(const engraving::Score* score, const engraving::jubelle::SwingSettings& settings,
                                 const muse::String& feelName);
}
