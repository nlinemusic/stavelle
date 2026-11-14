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

#include "notation/internal/jubelle/condensingplanner.h"

using namespace mu::notation::condensing;

namespace {
constexpr int Q = 480; // quarter note
Event n(int tick, int dur, std::vector<int> pitches) { return Event { tick, dur, pitches }; }
Event rest(int tick, int dur) { return Event { tick, dur, {} }; }
}

TEST(CondensingPlannerTests, UnisonIsA2)
{
    PlayerPassage t1 { n(0, Q, { 72 }), n(Q, Q, { 74 }), n(2 * Q, 2 * Q, { 76 }) };
    Result r = decide({ t1, t1 });
    EXPECT_EQ(r.decision, Decision::Unison);
    EXPECT_EQ(r.label, "a2");

    EXPECT_EQ(decide({ t1, t1, t1 }).label, "a3");
}

TEST(CondensingPlannerTests, SameRhythmDifferentNotesBecomesChords)
{
    PlayerPassage t1 { n(0, Q, { 72 }), n(Q, Q, { 74 }) };
    PlayerPassage t2 { n(0, Q, { 67 }), n(Q, Q, { 71 }) };
    Result r = decide({ t1, t2 });
    EXPECT_EQ(r.decision, Decision::Chords);
    EXPECT_EQ(r.label, "");
}

TEST(CondensingPlannerTests, DifferentRhythmsBecomeTwoVoices)
{
    PlayerPassage t1 { n(0, Q / 2, { 72 }), n(Q / 2, Q / 2, { 74 }), n(Q, Q, { 76 }) };
    PlayerPassage t2 { n(0, 2 * Q, { 67 }) };
    EXPECT_EQ(decide({ t1, t2 }).decision, Decision::TwoVoices);
}

TEST(CondensingPlannerTests, OnePlayerRestingGivesPlayerLabel)
{
    PlayerPassage playing { n(0, Q, { 72 }) };
    PlayerPassage resting { rest(0, 4 * Q) };
    Result r = decide({ resting, playing });
    EXPECT_EQ(r.decision, Decision::OnlyOne);
    EXPECT_EQ(r.soloPlayer, 1);
    EXPECT_EQ(r.label, "2.");

    EXPECT_EQ(decide({ resting, resting }).decision, Decision::AllRest);
}

TEST(CondensingPlannerTests, RestsDoNotBreakUnison)
{
    PlayerPassage a { n(0, Q, { 72 }), rest(Q, Q), n(2 * Q, Q, { 74 }) };
    PlayerPassage b { n(0, Q, { 72 }), n(2 * Q, Q, { 74 }) }; // same notes, rest written differently
    EXPECT_EQ(decide({ a, b }).decision, Decision::Unison);
}

TEST(CondensingPlannerTests, ComplexPassagesStaySeparate)
{
    // crossing parts
    PlayerPassage high { n(0, Q, { 64 }) };
    PlayerPassage low { n(0, Q, { 67 }) };
    EXPECT_EQ(decide({ high, low }).decision, Decision::Separate);

    // divisi (a player already plays a chord) against a different rhythm
    PlayerPassage divisi { n(0, 2 * Q, { 67, 72 }) };
    PlayerPassage moving { n(0, Q, { 60 }), n(Q, Q, { 62 }) };
    EXPECT_EQ(decide({ divisi, moving }).decision, Decision::Separate);

    // three players with different rhythms
    PlayerPassage a { n(0, Q, { 76 }) };
    PlayerPassage b { n(0, Q / 2, { 72 }), n(Q / 2, Q / 2, { 71 }) };
    PlayerPassage c { n(0, 2 * Q, { 64 }) };
    Result r = decide({ a, b, c });
    EXPECT_EQ(r.decision, Decision::Separate);
    EXPECT_FALSE(r.reason.empty());

    // one player only: nothing to condense
    EXPECT_EQ(decide({ a }).decision, Decision::Separate);
}

TEST(CondensingPlannerTests, ThreePlayersTwoPlayingInUnisonAreLabelled)
{
    PlayerPassage line { n(0, Q, { 72 }) };
    PlayerPassage resting { rest(0, Q) };
    Result r = decide({ line, line, resting });
    EXPECT_EQ(r.decision, Decision::Unison);
    EXPECT_EQ(r.label, "1.2.");
}
