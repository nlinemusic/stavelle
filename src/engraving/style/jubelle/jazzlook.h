/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore).
 *
 * T2/F1 (15 Nov 2026): a one-step switch between the standard engraved look
 * (Leland) and the "Jazz handwritten" look (MuseJazz). Only fonts and the
 * chord-symbol style change; layout, spacing and the music never do. A text
 * style whose font you chose yourself is left alone. One undo step.
 */
#pragma once

#include <unordered_map>

#include "../styledef.h"
#include "../../types/propertyvalue.h"

namespace mu::engraving {
class MStyle;
class Score;
}

namespace mu::engraving::jubelle {
bool hasJazzLook(const MStyle& style);

//! The style values that change, from `current`
std::unordered_map<Sid, PropertyValue> jazzLookChanges(const MStyle& current);
std::unordered_map<Sid, PropertyValue> standardLookChanges(const MStyle& current);

//! Undoable; call inside startCmd/endCmd. Returns the number of values changed.
size_t applyJazzLook(Score* score, bool jazz);
}
