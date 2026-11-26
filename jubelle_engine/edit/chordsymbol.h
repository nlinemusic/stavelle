/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * Chord symbols as music: what a symbol's name means (its quality, extension and modifiers) and the notes it is
 * realized as. Ported from MuseScore Studio 4.7.7 (GPL v3): ParsedChord::parse (engraving/dom/chordlist.cpp, the
 * parts that decide the chord rather than its MusicXML export) and RealizedHarmony (engraving/dom/realizedharmony.cpp:
 * the intervals with their ranking, the jazz additions before the next chord, the voicings). Used by the "realize
 * chord symbols" command, and by playback when it plays chord symbols (S5).
 */
#pragma once

#include <map>
#include <string>
#include <vector>

namespace je::edit::chords {
//! ParsedChord: "maj7(#11)" → quality "major", extension "7", modifiers { "#11" }
struct ParsedChord {
    std::string name;        // as parsed ("=" prepended to an implied minor chord, as MuseScore does)
    std::string quality;     // major, minor, dominant, diminished, half-diminished, augmented
    std::string extension;   // "7", "9", "69"…
    std::vector<std::string> modifiers;   // "b9", "#11", "sus4", "add9", "no3", "alt"…
    bool understandable = true;

    //! preferMinor: the chord was written in lower case with lower-case minor chords on
    bool parse(const std::string& text, bool preferMinor = false);
    int extensionNumber() const;
};

//! MuseScore's Voicing and HDuration (the style's and each chord symbol's settings, same numbers)
enum class Voicing { Auto = 0, RootOnly, Close, Drop2, SixNote, FourNote, ThreeNote };
enum class Duration { UntilNextChordSymbol = 0, StopAtMeasureEnd, SegmentDuration };

//! pitch → spelling (TPC); several notes may share a pitch class while voicings are built
using PitchMap = std::multimap<int, int>;

//! The chord symbol after this one on its staff and voice, for the jazz additions (MuseScore looks at it when
//! literal is off; one without a root, "N.C.", stops the look-ahead)
struct Next {
    bool present = false;
    int rootTpc = -9;
    ParsedChord chord;
};

//! RealizedHarmony::getIntervals: interval (+ 128 × rank) → spelling, from a root
PitchMap intervals(const ParsedChord& chord, int rootTpc, bool literal, const Next& next);

//! RealizedHarmony::generateNotes: the chord's notes (MIDI pitch → spelling); transposeOffset in semitones, added
//! to the root and bass (an instrument's transposition when the score isn't shown in concert pitch); bassTpc < -1
//! or -9 (MuseScore's invalid) for none
PitchMap generateNotes(const ParsedChord& chord, int rootTpc, int bassTpc, bool literal, Voicing voicing, int transposeOffset,
                       const Next& next);

//! MuseScore's tpc2pitch (0 … 11 for the plain spellings, below 0 or above 11 for those past C)
int tpcPitch(int tpc);
}
