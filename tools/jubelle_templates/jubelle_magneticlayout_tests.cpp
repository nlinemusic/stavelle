/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle G7: magnetic layout and collision avoidance, on the real engine.
 */
#include <gtest/gtest.h>

#include <cmath>
#include <iostream>
#include <map>
#include <memory>
#include <sstream>

#include "engraving/dom/chord.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/rehearsalmark.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/stafftext.h"
#include "engraving/dom/system.h"
#include "engraving/rendering/score/jubelle/magneticlayout.h"
#include "engraving/rendering/score/jubelle/overlapchecker.h"

#include "fontproviderstub.h"
#include "templatebuilder.h"

using namespace mu::engraving;
namespace jl = mu::engraving::jubelle;

namespace {
const std::string ROOT = std::string(MU_ROOT_DIR) + "/";

#define REQUIRE_REALISTIC() \
    if (!muse::draw::FontProviderStub::realisticMetrics()) { GTEST_SKIP() << "needs realistic metrics"; }

std::unique_ptr<MasterScore> bigBand()
{
    jubelle_templates::loadInstrumentData();
    return std::unique_ptr<MasterScore>(jubelle_templates::readTemplate(ROOT + "tools/jubelle_templates/testscores/Big_Band_test_music.mscz"));
}

std::unique_ptr<MasterScore> violinAndPiano()
{
    jubelle_templates::loadInstrumentData();
    return std::unique_ptr<MasterScore>(jubelle_templates::readTemplate(
                                            ROOT + "share/templates/09-Solo_with_Piano/01-Violin_+_Piano/01-Violin_+_Piano.mscx"));
}

void setStyle(MasterScore* s, Sid id, const PropertyValue& v)
{
    s->style().set(id, v);
    s->update();
    s->doLayout();
}

void allOff(MasterScore* s)
{
    s->style().set(Sid::jubelleMagneticLayout, false);
    s->style().set(Sid::jubelleCollisionAvoidance, false);
    s->update();
    s->doLayout();
}

//! every drawn item's type and page position, in score order
std::string layoutFingerprint(MasterScore* s)
{
    std::ostringstream out;
    out.precision(6);
    s->scanElements([&out](EngravingItem* item) {
        if (!item->ldata()->isSkipDraw()) {
            const PointF p = item->pagePos();
            out << item->typeName() << ' ' << std::fixed << p.x() << ' ' << p.y() << '\n';
        }
    });
    return out.str();
}

Dynamic* addDynamic(MasterScore* s, Segment* seg, staff_idx_t staff, DynamicType t)
{
    Dynamic* d = Factory::createDynamic(seg);
    d->setDynamicType(t);
    d->setParent(seg);
    d->setTrack(staff * VOICES);
    s->undoAddElement(d);
    return d;
}

//! the bar's first chord/rest segment
Segment* barStart(MasterScore* s, int bar)
{
    Measure* m = s->firstMeasure();
    for (int i = 0; i < bar && m; ++i) {
        m = m->nextMeasure();
    }
    return m ? m->first(SegmentType::ChordRest) : nullptr;
}

//! Violin: bars 1-3 with a dynamic each. Bars 1 and 2 have a low G (ledger
//! lines below, so their dynamics are pushed down), bar 3 a high note.
struct ThreeDynamics {
    std::unique_ptr<MasterScore> s;
    Dynamic* low1 = nullptr;
    Dynamic* low2 = nullptr;
    Dynamic* high = nullptr;

    ThreeDynamics()
    {
        s = violinAndPiano();
        if (!s) {
            return;
        }
        s->startCmd(muse::TranslatableString::untranslatable("magnetic test"));
        s->setNoteRest(barStart(s.get(), 0), 0, NoteVal(55), Fraction(1, 1));
        s->setNoteRest(barStart(s.get(), 1), 0, NoteVal(55), Fraction(1, 1));
        s->setNoteRest(barStart(s.get(), 2), 0, NoteVal(83), Fraction(1, 1));
        low1 = addDynamic(s.get(), barStart(s.get(), 0), 0, DynamicType::P);
        low2 = addDynamic(s.get(), barStart(s.get(), 1), 0, DynamicType::MF);
        high = addDynamic(s.get(), barStart(s.get(), 2), 0, DynamicType::F);
        s->endCmd();
        s->doLayout();
    }
};
}

TEST(Jubelle_MagneticLayout, NewSettingsAreOffByDefault)
{
    auto s = violinAndPiano();
    ASSERT_TRUE(s);
    EXPECT_FALSE(s->style().styleB(Sid::jubelleMagneticLayout));
    EXPECT_FALSE(s->style().styleB(Sid::jubelleCollisionAvoidance));
    EXPECT_DOUBLE_EQ(jl::nudgeLimitSp(s->style()), 0.5);
}

TEST(Jubelle_MagneticLayout, OffMeansIdentical)
{
    REQUIRE_REALISTIC();   // the big-band chart needs real text sizes to lay out
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->doLayout();
    const std::string musescore = layoutFingerprint(s.get());
    ASSERT_FALSE(musescore.empty());

    // the master switch on, but every behaviour off: still MuseScore's layout
    s->style().set(Sid::jubelleMagneticRows, false);
    s->style().set(Sid::jubelleSidewaysNudge, false);
    setStyle(s.get(), Sid::jubelleMagneticLayout, true);
    EXPECT_EQ(layoutFingerprint(s.get()), musescore);

    // everything on, then everything off again: back exactly (nothing creeps)
    s->style().set(Sid::jubelleMagneticRows, true);
    s->style().set(Sid::jubelleSidewaysNudge, true);
    setStyle(s.get(), Sid::jubelleCollisionAvoidance, true);
    allOff(s.get());
    EXPECT_EQ(layoutFingerprint(s.get()), musescore);
}

TEST(Jubelle_MagneticLayout, LayoutIsSteady)
{
    REQUIRE_REALISTIC();   // the big-band chart needs real text sizes to lay out
    // laying out again gives the same result (positions come from MuseScore's
    // defaults each time, never from the previous result)
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->style().set(Sid::jubelleMagneticLayout, true);
    s->style().set(Sid::jubelleCollisionAvoidance, true);
    s->update();
    s->doLayout();
    const std::string first = layoutFingerprint(s.get());
    s->update();
    s->doLayout();
    EXPECT_EQ(layoutFingerprint(s.get()), first);
}

TEST(Jubelle_MagneticLayout, DynamicsLineUpInARow)
{
    REQUIRE_REALISTIC();
    ThreeDynamics t;
    ASSERT_TRUE(t.s);
    const double sp = t.s->style().spatium();
    const double yLow1 = t.low1->pagePos().y();
    const double yHigh = t.high->pagePos().y();
    ASSERT_GT(yLow1 - yHigh, 0.5 * sp) << "the low note pushes its dynamic further down";

    setStyle(t.s.get(), Sid::jubelleMagneticLayout, true);
    // the dynamic under the high note joins the others' row
    EXPECT_NEAR(t.high->pagePos().y(), t.low1->pagePos().y(), 0.05 * sp);
    EXPECT_NEAR(t.low2->pagePos().y(), t.low1->pagePos().y(), 0.05 * sp);
    // and the ones that were pushed out didn't move
    EXPECT_NEAR(t.low1->pagePos().y(), yLow1, 0.01 * sp);

    // rows off: back to MuseScore's positions
    setStyle(t.s.get(), Sid::jubelleMagneticRows, false);
    EXPECT_NEAR(t.high->pagePos().y(), yHigh, 0.01 * sp);
}

TEST(Jubelle_MagneticLayout, AnItemOutsideTheRowStaysWhereItIsPushed)
{
    // only one dynamic is under a low note: the row is where most of them are
    // (at the staff), and the one pushed down stays down rather than pulling
    // the others away from the staff
    REQUIRE_REALISTIC();
    ThreeDynamics t;
    ASSERT_TRUE(t.s);
    t.s->startCmd(muse::TranslatableString::untranslatable("magnetic test"));
    t.s->setNoteRest(barStart(t.s.get(), 1), 0, NoteVal(83), Fraction(1, 1));
    t.s->endCmd();
    t.s->doLayout();
    const double sp = t.s->style().spatium();
    const double yLow1 = t.low1->pagePos().y();
    const double yHigh = t.high->pagePos().y();
    setStyle(t.s.get(), Sid::jubelleMagneticLayout, true);
    EXPECT_NEAR(t.low1->pagePos().y(), yLow1, 0.01 * sp);
    EXPECT_NEAR(t.high->pagePos().y(), yHigh, 0.01 * sp);
}

TEST(Jubelle_MagneticLayout, ManualPositionsAreNeverMoved)
{
    REQUIRE_REALISTIC();
    ThreeDynamics t;
    ASSERT_TRUE(t.s);
    const double sp = t.s->style().spatium();
    // the user dragged the high-note dynamic a little
    t.high->setOffset(t.high->offset() + PointF(0.3 * sp, 0.0));
    t.s->doLayout();
    const double yHigh = t.high->pagePos().y();

    setStyle(t.s.get(), Sid::jubelleMagneticLayout, true);
    EXPECT_NEAR(t.high->pagePos().y(), yHigh, 0.01 * sp);

    // automatic placement unticked: never moved either
    t.high->setOffset(t.high->propertyDefault(Pid::OFFSET).value<PointF>());
    t.high->setAutoplace(false);
    allOff(t.s.get());
    const double yFixed = t.high->pagePos().y();
    setStyle(t.s.get(), Sid::jubelleMagneticLayout, true);
    EXPECT_NEAR(t.high->pagePos().y(), yFixed, 0.01 * sp);
}

TEST(Jubelle_MagneticLayout, SidewaysNudgingStaysWithinTheLimitAndNeverPushesFurther)
{
    REQUIRE_REALISTIC();
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->doLayout();
    const double sp = s->style().spatium();

    std::map<EngravingItem*, PointF> before;
    s->scanElements([&before](EngravingItem* item) {
        if (item->isDynamic() || item->isExpression() || item->isStaffText()) {
            before[item] = item->pagePos();
        }
    });
    ASSERT_FALSE(before.empty());

    s->style().set(Sid::jubelleMagneticRows, false);   // nudging alone
    setStyle(s.get(), Sid::jubelleMagneticLayout, true);

    int nudged = 0;
    for (const auto& [item, p] : before) {
        const PointF now = item->pagePos();
        const double dx = std::abs(now.x() - p.x());
        EXPECT_LE(dx, 0.5 * sp + 1e-6) << item->typeName();
        if (item->isHarmony()) {
            EXPECT_LE(dx, 1e-6) << "chord symbols are never nudged";
        }
        if (dx > 1e-6) {
            ++nudged;
            // a nudged item ends up no further from its staff than before
            const bool above = item->placeAbove();
            const double fartherBy = above ? p.y() - now.y() : now.y() - p.y();
            EXPECT_LE(fartherBy, 1e-6) << item->typeName();
        }
    }
    std::cout << "[ Magnetic ] Big Band chart: " << nudged << " of " << before.size() << " text items nudged sideways\n";
}

TEST(Jubelle_MagneticLayout, CollisionAvoidanceMovesTheLowerPriorityItem)
{
    REQUIRE_REALISTIC();
    auto s = violinAndPiano();
    ASSERT_TRUE(s);
    Segment* seg = barStart(s.get(), 0);
    s->startCmd(muse::TranslatableString::untranslatable("collision test"));
    s->setNoteRest(seg, 0, NoteVal(76), Fraction(1, 1));
    StaffText* t = Factory::createStaffText(seg);
    t->setXmlText(u"espressivo");
    t->setParent(seg);
    t->setTrack(0);
    t->setAutoplace(false);                   // a fixed obstacle
    s->undoAddElement(t);
    RehearsalMark* r = Factory::createRehearsalMark(seg);
    r->setXmlText(u"A");
    r->setParent(seg);
    r->setTrack(0);
    s->undoAddElement(r);
    s->endCmd();
    s->doLayout();
    // let the rehearsal mark sit on the text (a negative minimum distance)
    r->setMinDistance(Spatium(-20.0));
    t->setOffset(t->offset() + (r->pagePos() - t->pagePos()));
    s->doLayout();

    auto overlapsWith = [&s](const EngravingItem* item) {
        int n = 0;
        for (const jl::Overlap& o : jl::findOverlaps(s.get())) {
            n += (o.moves == item || o.stays == item) ? 1 : 0;
        }
        return n;
    };
    ASSERT_GT(overlapsWith(r), 0) << "test set-up: the two overlap";

    setStyle(s.get(), Sid::jubelleCollisionAvoidance, true);
    EXPECT_EQ(overlapsWith(r), 0);
    EXPECT_EQ(overlapsWith(t), 0);

    // the text (higher priority, and fixed) stayed where it was put in its bar
    // (the whole system may sit lower on the page: the mark above needs room)
    const PointF textAt = t->pos();
    const double markWas = r->pos().y();
    allOff(s.get());
    EXPECT_NEAR(t->pos().x(), textAt.x(), 1e-6);
    EXPECT_NEAR(t->pos().y(), textAt.y(), 1e-6);
    EXPECT_GT(r->pos().y(), markWas) << "the rehearsal mark was the one moved (up)";
}

TEST(Jubelle_MagneticLayout, EverythingOnLeavesNoMoreOverlapsInTheBigBandChart)
{
    REQUIRE_REALISTIC();
    auto s = bigBand();
    ASSERT_TRUE(s);
    s->doLayout();
    const size_t off = jl::findOverlaps(s.get()).size();
    s->style().set(Sid::jubelleCollisionAvoidance, true);
    setStyle(s.get(), Sid::jubelleMagneticLayout, true);
    const size_t on = jl::findOverlaps(s.get()).size();
    std::cout << "[ Magnetic ] Big Band chart overlaps: " << off << " off, " << on << " on\n";
    EXPECT_LE(on, off);
}

// ---------------------------------------------------------------------------
// G7 step 2: engraving rules
// ---------------------------------------------------------------------------
#include "engraving/rendering/score/jubelle/engravingrules.h"

TEST(Jubelle_EngravingRules, StoredTextRoundTripsAndSkipsWhatIsntARule)
{
    const muse::String text = u"dynamics group:vocals above\n"
                              u"# a comment\n"
                              u"chordsymbols all default 1.5\n"
                              u"nonsense line here\n"
                              u"dynamics somewhere above\n"
                              u"lyrics instrument:soprano below";
    const auto rules = jl::parseRules(text);
    ASSERT_EQ(rules.size(), 3u);
    EXPECT_EQ(rules[0].kind, jl::RuleKind::Dynamics);
    EXPECT_EQ(rules[0].scope, u"group:vocals");
    EXPECT_EQ(rules[0].placement, jl::RulePlacement::Above);
    EXPECT_FALSE(rules[0].minDistanceSp.has_value());
    ASSERT_TRUE(rules[1].minDistanceSp.has_value());
    EXPECT_DOUBLE_EQ(*rules[1].minDistanceSp, 1.5);
    EXPECT_EQ(jl::parseRules(jl::formatRules(rules)), rules);
    EXPECT_TRUE(jl::parseRules(u"").empty());
}

TEST(Jubelle_EngravingRules, TheMostSpecificRuleWins)
{
    const auto rules = jl::parseRules(u"dynamics all below\n"
                                      u"dynamics group:vocals above\n"
                                      u"dynamics instrument:soprano below\n"
                                      u"dynamics all above");
    const jl::EngravingRule* r = jl::ruleFor(rules, jl::RuleKind::Dynamics, u"brass", u"trumpet");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->placement, jl::RulePlacement::Above) << "between equals, the later rule wins";
    r = jl::ruleFor(rules, jl::RuleKind::Dynamics, u"vocals", u"alto");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->scope, u"group:vocals");
    r = jl::ruleFor(rules, jl::RuleKind::Dynamics, u"vocals", u"soprano");
    ASSERT_TRUE(r);
    EXPECT_EQ(r->placement, jl::RulePlacement::Below);
    EXPECT_EQ(jl::ruleFor(rules, jl::RuleKind::Lyrics, u"vocals", u"soprano"), nullptr);
}

TEST(Jubelle_EngravingRules, PresetsFollowBehindBars)
{
    EXPECT_TRUE(jl::presetRules(u"standard").empty());
    for (const char16_t* id : { u"jazz", u"choral", u"orchestral" }) {
        const auto rules = jl::presetRules(id);
        ASSERT_FALSE(rules.empty()) << muse::String(id).toStdString();
        // vocal dynamics go above the staff in every preset that sets them
        const jl::EngravingRule* r = jl::ruleFor(rules, jl::RuleKind::Dynamics, u"vocals", u"soprano");
        ASSERT_TRUE(r);
        EXPECT_EQ(r->placement, jl::RulePlacement::Above);
        EXPECT_EQ(jl::parseRules(jl::formatRules(rules)), rules);
    }
}

TEST(Jubelle_EngravingRules, ARulePlacesMarkingsAndRemovingItRestoresMuseScores)
{
    REQUIRE_REALISTIC();
    ThreeDynamics t;
    ASSERT_TRUE(t.s);
    ASSERT_FALSE(t.high->placeAbove());
    const double yBelow = t.high->pagePos().y();

    // violin is in the "strings" group
    setStyle(t.s.get(), Sid::jubelleEngravingRules, muse::String(u"dynamics group:strings above"));
    EXPECT_TRUE(t.high->placeAbove());
    EXPECT_TRUE(t.low1->placeAbove());
    EXPECT_LT(t.high->pagePos().y(), yBelow - t.s->style().spatium());
    // the rule changes nothing in the music: the direction is still automatic
    EXPECT_EQ(t.high->getProperty(Pid::DIRECTION).value<DirectionV>(), DirectionV::AUTO);

    // a rule for another group doesn't apply
    setStyle(t.s.get(), Sid::jubelleEngravingRules, muse::String(u"dynamics group:vocals above"));
    EXPECT_FALSE(t.high->placeAbove());
    EXPECT_NEAR(t.high->pagePos().y(), yBelow, 0.01 * t.s->style().spatium());

    // rules removed: exactly MuseScore's placement again
    setStyle(t.s.get(), Sid::jubelleEngravingRules, muse::String(u"dynamics all above"));
    t.s->style().set(Sid::jubelleEngravingRules, muse::String());
    t.s->styleChanged();
    t.s->update();
    t.s->doLayout();
    EXPECT_FALSE(t.high->placeAbove());
    EXPECT_NEAR(t.high->pagePos().y(), yBelow, 0.01 * t.s->style().spatium());
}

TEST(Jubelle_EngravingRules, PlacementSetByHandWins)
{
    REQUIRE_REALISTIC();
    ThreeDynamics t;
    ASSERT_TRUE(t.s);
    // Properties › Dynamics: "below" chosen by hand (MuseScore 4 keeps it as the direction)
    t.high->setProperty(Pid::DIRECTION, PropertyValue(DirectionV::DOWN));
    setStyle(t.s.get(), Sid::jubelleEngravingRules, muse::String(u"dynamics all above 2"));
    EXPECT_FALSE(t.high->placeAbove()) << "set by hand in Properties";
    EXPECT_TRUE(t.low2->placeAbove());
    EXPECT_DOUBLE_EQ(t.low2->minDistance().val(), 2.0);
}
