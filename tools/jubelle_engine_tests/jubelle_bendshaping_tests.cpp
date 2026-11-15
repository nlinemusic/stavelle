/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

// Jubelle: falls without "snap back", and fading gestures (playback block).
#include <gtest/gtest.h>

#include "audio/engine/internal/synthesizers/fluidsynth/jubellebendshaping.h"

using namespace muse;
using namespace muse::audio::jubelle;
using Map = std::map<int64_t, std::vector<midi::Event> >;

namespace {
midi::Event noteOn(midi::channel_t ch)
{
    midi::Event e(midi::Event::Opcode::NoteOn, midi::Event::MessageType::ChannelVoice20);
    e.setChannel(ch);
    e.setNote(60);
    return e;
}

bool isReset(const midi::Event& e, midi::channel_t ch)
{
    return e.opcode() == midi::Event::Opcode::PitchBend && e.channel() == ch && e.data() == PITCH_BEND_CENTER;
}
}

TEST(Jubelle_BendShaping, ResetWaitsForTheRelease)
{
    Map events;
    events[1000000].push_back(pitchBendEvent(0, 4000));          // the fall
    placeResets(events, { { 1200000, 0, true } });
    // nothing at the note end; the reset comes after the release
    EXPECT_EQ(events.count(1200000), 0u);
    ASSERT_EQ(events.count(1200000 + RELEASE_US), 1u);
    const auto& slot = events[1200000 + RELEASE_US];
    ASSERT_EQ(slot.size(), 2u);
    EXPECT_TRUE(isReset(slot[0], 0));
    EXPECT_EQ(slot[1].opcode(), midi::Event::Opcode::ControlChange);
    EXPECT_EQ(slot[1].index(), CHANNEL_VOLUME_CC);
    EXPECT_EQ(slot[1].data(), FULL_VOLUME);
}

TEST(Jubelle_BendShaping, ResetComesBeforeTheNextNoteOnItsChannel)
{
    Map events;
    events[1300000].push_back(noteOn(0));       // next note on the same channel, inside the release
    events[1250000].push_back(noteOn(1));       // another channel: ignored
    placeResets(events, { { 1200000, 0, false } });
    const auto& slot = events[1300000];
    ASSERT_EQ(slot.size(), 2u);
    EXPECT_TRUE(isReset(slot[0], 0)) << "reset before the note-on, so the next note isn't bent";
    EXPECT_EQ(slot[1].opcode(), midi::Event::Opcode::NoteOn);
    EXPECT_EQ(events.count(1200000 + RELEASE_US), 0u);
}

TEST(Jubelle_BendShaping, LegatoNextNoteAtTheSameMoment)
{
    Map events;
    events[1200000].push_back(noteOn(0));
    placeResets(events, { { 1200000, 0, true } });
    const auto& slot = events[1200000];
    ASSERT_EQ(slot.size(), 3u);
    EXPECT_TRUE(isReset(slot[0], 0));
    EXPECT_EQ(slot[2].opcode(), midi::Event::Opcode::NoteOn);
}

TEST(Jubelle_BendShaping, FadeFollowsTheGesture)
{
    EXPECT_EQ(fadedVolume(0.0), FULL_VOLUME);
    EXPECT_GT(fadedVolume(0.3), fadedVolume(0.7));
    EXPECT_NEAR(double(fadedVolume(1.0)), FULL_VOLUME * (1.0 - FADE_DEPTH), 1.0);
    // about -16 dB at the end (FluidSynth: 40 log10(v / 127) dB)
    EXPECT_NEAR(40.0 * std::log10(fadedVolume(1.0) / 127.0), -16.0, 0.5);
}
