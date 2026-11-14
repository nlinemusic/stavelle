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

#include <string>
#include <vector>

//! Jubelle Phase 4 groundwork: automatic condensing decisions.
//!
//! Given what each player of a condensing group (e.g. Trumpet 1 and 2) plays in
//! one passage, decides how a condensed staff should show it. This is only the
//! decision logic; drawing condensed staves is a later layout step. It is not
//! connected to the user interface yet, and the "Condense automatically" on/off
//! setting will decide whether it is used at all (off = never condense).
namespace mu::notation::condensing {
struct Event {
    int tick = 0;              //!< start, in ticks from the passage start
    int duration = 0;          //!< in ticks
    std::vector<int> pitches;  //!< empty = rest
};

using PlayerPassage = std::vector<Event>; //!< events in time order

enum class Decision {
    Unison,       //!< everyone plays the same notes: one voice, labelled "a2", "a3"…
    Chords,       //!< same rhythm, different notes: chords on one stem
    TwoVoices,    //!< two players with different rhythms: stems up (1.) and down (2.)
    OnlyOne,      //!< only one player plays; the others rest: label "1." / "2." …
    AllRest,      //!< nobody plays
    Separate,     //!< too complex: keep separate staves
};

struct Result {
    Decision decision = Decision::Separate;
    int soloPlayer = -1;   //!< for OnlyOne: index of the player who plays
    std::string label;     //!< "a2", "1.", "2.", "1.2.", or "" when no label is needed
    std::string reason;    //!< plain-language reason for Separate
};

//! Decides for one passage. `players` has one entry per player, in order (player 1 first).
Result decide(const std::vector<PlayerPassage>& players);
}
