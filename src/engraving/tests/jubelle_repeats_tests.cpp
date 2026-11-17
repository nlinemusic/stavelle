/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

#include <gtest/gtest.h>

#include "engraving/dom/barline.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/spanner.h"
#include "engraving/dom/volta.h"

#include "utils/scorerw.h"

using namespace mu::engraving;

//! Jubelle Phase 5D: "Play this repeat", on MuseScore's own repeat files
//! (src/engraving/tests/repeat_data) and MuseScore's own RepeatList.
namespace {
String playedBars(MasterScore* score)
{
    score->setExpandRepeats(true);
    StringList sl;
    for (const RepeatSegment* rs : score->repeatList()) {
        for (const Measure* m = score->tick2measure(Fraction::fromTicks(rs->tick)); m; m = m->nextMeasure()) {
            sl.append(String::number(m->no() + 1));
            if (m->endTick().ticks() >= rs->endTick()) {
                break;
            }
        }
    }
    return sl.join(u";");
}

std::vector<Measure*> endRepeats(Score* score)
{
    std::vector<Measure*> out;
    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        if (m->repeatEnd()) {
            out.push_back(m);
        }
    }
    return out;
}

BarLine* endRepeatBarline(Measure* m)
{
    Segment* s = m->findSegment(SegmentType::EndBarLine, m->endTick());
    return s && s->element(0) ? toBarLine(s->element(0)) : nullptr;
}

std::vector<Volta*> voltas(Score* score)
{
    std::vector<Volta*> out;
    for (auto& [tick, sp] : score->spanner()) {
        if (sp->isVolta()) {
            out.push_back(toVolta(sp));
        }
    }
    return out;
}

void setPlay(Score* score, EngravingItem* item, bool play)
{
    score->startCmd(muse::TranslatableString::untranslatable("play repeat"));
    item->undoChangeProperty(Pid::JUBELLE_PLAY_REPEAT, play);
    score->endCmd();
}

String noSpaces(String s)
{
    s.replace(u" ", u"");
    return s;
}
}

TEST(Jubelle_Repeats, SimpleRepeatSwitchedOff)
{
    MasterScore* score = ScoreRW::readScore(u"repeat_data/repeat01.mscx");
    ASSERT_TRUE(score);
    EXPECT_EQ(playedBars(score), noSpaces(u"1;2;3; 2;3;4;5;6"));   // MuseScore, unchanged

    const std::vector<Measure*> ends = endRepeats(score);
    ASSERT_EQ(ends.size(), 1u);
    BarLine* bl = endRepeatBarline(ends[0]);
    ASSERT_TRUE(bl);
    EXPECT_EQ(bl->getProperty(Pid::JUBELLE_PLAY_REPEAT).toBool(), true);

    setPlay(score, bl, false);   // from the repeat barline, as in the Properties panel
    EXPECT_EQ(playedBars(score), noSpaces(u"1;2;3;4;5;6"));
    EXPECT_TRUE(ends[0]->repeatEnd()) << "the repeat sign stays in the score";
    EXPECT_EQ(ends[0]->repeatCount(), 2);

    score->undoRedo(true, nullptr);
    EXPECT_EQ(playedBars(score), noSpaces(u"1;2;3; 2;3;4;5;6"));
    score->undoRedo(false, nullptr);
    EXPECT_EQ(playedBars(score), noSpaces(u"1;2;3;4;5;6"));

    // Saved and reopened
    const String saved = u"jubelle_repeats_test.mscx";
    ASSERT_TRUE(ScoreRW::saveScore(score, saved));
    MasterScore* reopened = ScoreRW::readScore(saved, true);
    ASSERT_TRUE(reopened);
    EXPECT_FALSE(endRepeats(reopened)[0]->jubellePlayRepeat());
    EXPECT_EQ(playedBars(reopened), noSpaces(u"1;2;3;4;5;6"));
    delete reopened;
    delete score;
}

TEST(Jubelle_Repeats, FirstAndSecondEndings)
{
    // Simple volta: bar 3 is the 1st ending, bar 4 the 2nd
    MasterScore* score = ScoreRW::readScore(u"repeat_data/repeat06.mscx");
    ASSERT_TRUE(score);
    EXPECT_EQ(playedBars(score), noSpaces(u"1;2;3; 2; 4;5;6"));

    const std::vector<Volta*> vs = voltas(score);
    ASSERT_EQ(vs.size(), 2u);
    Volta* first = vs[0]->hasEnding(1) ? vs[0] : vs[1];
    Volta* second = first == vs[0] ? vs[1] : vs[0];
    // Both endings belong to the same repeat
    ASSERT_TRUE(first->jubelleRepeatMeasure());
    EXPECT_EQ(first->jubelleRepeatMeasure(), second->jubelleRepeatMeasure());

    // Switched off from the 2nd ending: played once, straight to the last ending
    setPlay(score, second, false);
    EXPECT_EQ(playedBars(score), noSpaces(u"1;2; 4;5;6"));
    EXPECT_FALSE(first->getProperty(Pid::JUBELLE_PLAY_REPEAT).toBool());
    EXPECT_EQ(voltas(score).size(), 2u) << "the endings stay in the score";

    setPlay(score, first, true);
    EXPECT_EQ(playedBars(score), noSpaces(u"1;2;3; 2; 4;5;6"));
    delete score;
}

TEST(Jubelle_Repeats, ThreeEndingsAndTheGlobalSwitch)
{
    // 3 voltas, with a repeat played three times
    MasterScore* score = ScoreRW::readScore(u"repeat_data/repeat10.mscx");
    ASSERT_TRUE(score);
    const String museScore = noSpaces(u"1;2;3;4; 1;2; 5;6;7;8; 1;2; 9;10; 1;2; 11;12");
    EXPECT_EQ(playedBars(score), museScore);

    std::vector<Measure*> ends = endRepeats(score);
    ASSERT_GE(ends.size(), 1u);
    for (Measure* m : ends) {
        setPlay(score, endRepeatBarline(m), false);
    }
    const String once = playedBars(score);
    // Played once, straight into the final ending
    EXPECT_EQ(once, noSpaces(u"1;2; 11;12"));
    EXPECT_TRUE(once.startsWith(u"1;2;")) << once.toStdString();
    EXPECT_TRUE(once.endsWith(u"11;12")) << once.toStdString();   // the final ending is played

    // Turning "play repeats" off for the whole score still works as in MuseScore
    score->setExpandRepeats(false);
    StringList all;
    for (const RepeatSegment* rs : score->repeatList()) {
        (void)rs;
        all.append(u"x");
    }
    EXPECT_EQ(all.size(), 1u);
    delete score;
}

TEST(Jubelle_Repeats, JumpsKeepTheirOwnSetting)
{
    // DC al fine: no repeat barlines, so nothing changes
    MasterScore* score = ScoreRW::readScore(u"repeat_data/repeat07.mscx");
    ASSERT_TRUE(score);
    EXPECT_EQ(playedBars(score), noSpaces(u"1;2;3;4;5;6; 1;2;3"));
    EXPECT_TRUE(endRepeats(score).empty());
    delete score;
}
