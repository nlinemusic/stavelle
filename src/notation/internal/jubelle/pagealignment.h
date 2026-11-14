/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle: title-page alignment and distribution (approved 14 Nov 2026).
//
// Moves title-page text, images and other freely movable items to exact
// positions, by changing only their offset (and, for frame text centred on
// the margins, MuseScore's own centre alignment). Sizes, fonts, formatting
// and image proportions never change; music is never touched. One command =
// one undo step. Items whose position comes from the music layout are
// refused, with the reason.

#pragma once

#include <vector>

#include "global/types/string.h"

namespace mu::engraving {
class EngravingItem;
class Score;
}

namespace mu::notation::jubelle {
enum class AlignCommand {
    Left, HCenter, Right,
    Top, VCenter, Bottom,
    Center,                 // both axes
    DistributeHorizontally, // three or more items
    DistributeVertically,
};

enum class AlignReference {
    Margins,    // the printable area inside the page margins (default)
    Page,       // the whole sheet
    EachOther,  // the selected items relative to one another (two or more)
};

struct AlignReport {
    int moved = 0;
    std::vector<muse::String> refused;   // "Bar 5 can't be aligned: …"
    std::vector<muse::String> warnings;  // e.g. text moved outside its frame
};

//! Why an item can't be aligned (empty = it can). `vertical` asks about a
//! vertical move.
muse::String alignRefusal(const engraving::EngravingItem* item, bool vertical);

//! Aligns or distributes the items. Undoable: call inside one command
//! (startCmd/endCmd). The score must be laid out (as it always is in the app).
AlignReport alignItems(engraving::Score* score, const std::vector<engraving::EngravingItem*>& items, AlignCommand command,
                       AlignReference reference);
}
