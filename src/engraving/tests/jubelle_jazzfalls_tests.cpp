/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "engraving/dom/articulation.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "engraving/playback/jubelle/articulationplayback.h"
#include "engraving/playback/jubelle/jazzfalls.h"
#include "engraving/playback/playbackmodel.h"

#include "mpe/tests/mocks/articulationprofilesrepositorymock.h"

#include "utils/scorerw.h"

using namespace mu::engraving;
using namespace mu::engraving::jubelle;
using namespace muse::mpe;

//! Jubelle playback block: falls and doits in real time, by instrument.
namespace {
// MuseScore-like fall: holds until 50%, then drops 10 semitones, speeding up
PitchCurve museScoreFall()
{
    PitchCurve c;
    c[0] = 0;
    c[50 * ONE_PERCENT] = 0;
    c[60 * ONE_PERCENT] = -1 * PITCH_LEVEL_STEP;
    c[70 * ONE_PERCENT] = -2 * PITCH_LEVEL_STEP;
    c[80 * ONE_PERCENT] = -4 * PITCH_LEVEL_STEP;
    c[90 * ONE_PERCENT] = -7 * PITCH_LEVEL_STEP;
    c[HUNDRED_PERCENT] = -10 * PITCH_LEVEL_STEP;
    return c;
}

double moveStart(const PitchCurve& c)
{
    duration_percentage_t last = 0;
    for (auto it = c.cbegin(); it != c.cend(); ++it) {
        if (it->second != 0) {
            return double(last) / HUNDRED_PERCENT;
        }
        last = it->first;
    }
    return 1.0;
}
}

TEST(Jubelle_JazzFalls, LengthIsInMillisecondsNotAShareOfTheNote)
{
    // A 300 ms fall: on a 2 s whole note it starts at 85%, on a 600 ms note at 50%
    EXPECT_NEAR(moveStart(retimeFall(museScoreFall(), 2000, 300, false)), 0.85, 1e-3);
    EXPECT_NEAR(moveStart(retimeFall(museScoreFall(), 600, 300, false)), 0.50, 1e-3);
    // never before 30% of a short note
    EXPECT_NEAR(moveStart(retimeFall(museScoreFall(), 200, 300, false)), 0.30, 1e-3);
}

TEST(Jubelle_JazzFalls, TheDropAndItsCurveAreKept)
{
    const PitchCurve c = retimeFall(museScoreFall(), 2000, 300, false);
    EXPECT_EQ(c.rbegin()->second, -10 * PITCH_LEVEL_STEP);   // same total drop
    // The same shape, compressed into the last 15%: halfway through the fall
    // (original 75%) the pitch is between -2 and -4 semitones as before
    std::vector<pitch_level_t> values;
    for (auto it = c.cbegin(); it != c.cend(); ++it) {
        values.push_back(it->second);
    }
    EXPECT_TRUE(std::is_sorted(values.rbegin(), values.rend()));   // only ever falling
    EXPECT_EQ(values.size(), museScoreFall().size());
}

TEST(Jubelle_JazzFalls, FingeredAndRoughFallsMoveBySemitones)
{
    const PitchCurve c = retimeFall(museScoreFall(), 2000, 300, true);
    pitch_level_t previous = 0;
    int steps = 0;
    for (auto it = c.cbegin(); it != c.cend(); ++it) {
        EXPECT_EQ(it->second % PITCH_LEVEL_STEP, 0);   // always a whole semitone
        EXPECT_LE(it->second, previous);
        if (it->second != previous) {
            ++steps;
        }
        previous = it->second;
    }
    EXPECT_EQ(steps, 10);                                 // one step per semitone
    EXPECT_EQ(c.rbegin()->second, -10 * PITCH_LEVEL_STEP);
    // the fall starts at 85%; the first semitone step comes when the pitch has
    // dropped half a semitone, a moment later (but within the first semitone)
    EXPECT_GE(moveStart(c), 0.85);
    EXPECT_LT(moveStart(c), 0.85 + 0.15 * 0.2);
}

TEST(Jubelle_JazzFalls, ByInstrumentSymbolRegisterAndTempo)
{
    const FallShape trumpet = fallShapeFor(u"brass.trumpet", FallLength::Medium, FallStyle::Lip, 67, 120);
    const FallShape trombone = fallShapeFor(u"brass.trombone", FallLength::Medium, FallStyle::Smooth, 67, 120);
    const FallShape sax = fallShapeFor(u"wind.reed.saxophone.alto", FallLength::Medium, FallStyle::Smooth, 67, 120);
    EXPECT_DOUBLE_EQ(trumpet.lengthMs, 300.0);
    EXPECT_FALSE(trumpet.stepped);                       // lip fall: smooth
    EXPECT_GT(trombone.lengthMs, trumpet.lengthMs);      // the slide
    EXPECT_FALSE(trombone.stepped);
    EXPECT_LT(sax.lengthMs, trumpet.lengthMs);           // fingered: quicker
    EXPECT_TRUE(sax.stepped);                            // and by semitones
    EXPECT_FALSE(fallShapeFor(u"wind.reed.saxophone.alto", FallLength::Medium, FallStyle::Lip, 67, 120).stepped);
    EXPECT_TRUE(fallShapeFor(u"brass.trumpet", FallLength::Medium, FallStyle::Rough, 67, 120).stepped);

    EXPECT_LT(fallShapeFor(u"brass.trumpet", FallLength::Short, FallStyle::Lip, 67, 120).lengthMs, trumpet.lengthMs);
    EXPECT_GT(fallShapeFor(u"brass.trumpet", FallLength::Long, FallStyle::Lip, 67, 120).lengthMs, trumpet.lengthMs);
    EXPECT_LT(fallShapeFor(u"brass.trumpet", FallLength::Medium, FallStyle::Lip, 80, 120).lengthMs, trumpet.lengthMs);
    EXPECT_GT(fallShapeFor(u"brass.trumpet", FallLength::Medium, FallStyle::Lip, 48, 120).lengthMs, trumpet.lengthMs);
    EXPECT_LT(fallShapeFor(u"brass.trumpet", FallLength::Medium, FallStyle::Lip, 67, 240).lengthMs, trumpet.lengthMs);
    EXPECT_GT(fallShapeFor(u"brass.trumpet", FallLength::Medium, FallStyle::Lip, 67, 60).lengthMs, trumpet.lengthMs);
}

TEST(Jubelle_JazzFalls, NothingToMoveIsLeftAlone)
{
    PitchCurve flat;
    flat[0] = 0;
    flat[HUNDRED_PERCENT] = 0;
    EXPECT_EQ(retimeFall(flat, 2000, 300, true), flat);
    EXPECT_EQ(retimeFall(museScoreFall(), 0, 300, false), museScoreFall());
}

//! On MuseScore's real playback model: a medium fall on a whole note at 120 bpm
//! lasts about 300 ms at the end of the note, instead of the second half of it;
//! a fall start you set yourself is kept as you set it.
TEST(Jubelle_JazzFalls, OnTheRealPlaybackModel)
{
    auto pattern = [](std::vector<int> pitches) {
        ArticulationPatternSegment segment;
        segment.arrangementPattern = ArrangementPattern(9900, 0);
        for (size_t i = 0; i < pitches.size(); ++i) {
            segment.expressionPattern.dynamicOffsetMap.emplace(int(i) * 10 * ONE_PERCENT, dynamicLevelFromType(muse::mpe::DynamicType::Natural));
            segment.pitchPattern.pitchOffsetMap.emplace(int(i) * 10 * ONE_PERCENT, pitches[i]);
        }
        ArticulationPattern p;
        p.emplace(0, segment);
        return p;
    };
    auto profile = std::make_shared<ArticulationsProfile>();
    profile->setPattern(ArticulationType::Standard, pattern(std::vector<int>(11, 0)));
    profile->setPattern(ArticulationType::Fall, pattern({ 0, 0, 0, 0, 0, 0, -100, -200, -300, -400, -500 }));
    profile->setPattern(ArticulationType::QuickFall, pattern({ 0, 0, 0, 0, 0, 0, -100, -200, -300, -400, -500 }));
    auto repository = std::make_shared<::testing::NiceMock<muse::mpe::ArticulationProfilesRepositoryMock> >();
    ON_CALL(*repository, defaultProfile(::testing::_)).WillByDefault(::testing::Return(profile));

    MasterScore* score = ScoreRW::readScore(u"jubelle_data/articulation_trumpet.mscx");
    ASSERT_TRUE(score);
    Part* part = score->parts().front();
    Chord* chord = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
    ASSERT_TRUE(chord);
    score->startCmd(muse::TranslatableString::untranslatable("fall"));
    Articulation* a = Factory::createArticulation(chord);
    a->setSymId(SymId::brassFallLipMedium);
    a->setParent(chord);
    a->setTrack(chord->track());
    score->undoAddElement(a);
    score->endCmd();

    PlaybackModel model(muse::modularity::globalCtx());
    model.profilesRepository.set(repository);
    model.load(score);

    auto firstFallStart = [&]() {
        const auto& events = model.resolveTrackPlaybackData(part->id(), part->instrumentId()).originEvents;
        for (const auto& [ts, list] : events) {
            for (const PlaybackEvent& e : list) {
                if (const auto* n = std::get_if<muse::mpe::NoteEvent>(&e)) {
                    return moveStart(n->pitchCtx().pitchCurve);
                }
            }
        }
        return -1.0;
    };

    // MuseScore plays a lip fall as a "quick fall"; the symbol says medium.
    // 2 s whole note x 99% = 1980 ms; a 300 ms fall starts at 1 - 300/1980
    EXPECT_NEAR(firstFallStart(), 1.0 - 300.0 / 1980.0, 0.002);

    // Your own fall start wins (here: as the profile has it, from 50%)
    ArticulationPlaybackSettings settings;
    settings.scoreValues.set(ArtParam::FallStart, 50);
    score->startCmd(muse::TranslatableString::untranslatable("settings"));
    settings.write(score);
    score->endCmd();
    EXPECT_NEAR(firstFallStart(), 0.5, 0.002);

    delete score;
}

TEST(Jubelle_JazzFalls, OtherJazzGestures)
{
    EXPECT_EQ(jazzGestureForSymbol("brassBend"), JazzGesture::Bend);
    EXPECT_EQ(jazzGestureForSymbol("brassLiftShort"), JazzGesture::LiftShort);
    EXPECT_EQ(jazzGestureForSymbol("brassLiftSmoothLong"), JazzGesture::LiftLong);
    EXPECT_EQ(jazzGestureForSymbol("brassSmear"), JazzGesture::Smear);
    EXPECT_EQ(jazzGestureForSymbol("brassFlip"), JazzGesture::Flip);
    EXPECT_EQ(jazzGestureForSymbol("brassValveTrill"), JazzGesture::ValveTrill);
    EXPECT_EQ(jazzGestureForSymbol("articStaccatoAbove"), JazzGesture::None);

    auto lowest = [](const PitchCurve& c) {
        pitch_level_t v = 0;
        for (auto it = c.cbegin(); it != c.cend(); ++it) {
            v = std::min(v, it->second);
        }
        return v;
    };
    auto highest = [](const PitchCurve& c) {
        pitch_level_t v = 0;
        for (auto it = c.cbegin(); it != c.cend(); ++it) {
            v = std::max(v, it->second);
        }
        return v;
    };
    // Bend: down a semitone and back, then on pitch
    const PitchCurve bend = jazzGestureCurve(JazzGesture::Bend, 1000);
    EXPECT_EQ(lowest(bend), -PITCH_LEVEL_STEP);
    EXPECT_EQ(bend.cbegin()->second, 0);
    EXPECT_EQ(bend.rbegin()->second, 0);

    // Lifts start below and arrive on the note, in real time
    const PitchCurve shortLift = jazzGestureCurve(JazzGesture::LiftShort, 1000);
    const PitchCurve longLift = jazzGestureCurve(JazzGesture::LiftLong, 1000);
    EXPECT_EQ(shortLift.cbegin()->second, -3 * PITCH_LEVEL_STEP);
    EXPECT_EQ(longLift.cbegin()->second, -7 * PITCH_LEVEL_STEP);
    EXPECT_EQ(longLift.rbegin()->second, 0);
    EXPECT_EQ(moveStart(jazzGestureCurve(JazzGesture::LiftMedium, 1000)), 0.0);
    // arrives at 160 ms of a 1 s note, and at 160 ms (not 16%) of a 4 s note
    auto arrival = [](const PitchCurve& c) {
        for (auto it = c.cbegin(); it != c.cend(); ++it) {
            if (it->second == 0) {
                return double(it->first) / HUNDRED_PERCENT;
            }
        }
        return 1.0;
    };
    EXPECT_NEAR(arrival(jazzGestureCurve(JazzGesture::LiftMedium, 1000)), 0.16, 1e-3);
    EXPECT_NEAR(arrival(jazzGestureCurve(JazzGesture::LiftMedium, 4000)), 0.04, 1e-3);

    // Smear: a little flat, sliding up
    const PitchCurve smear = jazzGestureCurve(JazzGesture::Smear, 1000);
    EXPECT_LT(smear.cbegin()->second, 0);
    EXPECT_GT(smear.cbegin()->second, -PITCH_LEVEL_STEP);

    // Flip: flat until near the end, then up and down
    const PitchCurve flip = jazzGestureCurve(JazzGesture::Flip, 1000);
    EXPECT_GE(moveStart(flip), 0.85);
    EXPECT_EQ(highest(flip), 2 * PITCH_LEVEL_STEP);
    EXPECT_LT(flip.rbegin()->second, 0);

    // Valve trill: narrow and fast
    const PitchCurve shake = jazzGestureCurve(JazzGesture::ValveTrill, 1000);
    EXPECT_LE(highest(shake), PITCH_LEVEL_STEP / 2);
    EXPECT_GE(lowest(shake), -PITCH_LEVEL_STEP / 2);
    EXPECT_GE(shake.size(), 12u);   // about 8 a second, both ways
}
