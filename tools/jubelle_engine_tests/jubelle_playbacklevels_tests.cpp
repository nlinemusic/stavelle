/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

// Jubelle: even instrument levels and the safety limiter (P1).
#include <gtest/gtest.h>

#include <cmath>
#include <iostream>
#include <vector>

#include "audio/engine/internal/dsp/jubellesafetylimiter.h"
#include "jubelle_oldsafetylimiter.h"
#include "audio/engine/internal/synthesizers/fluidsynth/jubellepresetgain.h"
#include "audio/engine/internal/synthesizers/fluidsynth/jubelledynamics.h"

using namespace muse::audio;
using namespace muse::audio::jubelle;

namespace {
// output of the limiter, with its look-ahead delay removed
std::vector<float> limited(dsp::JubelleSafetyLimiter& lim, std::vector<float> in, size_t channels)
{
    const size_t lat = lim.latencySamples();
    in.resize(in.size() + lat * channels, 0.f);
    lim.process(in.data(), in.size() / channels, channels);
    return std::vector<float>(in.begin() + long(lat * channels), in.end());
}

//! energy above `hz` relative to the whole signal, in dB (the "click" a sudden gain change adds)
double energyAboveDb(const std::vector<float>& mono, double sampleRate, double hz)
{
    // a plain DFT on a short window is enough here (and needs no library)
    const size_t n = mono.size();
    double total = 0, high = 0;
    for (size_t k = 1; k < n / 2; k += 3) {
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i) {
            const double w = 0.5 - 0.5 * std::cos(2 * M_PI * double(i) / double(n - 1));
            re += mono[i] * w * std::cos(2 * M_PI * double(k) * double(i) / double(n));
            im -= mono[i] * w * std::sin(2 * M_PI * double(k) * double(i) / double(n));
        }
        const double e = re * re + im * im;
        total += e;
        if (double(k) * sampleRate / double(n) > hz) {
            high += e;
        }
    }
    return 10 * std::log10(high / total);
}
}

TEST(Jubelle_SafetyLimiter, LeavesQuieterMusicExactlyUnchanged)
{
    dsp::JubelleSafetyLimiter lim;
    lim.setSampleRate(44100);
    std::vector<float> orig(4410 * 2);
    for (size_t i = 0; i < orig.size(); ++i) {
        orig[i] = 0.85f * std::sin(float(i) * 0.01f);
    }
    EXPECT_EQ(limited(lim, orig, 2), orig);   // the same samples, 5 ms later
    EXPECT_FALSE(lim.isLimiting());
    EXPECT_EQ(lim.latencySamples(), 220u);    // 5 ms at 44.1 kHz
}

TEST(Jubelle_SafetyLimiter, NoPeakGoesPastTheThreshold)
{
    for (unsigned rate : { 44100u, 48000u, 96000u }) {
        dsp::JubelleSafetyLimiter lim;
        lim.setSampleRate(rate);
        std::vector<float> buf(rate * 2);
        for (size_t i = 0; i < buf.size(); ++i) {
            // a loud passage with sudden accents
            const float accent = (i / 2) % 9000 < 300 ? 2.5f : 1.f;
            buf[i] = 1.6f * accent * std::sin(float(i / 2) * 0.05f) * (i % 2 ? 0.5f : 1.f);
        }
        const auto out = limited(lim, buf, 2);
        float peak = 0;
        for (float s : out) {
            peak = std::max(peak, std::fabs(s));
        }
        EXPECT_LE(peak, dsp::JubelleSafetyLimiter::THRESHOLD + 1e-6f) << rate;
        EXPECT_GT(peak, 0.85f) << rate;   // turned down only as far as needed
    }
}

TEST(Jubelle_SafetyLimiter, ChannelsAreTurnedDownTogether)
{
    dsp::JubelleSafetyLimiter lim;
    lim.setSampleRate(44100);
    std::vector<float> in(2000 * 2);
    for (size_t i = 0; i < in.size(); i += 2) {
        in[i] = 1.5f;
        in[i + 1] = 0.3f;
    }
    const auto out = limited(lim, in, 2);
    for (size_t i = 0; i < out.size(); i += 2) {
        EXPECT_NEAR(out[i] / out[i + 1], 5.f, 1e-4f);   // stereo image kept
    }
}

TEST(Jubelle_SafetyLimiter, TurnsDownBeforeTheAttackNotAfter)
{
    // a soft chord swells quickly (10 ms) to well over the threshold: the old
    // limiter turned the level down sample by sample as each new peak arrived
    // (a crackle on the attack); the new one ramps down smoothly beforehand
    const unsigned rate = 48000;
    std::vector<float> in(rate / 5 * 2, 0.f);
    for (size_t i = 0; i < in.size() / 2; ++i) {
        const float t = float(i) / float(rate);
        const float onset = 0.1f;
        const float x = std::clamp((t - onset) / 0.010f, 0.f, 1.f);
        const float env = 0.2f + 0.8f * (0.5f - 0.5f * std::cos(float(M_PI) * x));   // smooth: no click of its own
        const float v = env * (0.8f * std::sin(2 * float(M_PI) * 220.f * t) + 0.7f * std::sin(2 * float(M_PI) * 277.f * t)
                               + 0.5f * std::sin(2 * float(M_PI) * 330.f * t));
        in[i * 2] = in[i * 2 + 1] = v;
    }
    dsp::JubelleSafetyLimiter lim;
    lim.setSampleRate(rate);
    const auto now = limited(lim, in, 2);

    jubelle_test_old::JubelleOldSafetyLimiter old;
    old.setSampleRate(rate);
    std::vector<float> before = in;
    old.process(before.data(), before.size() / 2, 2);

    // the attack: 40 ms around the onset, left channel
    auto window = [rate](const std::vector<float>& v) {
        std::vector<float> m;
        for (size_t i = rate / 10 - rate / 100; i < rate / 10 + rate / 33; ++i) {
            m.push_back(v[i * 2]);
        }
        return m;
    };
    const double clickNow = energyAboveDb(window(now), rate, 4000);
    const double clickBefore = energyAboveDb(window(before), rate, 4000);
    // the chord itself has nothing above 4 kHz: what is there is the limiter's own noise
    std::cout << "[ measure  ] limiter noise above 4 kHz on the attack: new " << clickNow << " dB, old " << clickBefore << " dB\n";
    EXPECT_LT(clickNow, clickBefore - 10.0) << "new " << clickNow << " dB, old " << clickBefore << " dB";
    float peak = 0;
    for (float s : now) {
        peak = std::max(peak, std::fabs(s));
    }
    EXPECT_LE(peak, dsp::JubelleSafetyLimiter::THRESHOLD + 1e-6f);
}

TEST(Jubelle_SafetyLimiter, ComesBackSmoothlyAfterALoudPeak)
{
    dsp::JubelleSafetyLimiter lim;
    lim.setSampleRate(44100);
    std::vector<float> loud(64 * 2, 2.f);
    lim.process(loud.data(), loud.size() / 2, 2);
    std::vector<float> settle(400 * 2, 0.1f);
    lim.process(settle.data(), settle.size() / 2, 2);
    EXPECT_LT(lim.currentGain(), 0.5f);

    std::vector<float> quiet(44100 / 10 * 2, 0.1f);   // 100 ms
    lim.process(quiet.data(), quiet.size() / 2, 2);
    const float after100ms = lim.currentGain();
    EXPECT_GT(after100ms, 0.6f);
    EXPECT_LT(after100ms, 0.9f);                        // not an abrupt jump
    for (size_t i = 2; i < quiet.size(); i += 2) {
        EXPECT_GE(quiet[i], quiet[i - 2] - 1e-7f);      // only ever rising
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

// ---------------------------------------------------------------------------
// Audio export quality (18 Nov 2026)
// ---------------------------------------------------------------------------

TEST(Jubelle_Dynamics, VelocityInstrumentsUseMuseScoresScale)
{
    using namespace muse::audio::jubelle;
    // MuseScore: ppp 16, pp 32, p 48, mp 64, mf 80, f 96, ff 112, fff 127; unmarked = 64
    const int expected[8] = { 16, 32, 48, 64, 80, 96, 112, 127 };
    for (int step = 0; step < 8; ++step) {
        EXPECT_EQ(museScoreLevel(step), expected[step]) << step;
    }
    EXPECT_EQ(museScoreLevel(3.5 - 0.5), 64) << "unmarked music (Natural is 3.5 steps above ppp, less half a step)";
    EXPECT_EQ(museScoreLevel(-3), 16);
    EXPECT_EQ(museScoreLevel(12), 127);
    // the make-up gain is exactly taken back: 3.9 dB as attenuation
    EXPECT_NEAR(velocityInstrumentAttenuationCb() / 10.0, 20.0 * std::log10(makeupGain()), 1e-9);
}

#include "audio/engine/internal/dsp/jubelledither.h"

TEST(Jubelle_Dither, SixteenBitQuietSoundsStayClean)
{
    // a very quiet sine (-80 dBFS, about 3 steps of 16 bits): plain rounding
    // turns it into a staircase whose error follows the music (distortion);
    // with dither the error is noise, unrelated to the music
    const double rate = 48000;
    const size_t n = 48000;
    std::vector<double> sig(n);
    for (size_t i = 0; i < n; ++i) {
        sig[i] = 1e-4 * std::sin(2 * M_PI * 1000.0 * double(i) / rate);
    }
    dsp::JubelleTpdfDither dither;
    auto harmonicError = [&](bool dithered) {
        // error correlated with the 3rd harmonic of the tone: the audible distortion
        double re = 0, im = 0;
        for (size_t i = 0; i < n; ++i) {
            const double q = dithered ? double(dither.toInt16(float(sig[i])))
                             : std::round(double(std::clamp(float(sig[i]), -1.f, 1.f)) * 32767.0);
            const double err = q / 32767.0 - sig[i];
            re += err * std::cos(2 * M_PI * 3000.0 * double(i) / rate);
            im += err * std::sin(2 * M_PI * 3000.0 * double(i) / rate);
        }
        return 20 * std::log10(2 * std::sqrt(re * re + im * im) / double(n) + 1e-20);
    };
    const double plain = harmonicError(false);
    const double withDither = harmonicError(true);
    std::cout << "[ measure  ] 3rd-harmonic distortion of a -80 dBFS tone at 16 bits: plain " << plain << " dB, dithered "
              << withDither << " dB\n";
    EXPECT_LT(withDither, plain - 15.0) << "plain " << plain << " dB, dithered " << withDither << " dB";
}

TEST(Jubelle_Dither, RepeatableAndInRange)
{
    dsp::JubelleTpdfDither a, b;
    for (int i = 0; i < 10000; ++i) {
        const float v = float(std::sin(i * 0.01)) * 0.5f;
        EXPECT_EQ(a.toInt16(v), b.toInt16(v));   // the same export twice gives the same file
    }
    dsp::JubelleTpdfDither c;
    for (int i = 0; i < 1000; ++i) {
        EXPECT_GE(c.toInt16(2.f), 32765);   // full scale, never wrapped round
    }
    for (int i = 0; i < 1000; ++i) {
        const int32_t s = c.toInt16(-1.f);
        EXPECT_GE(s, -32768);
        EXPECT_LE(s, -32766);
    }
}
