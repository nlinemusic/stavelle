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
#include "audio/engine/internal/synthesizers/fluidsynth/jubelledynamics.h"

using namespace muse::audio;
using namespace muse::audio::jubelle;

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
    for (const auto& g : MS_BASIC_PRESET_GAINS) {
        EXPECT_LE(std::abs(int(g.correctionCentibels)), 120) << g.bank << ":" << g.program;
        ++count;
    }
    EXPECT_GT(count, 300);
}

TEST(Jubelle_PresetGain, BigBandInstrumentsAreBroughtTogether)
{
    // The soundfont's trombone and tuba are much louder than its piano and
    // baritone sax. Table v2 (17 Nov 2026) measures perceived loudness
    // (K-weighted) over three notes and adds acoustic-role offsets, so the
    // exact figures differ from v1's, but the direction must hold: the loud
    // brass comes down against the piano and the baritone sax.
    EXPECT_LT(presetCorrectionCentibels(0, 57), 0);    // trombone down
    EXPECT_LT(presetCorrectionCentibels(0, 58), 0);    // tuba down
    EXPECT_GT(presetCorrectionCentibels(0, 0), 20);    // piano up
    EXPECT_GT(presetCorrectionCentibels(0, 67), 20);   // baritone sax up
    EXPECT_GE(presetCorrectionCentibels(0, 0) - presetCorrectionCentibels(0, 57), 60);   // at least 6 dB closer together
    EXPECT_EQ(presetCorrectionCentibels(99, 99), 0);   // unknown: unchanged
}

TEST(Jubelle_PresetGain, OnlyTheBuiltInSoundFontIsCorrected)
{
    EXPECT_TRUE(isBuiltInSoundFont("/usr/share/jubelle/sound/MS Basic.sf3"));
    EXPECT_TRUE(isBuiltInSoundFont("C:\\Program Files\\Jubelle\\sound\\ms basic.SF3"));
    EXPECT_FALSE(isBuiltInSoundFont("/home/me/SoundFonts/GeneralUser GS.sf2"));
    EXPECT_FALSE(isBuiltInSoundFont("/home/me/MS Basic copy.sf3"));
}

TEST(Jubelle_PresetGain, EnsembleTrimGrowsWithTheBand)
{
    EXPECT_DOUBLE_EQ(ensembleTrimDb(1), 0.0);    // solo: unchanged
    EXPECT_DOUBLE_EQ(ensembleTrimDb(4), 0.0);    // small group: unchanged
    EXPECT_NEAR(ensembleTrimDb(8), -4.52, 0.01);
    EXPECT_NEAR(ensembleTrimDb(17), -9.43, 0.01);   // big band
    EXPECT_DOUBLE_EQ(ensembleTrimDb(200), -15.0);   // never more than 15 dB
    for (int n = 5; n < 60; ++n) {
        EXPECT_LE(ensembleTrimDb(n + 1), ensembleTrimDb(n));   // more players, never louder
    }
}

// ---------------------------------------------------------------------------
// Dynamics (17 Nov 2026): the loudness of each marking, in decibels
// ---------------------------------------------------------------------------
namespace {
//! what a soundfont synth does with a MIDI level (SoundFont 2 standard curve)
double heardDb(int midiLevel)
{
    return 40.0 * std::log10(midiLevel / 127.0);
}

const muse::audio::jubelle::DynamicsFamily ALL_FAMILIES[] = {
    muse::audio::jubelle::DynamicsFamily::Generic, muse::audio::jubelle::DynamicsFamily::Brass,
    muse::audio::jubelle::DynamicsFamily::Reeds, muse::audio::jubelle::DynamicsFamily::Flutes,
    muse::audio::jubelle::DynamicsFamily::Strings, muse::audio::jubelle::DynamicsFamily::Keyboards,
    muse::audio::jubelle::DynamicsFamily::Percussion, muse::audio::jubelle::DynamicsFamily::Voices,
};
}

TEST(Jubelle_Dynamics, MuseScoresMappingMadeLoudMarkingsNearlyEqual)
{
    // the reason for the change: MuseScore's levels 80 (mf), 96 (f), 112 (ff), 127 (fff)
    EXPECT_NEAR(heardDb(96) - heardDb(80), 3.2, 0.1);
    EXPECT_NEAR(heardDb(127) - heardDb(112), 2.2, 0.1);
}

TEST(Jubelle_Dynamics, EveryStepIsClearlyAudibleForEveryFamily)
{
    using namespace muse::audio::jubelle;
    for (DynamicsFamily f : ALL_FAMILIES) {
        int previous = 0;
        for (int step = 0; step <= 7; ++step) {
            const int level = midiLevel(step, f);
            EXPECT_GT(level, previous) << "step " << step;
            if (step > 0 && step < 7) {
                // at least 3 dB between neighbouring markings, even for flutes (the top one may meet the ceiling)
                EXPECT_GE(heardDb(level) - heardDb(previous), 3.0) << "family " << int(f) << " step " << step;
            }
            previous = level;
        }
        EXPECT_EQ(midiLevel(4, f), MF_MIDI_LEVEL) << "mf is the anchor for every family";
        EXPECT_GE(heardDb(midiLevel(5, f)) - heardDb(midiLevel(4, f)), 4.0) << "mf to f";
        EXPECT_LE(midiLevel(7, f), 127);
        EXPECT_GE(midiLevel(0, f), 1);
    }
}

TEST(Jubelle_Dynamics, MezzoForteAndUnmarkedMusicAreAsLoudAsBefore)
{
    using namespace muse::audio::jubelle;
    const double makeupDb = 20.0 * std::log10(makeupGain());
    // mf: MuseScore's level 80 against Jubelle's 64 plus the make-up gain
    EXPECT_NEAR(heardDb(MF_MIDI_LEVEL) + makeupDb, heardDb(80), 0.05);
    // no dynamic at all: MuseScore's level 64
    EXPECT_NEAR(heardDb(naturalMidiLevel()) + makeupDb, heardDb(64), 0.35);
}

TEST(Jubelle_Dynamics, InstrumentsRespondDifferently)
{
    using namespace muse::audio::jubelle;
    EXPECT_EQ(familyFor(2, "trumpet"), DynamicsFamily::Brass);
    EXPECT_EQ(familyFor(2, "trombone"), DynamicsFamily::Brass);
    EXPECT_EQ(familyFor(2, "saxophone"), DynamicsFamily::Reeds);
    EXPECT_EQ(familyFor(2, "flute"), DynamicsFamily::Flutes);
    EXPECT_EQ(familyFor(1, "violin"), DynamicsFamily::Strings);
    EXPECT_EQ(familyFor(3, "drumset"), DynamicsFamily::Percussion);
    // brass has the widest range, flutes the narrowest
    const double brass = heardDb(midiLevel(6, DynamicsFamily::Brass)) - heardDb(midiLevel(2, DynamicsFamily::Brass));
    const double flute = heardDb(midiLevel(6, DynamicsFamily::Flutes)) - heardDb(midiLevel(2, DynamicsFamily::Flutes));
    EXPECT_GT(brass, flute + 3.0);
}

TEST(Jubelle_Dynamics, ACrescendoRisesSmoothly)
{
    using namespace muse::audio::jubelle;
    // p to ff in small steps, as a hairpin over a held note sends them
    double last = heardDb(midiLevel(2.0, DynamicsFamily::Strings));
    for (double s = 2.05; s <= 6.0; s += 0.05) {
        const double now = heardDb(midiLevel(s, DynamicsFamily::Strings));
        EXPECT_GE(now, last);
        EXPECT_LE(now - last, 0.6) << "no audible jump at " << s;
        last = now;
    }
}

TEST(Jubelle_Dynamics, Vst3VelocitiesFollowTheSameSteps)
{
    namespace jd = muse::audio::jubelle;
    EXPECT_NEAR(jd::vstVelocity(7.0), 1.0, 1e-9) << "fff is full velocity";
    EXPECT_GT(jd::vstVelocity(0.0), 0.12) << "ppp is still audible (MuseScore sent almost 0)";
    for (int step = 0; step < 7; ++step) {
        const double a = jd::vstVelocity(step);
        const double b = jd::vstVelocity(step + 1);
        EXPECT_GT(b, a);
        // on the usual 40*log10(v) curve each step is at least 3 dB
        EXPECT_GE(40.0 * std::log10(b / a), 2.9) << "step " << step;
    }
    // mf to f: 5 dB, as with the built-in sounds
    EXPECT_NEAR(40.0 * std::log10(jd::vstVelocity(5.0) / jd::vstVelocity(4.0)), 5.0, 0.05);
}
