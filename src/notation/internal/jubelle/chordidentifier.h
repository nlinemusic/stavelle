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

//! Jubelle: built-in chord identification. Names the chord formed by a set of
//! notes (e.g. "Cmaj7", "F#m7b5", "G7#9", "C/E"). The result is ordinary
//! chord-symbol text, so MuseScore's own chord-symbol style settings (Standard,
//! Jazz, capitalisation, …) decide how it is finally displayed.
//!
//! No Qt and no engraving types, so it is unit-tested on its own.
namespace mu::notation::chordidentifier {
struct InputNote {
    int pitch = 0;  //!< MIDI pitch (60 = middle C)
    int tpc = -1;   //!< MuseScore tonal pitch class (14 = C, 20 = F#, 12 = Bb), or -1 if unknown
};

struct Result {
    bool ok = false;
    std::string name;       //!< full symbol, e.g. "Am7/G"
    std::string root;       //!< e.g. "A"
    std::string quality;    //!< e.g. "m7"
    std::string bass;       //!< e.g. "G" for an inversion/slash chord, else ""
    bool isInversion = false;
};

//! Identifies the chord. Fewer than two different pitch classes, or a
//! combination that is not a recognised chord, gives ok = false.
Result identify(const std::vector<InputNote>& notes);

//! Note name for a tonal pitch class ("C", "F#", "Bb", "Ebb", "B#"…).
std::string tpcName(int tpc);
}
