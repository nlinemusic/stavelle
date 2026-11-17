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

//! Jubelle Phase 5B: rhythmic feels. swing_eighths.mscx: Trumpet and Alto Sax
//! (with their instrument ids, so Swing v2 knows they are horns),
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

    // Basie, Swing v2 (R5, 15 Nov 2026): the drummers' curve, 3:1 at 80 bpm and
    // slower to 2:1 at 200, with the short note never under 100 ms
    const SwingFeel& basie = builtIn(u"basie");
    EXPECT_TRUE(basie.tempoDependent);
    EXPECT_TRUE(basie.roleAware);
    EXPECT_TRUE(basie.hornArticulation);
    EXPECT_DOUBLE_EQ(basie.playedRatioAt(60), 3.0);
    EXPECT_DOUBLE_EQ(basie.playedRatioAt(80), 3.0);
    EXPECT_NEAR(basie.playedRatioAt(120), 8.0 / 3.0, 1e-9);
    EXPECT_NEAR(basie.playedRatioAt(160), 7.0 / 3.0, 1e-9);
    EXPECT_NEAR(basie.playedRatioAt(200), 2.0, 1e-9);
    EXPECT_NEAR(basie.playedRatioAt(240), 1.5, 1e-9);   // 250 ms beat: short note 100 ms
    EXPECT_NEAR(basie.playedRatioAt(300), 1.0, 1e-9);   // 200 ms beat: straight
    // offbeats are no longer softer or shorter (that was backwards for big-band phrasing)
    EXPECT_EQ(basie.offbeatLoudness, 100);
    EXPECT_EQ(basie.offbeatLength, 100);
    // horns behind on the beat; rhythm section behind when slow, together, ahead when fast
    EXPECT_DOUBLE_EQ(basie.hornDelayAt(80), 25.0);
    EXPECT_DOUBLE_EQ(basie.hornDelayAt(200), 12.0);
    EXPECT_DOUBLE_EQ(basie.rhythmPlacementAt(90), 10.0);
    EXPECT_DOUBLE_EQ(basie.rhythmPlacementAt(150), 0.0);
    EXPECT_DOUBLE_EQ(basie.rhythmPlacementAt(240), -8.0);
    EXPECT_EQ(basie.guitarQuarterLength, 55);

    // The four swing presets are clearly apart at a real big-band tempo:
    // at 160 bpm neighbours' offbeats land at least 12 ms apart (they were 10-25 ms before)
    auto offbeatMs = [](const SwingFeel& f, double bpm) {
        const double beat = 60000.0 / bpm;
        const double r = f.playedRatioAt(bpm);
        return beat * r / (r + 1.0);
    };
    const double light = offbeatMs(builtIn(u"light"), 160), medium = offbeatMs(builtIn(u"medium"), 160);
    const double basie160 = offbeatMs(basie, 160), heavy = offbeatMs(builtIn(u"heavy"), 160);
    EXPECT_GT(medium - light, 25.0);
    EXPECT_GT(basie160 - medium, 12.0);
    EXPECT_GT(heavy - basie160, 12.0);
    // Heavy keeps its unevenness at speed
    EXPECT_GT(builtIn(u"heavy").playedRatioAt(240), 2.5);
    // Older feels don't use the v2 values
    for (const char16_t* id : { u"straight", u"light", u"medium", u"heavy", u"fixed21", u"fixed31" }) {
        EXPECT_FALSE(builtIn(id).roleAware);
        EXPECT_EQ(builtIn(id).minShortMs, 0);
    }

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
    custom.roleAware = true;
    custom.hornDelayLowMs = 30;
    custom.rhythmFastMs = -5;
    custom.guitarQuarterLength = 70;
    custom.minShortMs = 90;
    custom.hornArticulation = true;
    s.setFeel(custom);
    EXPECT_EQ(custom.id, u"custom1");
    EXPECT_EQ(s.newCustomId(), u"custom2");

    const SwingSettings back = SwingSettings::fromText(s.toText());
    EXPECT_EQ(back, s);
    EXPECT_EQ(back.feel(u"custom1")->name, u"Count's 16ths = 100%");
    EXPECT_EQ(back.feel(u"custom1")->unit, 16);
    EXPECT_DOUBLE_EQ(back.feel(u"custom1")->ratio, 1.75);
    EXPECT_TRUE(back.feel(u"custom1")->roleAware);
    EXPECT_DOUBLE_EQ(back.feel(u"custom1")->hornDelayLowMs, 30.0);
    EXPECT_DOUBLE_EQ(back.feel(u"custom1")->rhythmFastMs, -5.0);
    EXPECT_EQ(back.feel(u"custom1")->guitarQuarterLength, 70);
    EXPECT_EQ(back.feel(u"custom1")->minShortMs, 90);
    EXPECT_TRUE(back.feel(u"custom1")->hornArticulation);
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

TEST_F(Jubelle_Swing, BasieOnTheTrumpet)
{
    SwingSettings s;
    s.scoreFeel = u"basie";
    write(s);

    // 120 bpm: ratio 3.0 - (120-80)/(200-80) = 8/3 : 1. The trumpet is a horn:
    // its offbeats with the drums, its on-beat notes about 20.7 ms behind.
    const double ratio = 8.0 / 3.0;
    const double delay = (25.0 + (120.0 - 80.0) / (200.0 - 80.0) * (12.0 - 25.0)) * 1000.0;   // µs
    std::vector<Played> notes = played(trumpet);
    ASSERT_EQ(notes.size(), 32u);
    const double longPart = offbeatStart(ratio);
    EXPECT_NEAR(notes[0].start, delay, 2.0);
    EXPECT_NEAR(notes[0].length, longPart - delay, 2.0);               // ends where it would have
    EXPECT_NEAR(notes[1].start, longPart, 2.0);                        // offbeat: with the drums
    EXPECT_NEAR(notes[1].length, 2 * EIGHTH - longPart, 2.0);           // full length
    EXPECT_EQ(notes[1].dynamic, notes[0].dynamic);                     // not softer
    EXPECT_NEAR(notes[2].start, 2 * EIGHTH + delay, 2.0);
    EXPECT_NEAR(notes[8].start, BAR + delay, 2.0);

    // The last eighth, before the end (a rest), is clipped and accented
    EXPECT_NEAR(notes[31].length, (2 * EIGHTH - longPart) * 0.5, 2.0);
    EXPECT_GT(notes[31].dynamic, notes[30].dynamic);

    // A tempo change to 240 bpm in bar 3: the short note stays at 100 ms (1.5:1)
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
    const double fastBeat = EIGHTH;   // a quarter at 240 bpm = 250 ms
    const double bar3Beat = notes[16].start - 12000.0;   // on-beat horn delay at 240 bpm: 12 ms
    EXPECT_NEAR(notes[17].start - bar3Beat, fastBeat * 1.5 / 2.5, 2.0);
    EXPECT_NEAR(fastBeat - (notes[17].start - bar3Beat), 100000.0, 2.0);   // the short note: 100 ms
}

//! Swing v2 at five tempos, by role: what each instrument does with the same
//! two eighth notes (a beat and its offbeat)
TEST_F(Jubelle_Swing, BasieByRoleAtFiveTempos)
{
    const SwingFeel& basie = builtIn(u"basie");
    Segment* seg = score->firstMeasure()->first(SegmentType::ChordRest);
    const Chord* beat = toChord(seg->element(0));
    const Chord* offbeat = toChord(seg->next(SegmentType::ChordRest)->element(0));
    ASSERT_TRUE(beat && offbeat);

    struct Expected {
        double bpm, ratio, hornDelayMs, rhythmMs;
    };
    const Expected table[] = {
        { 80, 3.0, 25.0, 10.0 },
        { 120, 8.0 / 3.0, 25.0 - 13.0 / 3.0, 0.0 },
        { 160, 7.0 / 3.0, 25.0 - 26.0 / 3.0, 0.0 },
        { 200, 2.0, 12.0, -4.0 },
        { 240, 1.5, 12.0, -8.0 },
    };
    for (const Expected& e : table) {
        SCOPED_TRACE(e.bpm);
        const double eighthMs = 30000.0 / e.bpm;
        const double swungShift = 2.0 * e.ratio / (e.ratio + 1.0) - 1.0;   // offbeat delay, in eighths

        // Drums: the curve itself, plus the rhythm section's placement
        const SwingTiming dBeat = swingTimingForFeel(beat, basie, e.bpm, SwingRole::Drums);
        const SwingTiming dOff = swingTimingForFeel(offbeat, basie, e.bpm, SwingRole::Drums);
        EXPECT_NEAR(dBeat.startShift * eighthMs, e.rhythmMs, 1e-6);
        EXPECT_NEAR(dOff.startShift, swungShift + e.rhythmMs / eighthMs, 1e-9);
        EXPECT_TRUE(dOff.offbeat);

        // Horns: offbeats exactly with the drums' curve; on the beat, behind
        const SwingTiming hBeat = swingTimingForFeel(beat, basie, e.bpm, SwingRole::Horn);
        const SwingTiming hOff = swingTimingForFeel(offbeat, basie, e.bpm, SwingRole::Horn);
        EXPECT_NEAR(hBeat.startShift * eighthMs, e.hornDelayMs, 1e-6);
        EXPECT_NEAR(hOff.startShift, swungShift, 1e-9);
        EXPECT_DOUBLE_EQ(hOff.loudnessFactor, 1.0);   // an ordinary offbeat eighth: not accented

        // Bass and guitar follow the rhythm section's placement; eighths keep their length
        const SwingTiming g = swingTimingForFeel(beat, basie, e.bpm, SwingRole::Guitar);
        EXPECT_NEAR(g.startShift * eighthMs, e.rhythmMs, 1e-6);
        EXPECT_NEAR(g.durationFactor, dBeat.durationFactor, 1e-9);
        EXPECT_NEAR(swingTimingForFeel(beat, basie, e.bpm, SwingRole::Bass).startShift, dBeat.startShift, 1e-12);
    }

    // A feel that isn't role-aware treats every role the same
    const SwingFeel& medium = builtIn(u"medium");
    for (SwingRole r : { SwingRole::Drums, SwingRole::Guitar, SwingRole::Bass, SwingRole::Keyboard }) {
        EXPECT_DOUBLE_EQ(swingTimingForFeel(offbeat, medium, 160, r).startShift,
                         swingTimingForFeel(offbeat, medium, 160, SwingRole::Horn).startShift);
        EXPECT_DOUBLE_EQ(swingTimingForFeel(beat, medium, 160, r).startShift, 0.0);
    }
}

TEST_F(Jubelle_Swing, BasieQuartersShortForHornsAndGuitar)
{
    // Make beat 1 of bar 1 a quarter note (beats 2-4 stay eighths)
    Segment* seg = score->firstMeasure()->first(SegmentType::ChordRest);
    Chord* first = toChord(seg->element(0));
    score->startCmd(muse::TranslatableString::untranslatable("quarter"));
    score->changeCRlen(first, Fraction(1, 4));
    score->endCmd();
    const Chord* quarter = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
    ASSERT_EQ(quarter->actualTicks(), Fraction(1, 4));

    const SwingFeel& basie = builtIn(u"basie");
    const double plain = swingTimingForFeel(quarter, builtIn(u"medium"), 160, SwingRole::Horn).durationFactor;
    const SwingTiming horn = swingTimingForFeel(quarter, basie, 160, SwingRole::Horn);
    const SwingTiming guitar = swingTimingForFeel(quarter, basie, 160, SwingRole::Guitar);
    const SwingTiming bass = swingTimingForFeel(quarter, basie, 160, SwingRole::Bass);
    const double quarterMs = 60000.0 / 160;
    const double hornDelay = basie.hornDelayAt(160) / quarterMs;
    EXPECT_NEAR(horn.durationFactor, (plain - hornDelay) * 0.65, 1e-9);   // "dit"
    EXPECT_NEAR(guitar.durationFactor, plain * 0.55, 1e-9);             // Green's short, light four
    EXPECT_NEAR(bass.durationFactor, plain, 1e-9);                      // walking bass: full length

    // A marked articulation is left to MuseScore's own playback of it
    const SwingFeel& noPhrasing = [&]() -> const SwingFeel& {
        static SwingFeel f = basie;
        f.hornArticulation = false;
        return f;
    }();
    EXPECT_NEAR(swingTimingForFeel(quarter, noPhrasing, 160, SwingRole::Horn).durationFactor, plain - hornDelay, 1e-9);
}

TEST(Jubelle_SwingFeel, RolesFromInstruments)
{
    EXPECT_EQ(swingRoleFor(u"drum.group.set", true), SwingRole::Drums);
    EXPECT_EQ(swingRoleFor(u"metal.cymbal.ride", false), SwingRole::Drums);
    EXPECT_EQ(swingRoleFor(u"pluck.guitar.electric", false), SwingRole::Guitar);
    EXPECT_EQ(swingRoleFor(u"pluck.bass.acoustic", false), SwingRole::Bass);
    EXPECT_EQ(swingRoleFor(u"strings.contrabass", false), SwingRole::Bass);
    EXPECT_EQ(swingRoleFor(u"keyboard.piano", false), SwingRole::Keyboard);
    EXPECT_EQ(swingRoleFor(u"pitched-percussion.vibraphone", false), SwingRole::Keyboard);
    EXPECT_EQ(swingRoleFor(u"brass.trombone.bass", false), SwingRole::Horn);   // not the bass section
    EXPECT_EQ(swingRoleFor(u"wind.reed.clarinet.bass", false), SwingRole::Horn);
    EXPECT_EQ(swingRoleFor(u"wind.reed.saxophone.alto", false), SwingRole::Horn);
    EXPECT_EQ(swingRoleFor(u"voice.alto", false), SwingRole::Horn);
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

    // Heavy at 120 bpm (Swing v2): 3.0 + (120-80)/(240-80) * (2.6-3.0) = 2.9 : 1
    const std::vector<Played> notes = played(trumpet);
    EXPECT_NEAR(notes[1].start, offbeatStart(2.0), 2.0);                 // bar 1: still the score's 2:1
    EXPECT_NEAR(notes[9].start - BAR, offbeatStart(2.9), 2.0);         // bar 2 on: Heavy
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

//! R0 (15 Nov 2026): an ordinary MuseScore "Swing" text no longer replaces the
//! feel chosen in the Swing dialog; it switches swing on with that feel
TEST_F(Jubelle_Swing, OrdinarySwingTextUsesTheChosenFeel)
{
    auto addText = [&](const char16_t* xml, int unit, int ratio) {
        Segment* seg = score->firstMeasure()->first(SegmentType::ChordRest);
        score->startCmd(muse::TranslatableString::untranslatable("swing text"));
        StaffText* text = Factory::createStaffText(seg);
        text->setParent(seg);
        text->setTrack(0);
        text->setXmlText(xml);
        text->setSwing(true);
        text->setSwingParameters(unit, ratio);
        score->undoAddElement(text);
        score->updateSwing();
        score->endCmd();
    };
    // MuseScore's palette "Swing" text: eighths at 60%
    addText(u"Swing", Constants::DIVISION / 2, 60);

    // Nothing chosen: MuseScore's own swing from the text, as before
    const double museScore60 = 2 * EIGHTH * 0.60;   // the offbeat starts at 60% of the beat
    EXPECT_NEAR(played(trumpet)[1].start, museScore60, 2.0);

    // A feel chosen in the dialog: the text no longer overrides it
    SwingSettings s;
    s.scoreFeel = u"fixed21";
    write(s);
    EXPECT_NEAR(played(trumpet)[1].start, offbeatStart(2.0), 2.0);

    // Different feels sound different under the same text
    s.scoreFeel = u"heavy";
    write(s);
    EXPECT_NEAR(played(trumpet)[1].start, offbeatStart(2.9), 2.0);
}

//! Jubelle Phase 5D, on MuseScore's real playback model: switching a repeat
//! off changes what is played straight away, and undo brings it back.
TEST(Jubelle_RepeatsPlayback, PlayThisRepeatReachesPlayback)
{
    MasterScore* score = ScoreRW::readScore(u"repeat_data/repeat01.mscx");   // bars 1;2;3; 2;3;4;5;6
    ASSERT_TRUE(score);
    auto repository = std::make_shared<::testing::NiceMock<muse::mpe::ArticulationProfilesRepositoryMock> >();
    ON_CALL(*repository, defaultProfile(::testing::_)).WillByDefault(::testing::Return(flatProfile()));
    PlaybackModel model(muse::modularity::globalCtx());
    model.profilesRepository.set(repository);
    model.load(score);

    const Part* part = score->parts().front();
    auto noteCount = [&]() {
        size_t n = 0;
        for (const auto& [ts, list] : model.resolveTrackPlaybackData(part->id(), part->instrumentId()).originEvents) {
            for (const muse::mpe::PlaybackEvent& e : list) {
                n += std::holds_alternative<muse::mpe::NoteEvent>(e) ? 1 : 0;
            }
        }
        return n;
    };
    const size_t withRepeat = noteCount();
    ASSERT_GT(withRepeat, 0u);

    Measure* end = nullptr;
    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        if (m->repeatEnd()) {
            end = m;
        }
    }
    ASSERT_TRUE(end);
    size_t notesInBars2and3 = 0;
    for (Segment* s = score->firstMeasure()->nextMeasure()->first(SegmentType::ChordRest); s && s->measure() != end->nextMeasure();
         s = s->next1(SegmentType::ChordRest)) {
        if (s->element(0) && s->element(0)->isChord()) {
            notesInBars2and3 += toChord(s->element(0))->notes().size();
        }
    }

    score->startCmd(muse::TranslatableString::untranslatable("play repeat"));
    end->undoChangeProperty(Pid::JUBELLE_PLAY_REPEAT, false);
    score->endCmd();
    EXPECT_EQ(noteCount(), withRepeat - notesInBars2and3);

    score->undoRedo(true, nullptr);
    EXPECT_EQ(noteCount(), withRepeat);
    delete score;
}
