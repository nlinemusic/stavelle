/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::layout). Not part of MuseScore.
 *
 * Stem directions (S7, stage ②), as MuseScore decides them (ChordLayout::computeUp and the beam's direction in
 * BeamLayout::layout1): a chord's own direction, its beam's (set on the beam, by a moved chord, by the voices, or
 * from the beam's notes), voices 1 and 3 up and 2 and 4 down where a staff has several, grace notes up, chords moved
 * to another staff, and otherwise the notes' balance around the middle line. Directions that depend on positions not
 * worked out yet (cross-staff beams, beams placed by hand, two-note tremolos) are marked as not yet known.
 */
#pragma once

#include <vector>

#include "beams.h"
#include "context.h"
#include "notes.h"

namespace je::layout {
//! Sets each chord's `up` (and `upKnown`); `beams` from layoutBeams on the same chords
void layoutStemDirections(const ScoreContext& ctx, std::vector<ChordLayout>& chords, const std::vector<Beam>& beams);

//! Sets each chord's stem, flag and, where no beam decides it, stem length (StemLayout::calcDefaultStemLength);
//! after layoutNotePositions
void layoutStemLengths(const ScoreContext& ctx, std::vector<ChordLayout>& chords, const std::vector<Beam>& beams);
}
