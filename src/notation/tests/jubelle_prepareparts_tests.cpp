/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

#include <gtest/gtest.h>

#include "engraving/dom/excerpt.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/instrchange.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/stafftext.h"
#include "engraving/dom/text.h"

#include "notation/internal/jubelle/prepareparts.h"

#include "templatebuilder.h"

using namespace mu::engraving;
namespace jpp = mu::notation::jubelle;

namespace {
const std::string ROOT = std::string(MU_ROOT_DIR) + "/";

int tacetTexts(Score* score)
{
    int n = 0;
    for (Segment* seg = score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (EngravingItem* a : seg->annotations()) {
            n += (a && a->isStaffText() && toStaffText(a)->plainText() == u"Tacet") ? 1 : 0;
        }
    }
    return n;
}
}

TEST(Jubelle_PrepareParts, EveryPartInOneStep)
{
    jubelle_templates::loadInstrumentData();
    std::unique_ptr<MasterScore> master(jubelle_templates::readTemplate(
                                            ROOT + "share/templates/09-Solo_with_Piano/01-Violin_+_Piano/01-Violin_+_Piano.mscx"));
    ASSERT_TRUE(master);
    master->startCmd(muse::TranslatableString::untranslatable("test"));
    // the violin plays and changes to viola in bar 2; the piano has nothing at all
    Segment* bar1 = master->firstMeasure()->first(SegmentType::ChordRest);
    master->setNoteRest(bar1, 0, NoteVal(67), Fraction(1, 1));
    Segment* bar2 = master->firstMeasure()->nextMeasure()->first(SegmentType::ChordRest);
    InstrumentChange* ic = Factory::createInstrumentChange(bar2, Instrument::fromTemplate(searchTemplate(u"viola")));
    ic->setParent(bar2);
    ic->setTrack(0);
    master->undoAddElement(ic);
    master->endCmd();
    for (Excerpt* ex : Excerpt::createExcerptsFromParts(master->parts(), master.get())) {
        master->initAndAddExcerpt(ex, false);
    }
    ASSERT_EQ(master->excerpts().size(), 2u);
    Score* violin = master->excerpts()[0]->excerptScore();
    Score* piano = master->excerpts()[1]->excerptScore();
    violin->undoChangeStyleVal(Sid::createMultiMeasureRests, false);

    EXPECT_EQ(jpp::instrumentLine(violin), master->parts().front()->partName() + u" (doubling Viola)");

    master->startCmd(muse::TranslatableString::untranslatable("prepare parts"));
    const std::vector<jpp::PreparedPart> done = jpp::prepareParts(master.get());
    master->endCmd();
    ASSERT_EQ(done.size(), 2u);

    // violin: multi-measure rests switched on, the doubling on the first page, not tacet
    EXPECT_TRUE(done[0].turnedOnMMRests);
    EXPECT_TRUE(violin->style().styleB(Sid::createMultiMeasureRests));
    EXPECT_FALSE(done[0].wholeTacet);
    if (Text* t = violin->getText(TextStyleType::INSTRUMENT_EXCERPT)) {
        EXPECT_TRUE(t->plainText().contains(u"(doubling Viola)"));
    }
    EXPECT_EQ(tacetTexts(violin), 0);

    // piano: tacet, written in the piano part only
    EXPECT_TRUE(done[1].wholeTacet);
    EXPECT_EQ(tacetTexts(piano), 1);
    EXPECT_EQ(tacetTexts(master.get()), 0);
    EXPECT_EQ(tacetTexts(violin), 0);

    // running it again adds nothing more
    master->startCmd(muse::TranslatableString::untranslatable("prepare parts again"));
    jpp::prepareParts(master.get());
    master->endCmd();
    EXPECT_EQ(tacetTexts(piano), 1);

    // one undo takes it all back
    master->undoRedo(true, nullptr);
    master->undoRedo(true, nullptr);
    EXPECT_EQ(tacetTexts(piano), 0);
    EXPECT_FALSE(violin->style().styleB(Sid::createMultiMeasureRests));
}
