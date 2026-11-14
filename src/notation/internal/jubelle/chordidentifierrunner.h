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

#include "global/types/color.h"

namespace mu::engraving {
class Score;
}

//! Jubelle: Chord Identifier, the score side. Works out what to analyse (the
//! selection, or the whole score when nothing is selected), names the chord at
//! every new attack, and adds chord symbols and/or Roman numerals. Optionally
//! colours chord tones.
//!
//! Only notation-engine types are used, so it runs in the engine test tool.
//! The caller wraps run() in one undoable command (startCmd/endCmd): notes,
//! rhythms and all other music are never changed; only chord symbols, Roman
//! numerals and (when asked) note colours are.
namespace mu::notation::chordidentifier {
enum class ExistingSymbols {
    Keep,     //!< leave existing symbols as they are; only fill positions without one
    Replace,  //!< replace an existing symbol of the same kind at that position
};

struct Options {
    bool chordSymbols = true;     //!< standard chord symbols above the top analysed staff
    bool romanNumerals = false;   //!< Roman numerals below the bottom analysed staff
    bool slashBass = true;        //!< "C/E" for inversions (chord symbols)
    bool figuredBass = true;      //!< "I6", "V65" (Roman numerals)
    bool onlyWhenChordChanges = true; //!< no symbol where the chord repeats the previous one
    ExistingSymbols existing = ExistingSymbols::Keep;
    bool highlightChordTones = false;
};

//! The colours used for highlighting. Only notes in exactly these colours are
//! ever reset by Jubelle; colours you chose yourself are never touched.
struct HighlightColors {
    static muse::Color root() { return muse::Color(0xE6, 0x00, 0xA9); }      // magenta
    static muse::Color third() { return muse::Color(0x9B, 0x4D, 0xE0); }     // purple
    static muse::Color fifth() { return muse::Color(0x1F, 0x7A, 0xE0); }     // blue
    static muse::Color seventh() { return muse::Color(0xE0, 0x7A, 0x10); }   // orange
    static muse::Color extension() { return muse::Color(0x1E, 0x9E, 0x5A); } // green
    static bool isHighlight(const muse::Color& c);
};

struct Scope {
    bool wholeScore = false;   //!< nothing selected: the whole score
    bool listSelection = false; //!< individual notes selected (only those are analysed)
    int firstBar = 0;          //!< 1-based bar numbers
    int lastBar = 0;
    int staffCount = 0;        //!< pitched staves analysed (drum staves are skipped)
    int noteCount = 0;         //!< for a list selection
    std::string firstStaffName;
    std::string lastStaffName;
};

struct Report {
    int added = 0;
    int replaced = 0;
    int keptExisting = 0;   //!< positions skipped because a symbol was already there
    int unrecognised = 0;   //!< attacks whose notes form no known chord
    int notesColoured = 0;
    int coloursRemoved = 0;
};

//! What run() would analyse right now.
Scope scope(const mu::engraving::Score* score);

//! Adds chord symbols / Roman numerals (and highlighting) as set in `options`.
Report run(mu::engraving::Score* score, const Options& options);

//! Removes Jubelle's highlight colours (and only those) from the notes in scope.
Report removeHighlight(mu::engraving::Score* score);
}
