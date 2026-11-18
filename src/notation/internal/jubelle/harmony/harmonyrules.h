/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#pragma once

// Jubelle: Big Band Harmony and Orchestration System, the rule engine.
//
// Deterministic and offline. Every rule here comes from the Pandemonium Big Band
// lessons (Elliot Deutsch, "Jazz arranging.txt") as set out in "Jubelle - Big Band
// Harmony and Orchestration System (proposal for approval, 18 Nov 2026)", approved
// 17 Nov 2026 with the conventional extras switched off. Section numbers in the
// comments (§1.2 …) refer to that proposal.
//
// Pitches are MIDI numbers at concert pitch (60 = middle C). Spellings are
// MuseScore tonal pitch classes (14 = C, 21 = C♯, 11 = E♭). No Qt and no score
// types, so every rule is tested on its own.

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace mu::notation::jubelle::harmony {
// ---------------------------------------------------------------------------
// Chords (§1.1)
// ---------------------------------------------------------------------------

enum class Family {
    Major,
    Minor,
    Dominant,
    Diminished,       // dim7
    HalfDiminished,   // m7♭5
};

enum Alteration : unsigned {
    None = 0,
    Flat9 = 1,
    Sharp9 = 2,
    Flat5 = 4,
    Sharp5 = 8,
};

struct ChordSymbol {
    bool valid = false;
    int rootPc = 0;
    int rootTpc = 14;
    Family family = Family::Major;
    bool sus = false;            // V9sus / 7sus4: the 4th replaces the 3rd (§1.7)
    unsigned alterations = 0;    // Alteration flags written in the symbol
    int bassPc = -1;             // a slash bass, if any
    bool fixed = false;          // named from the rhythm section's written notes: never altered or substituted
    std::string text;            // as read
};

//! Reads chord-symbol text such as "Cmaj7", "Dm11", "G7b9#5", "F#m7b5", "Bbo7", "C6/9", "G9sus", "C/E".
//! The colour (6, maj7, 9, 13…) doesn't matter: within a family they are interchangeable (§1.1).
ChordSymbol parseChordSymbol(const std::string& text);

//! The chord tones a voicing may use, as intervals above the root (the family's interchangeable colours,
//! with the symbol's alterations replacing the 5th and 9th)
//! A chord from MuseScore's own chord-symbol analysis: the root's spelling and the chord's intervals
//! (bit n set = n semitones above the root), as the score's chord-symbol parser realises them.
ChordSymbol chordFromIntervals(int rootTpc, unsigned intervalMask, int bassPc = -1);

std::vector<int> colourTones(const ChordSymbol& chord);

//! Is this pitch one of the chord's tones (§1.5: chord tones are harmonised first)?
bool isChordTone(int pitch, const ChordSymbol& chord);

// ---------------------------------------------------------------------------
// Closed voicing, four voices (§1.2)
// ---------------------------------------------------------------------------

struct Voicing {
    bool ok = false;
    std::array<int, 4> pitches {};   // lead first, then downwards
    std::string reason;              // when !ok, in plain words
};

//! The closed voicing under a lead note: at least a minor third under the lead, the root left out
//! unless the melody is on the 3rd (then the second voice takes it), the 6th/13th as second voice when
//! the melody is on the root, all within one octave, never doubling the melody.
Voicing closedVoicing(int lead, const ChordSymbol& chord);

//! The diminished passing chord: minor thirds stacked down from the melody note (§1.5)
Voicing diminishedVoicing(int lead);

//! The diatonic passing chord for a chord (§1.5): on major or minor its V7; on a dominant its ii
ChordSymbol diatonicPassingChord(const ChordSymbol& chord);

//! Chromatic planing (§1.5): the target's voicing moved by the lead's half step
Voicing planedVoicing(const Voicing& target, int leadStep);

// ---------------------------------------------------------------------------
// Non-chord tones (§1.5)
// ---------------------------------------------------------------------------

enum class NctTool {
    Automatic,      // planing where the approach is chromatic, else diatonic, else diminished (§4)
    Diminished,
    Diatonic,
    ChromaticPlaning,
};

struct MelodyNote {
    int pitch = 60;
    int tpc = 14;
    double seconds = 0.5;     // how long it sounds
    bool rest = false;
    int chordIndex = -1;      // which chord sounds under it (index into the chord list), -1 = none
};

struct VoicedNote {
    Voicing voicing;
    ChordSymbol chord;             // the chord it was voiced on (the passing chord for a non-chord tone)
    bool chordTone = true;
    NctTool tool = NctTool::Automatic;   // the tool used for a non-chord tone
    std::string note;              // a plain remark (fallbacks, rules that couldn't be met)
    int writtenRootPc = -1;        // the root of the chord symbol in the score (the bari and bass stay on it)
};

struct HarmonyOptions {
    NctTool nct = NctTool::Automatic;
    //! §1.7: add alterations to V chords that resolve to I, preferring ♭9♯5 where the melody allows
    bool addAlterations = false;
    //! §1.8: replace the V of ii–V–I with the dominant a half step above the I
    bool halfStepSlide = false;
};

//! The four-voice harmony for every melody note (rests stay empty)
std::vector<VoicedNote> voiceMelody(const std::vector<MelodyNote>& melody, std::vector<ChordSymbol>& chords,
                                    const HarmonyOptions& options);

//! §1.7: which alterations a dominant may take under this melody note (keeps a minor third under the lead,
//! no ♯9 with the melody on the 3rd, no ♭5 with the melody on ♯5…), preferring ♭9♯5
unsigned allowedAlterations(int leadPitch, const ChordSymbol& dominant);

//! §1.8: the half-step slide for the V of a ii–V–I (G7 before C becomes D♭7)
ChordSymbol halfStepSlide(const ChordSymbol& dominant);

//! Is chords[i] a V that resolves down a fifth (or by the half-step slide) to the next chord?
bool resolvesAsV(const std::vector<ChordSymbol>& chords, size_t i);

//! The chord-symbol text for a chord (for symbols Jubelle writes or changes)
std::string chordText(const ChordSymbol& chord);

// ---------------------------------------------------------------------------
// Orchestration (§1.3, §1.4, §1.6, §1.9)
// ---------------------------------------------------------------------------

enum class SaxTechnique {
    LeaveOut,
    DoubleTrombones,       // §1.3 full-band closed: altos and tenors on the trombone notes, bari roots
    Unison,
    OctaveUnison4Up1Down,
    OctaveUnison2Up3Down,
    TwoPart,
    Closed,
    Drop2,
    Drop2And4,
    FourPartDrop2,
    Drop2BariRoots,
};

enum class TrumpetTechnique {
    LeaveOut,
    Closed,                 // §1.2
    TrumpetTriads,          // §1.6
    Automatic,              // shout chorus: closed, or triads when the lead goes above concert A5 (§1.6)
    Unison,
};

enum class TromboneTechnique {
    LeaveOut,
    OctaveBelowTrumpets,    // §1.3
    Unison,
};

//! One sounding note for each player of each section (−1 = rest / not playing)
struct SectionNotes {
    std::array<int, 4> trumpets { -1, -1, -1, -1 };
    std::array<int, 4> trombones { -1, -1, -1, -1 };
    std::array<int, 5> saxes { -1, -1, -1, -1, -1 };   // alto 1, alto 2, tenor 1, tenor 2, baritone
    unsigned outOfRange = 0;   // bit 4+n trombone n, bit 8+n sax n: outside the playable range
};

struct Ranges {
    // concert pitch; "comfortable" = the amateur limits Jubelle colours notes by; "max" = professional
    int leadTromboneStrongLow = 58;    // B♭3: where the lead trombone's strong range starts (§1.6)
    int leadTromboneStrongHigh = 69;   // A4: where the author usually stops
    int tromboneMax = 72;              // C5
    int shoutTrumpetThreshold = 81;    // A5: above this, trumpet triads (§1.3, §1.6)
    // concert ranges (written B♭3–F♯6 on each saxophone): alto D♭3–A5, tenor A♭2–E5, baritone D♭2–A4
    std::array<int, 5> saxLow { 49, 49, 44, 44, 37 };     // alto, alto, tenor, tenor, bari
    std::array<int, 5> saxHigh { 81, 81, 76, 76, 69 };
};

struct OrchestrationOptions {
    TrumpetTechnique trumpets = TrumpetTechnique::Closed;
    TromboneTechnique trombones = TromboneTechnique::OctaveBelowTrumpets;
    SaxTechnique saxes = SaxTechnique::DoubleTrombones;
    bool bariAvailable = true;        // false: the bass trombone takes the roots (§1.3)
    int busySax = -1;                 // FourPartDrop2: which sax (0–4) is elsewhere; −1 = tenor 2
    bool altosFillHighTriads = false; // §1.6 / §4: off by default
};

//! Speed classes (§1.9, §4): fast notes get tight voicings, spread voicings need longer notes
constexpr double FAST_NOTE_SECONDS = 0.2;
constexpr double SPREAD_NOTE_SECONDS = 0.4;
//! "Short notes" (§4)
constexpr double SHORT_NOTE_SECONDS = 0.3;

//! Is a sax technique suitable for this passage's note lengths? Returns a plain reason when not.
std::optional<std::string> techniqueUnsuitable(SaxTechnique t, const std::vector<MelodyNote>& melody);

//! The notes for each player, from the voiced melody
std::vector<SectionNotes> orchestrate(const std::vector<MelodyNote>& melody, const std::vector<VoicedNote>& voiced,
                                      const OrchestrationOptions& options, const Ranges& ranges = Ranges());

// ---------------------------------------------------------------------------
// Spelling (§1.10)
// ---------------------------------------------------------------------------

//! The spelling of a voiced pitch: from its place in the chord (the 3rd of D is F♯, the ♯9 of G is A♯),
//! avoiding B♯, C♭, E♯ and F♭ and double accidentals
int spellChordTone(int pitch, const ChordSymbol& chord);
//! The spelling of a note a half step from a spelled target (planing): rising takes sharps, falling flats
int spellChromatic(int pitch, int targetPitch, int targetTpc);
//! Minor thirds stacked down from the melody's spelling (diminished passing chord)
int spellDiminished(int pitch, int leadPitch, int leadTpc);
//! Replaces B♯, C♭, E♯, F♭ and double accidentals by the simpler enharmonic
int simplerSpelling(int tpc);
//! Pitch class of a tonal pitch class
int tpcToPc(int tpc);
}
