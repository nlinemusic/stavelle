/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle Phase 5E: Apply Part Layout (Format → Apply Part Layout…).
//
// Copies one part's layout to other parts: system and page breaks,
// keep-together marks, system locks, measure widths, staff spacers and the
// page and spacing settings. Never copies or changes music. The destination's
// own layout items of the chosen kinds are replaced, not mixed. A one-off
// copy: parts are not kept in sync afterwards.
//
// All parts have the same bars (they are linked to the full score), so bars
// are matched by position. Frames are matched by position and type.

#pragma once

#include <vector>

#include "global/types/string.h"

namespace mu::engraving {
class Score;
}

namespace mu::notation::jubelle {
struct PartLayoutOptions {
    // System breaks, page breaks and keep-together marks are always copied
    bool systemLocks = true;
    bool measureWidths = true;
    bool spacers = true;
    bool pageAndSpacing = true;       // when off, the destination's own settings are left unchanged
    bool breaksInsideMMRests = true;  // breaks that fall inside the destination's multi-measure rests
};

struct PartLayoutCheck {
    bool compatible = true;           // false = "partly": everything transfers but some systems can't match
    std::vector<muse::String> issues; // plain sentences, one per issue
};

struct PartLayoutReport {
    engraving::Score* destination = nullptr;
    int systemBreaks = 0;
    int pageBreaks = 0;
    int keepTogether = 0;
    int systemLocks = 0;
    int measureWidths = 0;
    int spacers = 0;
    bool pageAndSpacing = false;
    int skippedInMMRests = 0;     // breaks left out (option off)
    int skippedNoFrame = 0;       // breaks on frames the destination doesn't have
    bool spacersSkipped = false;  // different number of staves
};

//! What would happen, without changing anything
PartLayoutCheck checkPartLayout(const engraving::Score* source, const engraving::Score* destination, const PartLayoutOptions& options);

//! Applies the layout. Undoable: call inside one command on the main score
//! (startCmd/endCmd), so all destinations are one undo step.
std::vector<PartLayoutReport> applyPartLayout(const engraving::Score* source, const std::vector<engraving::Score*>& destinations,
                                              const PartLayoutOptions& options);
}
