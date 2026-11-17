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

#include "notation/internal/jubelle/passagelinkplanner.h"

using namespace mu::notation::passagelink;

namespace {
constexpr int Q = 480;

Event note(int tick, int dur, int pitch, std::set<std::string> arts = {})
{
    Event e;
    e.tick = tick;
    e.duration = dur;
    e.pitches = { pitch };
    e.articulations = arts;
    return e;
}

std::vector<int> pitchesOf(const Passage& p)
{
    std::vector<int> out;
    for (const Event& e : p) {
        out.push_back(e.pitches.empty() ? -1 : e.pitches.front());
    }
    return out;
}

std::vector<int> ticksOf(const Passage& p)
{
    std::vector<int> out;
    for (const Event& e : p) {
        out.push_back(e.tick);
    }
    return out;
}
}

// The example from the requirements: Trumpet 1's rhythm changes; Trumpet 2 keeps
// its own pitches (a third lower) and gets the new rhythm.
TEST(PassageLinkPlannerTests, RhythmChangeKeepsTargetPitches)
{
    Passage tpt1Before { note(0, 2 * Q, 72), note(2 * Q, 2 * Q, 74) };
    Passage tpt1After { note(0, Q, 72), note(Q, Q, 72), note(2 * Q, 2 * Q, 74) }; // first half note split
    Passage tpt2 { note(0, 2 * Q, 69), note(2 * Q, 2 * Q, 71) };

    Passage out = propagate(tpt1Before, tpt1After, tpt2, SyncOptions::all());
    EXPECT_EQ(ticksOf(out), (std::vector<int> { 0, Q, 2 * Q }));
    // The new note gets Trumpet 2's interval (3 semitones below), not Trumpet 1's pitch.
    EXPECT_EQ(pitchesOf(out), (std::vector<int> { 69, 69, 71 }));
    EXPECT_EQ(out[0].duration, Q);
}

TEST(PassageLinkPlannerTests, OctaveDifferencesArePreserved)
{
    Passage src { note(0, Q, 72), note(Q, Q, 74) };
    Passage srcAfter { note(0, Q, 72), note(Q, Q / 2, 74), note(Q + Q / 2, Q / 2, 76) };
    Passage lowOctave { note(0, Q, 60), note(Q, Q, 62) };

    Passage out = propagate(src, srcAfter, lowOctave, SyncOptions::all());
    EXPECT_EQ(pitchesOf(out), (std::vector<int> { 60, 62, 64 }));
}

TEST(PassageLinkPlannerTests, OnlyChosenKindsOfChangeAreCopied)
{
    Passage before { note(0, Q, 72), note(Q, Q, 74) };
    Passage after { note(0, Q, 72, { "staccato" }), note(Q, Q, 74, { "accent" }) };
    after[0].dynamic = "f";
    Passage target { note(0, Q, 67, { "tenuto" }), note(Q, Q, 69) };
    target[0].dynamic = "mp";

    SyncOptions onlyArticulations;
    onlyArticulations.rhythm = false;
    onlyArticulations.dynamics = false;
    onlyArticulations.markings = false;

    Passage out = propagate(before, after, target, onlyArticulations);
    ASSERT_EQ(out.size(), 2u);
    EXPECT_EQ(out[0].articulations, (std::set<std::string> { "staccato" }));
    EXPECT_EQ(out[1].articulations, (std::set<std::string> { "accent" }));
    EXPECT_EQ(out[0].dynamic, "mp"); // dynamics not synced: target keeps its own
    EXPECT_EQ(pitchesOf(out), (std::vector<int> { 67, 69 }));

    SyncOptions noRhythm = SyncOptions::all();
    noRhythm.rhythm = false;
    Passage rhythmChanged { note(0, Q / 2, 72), note(Q / 2, Q / 2, 72), note(Q, Q, 74) };
    Passage kept = propagate(before, rhythmChanged, target, noRhythm);
    EXPECT_EQ(ticksOf(kept), (std::vector<int> { 0, Q })); // rhythm not synced
}

TEST(PassageLinkPlannerTests, HandEditedTargetNotesAreNotOverwritten)
{
    Passage before { note(0, Q, 72), note(Q, Q, 74), note(2 * Q, Q, 76) };
    Passage after { note(0, Q, 72, { "accent" }), note(Q, Q / 2, 74), note(Q + Q / 2, Q / 2, 75), note(2 * Q, Q, 76) };
    Passage target { note(0, Q, 67), note(Q, Q, 65), note(2 * Q, Q, 71) };
    target[1].detached = true;           // the player changed beat 2 by hand
    target[1].articulations = { "fermata" };

    Passage out = propagate(before, after, target, SyncOptions::all());
    EXPECT_EQ(ticksOf(out), (std::vector<int> { 0, Q, 2 * Q }));
    EXPECT_EQ(pitchesOf(out), (std::vector<int> { 67, 65, 71 }));
    EXPECT_EQ(out[1].articulations, (std::set<std::string> { "fermata" }));
    EXPECT_TRUE(out[1].detached);
    EXPECT_EQ(out[0].articulations, (std::set<std::string> { "accent" }));
}

TEST(PassageLinkPlannerTests, RestsFollowTheSource)
{
    Passage before { note(0, Q, 72), note(Q, Q, 74) };
    Passage after { note(0, Q, 72) };
    after.push_back(Event { Q, Q, {}, {}, "", {}, false }); // second note became a rest
    Passage target { note(0, Q, 64), note(Q, Q, 65) };

    Passage out = propagate(before, after, target, SyncOptions::all());
    EXPECT_EQ(pitchesOf(out), (std::vector<int> { 64, -1 }));

    // and a rest that becomes a note gets the target's interval
    Passage back = propagate(after, before, out, SyncOptions::all());
    EXPECT_EQ(pitchesOf(back), (std::vector<int> { 64, 66 }));
}

TEST(PassageLinkPlannerTests, DurationsCanStayIndependent)
{
    auto rest = [](int tick, int dur) { Event e; e.tick = tick; e.duration = dur; return e; };
    // Source: two quarter notes; target plays the first one short (an eighth
    // and an eighth rest). The source changes its second note's rhythm.
    Passage sourceBefore { note(0, 480, 72), note(480, 480, 74) };
    Passage sourceAfter { note(0, 480, 72), note(480, 240, 74), note(720, 240, 76) };
    Passage target { note(0, 240, 67), rest(240, 240), note(480, 480, 69) };

    SyncOptions o;
    o.durations = false;
    const Passage out = propagate(sourceBefore, sourceAfter, target, o);
    // The first note keeps its own short length (with its rest); the new
    // rhythm arrives for the rest of the passage
    ASSERT_EQ(out.size(), 4u);
    EXPECT_EQ(out[0].duration, 240);
    EXPECT_TRUE(out[1].pitches.empty());
    EXPECT_EQ(out[1].tick, 240);
    EXPECT_EQ(out[2].tick, 480);
    EXPECT_EQ(out[2].duration, 240);
    EXPECT_EQ(out[3].tick, 720);

    // With durations on, the source's lengths win
    const Passage all = propagate(sourceBefore, sourceAfter, target, SyncOptions::all());
    EXPECT_EQ(all[0].duration, 480);
}
