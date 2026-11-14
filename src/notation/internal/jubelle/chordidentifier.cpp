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
#include "chordidentifier.h"

#include <algorithm>
#include <cctype>
#include <set>

namespace mu::notation::chordidentifier {
namespace {
//! One chord type. `roles` has one letter per interval (same order):
//! R root, 3 third (or suspended tone), 5 fifth, 7 seventh, x extension.
struct ChordType {
    const char* quality;
    std::vector<int> intervals; //!< semitones above the root, sorted, including 0
    const char* roles;
};

//! Ordered from most common/simplest to most complex: when two readings fit
//! equally well, the earlier one wins.
const std::vector<ChordType>& chordTypes()
{
    static const std::vector<ChordType> types {
        // triads and power chord
        { "",         { 0, 4, 7 },              "R35" },
        { "m",        { 0, 3, 7 },              "R35" },
        { "dim",      { 0, 3, 6 },              "R35" },
        { "aug",      { 0, 4, 8 },              "R35" },
        { "sus4",     { 0, 5, 7 },              "R35" },
        { "sus2",     { 0, 2, 7 },              "R35" },
        { "5",        { 0, 7 },                 "R5" },
        // sevenths and sixths
        { "7",        { 0, 4, 7, 10 },          "R357" },
        { "maj7",     { 0, 4, 7, 11 },          "R357" },
        { "m7",       { 0, 3, 7, 10 },          "R357" },
        { "6",        { 0, 4, 7, 9 },           "R35x" },
        { "m6",       { 0, 3, 7, 9 },           "R35x" },
        { "m7b5",     { 0, 3, 6, 10 },          "R357" },
        { "dim7",     { 0, 3, 6, 9 },           "R357" },
        { "m(maj7)",  { 0, 3, 7, 11 },          "R357" },
        { "7sus4",    { 0, 5, 7, 10 },          "R357" },
        { "7#5",      { 0, 4, 8, 10 },          "R357" },
        { "7b5",      { 0, 4, 6, 10 },          "R357" },
        { "maj7#5",   { 0, 4, 8, 11 },          "R357" },
        { "maj7b5",   { 0, 4, 6, 11 },          "R357" },
        { "add9",     { 0, 2, 4, 7 },           "Rx35" },
        { "m(add9)",  { 0, 2, 3, 7 },           "Rx35" },
        // ninths
        { "9",        { 0, 2, 4, 7, 10 },       "Rx357" },
        { "maj9",     { 0, 2, 4, 7, 11 },       "Rx357" },
        { "m9",       { 0, 2, 3, 7, 10 },       "Rx357" },
        { "6/9",      { 0, 2, 4, 7, 9 },        "Rx35x" },
        { "m6/9",     { 0, 2, 3, 7, 9 },        "Rx35x" },
        { "m(maj9)",  { 0, 2, 3, 7, 11 },       "Rx357" },
        { "9sus4",    { 0, 2, 5, 7, 10 },       "Rx357" }, // also "11" without the 3rd
        { "7b9",      { 0, 1, 4, 7, 10 },       "Rx357" },
        { "7#9",      { 0, 3, 4, 7, 10 },       "Rx357" },
        { "7#11",     { 0, 4, 6, 7, 10 },       "R3x57" },
        { "7b13",     { 0, 4, 7, 8, 10 },       "R35x7" },
        { "maj7#11",  { 0, 4, 6, 7, 11 },       "R3x57" },
        { "m9b5",     { 0, 2, 3, 6, 10 },       "Rx357" },
        { "m7b9",     { 0, 1, 3, 7, 10 },       "Rx357" },
        { "m11",      { 0, 3, 5, 7, 10 },       "R3x57" }, // m7 with 11 (no 9)
        { "13",       { 0, 4, 7, 9, 10 },       "R35x7" }, // dominant 13 (no 9)
        // elevenths and thirteenths
        { "m11",      { 0, 2, 3, 5, 7, 10 },    "Rx3x57" },
        { "maj9#11",  { 0, 2, 4, 6, 7, 11 },    "Rx3x57" },
        { "9#11",     { 0, 2, 4, 6, 7, 10 },    "Rx3x57" },
        { "13",       { 0, 2, 4, 7, 9, 10 },    "Rx35x7" },
        { "m13",      { 0, 2, 3, 7, 9, 10 },    "Rx35x7" },
        { "maj13",    { 0, 2, 4, 7, 9, 11 },    "Rx35x7" },
        { "13sus4",   { 0, 2, 5, 7, 9, 10 },    "Rx35x7" },
        // altered dominants
        { "7b9b13",   { 0, 1, 4, 7, 8, 10 },    "Rx35x7" },
        { "7#9b13",   { 0, 3, 4, 7, 8, 10 },    "Rx35x7" },
        { "7b9#11",   { 0, 1, 4, 6, 7, 10 },    "Rx3x57" },
        { "7#9#11",   { 0, 3, 4, 6, 7, 10 },    "Rx3x57" },
        { "7b9#9",    { 0, 1, 3, 4, 7, 10 },    "Rxx357" },
        { "13b9",     { 0, 1, 4, 7, 9, 10 },    "Rx35x7" },
        { "13#9",     { 0, 3, 4, 7, 9, 10 },    "Rx35x7" },
        { "13#11",    { 0, 2, 4, 6, 7, 9, 10 }, "Rx3x5x7" },
        { "7#5#9",    { 0, 3, 4, 8, 10 },       "Rx357" },
        { "7#5b9",    { 0, 1, 4, 8, 10 },       "Rx357" },
        { "7b5b9",    { 0, 1, 4, 6, 10 },       "Rx357" },
        { "7b5#9",    { 0, 3, 4, 6, 10 },       "Rx357" },
        { "7alt",     { 0, 1, 3, 4, 6, 8, 10 }, "Rxx35x7" },
    };
    return types;
}

ToneRole roleFromChar(char c)
{
    switch (c) {
    case 'R': return ToneRole::Root;
    case '3': return ToneRole::Third;
    case '5': return ToneRole::Fifth;
    case '7': return ToneRole::Seventh;
    case 'x': return ToneRole::Extension;
    default: return ToneRole::None;
    }
}

bool hasSeventh(const ChordType& t)
{
    return std::string(t.roles).find('7') != std::string::npos;
}

int mod12(int v)
{
    return ((v % 12) + 12) % 12;
}

//! Pitch class of a tpc (C = 14 -> 0).
int tpcToPc(int tpc)
{
    return mod12((tpc - 14) * 7);
}

//! Default spelling for a pitch class when no tpc is known (flats for black keys
//! except F#, as most chord charts do).
int defaultTpcForPc(int pc)
{
    static const int tpcs[12] = { 14, 9, 16, 11, 18, 13, 20, 15, 10, 17, 12, 19 };
    return tpcs[mod12(pc)];
}

//! Letter index C=0 D=1 E=2 F=3 G=4 A=5 B=6 of a tpc.
int tpcStep(int tpc)
{
    static const int stepOfLineIndex[7] = { 3, 0, 4, 1, 5, 2, 6 }; // F C G D A E B
    return stepOfLineIndex[((tpc + 1) % 7 + 7) % 7];
}

int roleInterval(const Result& r, ToneRole role)
{
    for (size_t i = 0; i < r.intervals.size(); ++i) {
        if (r.intervalRoles[i] == role) {
            return r.intervals[i];
        }
    }
    return -1;
}
}

std::string tpcName(int tpc)
{
    if (tpc < -1 || tpc > 33) {
        return std::string();
    }
    // Line of fifths: Fbb(-1) Cbb Gbb Dbb Abb Ebb Bbb Fb(6) ... C(14) ... B#(26) ... B##(33)
    static const char letters[7] = { 'F', 'C', 'G', 'D', 'A', 'E', 'B' };
    const int idx = tpc + 1;               // 0 .. 34
    const char letter = letters[idx % 7];
    const int alter = idx / 7 - 2;         // -2 .. +2
    std::string s(1, letter);
    if (alter < 0) {
        s.append(static_cast<size_t>(-alter), 'b');
    } else if (alter > 0) {
        s.append(static_cast<size_t>(alter), '#');
    }
    return s;
}

Result identify(const std::vector<InputNote>& notes)
{
    Result result;
    if (notes.empty()) {
        return result;
    }

    std::set<int> pcs;
    const InputNote* bassNote = &notes.front();
    for (const InputNote& n : notes) {
        pcs.insert(mod12(n.pitch));
        if (n.pitch < bassNote->pitch) {
            bassNote = &n;
        }
    }
    if (pcs.size() < 2) {
        return result;
    }
    const int bassPc = mod12(bassNote->pitch);

    // The spelling used for each pitch class: the written note's own tpc
    // (lowest occurrence first), so enharmonic spellings are respected.
    std::vector<InputNote> sorted(notes);
    std::sort(sorted.begin(), sorted.end(), [](const InputNote& a, const InputNote& b) { return a.pitch < b.pitch; });
    auto tpcOf = [&](int pc) {
        for (const InputNote& n : sorted) {
            if (mod12(n.pitch) == pc && n.tpc >= -1 && n.tpc <= 33 && tpcToPc(n.tpc) == pc) {
                return n.tpc;
            }
        }
        return defaultTpcForPc(pc);
    };

    int bestScore = -1;
    int bestRoot = 0;
    const ChordType* bestType = nullptr;
    bool bestOmitsFifth = false;
    const std::vector<ChordType>& types = chordTypes();

    for (int root : pcs) {
        std::set<int> rel;
        for (int pc : pcs) {
            rel.insert(mod12(pc - root));
        }
        for (size_t i = 0; i < types.size(); ++i) {
            const ChordType& type = types[i];
            const std::set<int> want(type.intervals.begin(), type.intervals.end());

            int score = -1;
            bool omitsFifth = false;
            if (rel == want) {
                score = 1000;
            } else if ((hasSeventh(type) || type.intervals.size() >= 5) && want.count(7)) {
                // The perfect fifth may be left out of 7th chords and bigger chords
                // (C-E-Bb is C7, C-E-A-D is C6/9). Not of triads or plain sixth
                // chords: C-E-A is Am/C, not C6 without its fifth.
                std::set<int> withoutFifth = want;
                withoutFifth.erase(7);
                if (rel == withoutFifth) {
                    score = 800;
                    omitsFifth = true;
                }
            }
            if (score < 0) {
                continue;
            }
            if (root == bassPc) {
                score += 300; // a root-position reading is the most natural one
            }
            score -= static_cast<int>(i); // simpler chord types first

            if (score > bestScore) {
                bestScore = score;
                bestRoot = root;
                bestType = &type;
                bestOmitsFifth = omitsFifth;
            }
        }
    }

    if (!bestType) {
        return result;
    }

    result.ok = true;
    result.rootPc = bestRoot;
    result.rootTpc = tpcOf(bestRoot);
    result.bassPc = bassPc;
    result.bassTpc = tpcOf(bassPc);
    for (size_t i = 0; i < bestType->intervals.size(); ++i) {
        if (bestOmitsFifth && bestType->intervals[i] == 7) {
            continue;
        }
        result.intervals.push_back(bestType->intervals[i]);
        result.intervalRoles.push_back(roleFromChar(bestType->roles[i]));
    }

    result.root = tpcName(result.rootTpc);
    result.quality = bestType->quality;
    result.isInversion = bassPc != bestRoot;
    if (result.isInversion) {
        result.bass = tpcName(result.bassTpc);
    }
    result.name = symbolText(result, result.rootTpc, result.bassTpc, true);
    return result;
}

ToneRole roleOf(const Result& chord, int pitch)
{
    if (!chord.ok) {
        return ToneRole::None;
    }
    const int interval = mod12(pitch - chord.rootPc);
    for (size_t i = 0; i < chord.intervals.size(); ++i) {
        if (chord.intervals[i] == interval) {
            return chord.intervalRoles[i];
        }
    }
    return ToneRole::None;
}

std::string symbolText(const Result& chord, int rootTpc, int bassTpc, bool slashBass)
{
    if (!chord.ok) {
        return std::string();
    }
    std::string s = tpcName(rootTpc) + chord.quality;
    if (slashBass && chord.isInversion) {
        s += "/" + tpcName(bassTpc);
    }
    return s;
}

Key keyFromSignature(int sharps, bool minor)
{
    Key k;
    k.minor = minor;
    k.tonicTpc = 14 + sharps + (minor ? 3 : 0); // C major = 14; its relative minor A = 17
    return k;
}

std::string romanNumeral(const Result& chord, const Key& key, bool figuredBass)
{
    if (!chord.ok) {
        return std::string();
    }

    // Scale degree from the letter names, so spelling decides (F# in C is #IV, Gb is bV).
    static const char* numerals[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };
    static const int majorScale[7] = { 0, 2, 4, 5, 7, 9, 11 };
    static const int minorScale[7] = { 0, 2, 3, 5, 7, 8, 10 };
    const int degree = (tpcStep(chord.rootTpc) - tpcStep(key.tonicTpc) + 7) % 7;
    const int fromTonic = mod12(tpcToPc(chord.rootTpc) - tpcToPc(key.tonicTpc));
    int expected = key.minor ? minorScale[degree] : majorScale[degree];
    // In minor, the raised 7th (leading tone) is diatonic too: V and vii° need no sign.
    if (key.minor && degree == 6 && fromTonic == 11) {
        expected = 11;
    }
    int alter = fromTonic - expected;
    if (alter > 6) {
        alter -= 12;
    } else if (alter < -6) {
        alter += 12;
    }

    std::string accidental;
    if (alter < 0) {
        accidental.assign(static_cast<size_t>(-alter), 'b');
    } else if (alter > 0) {
        accidental.assign(static_cast<size_t>(alter), '#');
    }

    const int third = roleInterval(chord, ToneRole::Third);
    const int fifth = roleInterval(chord, ToneRole::Fifth);
    const int seventh = roleInterval(chord, ToneRole::Seventh);
    const bool minorThird = third == 3;
    const bool suspended = third == 2 || third == 5;

    std::string numeral = numerals[degree];
    if (minorThird) {
        std::transform(numeral.begin(), numeral.end(), numeral.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    }

    // Quality signs (MuseScore Roman numeral syntax): o dim, 0 half-dim, + aug.
    std::string sign;
    if (minorThird && fifth == 6) {
        sign = (seventh == 10) ? "0" : "o";
    } else if (third == 4 && fifth == 8) {
        sign = "+";
    }

    // Seventh type: "7" for a minor 7th (or the diminished 7th of a dim7),
    // "M7" for a major 7th above a major or minor triad.
    std::string seventhText;
    if (seventh == 11) {
        seventhText = "M7";
    } else if (seventh >= 0) {
        seventhText = "7";
    }

    // Inversion figures from the chord tone in the bass.
    std::string figure;
    const int bassInterval = mod12(chord.bassPc - chord.rootPc);
    ToneRole bassRole = ToneRole::Root;
    for (size_t i = 0; i < chord.intervals.size(); ++i) {
        if (chord.intervals[i] == bassInterval) {
            bassRole = chord.intervalRoles[i];
        }
    }
    if (figuredBass) {
        const bool isSeventhChord = seventh >= 0;
        const std::string m = seventh == 11 ? "M" : "";
        switch (bassRole) {
        case ToneRole::Root: figure = isSeventhChord ? seventhText : ""; break;
        case ToneRole::Third: figure = isSeventhChord ? m + "65" : "6"; break;
        case ToneRole::Fifth: figure = isSeventhChord ? m + "43" : "64"; break;
        case ToneRole::Seventh: figure = m + "42"; break;
        default: figure = seventhText; break; // an extension in the bass has no standard figure
        }
    } else {
        figure = seventhText;
    }

    // Extensions and alterations beyond the 7th chord, e.g. V7(b9), I(add9), V7(sus4).
    std::vector<std::string> extras;
    if (suspended) {
        extras.push_back(third == 5 ? "sus4" : "sus2");
    }
    for (size_t i = 0; i < chord.intervals.size(); ++i) {
        if (chord.intervalRoles[i] != ToneRole::Extension) {
            continue;
        }
        switch (chord.intervals[i]) {
        case 1: extras.push_back("b9"); break;
        case 2: extras.push_back(seventh >= 0 ? "9" : "add9"); break;
        case 3: extras.push_back("#9"); break;
        case 5: extras.push_back("11"); break;
        case 6: extras.push_back("#11"); break;
        case 8: extras.push_back("b13"); break;
        case 9: extras.push_back(seventh >= 0 ? "13" : "add6"); break;
        default: break;
        }
    }
    if (third < 0 && fifth == 7 && seventh < 0) {
        extras.push_back("5"); // power chord
    }

    std::string s = accidental + numeral + sign + figure;
    if (!extras.empty()) {
        s += "(";
        for (size_t i = 0; i < extras.size(); ++i) {
            s += (i ? "," : "") + extras[i];
        }
        s += ")";
    }
    return s;
}
}
