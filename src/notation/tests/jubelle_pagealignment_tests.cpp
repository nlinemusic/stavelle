/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

#include <gtest/gtest.h>

#include "engraving/dom/box.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/page.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/stafftext.h"
#include "engraving/dom/text.h"

#include "notation/internal/jubelle/pagealignment.h"

#include "utils/scorerw.h"

using namespace mu::engraving;
namespace jpa = mu::notation::jubelle;
using muse::RectF;

//! Jubelle title-page alignment, on the real notation engine with real
//! page layout: a title frame with title, subtitle and composer.
namespace {
constexpr double NEAR = 1.0;   // raster units (a fraction of a millimetre)

struct TitlePage {
    MasterScore* score = nullptr;
    VBox* frame = nullptr;
    Text* title = nullptr;
    Text* subtitle = nullptr;
    Text* composer = nullptr;
    const Page* page = nullptr;
};

Text* addText(Score* s, VBox* box, TextStyleType type, const char16_t* text)
{
    Text* t = Factory::createText(box, type);
    t->setParent(box);
    t->setXmlText(text);
    s->undoAddElement(t);
    return t;
}

TitlePage makeTitlePage()
{
    TitlePage tp;
    tp.score = ScoreRW::readScore(u"jubelle_data/articulation_trumpet.mscx");
    if (!tp.score) {
        return tp;
    }
    MasterScore* s = tp.score;
    s->startCmd(muse::TranslatableString::untranslatable("title frame"));
    tp.frame = toVBox(s->insertBox(ElementType::VBOX, s->firstMeasure()));
    tp.title = addText(s, tp.frame, TextStyleType::TITLE, u"Sunset Boulevard Shuffle");
    tp.subtitle = addText(s, tp.frame, TextStyleType::SUBTITLE, u"for big band");
    tp.composer = addText(s, tp.frame, TextStyleType::COMPOSER, u"N. Line");
    s->endCmd();
    s->doLayout();
    for (const EngravingObject* p = tp.title; p; p = p->explicitParent()) {
        if (p->isPage()) {
            tp.page = toPage(p);
        }
    }
    return tp;
}

RectF sheetOf(const Page* p)
{
    return RectF(0, 0, p->style().styleD(Sid::pageWidth) * DPI, p->style().styleD(Sid::pageHeight) * DPI);
}

RectF margins(const Page* p)
{
    const RectF b = sheetOf(p);
    return RectF(p->lm(), p->tm(), b.width() - p->lm() - p->rm(), b.height() - p->tm() - p->bm());
}

jpa::AlignReport run(Score* s, std::vector<EngravingItem*> items, jpa::AlignCommand c, jpa::AlignReference r)
{
    s->startCmd(muse::TranslatableString::untranslatable("align"));
    const jpa::AlignReport rep = jpa::alignItems(s, items, c, r);
    s->endCmd();
    s->doLayout();
    return rep;
}

std::string music(Score* s)
{
    std::string out;
    for (Segment* seg = s->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        if (seg->element(0) && seg->element(0)->isChord()) {
            out += std::to_string(seg->tick().ticks()) + ":" + std::to_string(toChord(seg->element(0))->notes().size()) + ";";
        }
    }
    return out;
}
}

TEST(Jubelle_PageAlignment, CentreTitleOnMarginsUsesMuseScoresCentreAlignment)
{
    TitlePage tp = makeTitlePage();
    ASSERT_TRUE(tp.score && tp.page && tp.title);

    // Move the title off-centre first, as if dragged
    tp.score->startCmd(muse::TranslatableString::untranslatable("drag"));
    tp.title->undoChangeProperty(Pid::OFFSET, muse::PointF(25.0, 3.0));
    tp.score->endCmd();
    tp.score->doLayout();
    const double width = tp.title->pageBoundingRect().width();
    const double y = tp.title->pageBoundingRect().top();

    const jpa::AlignReport r = run(tp.score, { tp.title }, jpa::AlignCommand::HCenter, jpa::AlignReference::Margins);
    EXPECT_TRUE(r.refused.empty());
    EXPECT_EQ(r.moved, 1);
    EXPECT_NEAR(tp.title->pageBoundingRect().center().x(), margins(tp.page).center().x(), NEAR);
    EXPECT_NEAR(tp.title->pageBoundingRect().top(), y, NEAR) << "only the horizontal position changes";
    EXPECT_NEAR(tp.title->pageBoundingRect().width(), width, 0.01) << "size unchanged";
    EXPECT_EQ(tp.title->getProperty(Pid::ALIGN).value<Align>().horizontal, AlignH::HCENTER);

    // Undo restores the dragged position
    tp.score->undoRedo(true, nullptr);
    tp.score->doLayout();
    EXPECT_NEAR(tp.title->offset().x(), 25.0, 0.01);
    delete tp.score;
}

TEST(Jubelle_PageAlignment, PageAndMarginEdges)
{
    TitlePage tp = makeTitlePage();
    ASSERT_TRUE(tp.score && tp.page);
    const RectF sheet = sheetOf(tp.page);

    run(tp.score, { tp.composer }, jpa::AlignCommand::Right, jpa::AlignReference::Page);
    EXPECT_NEAR(tp.composer->pageBoundingRect().right(), sheet.width(), NEAR);

    run(tp.score, { tp.composer }, jpa::AlignCommand::Right, jpa::AlignReference::Margins);
    EXPECT_NEAR(tp.composer->pageBoundingRect().right(), margins(tp.page).right(), NEAR);

    run(tp.score, { tp.subtitle }, jpa::AlignCommand::Left, jpa::AlignReference::Margins);
    EXPECT_NEAR(tp.subtitle->pageBoundingRect().left(), margins(tp.page).left(), NEAR);

    run(tp.score, { tp.title }, jpa::AlignCommand::Top, jpa::AlignReference::Page);
    EXPECT_NEAR(tp.title->pageBoundingRect().top(), 0.0, NEAR);
    delete tp.score;
}

TEST(Jubelle_PageAlignment, GroupKeepsItsSpacingOnThePage)
{
    TitlePage tp = makeTitlePage();
    ASSERT_TRUE(tp.score && tp.page);
    const RectF t0 = tp.title->pageBoundingRect();
    const RectF s0 = tp.subtitle->pageBoundingRect();
    const std::string before = music(tp.score);

    run(tp.score, { tp.title, tp.subtitle }, jpa::AlignCommand::Center, jpa::AlignReference::Page);
    const RectF t1 = tp.title->pageBoundingRect();
    const RectF s1 = tp.subtitle->pageBoundingRect();
    const RectF group(std::min(t1.left(), s1.left()), std::min(t1.top(), s1.top()),
                      std::max(t1.right(), s1.right()) - std::min(t1.left(), s1.left()),
                      std::max(t1.bottom(), s1.bottom()) - std::min(t1.top(), s1.top()));
    const RectF sheet = sheetOf(tp.page);
    EXPECT_NEAR(group.center().x(), sheet.width() / 2, NEAR);
    EXPECT_NEAR(group.center().y(), sheet.height() / 2, NEAR);
    // Relative spacing preserved
    EXPECT_NEAR(s1.left() - t1.left(), s0.left() - t0.left(), NEAR);
    EXPECT_NEAR(s1.top() - t1.top(), s0.top() - t0.top(), NEAR);
    EXPECT_EQ(music(tp.score), before);
    delete tp.score;
}

TEST(Jubelle_PageAlignment, EachOtherAndDistribute)
{
    TitlePage tp = makeTitlePage();
    ASSERT_TRUE(tp.score && tp.page);

    // Spread them out horizontally first
    tp.score->startCmd(muse::TranslatableString::untranslatable("drag"));
    tp.title->undoChangeProperty(Pid::OFFSET, muse::PointF(-60.0, 0.0));
    tp.subtitle->undoChangeProperty(Pid::OFFSET, muse::PointF(5.0, 0.0));
    tp.composer->undoChangeProperty(Pid::OFFSET, muse::PointF(-30.0, 0.0));
    tp.score->endCmd();
    tp.score->doLayout();

    // Align left edges to each other: the leftmost item stays put
    const double leftmost = std::min({ tp.title->pageBoundingRect().left(), tp.subtitle->pageBoundingRect().left(),
                                       tp.composer->pageBoundingRect().left() });
    run(tp.score, { tp.title, tp.subtitle, tp.composer }, jpa::AlignCommand::Left, jpa::AlignReference::EachOther);
    for (Text* t : { tp.title, tp.subtitle, tp.composer }) {
        EXPECT_NEAR(t->pageBoundingRect().left(), leftmost, NEAR);
    }

    // Distribute vertically: equal gaps, outer two unchanged
    std::vector<Text*> byTop { tp.title, tp.subtitle, tp.composer };
    std::sort(byTop.begin(), byTop.end(), [](Text* a, Text* b) { return a->pageBoundingRect().top() < b->pageBoundingRect().top(); });
    const double firstTop = byTop.front()->pageBoundingRect().top();
    const double lastTop = byTop.back()->pageBoundingRect().top();
    run(tp.score, { tp.title, tp.subtitle, tp.composer }, jpa::AlignCommand::DistributeVertically, jpa::AlignReference::Margins);
    EXPECT_NEAR(byTop.front()->pageBoundingRect().top(), firstTop, NEAR);
    EXPECT_NEAR(byTop.back()->pageBoundingRect().top(), lastTop, NEAR);
    const double gap1 = byTop[1]->pageBoundingRect().top() - byTop[0]->pageBoundingRect().bottom();
    const double gap2 = byTop[2]->pageBoundingRect().top() - byTop[1]->pageBoundingRect().bottom();
    EXPECT_NEAR(gap1, gap2, NEAR);

    // Two items aren't enough to distribute
    const jpa::AlignReport r = run(tp.score, { tp.title, tp.subtitle }, jpa::AlignCommand::DistributeHorizontally,
                                   jpa::AlignReference::Margins);
    EXPECT_FALSE(r.warnings.empty());
    delete tp.score;
}

TEST(Jubelle_PageAlignment, RefusesWhatTheMusicPlaces)
{
    TitlePage tp = makeTitlePage();
    ASSERT_TRUE(tp.score);
    Measure* bar1 = tp.score->firstMeasure();

    // A staff text on bar 1, automatically placed
    tp.score->startCmd(muse::TranslatableString::untranslatable("staff text"));
    Segment* seg = bar1->first(SegmentType::ChordRest);
    StaffText* st = Factory::createStaffText(seg);
    st->setParent(seg);
    st->setTrack(0);
    st->setXmlText(u"Lightly");
    tp.score->undoAddElement(st);
    tp.score->endCmd();
    tp.score->doLayout();

    EXPECT_TRUE(jpa::alignRefusal(bar1, false).startsWith(u"Bar 1 can't be aligned"));
    EXPECT_FALSE(jpa::alignRefusal(st, true).isEmpty()) << "no vertical moves for automatically placed text";
    EXPECT_TRUE(jpa::alignRefusal(st, false).isEmpty()) << "horizontal is fine";

    const muse::PointF titleOffset = tp.title->offset();
    const jpa::AlignReport r = run(tp.score, { bar1, st, tp.title }, jpa::AlignCommand::Top, jpa::AlignReference::Page);
    EXPECT_EQ(r.refused.size(), 2u);   // the bar and the staff text; the title moves
    EXPECT_EQ(r.moved, 1);
    EXPECT_NE(tp.title->offset(), titleOffset);
    EXPECT_EQ(bar1->offset(), muse::PointF());
    delete tp.score;
}
