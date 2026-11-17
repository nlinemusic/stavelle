/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle G8: Page-Turn Optimisation, on the real engine.
 */
#include <gtest/gtest.h>

#include <memory>
#include <set>

#include "engraving/dom/chord.h"
#include "engraving/dom/layoutbreak.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/page.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/tempo.h"
#include "notation/internal/jubelle/pageturns.h"

#include "templatebuilder.h"

using namespace mu::engraving;
namespace jp = mu::notation::jubelle;

namespace {
const std::string ROOT = std::string(MU_ROOT_DIR) + "/";
const int BARS = 160;

Measure* bar(Score* s, int n)   // 1 = first
{
    Measure* m = s->firstMeasure();
    for (int i = 1; i < n && m; ++i) {
        m = m->nextMeasure();
    }
    return m;
}

//! Violin and piano, 160 bars of 4/4 at the default tempo (2 s a bar). The violin plays a whole
//! note in every bar except the ones in `silent`; the piano rests throughout.
std::unique_ptr<MasterScore> piece(const std::set<int>& silent)
{
    jubelle_templates::loadInstrumentData();
    std::unique_ptr<MasterScore> s(jubelle_templates::readTemplate(
                                       ROOT + "share/templates/09-Solo_with_Piano/01-Violin_+_Piano/01-Violin_+_Piano.mscx"));
    if (!s) {
        return s;
    }
    s->startCmd(muse::TranslatableString::untranslatable("page turns test"));
    while (int(s->nmeasures()) < BARS) {
        s->insertMeasure(nullptr);
    }
    int n = 1;
    for (Measure* m = s->firstMeasure(); m; m = m->nextMeasure(), ++n) {
        if (!silent.count(n)) {
            s->setNoteRest(m->first(SegmentType::ChordRest), 0, NoteVal(60 + n % 12), Fraction(1, 1));
        }
    }
    s->endCmd();
    s->doLayout();
    return s;
}

//! two silent bars (4 s) every `period` bars
std::set<int> restsEvery(int period)
{
    std::set<int> out;
    for (int b = period; b + 1 <= BARS; b += period) {
        out.insert(b);
        out.insert(b + 1);
    }
    return out;
}

std::string music(Score* s)
{
    std::string out;
    for (Segment* seg = s->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (EngravingItem* e : seg->elist()) {
            if (e && e->isChord()) {
                out += std::to_string(seg->tick().ticks()) + ":" + std::to_string(toChord(e)->upNote()->pitch()) + " ";
            }
        }
    }
    return out;
}

std::vector<int> pageBreakBars(Score* s)
{
    std::vector<int> out;
    for (Measure* m = s->firstMeasure(); m; m = m->nextMeasure()) {
        if (m->pageBreak()) {
            out.push_back(m->no() + 1);
        }
    }
    return out;
}

jp::TurnSettings everyPage()
{
    jp::TurnSettings st;
    st.layout = jp::TurnLayout::EveryPage;
    return st;
}
}

TEST(Jubelle_PageTurns, RestIsMeasuredInSeconds)
{
    auto s = piece({ 5, 6 });
    ASSERT_TRUE(s);
    const double barSeconds = s->tempomap()->tick2time(bar(s.get(), 2)->tick().ticks());
    ASSERT_GT(barSeconds, 0.5);
    // at the end of bar 5: one silent bar before, one after
    const int tick = bar(s.get(), 5)->endTick().ticks();
    EXPECT_NEAR(jp::restBefore(s.get(), tick), barSeconds, 0.01);
    EXPECT_NEAR(jp::restAfter(s.get(), tick), barSeconds, 0.01);
    // in the middle of the music: none
    const int busy = bar(s.get(), 10)->endTick().ticks();
    EXPECT_NEAR(jp::restBefore(s.get(), busy), 0.0, 0.001);
    EXPECT_NEAR(jp::restAfter(s.get(), busy), 0.0, 0.001);
}

TEST(Jubelle_PageTurns, PoorTurnsMoveIntoTheRests)
{
    auto s = piece(restsEvery(7));
    ASSERT_TRUE(s);
    const jp::TurnSettings st = everyPage();
    ASSERT_GE(s->pages().size(), 3u) << "the piece must run over several pages";
    const std::string before = music(s.get());

    int poorBefore = 0;
    for (const jp::PageTurn& t : jp::analyseTurns(s.get(), st)) {
        poorBefore += t.quality != jp::TurnQuality::Good;
    }
    ASSERT_GT(poorBefore, 0) << "the test piece needs at least one bad turn to begin with";

    s->startCmd(muse::TranslatableString::untranslatable("page turns"));
    const jp::TurnResult r = jp::optimiseTurns(s.get(), st);
    s->endCmd();

    EXPECT_GT(r.moved, 0);
    EXPECT_EQ(r.stillPoor, 0);
    const auto after = jp::analyseTurns(s.get(), st);
    ASSERT_FALSE(after.empty());
    for (const jp::PageTurn& t : after) {
        EXPECT_EQ(t.quality, jp::TurnQuality::Good) << "page " << t.page << " turns before bar " << t.nextBar << " with " << t.rest()
                                                    << " s of rest";
        EXPECT_FALSE(t.userBreak);
    }
    EXPECT_EQ(music(s.get()), before) << "no note may change";

    // one undo takes all of it back
    s->undoRedo(true, nullptr);
    s->doLayout();
    EXPECT_TRUE(pageBreakBars(s.get()).empty());
}

TEST(Jubelle_PageTurns, NoRestsMeansAnHonestNoGoodTurn)
{
    auto s = piece({});
    ASSERT_TRUE(s);
    const jp::TurnSettings st = everyPage();
    const size_t turns = jp::analyseTurns(s.get(), st).size();
    ASSERT_GT(turns, 0u);

    s->startCmd(muse::TranslatableString::untranslatable("page turns"));
    const jp::TurnResult r = jp::optimiseTurns(s.get(), st);
    s->endCmd();

    EXPECT_EQ(r.moved, 0);
    EXPECT_EQ(size_t(r.stillPoor), r.turns.size());
    EXPECT_TRUE(pageBreakBars(s.get()).empty()) << "nothing is added when nothing would help";
}

TEST(Jubelle_PageTurns, YourOwnPageBreaksAreKept)
{
    auto s = piece(restsEvery(7));
    ASSERT_TRUE(s);
    const jp::TurnSettings st = everyPage();
    // your break after bar 10 (a busy bar: a poor turn, but yours)
    s->startCmd(muse::TranslatableString::untranslatable("user break"));
    bar(s.get(), 10)->undoSetPageBreak(true);
    s->endCmd();
    s->doLayout();

    s->startCmd(muse::TranslatableString::untranslatable("page turns"));
    const jp::TurnResult r = jp::optimiseTurns(s.get(), st);
    s->endCmd();

    ASSERT_FALSE(r.turns.empty());
    EXPECT_TRUE(r.turns.front().userBreak);
    EXPECT_FALSE(r.turns.front().moved);
    EXPECT_EQ(r.turns.front().nextBarAfter, 11);
    EXPECT_TRUE(bar(s.get(), 10)->pageBreak());

    // removing Jubelle's turns leaves exactly yours
    s->startCmd(muse::TranslatableString::untranslatable("clear"));
    const int removed = jp::clearJubelleTurns(s.get());
    s->endCmd();
    EXPECT_EQ(removed, r.moved);
    EXPECT_EQ(pageBreakBars(s.get()), std::vector<int> { 10 });
}

TEST(Jubelle_PageTurns, RunningItTwiceGivesTheSameResult)
{
    auto s = piece(restsEvery(7));
    ASSERT_TRUE(s);
    const jp::TurnSettings st = everyPage();
    s->startCmd(muse::TranslatableString::untranslatable("page turns"));
    jp::optimiseTurns(s.get(), st);
    s->endCmd();
    const std::vector<int> first = pageBreakBars(s.get());
    s->startCmd(muse::TranslatableString::untranslatable("page turns"));
    jp::optimiseTurns(s.get(), st);
    s->endCmd();
    EXPECT_EQ(pageBreakBars(s.get()), first);
}

TEST(Jubelle_PageTurns, TwoSidedPagesTurnOnlyAfterRightHandPages)
{
    auto s = piece(restsEvery(7));
    ASSERT_TRUE(s);
    jp::TurnSettings st;
    st.layout = jp::TurnLayout::AfterRightHandPages;
    for (const jp::PageTurn& t : jp::analyseTurns(s.get(), st)) {
        EXPECT_EQ(t.page % 2, 1) << "page 1 is on the right: turns come after pages 1, 3, 5";
    }
    st.layout = jp::TurnLayout::AfterLeftHandPages;
    for (const jp::PageTurn& t : jp::analyseTurns(s.get(), st)) {
        EXPECT_EQ(t.page % 2, 0);
    }
}

TEST(Jubelle_PageTurns, TheMarkingSurvivesSavingAndReopening)
{
    auto s = piece(restsEvery(7));
    ASSERT_TRUE(s);
    const jp::TurnSettings st = everyPage();
    s->startCmd(muse::TranslatableString::untranslatable("page turns"));
    const jp::TurnResult r = jp::optimiseTurns(s.get(), st);
    s->endCmd();
    ASSERT_GT(r.moved, 0);
    const std::vector<int> breaks = pageBreakBars(s.get());

    // its own folder: writeTemplate replaces the whole folder it writes into
    const std::string path = ::testing::TempDir() + "jubelle_pageturns_roundtrip/score.mscx";
    ASSERT_TRUE(jubelle_templates::writeTemplate(s.get(), path));
    std::unique_ptr<MasterScore> back(jubelle_templates::readTemplate(path));
    ASSERT_TRUE(back);
    back->doLayout();
    EXPECT_EQ(pageBreakBars(back.get()), breaks) << "the breaks are ordinary page breaks";

    back->startCmd(muse::TranslatableString::untranslatable("clear"));
    EXPECT_EQ(jp::clearJubelleTurns(back.get()), r.moved) << "Jubelle still knows which breaks are its own";
    back->endCmd();
    EXPECT_TRUE(pageBreakBars(back.get()).empty());
}
