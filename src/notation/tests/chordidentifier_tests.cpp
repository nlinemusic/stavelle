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
#include <gtest/gtest.h>

#include "notation/internal/jubelle/chordidentifier.h"

using namespace mu::notation::chordidentifier;

namespace {
// tpc values: C 14, D 16, E 18, F 13, G 15, A 17, B 19; +7 = sharp, -7 = flat
enum Tpc {
    Cb = 7, Gb = 8, Db = 9, Ab = 10, Eb = 11, Bb = 12, F = 13, C = 14, G = 15, D = 16, A = 17, E = 18, B = 19,
    Fs = 20, Cs = 21, Gs = 22, Ds = 23, As = 24, Es = 25, Bs = 26
};

std::string name(std::vector<InputNote> notes)
{
    return identify(notes).name;
}
}

TEST(ChordIdentifierTests, NoteNames)
{
    EXPECT_EQ(tpcName(14), "C");
    EXPECT_EQ(tpcName(20), "F#");
    EXPECT_EQ(tpcName(12), "Bb");
    EXPECT_EQ(tpcName(-1), "Fbb");
    EXPECT_EQ(tpcName(33), "B##");
    EXPECT_EQ(tpcName(26), "B#");
}

TEST(ChordIdentifierTests, Triads)
{
    EXPECT_EQ(name({ { 60, C }, { 64, E }, { 67, G } }), "C");
    EXPECT_EQ(name({ { 57, A }, { 60, C }, { 64, E } }), "Am");
    EXPECT_EQ(name({ { 59, B }, { 62, D }, { 65, F } }), "Bdim");
    EXPECT_EQ(name({ { 60, C }, { 64, E }, { 68, Gs } }), "Caug");
    EXPECT_EQ(name({ { 62, D }, { 67, G }, { 69, A } }), "Dsus4");
    EXPECT_EQ(name({ { 62, D }, { 64, E }, { 69, A } }), "Dsus2");
    EXPECT_EQ(name({ { 40, E }, { 47, B } }), "E5");
}

TEST(ChordIdentifierTests, SeventhsAndSixths)
{
    EXPECT_EQ(name({ { 55, G }, { 59, B }, { 62, D }, { 65, F } }), "G7");
    EXPECT_EQ(name({ { 60, C }, { 64, E }, { 67, G }, { 71, B } }), "Cmaj7");
    EXPECT_EQ(name({ { 62, D }, { 65, F }, { 69, A }, { 72, C } }), "Dm7");
    EXPECT_EQ(name({ { 59, B }, { 62, D }, { 65, F }, { 69, A } }), "Bm7b5");
    EXPECT_EQ(name({ { 59, B }, { 62, D }, { 65, F }, { 68, Ab } }), "Bdim7");
    EXPECT_EQ(name({ { 60, C }, { 64, E }, { 67, G }, { 69, A } }), "C6");
    EXPECT_EQ(name({ { 57, A }, { 60, C }, { 64, E }, { 67, G } }), "Am7"); // same notes, A in the bass
    EXPECT_EQ(name({ { 60, C }, { 63, Eb }, { 67, G }, { 71, B } }), "Cm(maj7)");
    EXPECT_EQ(name({ { 55, G }, { 60, C }, { 62, D }, { 65, F } }), "G7sus4");
    // fifth left out
    EXPECT_EQ(name({ { 48, C }, { 64, E }, { 70, Bb } }), "C7");
}

TEST(ChordIdentifierTests, ExtendedAndJazzChords)
{
    EXPECT_EQ(name({ { 48, C }, { 64, E }, { 67, G }, { 70, Bb }, { 74, D } }), "C9");
    EXPECT_EQ(name({ { 48, C }, { 64, E }, { 71, B }, { 74, D } }), "Cmaj9");      // no fifth
    EXPECT_EQ(name({ { 50, D }, { 65, F }, { 72, C }, { 76, E } }), "Dm9");        // no fifth
    EXPECT_EQ(name({ { 48, C }, { 64, E }, { 69, A }, { 74, D } }), "C6/9");       // no fifth
    EXPECT_EQ(name({ { 43, G }, { 59, B }, { 65, F }, { 69, A }, { 76, E } }), "G13"); // G B F A E (no 5th)
    EXPECT_EQ(name({ { 48, C }, { 64, E }, { 66, Fs }, { 67, G }, { 71, B } }), "Cmaj7#11");
    EXPECT_EQ(name({ { 48, C }, { 64, E }, { 66, Fs }, { 71, B } }), "Cmaj7b5"); // no natural 5th: the F# is the 5th
    EXPECT_EQ(name({ { 50, D }, { 65, F }, { 67, G }, { 72, C }, { 76, E } }), "Dm11");
    EXPECT_EQ(name({ { 43, G }, { 60, C }, { 65, F }, { 69, A } }), "G9sus4");     // G C F A (no 5th)
}

TEST(ChordIdentifierTests, AlteredChords)
{
    EXPECT_EQ(name({ { 43, G }, { 59, B }, { 65, F }, { 68, Ab } }), "G7b9");
    EXPECT_EQ(name({ { 43, G }, { 59, B }, { 65, F }, { 70, As } }), "G7#9");
    EXPECT_EQ(name({ { 43, G }, { 59, B }, { 61, Cs }, { 65, F } }), "G7b5");
    EXPECT_EQ(name({ { 43, G }, { 59, B }, { 63, Ds }, { 65, F } }), "G7#5");
    // With the natural 5th the Eb is a 13th; without it, it is the (raised) 5th.
    EXPECT_EQ(name({ { 43, G }, { 59, B }, { 62, D }, { 65, F }, { 68, Ab }, { 75, Eb } }), "G7b9b13");
    EXPECT_EQ(name({ { 43, G }, { 59, B }, { 62, D }, { 65, F }, { 70, As }, { 75, Eb } }), "G7#9b13");
    EXPECT_EQ(name({ { 43, G }, { 59, B }, { 65, F }, { 68, Ab }, { 75, Eb } }), "G7#5b9");
    EXPECT_EQ(name({ { 43, G }, { 59, B }, { 65, F }, { 70, As }, { 75, Eb } }), "G7#5#9");
}

TEST(ChordIdentifierTests, InversionsAreSlashChords)
{
    Result r = identify({ { 52, E }, { 55, G }, { 60, C } });
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.name, "C/E");
    EXPECT_TRUE(r.isInversion);
    EXPECT_EQ(r.root, "C");
    EXPECT_EQ(r.bass, "E");

    EXPECT_EQ(name({ { 55, G }, { 60, C }, { 64, E } }), "C/G");
    EXPECT_EQ(name({ { 53, F }, { 55, G }, { 59, B }, { 62, D } }), "G7/F");
    EXPECT_EQ(name({ { 47, B }, { 60, C }, { 64, E }, { 67, G } }), "Cmaj7/B");
}

TEST(ChordIdentifierTests, EnharmonicSpellingFollowsTheWrittenNotes)
{
    EXPECT_EQ(name({ { 66, Fs }, { 70, As }, { 73, Cs } }), "F#");
    EXPECT_EQ(name({ { 66, Gb }, { 70, Bb }, { 73, Db } }), "Gb");
    EXPECT_EQ(name({ { 63, Eb }, { 67, G }, { 70, Bb }, { 74, D } }), "Ebmaj7");
    EXPECT_EQ(name({ { 63, Ds }, { 66, Fs }, { 70, As } }), "D#m");
    // unknown spelling falls back to common chart spelling
    EXPECT_EQ(name({ { 70, -1 }, { 74, -1 }, { 77, -1 } }), "Bb");
}

TEST(ChordIdentifierTests, UnisonsOctavesAndUnknownCombinations)
{
    EXPECT_FALSE(identify({}).ok);
    EXPECT_FALSE(identify({ { 60, C } }).ok);
    EXPECT_FALSE(identify({ { 60, C }, { 72, C } }).ok);                 // one pitch class
    EXPECT_EQ(name({ { 48, C }, { 60, C }, { 64, E }, { 67, G }, { 76, E } }), "C"); // doublings
    EXPECT_FALSE(identify({ { 60, C }, { 61, Db }, { 62, D } }).ok);     // cluster: not a chord
}
