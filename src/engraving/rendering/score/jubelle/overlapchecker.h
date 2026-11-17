/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle G7, step 1 (G3 design approved 15 Nov 2026): the overlap checker.
//
// After MuseScore's own placement, finds real overlaps between markings of
// different kinds on a system, using their exact outlines (shapes). It changes
// nothing: collision avoidance (step 3) and magnetic layout (steps 4-5) act on
// what it finds, and Proofreading's collision checks (6.3) report it, so there
// is one definition of "collision".
//
// Not counted as overlaps:
//  - an item against the chord or note it belongs to (an articulation on its
//    own chord, a lyric under its own note);
//  - two items of the same kind (MuseScore stacks those itself);
//  - invisible items and items that aren't drawn;
//  - anything touching by less than the tolerance.

#pragma once

#include <vector>

#include "draw/types/geometry.h"

namespace mu::engraving {
class EngravingItem;
class Score;
class System;
}

namespace mu::engraving::jubelle {
//! The G3 priority table: when two items overlap, the one with the lower number
//! stays and the other moves.
enum class LayoutPriority {
    Notes = 1,
    Articulations = 2,
    SlursTies = 3,
    Dynamics = 4,       // dynamics and hairpins
    Lyrics = 5,
    Text = 6,           // staff, system and expression text, chord symbols, fingering…
    RehearsalTempo = 7, // rehearsal marks and tempo
    Other = 8,
};

LayoutPriority priorityOf(const EngravingItem* item);

struct Overlap {
    EngravingItem* stays = nullptr;   // higher priority
    EngravingItem* moves = nullptr;   // lower priority
    muse::RectF area;                 // where they overlap (page coordinates)
};

//! Overlaps on one system (tolerance in staff spaces: smaller overlaps are ignored)
std::vector<Overlap> findOverlaps(const System* system, double toleranceSp = 0.1);

//! How many items on the system take part in the check (for tests and reports)
size_t checkedItemCount(const System* system);

//! Overlaps in the whole laid-out score
std::vector<Overlap> findOverlaps(const Score* score, double toleranceSp = 0.1);

//! Collision avoidance's view of one system. Only a few items may be moved
//! (the "candidates"); everything else is an obstacle. Obstacles are kept as
//! cheap boxes and their exact outlines are read only when a candidate comes
//! near, and only once. Reading every outline of a 39-staff system for every
//! question made each edit measurably slower (17 Nov 2026).
class OverlapIndex
{
public:
    using IsCandidate = bool (*)(const EngravingItem*);

    OverlapIndex(const System* system, IsCandidate isCandidate, double toleranceSp = 0.1);
    ~OverlapIndex();

    //! overlaps in which the item that should move is a candidate
    std::vector<Overlap> overlapsToResolve() const;
    //! how many overlaps this candidate is part of (either role)
    size_t overlapCountOf(const EngravingItem* candidate) const;
    //! the candidate was moved: read its outline again
    void itemMoved(const EngravingItem* candidate);

private:
    struct Impl;
    Impl* m_impl = nullptr;
};

}
