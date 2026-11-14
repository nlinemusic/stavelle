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
//! notes (e.g. "Cmaj7", "F#m7b5", "G7#9", "C/E") and, given a key, as a Roman
//! numeral ("V65", "viio7", "bVII"). Also tells which chord tone each note is
//! (root, 3rd, 5th, 7th, extension), for highlighting.
//!
//! Chord-symbol text is ordinary MuseScore chord-symbol text, so MuseScore's own
//! chord-symbol style settings decide how it is finally displayed. Roman numeral
//! text uses MuseScore's Roman numeral analysis syntax ("o" diminished, "0"
//! half-diminished, "+" augmented, figures such as 6, 64, 65, 43, 42).
//!
//! Independent implementation (no code from the GPLv2-only "Chord Identifier"
//! plugin by Emmanuel Roussel and Ziya Mete Demircan, whose feature set it covers).
//! No Qt and no engraving types, so it is unit-tested on its own.
namespace mu::notation::chordidentifier {
struct InputNote {
    int pitch = 0;  //!< MIDI pitch (60 = middle C)
    int tpc = -1;   //!< MuseScore tonal pitch class (14 = C, 20 = F#, 12 = Bb), or -1 if unknown
};

enum class ToneRole {
    None,       //!< not part of the identified chord
    Root,
    Third,      //!< the 3rd, or the suspended 2nd/4th that replaces it
    Fifth,
    Seventh,    //!< the 7th (also the diminished 7th of a dim7 chord)
    Extension,  //!< 9ths, 11ths, 13ths, added 6ths
};

struct Result {
    bool ok = false;
    std::string name;       //!< full symbol, e.g. "Am7/G"
    std::string root;       //!< e.g. "A"
    std::string quality;    //!< e.g. "m7"
    std::string bass;       //!< e.g. "G" for an inversion/slash chord, else ""
    bool isInversion = false;

    int rootPc = -1;        //!< pitch class 0..11 of the root
    int rootTpc = -1;       //!< spelling of the root
    int bassPc = -1;        //!< pitch class of the lowest note
    int bassTpc = -1;
    std::vector<int> intervals;          //!< semitones above the root, sorted (0 first)
    std::vector<ToneRole> intervalRoles; //!< role of each interval
};

//! Identifies the chord. Fewer than two different pitch classes, or a
//! combination that is not a recognised chord, gives ok = false.
Result identify(const std::vector<InputNote>& notes);

//! The role of a note of `pitch` in the identified chord (None if not in it).
ToneRole roleOf(const Result& chord, int pitch);

//! Chord-symbol text with the given root and bass spellings (for example the
//! written-pitch spellings on a transposing staff). `slashBass` adds "/bass"
//! for inversions.
std::string symbolText(const Result& chord, int rootTpc, int bassTpc, bool slashBass);

struct Key {
    int tonicTpc = 14;   //!< e.g. 14 = C, 17 = A
    bool minor = false;
};

//! The key of a key signature (-7 flats .. +7 sharps) in major or minor.
Key keyFromSignature(int sharps, bool minor);

//! Roman numeral in MuseScore's Roman numeral analysis syntax.
//! `figuredBass`: inversions as figures (I6, I64, V65, V43, V42); otherwise
//! seventh chords get "7" and inversions are not shown.
std::string romanNumeral(const Result& chord, const Key& key, bool figuredBass);

//! Note name for a tonal pitch class ("C", "F#", "Bb", "Ebb", "B#"…).
std::string tpcName(int tpc);
}
