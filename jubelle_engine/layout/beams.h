/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::layout). Not part of MuseScore.
 *
 * Which chords and rests are beamed together (S7, stage ②), as MuseScore decides it (BeamLayout::createBeams and
 * beamGraceNotes with Groups' beam modes): the time signature's beam groups (MuseScore's defaults, or the score's
 * own), the beam modes set on chords and rests, the beat subdivision rule of x/4 meters, tuplets, grace notes and
 * beams continued from the previous measure. Where the beams go and their slope come later.
 */
#pragma once

#include <map>
#include <vector>

#include "context.h"
#include "notes.h"

namespace je::layout {
struct Beam {
    int staff = 0, voice = 0;
    bool grace = false;
    std::vector<Id> elements;   // chords and rests, in order (a beam joins two or more)
};

//! Every beam of the score; `chords` from layoutNotes (their shown value: cross-measure values)
std::vector<Beam> layoutBeams(const ScoreContext& ctx, const std::vector<ChordLayout>& chords);
}
