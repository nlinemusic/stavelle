/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::layout). Not part of MuseScore.
 *
 * How the chords at one time on one staff are put together (S7, stage ②; MuseScore's ChordLayout::layoutChords1):
 * first which noteheads go on the other side of the stem (layoutChords2: seconds within a chord, and between voices
 * whose stems go the same way, with the user's own choice per note). Offsets between voices, dots and accidentals
 * follow.
 */
#pragma once

#include <vector>

#include "context.h"
#include "notes.h"

namespace je::layout {
//! Sets each note's `mirror`; the chords' stem directions (layoutStemDirections) must be known
void layoutMirrors(const ScoreContext& ctx, std::vector<ChordLayout>& chords);
}
