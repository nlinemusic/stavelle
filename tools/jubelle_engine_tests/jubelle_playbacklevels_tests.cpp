/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

// Jubelle: even instrument levels and the safety limiter (P1).
#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "audio/engine/internal/dsp/jubellesafetylimiter.h"
#include "audio/engine/internal/synthesizers/fluidsynth/jubellepresetgain.h"

using namespace muse::audio;
using namespace muse::audio::synth;

TEST(Jubelle_SafetyLimiter, LeavesQuieterMusicExactlyUnchanged)
{
    dsp::JubelleSafetyLimiter lim;
    lim.setSampleRate(44100);
    std::vector<float> buf(4410 * 2), orig;
    for (size_t i = 0; i < buf.size(); ++i) {
        buf[i] = 0.85f * std::sin(float(i) * 0.01f);
    }
    orig = buf;
    lim.process(buf.data(), buf.size() / 2, 2);
    EXPECT_EQ(buf, orig);
    EXPECT_FALSE(lim.isLimiting());
}

TEST(Jubelle_SafetyLimiter, NoPeakGoesPastTheThreshold)
{
    dsp::JubelleSafetyLimiter lim;
    lim.setSampleRate(44100);
    std::vector<float> buf(44100 * 2);
    for (size_t i = 0; i < buf.size(); ++i) {
        buf[i] = 1.6f * std::sin(float(i / 2) * 0.05f) * (i % 2 ? 0.5f : 1.f);
    }
    lim.process(buf.data(), buf.size() / 2, 2);
    float peak = 0;
    for (float s : buf) {
        peak = std::max(peak, std::fabs(s));
    }
    EXPECT_LE(peak, dsp::JubelleSafetyLimiter::THRESHOLD + 1e-6f);
    EXPECT_GT(peak, 0.85f); // turned down only as far as needed
}

TEST(Jubelle_SafetyLimiter, ChannelsAreTurnedDownTogether)
{
    dsp::JubelleSafetyLimiter lim;
    lim.setSampleRate(44100);
    float frame[2] = { 1.5f, 0.3f };
    lim.process(frame, 1, 2);
    EXPECT_NEAR(frame[0] / frame[1], 5.f, 1e-4f); // stereo image kept
}

TEST(Jubelle_SafetyLimiter, ComesBackSmoothlyAfterALoudPeak)
{
    dsp::JubelleSafetyLimiter lim;
    lim.setSampleRate(44100);
    float loud[2] = { 2.f, 2.f };
    lim.process(loud, 1, 2);
    EXPECT_NEAR(lim.currentGain(), dsp::JubelleSafetyLimiter::THRESHOLD / 2.f, 1e-5f);

    std::vector<float> quiet(44100 / 10 * 2, 0.1f);   // 100 ms
    lim.process(quiet.data(), quiet.size() / 2, 2);
    const float after100ms = lim.currentGain();
    EXPECT_GT(after100ms, 0.45f);
    EXPECT_LT(after100ms, 0.9f);                        // not an abrupt jump
    for (size_t i = 2; i < quiet.size(); i += 2) {
        EXPECT_GE(quiet[i], quiet[i - 2]);              // only ever rising
    }

    std::vector<float> rest(44100 * 3 * 2, 0.1f);       // 3 s
    lim.process(rest.data(), rest.size() / 2, 2);
    EXPECT_FALSE(lim.isLimiting());
    EXPECT_EQ(rest.back(), 0.1f);
}

TEST(Jubelle_PresetGain, TableCoversTheBuiltInSoundFontWithinLimits)
{
    int count = 0;
    for (const auto& g : jubelle::MS_BASIC_PRESET_GAINS) {
        EXPECT_LE(std::abs(int(g.correctionCentibels)), 120) << g.bank << ":" << g.program;
        ++count;
    }
    EXPECT_GT(count, 300);
}

TEST(Jubelle_PresetGain, BigBandInstrumentsAreBroughtTogether)
{
    // Measured before correction: trombone -13.7, tuba -14.5, piano -23.1,
    // baritone sax -24.6 dBFS. The table must pull them toward each other.
    EXPECT_LT(jubelle::presetCorrectionCentibels(0, 57), -40);  // trombone down
    EXPECT_LT(jubelle::presetCorrectionCentibels(0, 58), -40);  // tuba down
    EXPECT_GT(jubelle::presetCorrectionCentibels(0, 0), 20);    // piano up
    EXPECT_GT(jubelle::presetCorrectionCentibels(0, 67), 20);   // baritone sax up
    EXPECT_EQ(jubelle::presetCorrectionCentibels(99, 99), 0);   // unknown: unchanged
}

TEST(Jubelle_PresetGain, OnlyTheBuiltInSoundFontIsCorrected)
{
    EXPECT_TRUE(jubelle::isBuiltInSoundFont("/usr/share/jubelle/sound/MS Basic.sf3"));
    EXPECT_TRUE(jubelle::isBuiltInSoundFont("C:\\Program Files\\Jubelle\\sound\\ms basic.SF3"));
    EXPECT_FALSE(jubelle::isBuiltInSoundFont("/home/me/SoundFonts/GeneralUser GS.sf2"));
    EXPECT_FALSE(jubelle::isBuiltInSoundFont("/home/me/MS Basic copy.sf3"));
}
