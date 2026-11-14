/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

#include <gtest/gtest.h>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"
#include "engraving/playback/jubelle/articulationplayback.h"

#include "mpe/events.h"

#include "utils/scorerw.h"

using namespace mu::engraving;
using namespace mu::engraving::jubelle;
using namespace muse::mpe;

//! Jubelle Phase 5A: articulation playback settings.
//! The patterns below are copied from MuseScore's own wind-instrument profile
//! (general_winds_articulations_profile.json), and the played notes are built
//! by MuseScore's own playback event code (mpe/events.h).
namespace {
ArticulationPattern makePattern(duration_percentage_t durationFactor, duration_percentage_t timestampOffset,
                                const std::vector<int>& dynamics, const std::vector<int>& pitches)
{
    ArticulationPatternSegment segment;
    segment.arrangementPattern = ArrangementPattern(durationFactor, timestampOffset);
    for (size_t i = 0; i < dynamics.size(); ++i) {
        segment.expressionPattern.dynamicOffsetMap.emplace(int(i) * 10 * ONE_PERCENT, dynamics[i]);
        segment.pitchPattern.pitchOffsetMap.emplace(int(i) * 10 * ONE_PERCENT, pitches[i]);
    }
    ArticulationPattern pattern;
    pattern.emplace(0, segment);
    return pattern;
}

ArticulationsProfilePtr museScoreWindsProfile()
{
    const std::vector<int> flat(11, 0);
    const std::vector<int> standardDyn { 0, 1500, 2500, 3500, 4500, 5000, 4500, 3500, 2500, 1500, 0 };
    auto profile = std::make_shared<ArticulationsProfile>();
    profile->setPattern(ArticulationType::Standard, makePattern(9900, 0, standardDyn, flat));
    profile->setPattern(ArticulationType::Staccato, makePattern(5000, 0, standardDyn, flat));
    profile->setPattern(ArticulationType::Staccatissimo, makePattern(2500, 0, standardDyn, flat));
    profile->setPattern(ArticulationType::Tenuto, makePattern(9900, 0, standardDyn, flat));
    profile->setPattern(ArticulationType::Accent,
                        makePattern(9900, 0, { 0, 2000, 4000, 6000, 5500, 5000, 4500, 3500, 2500, 1500, 0 }, flat));
    profile->setPattern(ArticulationType::Marcato,
                        makePattern(9900, 0, { 0, 2000, 4000, 6000, 5500, 5000, 4500, 3500, 2500, 1500, 0 }, flat));
    profile->setPattern(ArticulationType::Fall,
                        makePattern(9900, 0, { 0, 1500, 3200, 4300, 5000, 4500, 3500, 2500, 1500, 500, 0 },
                                    { 0, 0, 0, 0, 0, 0, -100, -200, -300, -400, -500 }));
    profile->setPattern(ArticulationType::Doit,
                        makePattern(9900, 0, standardDyn, { 0, 0, 0, 0, 0, 0, 100, 200, 300, 400, 500 }));
    profile->setPattern(ArticulationType::Scoop,
                        makePattern(12900, -3000, standardDyn, { -200, -100, -50, 0, 0, 0, 0, 0, 0, 0, 0 }));
    profile->setPattern(ArticulationType::ContinuousGlissando,
                        makePattern(9900, 0, standardDyn, { 0, 10, 20, 30, 40, 50, 60, 70, 80, 90, 100 }));
    return profile;
}

constexpr timestamp_t NOTE_START = 1000000;   // 1 s
constexpr duration_t NOTE_LENGTH = 500000;    // half a second

//! Builds the note MuseScore would play for one articulation
muse::mpe::NoteEvent play(const ArticulationsProfilePtr& profile, ArticulationType type)
{
    ArticulationMeta meta(type, profile->pattern(type), NOTE_START, NOTE_LENGTH);
    ArticulationMap map;
    map.emplace(type, ArticulationAppliedData(meta, 0, HUNDRED_PERCENT));
    map.preCalculateAverageData();
    return muse::mpe::NoteEvent(NOTE_START, NOTE_LENGTH, 0, 0, muse::mpe::pitchLevel(muse::mpe::PitchClass::C, 5),
                     dynamicLevelFromType(muse::mpe::DynamicType::mf), map, 2.0);
}

ArticulationPlaybackValues values(std::initializer_list<std::pair<ArtParam, int> > list)
{
    ArticulationPlaybackValues v;
    for (const auto& [p, value] : list) {
        v.set(p, value);
    }
    return v;
}

pitch_level_t lowestPitchOffset(const muse::mpe::NoteEvent& e)
{
    pitch_level_t lowest = 0;
    for (const auto& p : e.pitchCtx().pitchCurve) {
        lowest = std::min(lowest, p.second);
    }
    return lowest;
}
}

TEST(Jubelle_ArticulationPlayback, NothingSetSoundsExactlyLikeMuseScore)
{
    const ArticulationsProfilePtr base = museScoreWindsProfile();
    EXPECT_EQ(applyArticulationPlayback(base, ArticulationPlaybackValues()), base);

    // Setting a value to MuseScore's own default also changes nothing audible
    const ArticulationsProfilePtr same = applyArticulationPlayback(base, values({ { ArtParam::StaccatoLength, 50 },
                                                                                   { ArtParam::FallStart, 50 },
                                                                                   { ArtParam::FallRange, 10 },
                                                                                   { ArtParam::GlissandoStart, 0 } }));
    for (ArticulationType t : { ArticulationType::Staccato, ArticulationType::Fall, ArticulationType::ContinuousGlissando }) {
        EXPECT_EQ(play(same, t).arrangementCtx().actualDuration, play(base, t).arrangementCtx().actualDuration);
        EXPECT_EQ(play(same, t).pitchCtx().pitchCurve, play(base, t).pitchCtx().pitchCurve);
        EXPECT_EQ(play(same, t).expressionCtx().expressionCurve, play(base, t).expressionCtx().expressionCurve);
    }
}

TEST(Jubelle_ArticulationPlayback, NoteLengths)
{
    const ArticulationsProfilePtr base = museScoreWindsProfile();
    EXPECT_EQ(play(base, ArticulationType::Staccato).arrangementCtx().actualDuration, NOTE_LENGTH / 2);

    const ArticulationsProfilePtr p = applyArticulationPlayback(base, values({ { ArtParam::StaccatoLength, 30 },
                                                                                { ArtParam::StaccatissimoLength, 15 },
                                                                                { ArtParam::TenutoLength, 100 },
                                                                                { ArtParam::MarcatoLength, 70 } }));
    EXPECT_EQ(play(p, ArticulationType::Staccato).arrangementCtx().actualDuration, NOTE_LENGTH * 30 / 100);
    EXPECT_EQ(play(p, ArticulationType::Staccatissimo).arrangementCtx().actualDuration, NOTE_LENGTH * 15 / 100);
    EXPECT_EQ(play(p, ArticulationType::Tenuto).arrangementCtx().actualDuration, NOTE_LENGTH);
    EXPECT_EQ(play(p, ArticulationType::Marcato).arrangementCtx().actualDuration, NOTE_LENGTH * 70 / 100);

    // The note still starts on time, and the base profile is untouched
    EXPECT_EQ(play(p, ArticulationType::Staccato).arrangementCtx().actualTimestamp, NOTE_START);
    EXPECT_EQ(play(base, ArticulationType::Staccato).arrangementCtx().actualDuration, NOTE_LENGTH / 2);
}

TEST(Jubelle_ArticulationPlayback, AccentLoudness)
{
    const ArticulationsProfilePtr base = museScoreWindsProfile();
    auto peak = [](const muse::mpe::NoteEvent& e) { return e.expressionCtx().expressionCurve.maxAmplitudeLevel(); };

    const dynamic_level_t plain = peak(play(base, ArticulationType::Standard));
    const dynamic_level_t museScoreAccent = peak(play(base, ArticulationType::Accent));
    EXPECT_GT(museScoreAccent, plain);

    const dynamic_level_t softer = peak(play(applyArticulationPlayback(base, values({ { ArtParam::AccentLoudness, 4 } })),
                                             ArticulationType::Accent));
    const dynamic_level_t louder = peak(play(applyArticulationPlayback(base, values({ { ArtParam::AccentLoudness, 25 } })),
                                             ArticulationType::Accent));
    const dynamic_level_t none = peak(play(applyArticulationPlayback(base, values({ { ArtParam::AccentLoudness, 0 } })),
                                           ArticulationType::Accent));
    EXPECT_LT(softer, museScoreAccent);
    EXPECT_GT(louder, museScoreAccent);
    EXPECT_EQ(none, plain);

    // Marcato loudness is separate from accent loudness
    const ArticulationsProfilePtr p = applyArticulationPlayback(base, values({ { ArtParam::MarcatoLoudness, 30 } }));
    EXPECT_GT(peak(play(p, ArticulationType::Marcato)), museScoreAccent);
    EXPECT_EQ(peak(play(p, ArticulationType::Accent)), museScoreAccent);
}

TEST(Jubelle_ArticulationPlayback, FallsAndDoits)
{
    const ArticulationsProfilePtr base = museScoreWindsProfile();
    const pitch_level_t museScoreDrop = lowestPitchOffset(play(base, ArticulationType::Fall));
    EXPECT_EQ(museScoreDrop, -10 * PITCH_LEVEL_STEP);   // MuseScore's fall: ten semitones

    const ArticulationsProfilePtr p = applyArticulationPlayback(base, values({ { ArtParam::FallRange, 8 },
                                                                                { ArtParam::FallStart, 70 } }));
    const muse::mpe::NoteEvent fall = play(p, ArticulationType::Fall);
    EXPECT_EQ(lowestPitchOffset(fall), -8 * PITCH_LEVEL_STEP);
    for (const auto& [position, offset] : fall.pitchCtx().pitchCurve) {
        if (position <= 70 * ONE_PERCENT) {
            EXPECT_EQ(offset, 0) << "the fall must not start before 70% of the note";
        }
    }

    const muse::mpe::NoteEvent doit = play(p, ArticulationType::Doit);
    pitch_level_t highest = 0;
    for (const auto& pt : doit.pitchCtx().pitchCurve) {
        highest = std::max(highest, pt.second);
    }
    EXPECT_EQ(highest, 8 * PITCH_LEVEL_STEP);

    // Curve: "speeds up" is still near the note halfway through the fall,
    // "slows down" has already dropped further
    auto at = [](const muse::mpe::NoteEvent& e, int percent) { return e.pitchCtx().pitchCurve.at(percent * ONE_PERCENT); };
    const muse::mpe::NoteEvent accelerating = play(applyArticulationPlayback(base, values({ { ArtParam::FallCurve, 1 } })), ArticulationType::Fall);
    const muse::mpe::NoteEvent decelerating = play(applyArticulationPlayback(base, values({ { ArtParam::FallCurve, -1 } })), ArticulationType::Fall);
    EXPECT_GT(at(accelerating, 80), at(play(base, ArticulationType::Fall), 80));
    EXPECT_LT(at(decelerating, 80), at(play(base, ArticulationType::Fall), 80));
}

TEST(Jubelle_ArticulationPlayback, ScoopsStartBeforeTheNote)
{
    const ArticulationsProfilePtr base = museScoreWindsProfile();
    const ArticulationsProfilePtr p = applyArticulationPlayback(base, values({ { ArtParam::ScoopLength, 20 },
                                                                                { ArtParam::ScoopRange, 3 } }));
    const muse::mpe::NoteEvent scoop = play(p, ArticulationType::Scoop);
    EXPECT_EQ(scoop.arrangementCtx().actualTimestamp, NOTE_START - NOTE_LENGTH * 20 / 100);
    EXPECT_EQ(scoop.arrangementCtx().actualDuration, NOTE_LENGTH * 119 / 100);
    EXPECT_EQ(scoop.pitchCtx().pitchCurve.begin()->second, -3 * PITCH_LEVEL_STEP);
    EXPECT_EQ(scoop.pitchCtx().pitchCurve.rbegin()->second, 0);   // ends on the written pitch
}

TEST(Jubelle_ArticulationPlayback, GlissandoStartAndCurve)
{
    const ArticulationsProfilePtr base = museScoreWindsProfile();
    const ArticulationsProfilePtr p = applyArticulationPlayback(base, values({ { ArtParam::GlissandoStart, 40 } }));
    const ArticulationPattern& pattern = p->pattern(ArticulationType::ContinuousGlissando);
    const PitchCurve& curve = pattern.begin()->second.pitchPattern.pitchOffsetMap;
    EXPECT_EQ(curve.at(40 * ONE_PERCENT), 0);         // still on the first note
    EXPECT_EQ(curve.at(100 * ONE_PERCENT), 100);      // arrives on the second note
    EXPECT_EQ(curve.at(70 * ONE_PERCENT), 50);        // halfway
}

TEST(Jubelle_ArticulationPlayback, SettingsTextRoundTripAndLimits)
{
    ArticulationPlaybackSettings s;
    s.scoreValues.set(ArtParam::StaccatoLength, 40);
    s.scoreValues.set(ArtParam::TurnSpeed, 999);       // above the limit
    s.partValues[123456789012ULL].set(ArtParam::StaccatoLength, 30);
    EXPECT_EQ(s.scoreValues.get(ArtParam::TurnSpeed), 200);

    const ArticulationPlaybackSettings back = ArticulationPlaybackSettings::fromText(s.toText());
    EXPECT_EQ(back, s);

    // Part override falls back to the score values for anything it doesn't set
    EXPECT_EQ(back.effective(123456789012ULL).get(ArtParam::StaccatoLength), 30);
    EXPECT_EQ(back.effective(123456789012ULL).get(ArtParam::TurnSpeed), 200);
    EXPECT_EQ(back.effective(1).get(ArtParam::StaccatoLength), 40);

    // Unknown or damaged text is ignored safely
    EXPECT_TRUE(ArticulationPlaybackSettings::fromText(u"something else\nscore staccato=10").isEmpty());
    const auto damaged = ArticulationPlaybackSettings::fromText(u"jubelle-articulation-playback 1\nscore staccato=abc tenuto=80 =5 x");
    EXPECT_FALSE(damaged.scoreValues.get(ArtParam::StaccatoLength).has_value());
    EXPECT_EQ(damaged.scoreValues.get(ArtParam::TenutoLength), 80);

    EXPECT_TRUE(ArticulationPlaybackSettings().toText().isEmpty());
}

TEST(Jubelle_ArticulationPlayback, StoredInTheScoreUndoableAndSaved)
{
    MasterScore* score = ScoreRW::readScore(u"jubelle_data/articulation_trumpet.mscx");
    ASSERT_TRUE(score);
    const uint64_t partId = score->parts().front()->id().toUint64();

    ArticulationPlaybackSettings s;
    s.scoreValues.set(ArtParam::AccentLoudness, 20);
    s.partValues[partId].set(ArtParam::StaccatoLength, 35);
    s.partValues[partId].set(ArtParam::TurnSpeed, 150);

    score->startCmd(muse::TranslatableString::untranslatable("articulation playback"));
    s.write(score);
    score->endCmd();
    EXPECT_EQ(ArticulationPlaybackSettings::read(score), s);
    EXPECT_DOUBLE_EQ(turnSpeedFactor(score, partId), 1.5);
    EXPECT_DOUBLE_EQ(turnSpeedFactor(score, partId + 1), 1.0);

    // Saved with the score and read back by MuseScore's own file reader
    const muse::String saved = u"jubelle_articulation_playback.mscx";
    ASSERT_TRUE(ScoreRW::saveScore(score, saved));
    MasterScore* reopened = ScoreRW::readScore(saved, true);
    ASSERT_TRUE(reopened);
    EXPECT_EQ(ArticulationPlaybackSettings::read(reopened), s);
    delete reopened;

    // Undo removes it, redo restores it
    score->undoRedo(true, nullptr);
    EXPECT_TRUE(ArticulationPlaybackSettings::read(score).isEmpty());
    EXPECT_TRUE(score->metaTag(muse::String(ArticulationPlaybackSettings::META_TAG)).isEmpty());
    EXPECT_DOUBLE_EQ(turnSpeedFactor(score, partId), 1.0);
    score->undoRedo(false, nullptr);
    EXPECT_EQ(ArticulationPlaybackSettings::read(score), s);

    // Clearing all settings removes the tag entirely
    score->startCmd(muse::TranslatableString::untranslatable("articulation playback"));
    ArticulationPlaybackSettings().write(score);
    score->endCmd();
    EXPECT_TRUE(score->masterScore()->metaTags().find(muse::String(ArticulationPlaybackSettings::META_TAG))
                == score->masterScore()->metaTags().end());
    delete score;
}

// ---------------------------------------------------------------------------
// End to end: a real score played by MuseScore's own playback model
// ---------------------------------------------------------------------------

#include <gmock/gmock.h>

#include "engraving/dom/articulation.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/measure.h"
#include "engraving/playback/playbackmodel.h"
#include "mpe/tests/mocks/articulationprofilesrepositorymock.h"

namespace {
using namespace mu::engraving;
using namespace muse::mpe;

struct PlayedScore {
    MasterScore* score = nullptr;
    Part* part = nullptr;
    Chord* staccatoChord = nullptr;   // bar 1, whole note C E G, staccato
    Chord* turnChord = nullptr;       // bar 2, whole note E G C, jazz turn
};

Chord* firstChord(Measure* m)
{
    for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
        if (s->element(0) && s->element(0)->isChord()) {
            return toChord(s->element(0));
        }
    }
    return nullptr;
}

void addArticulation(Score* score, Chord* chord, SymId id)
{
    Articulation* a = Factory::createArticulation(chord);
    a->setSymId(id);
    a->setParent(chord);
    a->setTrack(chord->track());
    score->undoAddElement(a);
}

PlayedScore loadPlayedScore()
{
    PlayedScore p;
    p.score = ScoreRW::readScore(u"jubelle_data/articulation_trumpet.mscx");
    if (!p.score) {
        return p;
    }
    p.part = p.score->parts().front();
    p.staccatoChord = firstChord(p.score->firstMeasure());
    p.turnChord = firstChord(p.score->firstMeasure()->nextMeasure());
    p.score->startCmd(muse::TranslatableString::untranslatable("test"));
    addArticulation(p.score, p.staccatoChord, SymId::articStaccatoAbove);
    addArticulation(p.score, p.turnChord, SymId::brassJazzTurn);
    p.score->endCmd();
    return p;
}

ArticulationsProfilePtr playbackProfile()
{
    const std::vector<int> flat(11, 0);
    const std::vector<int> dyn(11, 5000);
    auto profile = std::make_shared<ArticulationsProfile>();
    profile->setPattern(ArticulationType::Standard, makePattern(10000, 0, dyn, flat));
    profile->setPattern(ArticulationType::Staccato, makePattern(5000, 0, dyn, flat));
    profile->setPattern(ArticulationType::Turn, makePattern(10000, 0, dyn, flat));
    return profile;
}

std::vector<muse::mpe::NoteEvent> notesAt(const PlaybackEventsMap& events, timestamp_t from, timestamp_t to)
{
    std::vector<muse::mpe::NoteEvent> result;
    for (const auto& [ts, list] : events) {
        for (const PlaybackEvent& e : list) {
            if (const muse::mpe::NoteEvent* n = std::get_if<muse::mpe::NoteEvent>(&e)) {
                const timestamp_t t = n->arrangementCtx().actualTimestamp;
                if (t >= from && t < to) {
                    result.push_back(*n);
                }
            }
        }
    }
    return result;
}

void writeSettings(Score* score, const ArticulationPlaybackSettings& s)
{
    score->startCmd(muse::TranslatableString::untranslatable("articulation playback"));
    s.write(score);
    score->endCmd();
}

constexpr duration_t WHOLE_NOTE = 2000000;   // 4/4 at 120 bpm
}

TEST(Jubelle_ArticulationPlayback, RealScoreStaccatoTurnUndo)
{
    PlayedScore p = loadPlayedScore();
    ASSERT_TRUE(p.score && p.part && p.staccatoChord && p.turnChord);

    auto repository = std::make_shared<::testing::NiceMock<ArticulationProfilesRepositoryMock> >();
    ON_CALL(*repository, defaultProfile(::testing::_)).WillByDefault(::testing::Return(playbackProfile()));

    PlaybackModel model(muse::modularity::globalCtx());
    model.profilesRepository.set(repository);
    model.load(p.score);
    const PlaybackEventsMap& events = model.resolveTrackPlaybackData(p.part->id(), p.part->instrumentId()).originEvents;

    auto staccatoLengths = [&]() {
        std::vector<duration_t> lengths;
        for (const muse::mpe::NoteEvent& n : notesAt(events, 0, WHOLE_NOTE)) {
            lengths.push_back(n.arrangementCtx().actualDuration);
        }
        return lengths;
    };
    auto turnNotes = [&]() { return notesAt(events, WHOLE_NOTE, 2 * WHOLE_NOTE); };

    // MuseScore's own playback first: staccato at half length
    ASSERT_EQ(staccatoLengths().size(), 3u);
    for (duration_t d : staccatoLengths()) {
        EXPECT_EQ(d, WHOLE_NOTE / 2);
    }
    const std::vector<muse::mpe::NoteEvent> normalTurn = turnNotes();
    ASSERT_GT(normalTurn.size(), 3u) << "the jazz turn plays as a real turn";
    duration_t firstTurnNote = 0;
    for (const muse::mpe::NoteEvent& n : normalTurn) {
        if (n.arrangementCtx().actualTimestamp == WHOLE_NOTE) {
            firstTurnNote = n.arrangementCtx().actualDuration;
        }
    }
    ASSERT_GT(firstTurnNote, 0);

    // Settings: shorter staccato for the whole score, a faster turn
    ArticulationPlaybackSettings s;
    s.scoreValues.set(ArtParam::StaccatoLength, 30);
    s.scoreValues.set(ArtParam::TurnSpeed, 200);
    writeSettings(p.score, s);   // playback re-renders by itself

    for (duration_t d : staccatoLengths()) {
        EXPECT_EQ(d, WHOLE_NOTE * 30 / 100);
    }
    duration_t fastTurnNote = 0;
    for (const muse::mpe::NoteEvent& n : turnNotes()) {
        if (n.arrangementCtx().actualTimestamp == WHOLE_NOTE) {
            fastTurnNote = n.arrangementCtx().actualDuration;
        }
    }
    EXPECT_NEAR(double(fastTurnNote), firstTurnNote / 2.0, firstTurnNote * 0.02) << "twice as fast";

    // This instrument's own value wins over the whole-score value
    s.partValues[p.part->id().toUint64()].set(ArtParam::StaccatoLength, 20);
    writeSettings(p.score, s);
    for (duration_t d : staccatoLengths()) {
        EXPECT_EQ(d, WHOLE_NOTE * 20 / 100);
    }

    // Undo twice: back to MuseScore's own playback
    p.score->undoRedo(true, nullptr);
    for (duration_t d : staccatoLengths()) {
        EXPECT_EQ(d, WHOLE_NOTE * 30 / 100);
    }
    p.score->undoRedo(true, nullptr);
    for (duration_t d : staccatoLengths()) {
        EXPECT_EQ(d, WHOLE_NOTE / 2);
    }

    // The written music never changed
    EXPECT_EQ(p.staccatoChord->notes().size(), 3u);
    EXPECT_EQ(p.staccatoChord->ticks(), Fraction(1, 1));
    EXPECT_EQ(p.staccatoChord->articulations().size(), 1u);
    EXPECT_EQ(p.turnChord->articulations().size(), 1u);
    delete p.score;
}
