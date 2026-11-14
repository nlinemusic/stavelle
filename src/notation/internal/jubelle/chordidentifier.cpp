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
#include <set>

namespace mu::notation::chordidentifier {
namespace {
struct ChordType {
    const char* quality;
    std::vector<int> intervals; //!< semitones above the root, including 0
};

//! Ordered from most common/simplest to most complex: when two readings fit
//! equally well, the earlier one wins.
const std::vector<ChordType>& chordTypes()
{
    static const std::vector<ChordType> types {
        // triads and power chord
        { "",         { 0, 4, 7 } },
        { "m",        { 0, 3, 7 } },
        { "dim",      { 0, 3, 6 } },
        { "aug",      { 0, 4, 8 } },
        { "sus4",     { 0, 5, 7 } },
        { "sus2",     { 0, 2, 7 } },
        { "5",        { 0, 7 } },
        // sevenths and sixths
        { "7",        { 0, 4, 7, 10 } },
        { "maj7",     { 0, 4, 7, 11 } },
        { "m7",       { 0, 3, 7, 10 } },
        { "6",        { 0, 4, 7, 9 } },
        { "m6",       { 0, 3, 7, 9 } },
        { "m7b5",     { 0, 3, 6, 10 } },
        { "dim7",     { 0, 3, 6, 9 } },
        { "m(maj7)",  { 0, 3, 7, 11 } },
        { "7sus4",    { 0, 5, 7, 10 } },
        { "7#5",      { 0, 4, 8, 10 } },
        { "7b5",      { 0, 4, 6, 10 } },
        { "maj7#5",   { 0, 4, 8, 11 } },
        { "maj7b5",   { 0, 4, 6, 11 } },
        { "add9",     { 0, 2, 4, 7 } },
        { "m(add9)",  { 0, 2, 3, 7 } },
        // ninths
        { "9",        { 0, 2, 4, 7, 10 } },
        { "maj9",     { 0, 2, 4, 7, 11 } },
        { "m9",       { 0, 2, 3, 7, 10 } },
        { "6/9",      { 0, 2, 4, 7, 9 } },
        { "m6/9",     { 0, 2, 3, 7, 9 } },
        { "m(maj9)",  { 0, 2, 3, 7, 11 } },
        { "9sus4",    { 0, 2, 5, 7, 10 } },
        { "7b9",      { 0, 1, 4, 7, 10 } },
        { "7#9",      { 0, 3, 4, 7, 10 } },
        { "7#11",     { 0, 4, 6, 7, 10 } },
        { "7b13",     { 0, 4, 7, 8, 10 } },
        { "maj7#11",  { 0, 4, 6, 7, 11 } },
        { "m9b5",     { 0, 2, 3, 6, 10 } },
        { "m7b9",     { 0, 1, 3, 7, 10 } },
        // elevenths and thirteenths (the 11th usually replaces the 3rd in dominant 11ths)
        // "11" ({0, 2, 5, 7, 10}) is the same set as 9sus4 above.
        { "m11",      { 0, 2, 3, 5, 7, 10 } },
        { "maj9#11",  { 0, 2, 4, 6, 7, 11 } },
        { "9#11",     { 0, 2, 4, 6, 7, 10 } },
        { "13",       { 0, 2, 4, 7, 9, 10 } },
        { "m13",      { 0, 2, 3, 7, 9, 10 } },
        { "maj13",    { 0, 2, 4, 7, 9, 11 } },
        { "13sus4",   { 0, 2, 5, 7, 9, 10 } },
        // altered dominants
        { "7b9b13",   { 0, 1, 4, 7, 8, 10 } },
        { "7#9b13",   { 0, 3, 4, 7, 8, 10 } },
        { "7b9#11",   { 0, 1, 4, 6, 7, 10 } },
        { "7#9#11",   { 0, 3, 4, 6, 7, 10 } },
        { "7b9#9",    { 0, 1, 3, 4, 7, 10 } },
        { "13b9",     { 0, 1, 4, 7, 9, 10 } },
        { "13#9",     { 0, 3, 4, 7, 9, 10 } },
        { "13#11",    { 0, 2, 4, 6, 7, 9, 10 } },
        { "7#5#9",    { 0, 3, 4, 8, 10 } },
        { "7#5b9",    { 0, 1, 4, 8, 10 } },
        { "7b5b9",    { 0, 1, 4, 6, 10 } },
        { "7b5#9",    { 0, 3, 4, 6, 10 } },
        { "7alt",     { 0, 1, 3, 4, 6, 8, 10 } },
    };
    return types;
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

std::set<int> intervalSet(const std::set<int>& pcs, int root)
{
    std::set<int> rel;
    for (int pc : pcs) {
        rel.insert(mod12(pc - root));
    }
    return rel;
}

//! Default spelling for a pitch class when no tpc is known (flats for black keys
//! except F#, as most chord charts do).
int defaultTpcForPc(int pc)
{
    static const int tpcs[12] = { 14, 9, 16, 11, 18, 13, 20, 15, 10, 17, 12, 19 };
    return tpcs[mod12(pc)];
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
    const std::vector<ChordType>& types = chordTypes();

    for (int root : pcs) {
        const std::set<int> rel = intervalSet(pcs, root);
        for (size_t i = 0; i < types.size(); ++i) {
            const ChordType& type = types[i];
            const std::set<int> want(type.intervals.begin(), type.intervals.end());

            int score = -1;
            if (rel == want) {
                score = 1000;
            } else if (type.intervals.size() >= 4 && want.count(7)) {
                // The perfect fifth may be left out of sevenths and bigger chords.
                std::set<int> withoutFifth = want;
                withoutFifth.erase(7);
                if (rel == withoutFifth) {
                    score = 800;
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
            }
        }
    }

    if (!bestType) {
        return result;
    }

    result.ok = true;
    result.root = tpcName(tpcOf(bestRoot));
    result.quality = bestType->quality;
    result.name = result.root + result.quality;
    if (bassPc != bestRoot) {
        result.isInversion = true;
        result.bass = tpcName(tpcOf(bassPc));
        result.name += "/" + result.bass;
    }
    return result;
}
}
