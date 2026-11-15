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

TEST(Jubelle_BendShaping, SmoothCurveThroughThePatternPoints)
{
    // A fall from the middle of the note: flat, then down 10 semitones (bend units)
    std::vector<CurvePoint> pts;
    for (int i = 0; i <= 10; ++i) {
        const double t = i / 10.0;
        const double drop = t <= 0.5 ? 0.0 : (t - 0.5) / 0.5;
        pts.push_back({ t * 1000000.0, 8192.0 - 4096.0 * drop * drop });
    }
    const std::vector<CurvePoint> out = smoothCurve(pts, 8, 16.0);
    ASSERT_GT(out.size(), pts.size() * 8);
    EXPECT_DOUBLE_EQ(out.front().value, 8192.0);
    EXPECT_DOUBLE_EQ(out.back().value, pts.back().value) << "ends exactly at the pattern's last point";
    for (const CurvePoint& p : pts) {
        bool hit = false;
        for (const CurvePoint& o : out) {
            if (std::abs(o.time - p.time) < 1e-6) {
                EXPECT_NEAR(o.value, p.value, 1e-6) << "passes through every pattern point";
                hit = true;
            }
        }
        EXPECT_TRUE(hit);
    }
    double maxStep = 0;
    for (size_t i = 1; i < out.size(); ++i) {
        EXPECT_LE(out[i].value, out[i - 1].value + 1e-9) << "monotone: a fall never goes back up";
        EXPECT_GE(out[i].value, pts.back().value - 1e-9) << "no overshoot below the target";
        EXPECT_LE(out[i].value, 8192.0 + 1e-9) << "no overshoot above the written pitch";
        maxStep = std::max(maxStep, out[i - 1].value - out[i].value);
    }
    EXPECT_LT(maxStep, 200.0) << "finely sampled";
    // and smooth: no abrupt change of speed at the pattern points (with straight
    // lines the speed would jump at every point)
    for (size_t i = 2; i < out.size(); ++i) {
        const double s1 = (out[i - 1].value - out[i - 2].value) / (out[i - 1].time - out[i - 2].time);
        const double s2 = (out[i].value - out[i - 1].value) / (out[i].time - out[i - 1].time);
        EXPECT_LT(std::abs(s2 - s1), 0.0012) << "at sample " << i;
    }
}
