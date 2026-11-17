/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G7 (G3 design approved 15 Nov 2026): magnetic layout and collision
// avoidance, as passes on top of MuseScore's own placement.
//
//  - Magnetic rows: on each staff, dynamics, expressions and hairpins (below
//    or above) and, on the system, rehearsal marks with tempo marks line up on
//    a shared row: the row is where most of them already are, items closer to
//    the staff move out to it, and items MuseScore had to push further out
//    (round a low or high note) stay put: they leave the row only where they
//    would collide, and are back in it in the next bar that allows it.
//  - Sideways nudging: a dynamic, expression or staff text that would be
//    pushed away may instead move left or right by up to the limit
//    (default 1/2 space, at most 2), if that saves a real distance.
//  - Steadiness: everything is worked out again from MuseScore's default
//    positions at each layout, never from the previous result, and moves
//    smaller than a small margin aren't made.
//  - Collision avoidance: overlaps MuseScore's one-way vertical pushing leaves
//    (found by the overlap checker) are resolved by the priority table: the
//    lower-priority item moves, sideways first (if nudging is on), else away
//    from the staff.
//
// Manual overrides always win: an item you dragged, or one with Automatic
// placement unticked, is never moved; it is an obstacle for the others.
// With the style's switches off (the default) nothing here runs, so the
// layout is exactly MuseScore's.

#pragma once

#include <vector>

namespace mu::engraving {
class EngravingItem;
class MStyle;
class Shape;
class SkylineLine;
class System;
}

namespace mu::engraving::jubelle {
bool magneticLayoutOn(const MStyle& style);
bool magneticRowsOn(const MStyle& style);
bool sidewaysNudgeOn(const MStyle& style);
bool collisionAvoidanceOn(const MStyle& style);
//! the sideways limit, in staff spaces (clamped to 0..2)
double nudgeLimitSp(const MStyle& style);

//! true if magnetic layout may move this item (automatic placement on, and
//! not dragged by the user)
bool mayMove(const EngravingItem* item);

//! Lines items up in rows (per staff and side). Call after MuseScore's own
//! alignment of the same items.
void alignRows(const std::vector<EngravingItem*>& items, const System* system);

//! Sideways nudging for autoplace: given the item's shape (system
//! coordinates) and the skyline it is pushed away from, the sideways move
//! (in absolute units) that keeps it closest to the staff, or 0.
//! `pushNow` is MuseScore's push for the current position.
double bestSidewaysMove(const EngravingItem* item, const Shape& shape, const SkylineLine& skyline, bool above,
                        double minHorizontalClearance, double minDistance, double pushNow);

//! Resolves the overlaps left on the system after all placement. Returns how
//! many items were moved.
int resolveCollisions(System* system);
}
