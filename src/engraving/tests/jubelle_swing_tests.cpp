/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "engraving/dom/chord.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/stafftext.h"
#include "engraving/dom/tempotext.h"
#include "engraving/playback/jubelle/swingfeel.h"
#include "engraving/playback/playbackmodel.h"

#include "mpe/tests/mocks/articulationprofilesrepositorymock.h"

#include "utils/scorerw.h"

using namespace mu::engraving;
using namespace mu::engraving::jubelle;

//! Jubelle Phase 5B: rhythmic feels. swing_eighths.mscx: Trumpet and Alto Sax,
//! 4 bars of eighth notes in 4/4 at 120 bpm (a quarter = 500 000 µs).
namespace {
constexpr double EIGHTH = 250000.0;   // µs at 120 bpm
constexpr double BAR = 2000000.0;

const SwingFeel& builtIn(const char16_t* id)
{
    for (const SwingFeel& f : builtInFeels()) {
        if (f.id == id) {
            return f;
        }
    }
    static SwingFeel none;
    ADD_FAILURE() << "no built-in feel";
    return none;
}

muse::mpe::ArticulationsProfilePtr flatProfile()
{
    using namespace muse::mpe;
    ArticulationPatternSegment segment;
    segment.arrangementPattern = ArrangementPattern(HUNDRED_PERCENT, 0);
    for (int i = 0; i <= 10; ++i) {
        segment.expressionPattern.dynamicOffsetMap.emplace(i * 10 * ONE_PERCENT, dynamicLevelFromType(muse::mpe::DynamicType::Natural));
        segment.pitchPattern.pitchOffsetMap.emplace(i * 10 * ONE_PERCENT, 0);
    }
    ArticulationPattern pattern;
    pattern.emplace(0, segment);
    auto profile = std::make_shared<ArticulationsProfile>();
    profile->setPattern(ArticulationType::Standard, pattern);
    return profile;
}

struct Played {
    double start;
    double length;
    int dynamic;
};

class Jubelle_Swing : public ::testing::Test
{
protected:
    void SetUp() override
    {
        score = ScoreRW::readScore(u"jubelle_data/swing_eighths.mscx");
        ASSERT_TRUE(score);
        ASSERT_EQ(score->parts().size(), 2u);
        trumpet = score->parts()[0];
        sax = score->parts()[1];
        repository = std::make_shared<::testing::NiceMock<muse::mpe::ArticulationProfilesRepositoryMock> >();
        ON_CALL(*repository, defaultProfile(::testing::_)).WillByDefault(::testing::Return(flatProfile()));
        model = std::make_unique<PlaybackModel>(muse::modularity::globalCtx());
        model->profilesRepository.set(repository);
        model->load(score);
    }

    void TearDown() override
    {
        model.reset();
        delete score;
    }

    //! The notes one instrument plays, in order
    std::vector<Played> played(const Part* part)
    {
        std::vector<Played> out;
        const auto& events = model->resolveTrackPlaybackData(part->id(), part->instrumentId()).originEvents;
        for (const auto& [ts, list] : events) {
            for (const muse::mpe::PlaybackEvent& e : list) {
                if (const auto* n = std::get_if<muse::mpe::NoteEvent>(&e)) {
                    out.push_back({ double(n->arrangementCtx().actualTimestamp), double(n->arrangementCtx().actualDuration),
                                    int(n->expressionCtx().nominalDynamicLevel) });
                }
            }
        }
        std::sort(out.begin(), out.end(), [](const Played& a, const Played& b) { return a.start < b.start; });
        return out;
    }

    void write(const SwingSettings& s)
    {
        score->startCmd(muse::TranslatableString::untranslatable("swing"));
        s.write(score);
        score->endCmd();
    }

    //! Expected offbeat start for a long:short ratio at 120 bpm
    static double offbeatStart(double ratio) { return EIGHTH * 2 * ratio / (ratio + 1); }

    MasterScore* score = nullptr;
    Part* trumpet = nullptr;
    Part* sax = nullptr;
    std::shared_ptr<::testing::NiceMock<muse::mpe::ArticulationProfilesRepositoryMock> > repository;
    std::unique_ptr<PlaybackModel> model;
};
}

TEST(Jubelle_SwingFeel, BuiltInFeelsAndTempo)
{
    const std::vector<SwingFeel>& feels = builtInFeels();
    std::vector<std::string> ids;
    for (const SwingFeel& f : feels) {
        ids.push_back(f.id.toStdString());
    }
    EXPECT_EQ(ids, (std::vector<std::string> { "straight", "light", "medium", "heavy", "fixed21", "fixed31", "basie" }));

    // Basie, as approved: 2.2:1 at 100 bpm and slower, 1.6:1 at 220 and faster
    const SwingFeel& basie = builtIn(u"basie");
    EXPECT_TRUE(basie.tempoDependent);
    EXPECT_DOUBLE_EQ(basie.ratioAt(60), 2.2);
    EXPECT_DOUBLE_EQ(basie.ratioAt(100), 2.2);
    EXPECT_NEAR(basie.ratioAt(160), 1.9, 1e-9);
    EXPECT_DOUBLE_EQ(basie.ratioAt(220), 1.6);
    EXPECT_DOUBLE_EQ(basie.ratioAt(300), 1.6);
    EXPECT_EQ(basie.offbeatLoudness, 92);
    EXPECT_EQ(basie.offbeatLength, 90);

    // Fixed feels don't depend on tempo; more swing when slow for the others
    EXPECT_DOUBLE_EQ(builtIn(u"fixed21").ratioAt(60), builtIn(u"fixed21").ratioAt(300));
    for (const char16_t* id : { u"light", u"medium", u"heavy" }) {
        EXPECT_GT(builtIn(id).ratioAt(80), builtIn(id).ratioAt(240));
    }
    EXPECT_NEAR(SwingFeel::ratioToPercent(2.0), 66.6667, 1e-3);
    EXPECT_NEAR(SwingFeel::percentToRatio(75.0), 3.0, 1e-9);
}

TEST(Jubelle_SwingFeel, SettingsStoredAndEdited)
{
    SwingSettings s;
    s.scoreFeel = u"basie";
    s.partFeels[42] = u"straight";

    // Editing Basie's amount is stored; putting it back removes the edit
    SwingFeel basie = *s.feel(u"basie");
    basie.lowTempoRatio = 2.5;
    s.setFeel(basie);
    EXPECT_DOUBLE_EQ(s.feel(u"basie")->lowTempoRatio, 2.5);
    s.setFeel(builtIn(u"basie"));
    EXPECT_TRUE(s.feels.empty());
    s.setFeel(basie);

    // A custom feel, with a name that needs escaping
    SwingFeel custom;
    custom.id = s.newCustomId();
    custom.name = u"Count's 16ths = 100%";
    custom.unit = 16;
    custom.ratio = 1.75;
    s.setFeel(custom);
    EXPECT_EQ(custom.id, u"custom1");
    EXPECT_EQ(s.newCustomId(), u"custom2");

    const SwingSettings back = SwingSettings::fromText(s.toText());
    EXPECT_EQ(back, s);
    EXPECT_EQ(back.feel(u"custom1")->name, u"Count's 16ths = 100%");
    EXPECT_EQ(back.feel(u"custom1")->unit, 16);
    EXPECT_DOUBLE_EQ(back.feel(u"custom1")->ratio, 1.75);
    EXPECT_EQ(back.allFeels().size(), builtInFeels().size() + 1);

    // Limits and damaged text
    SwingFeel wild = custom;
    wild.ratio = 10.0;
    wild.offbeatLength = 5;
    s.setFeel(wild);
    EXPECT_DOUBLE_EQ(s.feel(wild.id)->ratio, 4.0);
    EXPECT_EQ(s.feel(wild.id)->offbeatLength, 50);
    EXPECT_TRUE(SwingSettings::fromText(u"not ours\nscore basie").isEmpty());
    EXPECT_EQ(SwingSettings::fromText(u"jubelle-swing 1\npart xyz basie\nscore heavy").scoreFeel, u"heavy");
}

TEST_F(Jubelle_Swing, NothingChosenPlaysAsMuseScore)
{
    const std::vector<Played> notes = played(trumpet);
    ASSERT_EQ(notes.size(), 32u);
    for (size_t i = 0; i < notes.size(); ++i) {
        EXPECT_NEAR(notes[i].start, i * EIGHTH, 1.0);
        EXPECT_NEAR(notes[i].length, EIGHTH, 1.0);
    }
}

TEST_F(Jubelle_Swing, FixedTripletSwingForTheWholeScore)
{
    SwingSettings s;
    s.scoreFeel = u"fixed21";
    write(s);   // playback re-renders by itself

    for (const Part* part : { trumpet, sax }) {
        const std::vector<Played> notes = played(part);
        ASSERT_EQ(notes.size(), 32u);
        // Beat 1: long eighth, then the swung (late, short) eighth
        EXPECT_NEAR(notes[0].start, 0, 1.0);
        EXPECT_NEAR(notes[0].length, EIGHTH * 4 / 3, 2.0);
        EXPECT_NEAR(notes[1].start, offbeatStart(2.0), 2.0);
        EXPECT_NEAR(notes[1].length, EIGHTH * 2 / 3, 2.0);
        // Beats still land on time
        EXPECT_NEAR(notes[2].start, 2 * EIGHTH, 1.0);
        EXPECT_NEAR(notes[8].start, BAR, 1.0);
    }

    // Undo: straight again
    score->undoRedo(true, nullptr);
    EXPECT_NEAR(played(trumpet)[1].start, EIGHTH, 1.0);
}

TEST_F(Jubelle_Swing, BasieDependsOnTempoWithSofterShorterOffbeats)
{
    SwingSettings s;
    s.scoreFeel = u"basie";
    write(s);

    // 120 bpm: 2.2 + (120-100)/(220-100) * (1.6-2.2) = 2.1 : 1
    std::vector<Played> notes = played(trumpet);
    ASSERT_EQ(notes.size(), 32u);
    const double ratio = 2.1;
    const double longPart = offbeatStart(ratio);
    EXPECT_NEAR(notes[1].start, longPart, 2.0);
    EXPECT_NEAR(notes[1].length, (2 * EIGHTH - longPart) * 0.90, 2.0);   // 90% length
    EXPECT_EQ(notes[1].dynamic, int(std::lround(notes[0].dynamic * 0.92)));   // 8% softer
    EXPECT_NEAR(notes[0].length, longPart, 2.0);                           // downbeats unchanged in loudness
    EXPECT_EQ(notes[2].dynamic, notes[0].dynamic);

    // A tempo change to 240 bpm in bar 3: Basie eases to 1.6:1 there
    Measure* bar3 = score->firstMeasure()->nextMeasure()->nextMeasure();
    Segment* seg = bar3->first(SegmentType::ChordRest);
    score->startCmd(muse::TranslatableString::untranslatable("tempo"));
    TempoText* tt = Factory::createTempoText(seg);
    tt->setParent(seg);
    tt->setTrack(0);
    tt->setTempo(BeatsPerSecond(4.0));
    tt->setFollowText(false);
    tt->setXmlText(u"Fast");
    score->undoAddElement(tt);
    score->endCmd();

    notes = played(trumpet);
    ASSERT_EQ(notes.size(), 32u);
    const double fastEighth = EIGHTH / 2;
    const double bar3Start = notes[16].start;
    EXPECT_NEAR(notes[17].start - bar3Start, fastEighth * 2 * 1.6 / 2.6, 2.0);
}

TEST_F(Jubelle_Swing, InstrumentOverrideAndSwingText)
{
    SwingSettings s;
    s.scoreFeel = u"fixed21";
    s.partFeels[sax->id().toUint64()] = u"straight";
    write(s);

    EXPECT_NEAR(played(trumpet)[1].start, offbeatStart(2.0), 2.0);
    EXPECT_NEAR(played(sax)[1].start, EIGHTH, 1.0);   // the sax plays straight

    // A Swing text in bar 2 on the trumpet staff chooses Heavy from there on
    Measure* bar2 = score->firstMeasure()->nextMeasure();
    Segment* seg = bar2->first(SegmentType::ChordRest);
    score->startCmd(muse::TranslatableString::untranslatable("swing text"));
    StaffText* text = Factory::createStaffText(seg);
    text->setParent(seg);
    text->setTrack(0);
    text->setXmlText(u"Heavy swing");
    text->setSwing(true);
    text->setSwingParameters(Constants::DIVISION / 2, 72);
    text->setJubelleSwingFeel(u"heavy");
    score->undoAddElement(text);
    score->updateSwing();
    score->endCmd();

    // Heavy at 120 bpm: 3.0 + (120-80)/(240-80) * (1.5-3.0) = 2.625 : 1
    const std::vector<Played> notes = played(trumpet);
    EXPECT_NEAR(notes[1].start, offbeatStart(2.0), 2.0);                 // bar 1: still the score's 2:1
    EXPECT_NEAR(notes[9].start - BAR, offbeatStart(2.625), 2.0);         // bar 2 on: Heavy
    EXPECT_NEAR(played(sax)[9].start - BAR, EIGHTH, 1.0);                // sax: unaffected, straight

    // Saved with the score: the feel is an extra attribute MuseScore ignores,
    // next to the ratio MuseScore plays
    const muse::String saved = u"jubelle_swing_test.mscx";
    ASSERT_TRUE(ScoreRW::saveScore(score, saved));
    MasterScore* reopened = ScoreRW::readScore(saved, true);
    ASSERT_TRUE(reopened);
    EXPECT_EQ(SwingSettings::read(reopened), s);
    bool found = false;
    for (EngravingItem* a : reopened->firstMeasure()->nextMeasure()->first(SegmentType::ChordRest)->annotations()) {
        if (a->isStaffText()) {
            const SwingParameters& sp = toStaffText(a)->swingParameters();
            EXPECT_EQ(sp.jubelleFeel, u"heavy");
            EXPECT_EQ(sp.swingRatio, 72);
            EXPECT_EQ(sp.swingUnit, Constants::DIVISION / 2);
            found = true;
        }
    }
    EXPECT_TRUE(found);
    delete reopened;

    // The written notes never changed
    for (Segment* sg = score->firstSegment(SegmentType::ChordRest); sg; sg = sg->next1(SegmentType::ChordRest)) {
        for (track_idx_t t : { track_idx_t(0), track_idx_t(4) }) {
            if (EngravingItem* e = sg->element(t)) {
                EXPECT_EQ(toChordRest(e)->ticks(), Fraction(1, 8));
            }
        }
    }
}
