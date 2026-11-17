/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle G6: Live Tempo Recording (tapping), on the real engine.
 */
#include <gtest/gtest.h>

#include <memory>

#include "engraving/dom/chord.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/tempotext.h"
#include "notation/internal/jubelle/tempotap.h"

#include "templatebuilder.h"

using namespace mu::engraving;
namespace jt = mu::notation::jubelle;

namespace {
const std::string ROOT = std::string(MU_ROOT_DIR) + "/";

std::unique_ptr<MasterScore> score()
{
    jubelle_templates::loadInstrumentData();
    return std::unique_ptr<MasterScore>(jubelle_templates::readTemplate(
                                            ROOT + "share/templates/09-Solo_with_Piano/01-Violin_+_Piano/01-Violin_+_Piano.mscx"));
}

Measure* bar(MasterScore* s, int n)
{
    Measure* m = s->firstMeasure();
    for (int i = 1; i < n && m; ++i) {
        m = m->nextMeasure();
    }
    return m;
}

//! tap times for quarter-note taps at the given tempos (one tempo per interval)
std::vector<double> taps(const std::vector<double>& bpms)
{
    std::vector<double> t { 0.0 };
    for (double b : bpms) {
        t.push_back(t.back() + 60.0 / b);
    }
    return t;
}
}

TEST(Jubelle_TempoTap, SteadyTappingGivesOnePoint)
{
    jt::TapSettings s;
    const auto points = jt::tapsToTempo(taps({ 120, 120, 120, 120, 120, 120 }), Fraction(0, 1), s);
    ASSERT_EQ(points.size(), 1u);
    EXPECT_NEAR(points[0].bpm, 120.0, 0.01);
    EXPECT_EQ(points[0].tick, Fraction(0, 1));
}

TEST(Jubelle_TempoTap, TinyJitterIsIgnoredButARitardandoIsKept)
{
    jt::TapSettings s;
    // ±1 % jitter: still one point
    EXPECT_EQ(jt::tapsToTempo(taps({ 120, 121, 119, 120, 121, 119 }), Fraction(0, 1), s).size(), 1u);

    // a ritardando: the tempo keeps falling, every point lower than the one before
    const auto rit = jt::tapsToTempo(taps({ 120, 114, 106, 98, 90, 82 }), Fraction(0, 1), s);
    ASSERT_GE(rit.size(), 4u);
    for (size_t i = 1; i < rit.size(); ++i) {
        EXPECT_LT(rit[i].bpm, rit[i - 1].bpm);
        EXPECT_GT(rit[i].tick, rit[i - 1].tick);
    }
    // smoothing keeps the shape, not the exact taps: the last point is between the last two taps' tempo and earlier ones
    EXPECT_LT(rit.back().bpm, 100.0);
}

TEST(Jubelle_TempoTap, HalfNoteTapsAndMistakes)
{
    jt::TapSettings s;
    s.beat = Fraction(1, 2);
    // half notes at 60 per minute = quarter = 120
    const auto points = jt::tapsToTempo({ 0.0, 1.0, 2.0, 3.0 }, Fraction(0, 1), s);
    ASSERT_EQ(points.size(), 1u);
    EXPECT_NEAR(points[0].bpm, 120.0, 0.01);

    // a doubled tap (almost no time between two taps) is ignored
    jt::TapSettings q;
    const auto withMistake = jt::tapsToTempo({ 0.0, 0.5, 0.52, 1.02, 1.52 }, Fraction(0, 1), q);
    for (const auto& p : withMistake) {
        EXPECT_LT(p.bpm, 400.0);
    }
}

TEST(Jubelle_TempoTap, LiveTempoFollowsTheNewestTaps)
{
    jt::TapSettings s;
    EXPECT_EQ(jt::liveTempo({ 0.0 }, s), 0.0);
    const double t = jt::liveTempo(taps({ 120, 120, 100, 100 }), s);
    EXPECT_LT(t, 110.0);
    EXPECT_GT(t, 99.0);
}

TEST(Jubelle_TempoTap, RecordedTempoPlaysHiddenAndJoinsBack)
{
    auto s = score();
    ASSERT_TRUE(s);
    const double original = s->tempo(bar(s.get(), 1)->tick()).toBPM().val;

    std::vector<jt::TempoPoint> points { { bar(s.get(), 2)->tick(), 90.0 }, { bar(s.get(), 3)->tick(), 80.0 } };
    s->startCmd(muse::TranslatableString::untranslatable("record tempo"));
    jt::applyTempoPoints(s.get(), points, bar(s.get(), 2)->tick(), bar(s.get(), 4)->tick());
    s->endCmd();
    s->doLayout();

    EXPECT_NEAR(s->tempo(bar(s.get(), 2)->tick()).toBPM().val, 90.0, 0.01);
    EXPECT_NEAR(s->tempo(bar(s.get(), 3)->tick() + Fraction(1, 8)).toBPM().val, 80.0, 0.01);
    EXPECT_NEAR(s->tempo(bar(s.get(), 4)->tick()).toBPM().val, original, 0.01) << "back to the tempo after the section";
    EXPECT_NEAR(s->tempo(bar(s.get(), 1)->tick()).toBPM().val, original, 0.01) << "before the section: untouched";

    const auto recorded = jt::recordedTempoPoints(s.get());
    ASSERT_EQ(recorded.size(), 3u);
    for (const TempoText* t : recorded) {
        EXPECT_FALSE(t->visible()) << "never printed";
    }
}

TEST(Jubelle_TempoTap, ReRecordingReplacesOnlyThatSection)
{
    auto s = score();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("record tempo"));
    jt::applyTempoPoints(s.get(), { { bar(s.get(), 1)->tick(), 100.0 }, { bar(s.get(), 5)->tick(), 70.0 } },
                         bar(s.get(), 1)->tick(), bar(s.get(), 8)->tick());
    s->endCmd();
    s->startCmd(muse::TranslatableString::untranslatable("re-record"));
    jt::applyTempoPoints(s.get(), { { bar(s.get(), 5)->tick(), 140.0 } }, bar(s.get(), 5)->tick(), bar(s.get(), 6)->tick());
    s->endCmd();
    s->doLayout();
    EXPECT_NEAR(s->tempo(bar(s.get(), 1)->tick()).toBPM().val, 100.0, 0.01) << "outside the section: untouched";
    EXPECT_NEAR(s->tempo(bar(s.get(), 5)->tick()).toBPM().val, 140.0, 0.01);
    EXPECT_NEAR(s->tempo(bar(s.get(), 6)->tick()).toBPM().val, 70.0, 0.01) << "the earlier take continues after the section";

    // editing a point
    auto recorded = jt::recordedTempoPoints(s.get());
    ASSERT_FALSE(recorded.empty());
    s->startCmd(muse::TranslatableString::untranslatable("edit"));
    jt::setRecordedPointTempo(s.get(), recorded.front(), 96.0);
    s->endCmd();
    s->doLayout();
    EXPECT_NEAR(s->tempo(bar(s.get(), 1)->tick()).toBPM().val, 96.0, 0.01);

    // undo all the way: no points left
    s->undoRedo(true, nullptr);
    s->undoRedo(true, nullptr);
    s->undoRedo(true, nullptr);
    EXPECT_TRUE(jt::recordedTempoPoints(s.get()).empty());
}

TEST(Jubelle_TempoTap, TheNotesAreUnchanged)
{
    auto s = score();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("notes"));
    s->setNoteRest(bar(s.get(), 2)->first(SegmentType::ChordRest), 0, NoteVal(67), Fraction(1, 4));
    s->endCmd();
    const Fraction noteTick = bar(s.get(), 2)->tick();
    s->startCmd(muse::TranslatableString::untranslatable("record tempo"));
    jt::applyTempoPoints(s.get(), { { noteTick, 72.0 } }, noteTick, bar(s.get(), 3)->tick());
    s->endCmd();
    ChordRest* cr = toChordRest(bar(s.get(), 2)->first(SegmentType::ChordRest)->element(0));
    ASSERT_TRUE(cr && cr->isChord());
    EXPECT_EQ(toChord(cr)->upNote()->pitch(), 67);
    EXPECT_EQ(cr->ticks(), Fraction(1, 4));
}

TEST(Jubelle_TempoTap, RecordedTempoSurvivesSavingAndReopening)
{
    auto s = score();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("record tempo"));
    jt::applyTempoPoints(s.get(), { { bar(s.get(), 2)->tick(), 88.0 } }, bar(s.get(), 2)->tick(), bar(s.get(), 3)->tick());
    s->endCmd();
    // its own folder: writeTemplate replaces the whole folder it writes into
    const std::string path = ::testing::TempDir() + "jubelle_tempotap_roundtrip/score.mscx";
    ASSERT_TRUE(jubelle_templates::writeTemplate(s.get(), path));

    std::unique_ptr<MasterScore> back(jubelle_templates::readTemplate(path));
    ASSERT_TRUE(back);
    back->doLayout();
    EXPECT_NEAR(back->tempo(bar(back.get(), 2)->tick()).toBPM().val, 88.0, 0.01) << "the tempo plays the same after reopening";
    const auto recorded = jt::recordedTempoPoints(back.get());
    ASSERT_EQ(recorded.size(), 2u) << "Jubelle still knows its own points (the point and the join)";
    EXPECT_FALSE(recorded.front()->visible());
}
