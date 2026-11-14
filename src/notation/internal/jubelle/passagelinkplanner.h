/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 * Music Composition & Notation
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#pragma once

#include <set>
#include <string>
#include <vector>

//! Jubelle Phase 4 groundwork: defined linked musical passages.
//!
//! Works out how an edit in a source passage carries over to one linked target
//! passage (for example Trumpet 1 -> Trumpet 2): the chosen kinds of change are
//! copied while the target keeps its own pitches and octave, and events the user
//! changed by hand in the target ("detached") are never overwritten.
//! This is only the decision logic; storing links in the score, applying the
//! result as an undoable edit and showing links are later steps. It is not
//! connected to the user interface yet.
namespace mu::notation::passagelink {
struct Event {
    int tick = 0;                       //!< start, from the passage start
    int duration = 0;
    std::vector<int> pitches;           //!< empty = rest
    std::set<std::string> articulations; //!< e.g. "staccato", "accent"
    std::string dynamic;                //!< e.g. "mf", or ""
    std::set<std::string> markings;     //!< slurs, ties and other markings, e.g. "slur-start", "tie"
    bool detached = false;              //!< changed by hand in the target: keep as it is
};

using Passage = std::vector<Event>; //!< in time order

struct SyncOptions {
    bool rhythm = true;         //!< note positions, rhythms and durations
    bool articulations = true;
    bool dynamics = true;       //!< dynamics and expression markings
    bool markings = true;       //!< slurs, ties and other markings

    static SyncOptions all() { return SyncOptions(); }
};

//! The target passage after the source changed from `sourceBefore` to `sourceAfter`.
Passage propagate(const Passage& sourceBefore, const Passage& sourceAfter, const Passage& target, const SyncOptions& options);
}
