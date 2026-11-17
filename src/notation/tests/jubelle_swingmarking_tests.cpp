/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

#include <gtest/gtest.h>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/systemtext.h"
#include "engraving/dom/tempotext.h"

#include "notation/internal/jubelle/swingmarking.h"

#include "utils/scorerw.h"

using namespace mu::engraving;
using namespace mu::engraving::jubelle;
namespace jsm = mu::notation::jubelle;

//! Jubelle Phase 5C: the automatic swing marking, on the real notation engine.
namespace {
std::vector<SystemText*> systemTexts(Score* score)
{
    std::vector<SystemText*> out;
    for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        for (EngravingItem* a : s->annotations()) {
            if (a->isSystemText()) {
                out.push_back(toSystemText(a));
            }
        }
    }
    return out;
}

Segment* tempoSegment(Score* score)
{
    for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        for (EngravingItem* a : s->annotations()) {
            if (a->isTempoText()) {
                return s;
            }
        }
    }
    return nullptr;
}

//! What the Swing dialog does on Apply: marking and settings in one undo step
jsm::SwingMarkingResult apply(Score* score, SwingSettings& settings, const muse::String& name = muse::String())
{
    score->startCmd(muse::TranslatableString::untranslatable("swing"));
    const jsm::SwingMarkingResult r = jsm::updateSwingMarking(score, settings, name);
    settings.write(score);
    score->endCmd();
    return r;
}
}

TEST(Jubelle_SwingMarking, PlacedAtTheTempoMarkingAndNeverTwice)
{
    MasterScore* score = ScoreRW::readScore(u"jubelle_data/swing_eighths.mscx");
    ASSERT_TRUE(score);
    Segment* tempoSeg = tempoSegment(score);
    ASSERT_TRUE(tempoSeg);
    const size_t before = systemTexts(score).size();

    SwingSettings s;
    s.scoreFeel = u"basie";
    const jsm::SwingMarkingResult r = apply(score, s);
    EXPECT_TRUE(r.added);
    ASSERT_EQ(systemTexts(score).size(), before + 1);
    SystemText* marking = jsm::findSwingMarking(score, SwingSettings::read(score));
    ASSERT_TRUE(marking);
    EXPECT_EQ(marking->explicitParent(), tempoSeg);                 // with the tempo marking
    EXPECT_EQ(marking->placement(), PlacementV::ABOVE);
    EXPECT_EQ(marking->textStyleType(), TextStyleType::TEMPO) << "readable: tempo-marking size (R0)";
    EXPECT_TRUE(marking->xmlText().startsWith(u"Basie swing "));
    EXPECT_TRUE(marking->xmlText().contains(u"textTuplet3ShortStem"));   // triplet equation

    // Again, and again after reading the settings back: still only one
    SwingSettings again = SwingSettings::read(score);
    const jsm::SwingMarkingResult r2 = apply(score, again);
    EXPECT_FALSE(r2.added || r2.updated || r2.removed);
    EXPECT_EQ(systemTexts(score).size(), before + 1);

    // Another feel updates the same text in place
    again.scoreFeel = u"fixed31";
    EXPECT_TRUE(apply(score, again).updated);
    EXPECT_EQ(systemTexts(score).size(), before + 1);
    EXPECT_EQ(jsm::findSwingMarking(score, again), marking);
    EXPECT_TRUE(marking->xmlText().startsWith(u"Swing 3:1 (dotted) "));
    EXPECT_TRUE(marking->xmlText().contains(u"textAugmentationDot"));   // dotted equation

    // Text only, then your own description
    again.markingStyle = SwingMarkingStyle::Text;
    apply(score, again, u"Basie-Schwung");
    EXPECT_EQ(marking->xmlText(), u"Basie-Schwung");
    again.markingStyle = SwingMarkingStyle::Custom;
    again.markingText = u"Relaxed swing <8ths>";
    apply(score, again);
    EXPECT_EQ(marking->xmlText(), u"Relaxed swing &lt;8ths&gt;");

    // Straight, or the marking switched off: removed
    again.scoreFeel = u"straight";
    EXPECT_TRUE(apply(score, again).removed);
    EXPECT_EQ(systemTexts(score).size(), before);
    EXPECT_TRUE(again.markingEid.isEmpty());

    again.scoreFeel = u"medium";
    again.markingStyle = SwingMarkingStyle::TextAndEquation;
    EXPECT_TRUE(apply(score, again).added);
    again.markingStyle = SwingMarkingStyle::Off;
    EXPECT_TRUE(apply(score, again).removed);
    EXPECT_EQ(systemTexts(score).size(), before);

    // Undo brings back the marking and the settings together
    score->undoRedo(true, nullptr);
    EXPECT_EQ(systemTexts(score).size(), before + 1);
    EXPECT_EQ(SwingSettings::read(score).markingStyle, SwingMarkingStyle::TextAndEquation);
    EXPECT_TRUE(jsm::findSwingMarking(score, SwingSettings::read(score)));
    delete score;
}

TEST(Jubelle_SwingMarking, DeletedByYouOrReopenedFile)
{
    MasterScore* score = ScoreRW::readScore(u"jubelle_data/swing_eighths.mscx");
    ASSERT_TRUE(score);
    const size_t before = systemTexts(score).size();

    SwingSettings s;
    s.scoreFeel = u"heavy";
    apply(score, s);
    SystemText* marking = jsm::findSwingMarking(score, s);
    ASSERT_TRUE(marking);

    // Saved and reopened: Jubelle still knows its own marking
    const muse::String saved = u"jubelle_swingmarking_test.mscx";
    ASSERT_TRUE(ScoreRW::saveScore(score, saved));
    MasterScore* reopened = ScoreRW::readScore(saved, true);
    ASSERT_TRUE(reopened);
    SwingSettings r = SwingSettings::read(reopened);
    ASSERT_TRUE(jsm::findSwingMarking(reopened, r));
    r.scoreFeel = u"light";
    EXPECT_TRUE(apply(reopened, r).updated);
    EXPECT_EQ(systemTexts(reopened).size(), before + 1);
    EXPECT_TRUE(jsm::findSwingMarking(reopened, r)->xmlText().startsWith(u"Light swing "));
    delete reopened;

    // You delete the marking yourself: the next change adds one again (once)
    score->startCmd(muse::TranslatableString::untranslatable("delete"));
    score->undoRemoveElement(marking);
    score->endCmd();
    EXPECT_EQ(systemTexts(score).size(), before);
    EXPECT_FALSE(jsm::findSwingMarking(score, s));
    s.scoreFeel = u"basie";
    EXPECT_TRUE(apply(score, s).added);
    EXPECT_EQ(systemTexts(score).size(), before + 1);
    delete score;
}

TEST(Jubelle_SwingMarking, NoTempoMarkingMeansTheStart)
{
    MasterScore* score = ScoreRW::readScore(u"jubelle_data/articulation_trumpet.mscx");
    ASSERT_TRUE(score);
    ASSERT_FALSE(tempoSegment(score));
    SwingSettings s;
    s.scoreFeel = u"fixed21";
    apply(score, s);
    SystemText* marking = jsm::findSwingMarking(score, s);
    ASSERT_TRUE(marking);
    EXPECT_EQ(marking->explicitParent(), score->firstMeasure()->first(SegmentType::ChordRest));
    EXPECT_TRUE(marking->xmlText().startsWith(u"Swing 2:1 (triplet) "));

    // The music itself is untouched
    int chords = 0;
    for (Segment* sg = score->firstSegment(SegmentType::ChordRest); sg; sg = sg->next1(SegmentType::ChordRest)) {
        chords += sg->element(0) && sg->element(0)->isChord() ? 1 : 0;
    }
    EXPECT_EQ(chords, 2);
    delete score;
}
