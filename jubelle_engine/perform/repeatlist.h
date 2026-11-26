/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle engine (je::perform), based on MuseScore Studio's RepeatList (src/engraving/dom/repeatlist.cpp,
 * Copyright (C) MuseScore Limited and others, GPL-3.0-only) and the repeat helpers of its dom/utils.cpp.
 *
 * The order in which a score's measures are played: repeats with their play counts, first and second endings
 * (voltas), D.C. / D.S. jumps with their markers (al Fine, al Coda), section breaks, and Jubelle's "Play this
 * repeat" switch (a repeat switched off is played once, on its final pass). Computed from a snapshot, the same way
 * MuseScore's engine unwinds the score (its 72 repeat tests and every corpus score are compared). Used by playback
 * (S5) and by editing commands that follow the music's order across repeats (lyric entry).
 */
#pragma once

#include <cstddef>
#include <vector>

#include "model/fraction.h"
#include "model/id.h"

namespace je::model {
class Snapshot;
}

namespace je::perform {
//! A run of measures played straight through
struct RepeatSegment {
    std::vector<size_t> measures;   // indices into RepeatList::measures(), consecutive
    int playbackCount = 1;          // which pass of the repeat this is (1, 2…)
    Fraction time;           // start in the score
    Fraction playTime;       // start in the played (unrolled) order
    Fraction length;         // the measures' total length

    bool contains(size_t measure) const;
    bool startsWith(size_t measure) const { return !measures.empty() && measures.front() == measure; }
    bool endsWith(size_t measure) const { return !measures.empty() && measures.back() == measure; }
};

class RepeatList
{
public:
    //! The play order of a flow (the score's first flow when none is given); with expand false, the measures once
    //! in score order (MuseScore's flatten, playback with repeats off)
    static RepeatList build(const model::Snapshot& snapshot, Id flow = Id(), bool expand = true);

    const std::vector<RepeatSegment>& segments() const { return m_segments; }
    //! The flow's measures, in score order
    const std::vector<Id>& measures() const { return m_measures; }
    //! The index of a measure in measures() (npos when it isn't one)
    size_t indexOf(Id measure) const;
    //! The index of the measure at a time in the score (npos when none)
    size_t measureAt(const Fraction& time) const;

    //! MuseScore's findFollowingRepeatMeasures: the measures played next after `measure` where it ends a segment
    //! (a repeat or jump back, or forward past an ending)
    std::vector<size_t> followingMeasures(size_t measure) const;
    //! findPreviousRepeatMeasures: the measures played just before `measure` where it starts a segment
    std::vector<size_t> precedingMeasures(size_t measure) const;
    //! segmentsAreAdjacent: whether music in measure `second` can directly follow music in `first` when played
    //! (the same measure, the next one in one segment, or the start of a segment that follows one ending at `first`)
    bool adjacent(size_t first, size_t second) const;

    //! The played length of the whole score
    Fraction playLength() const;

private:
    std::vector<RepeatSegment> m_segments;
    std::vector<Id> m_measures;
    std::vector<Fraction> m_times;
    std::vector<Fraction> m_lengths;
};
}
