/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle G4: custom timeline markers, on the real engine.
 */
#include <gtest/gtest.h>

#include <memory>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/stafftext.h"
#include "notation/internal/jubelle/timelinemarkers.h"

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
}

TEST(Jubelle_TimelineMarkers, AddRenameMoveDelete)
{
    auto s = score();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("marker"));
    StaffText* a = jt::addTimelineMarker(s.get(), bar(s.get(), 3)->tick(), u"Bridge");
    StaffText* b = jt::addTimelineMarker(s.get(), bar(s.get(), 1)->tick(), u"");
    s->endCmd();
    ASSERT_TRUE(a && b);
    EXPECT_FALSE(a->visible()) << "never printed or exported";

    auto markers = jt::timelineMarkers(s.get());
    ASSERT_EQ(markers.size(), 2u);
    EXPECT_EQ(markers[0].name, u"Marker 1");   // score order: bar 1 first
    EXPECT_EQ(markers[1].name, u"Bridge");
    EXPECT_EQ(markers[1].tick, bar(s.get(), 3)->tick());

    s->startCmd(muse::TranslatableString::untranslatable("rename"));
    jt::renameTimelineMarker(s.get(), a, u"Shout chorus");
    s->endCmd();
    EXPECT_EQ(jt::markerName(a), u"Shout chorus");

    s->startCmd(muse::TranslatableString::untranslatable("move"));
    StaffText* moved = jt::moveTimelineMarker(s.get(), a, bar(s.get(), 5)->tick());
    s->endCmd();
    ASSERT_TRUE(moved);
    markers = jt::timelineMarkers(s.get());
    ASSERT_EQ(markers.size(), 2u);
    EXPECT_EQ(markers[1].tick, bar(s.get(), 5)->tick());
    EXPECT_EQ(markers[1].name, u"Shout chorus");

    s->startCmd(muse::TranslatableString::untranslatable("delete"));
    jt::removeTimelineMarker(s.get(), b);
    s->endCmd();
    EXPECT_EQ(jt::timelineMarkers(s.get()).size(), 1u);

    // undo brings the deleted marker back
    s->undoRedo(true, nullptr);
    EXPECT_EQ(jt::timelineMarkers(s.get()).size(), 2u);
}

TEST(Jubelle_TimelineMarkers, MarkersMoveWithTheirBarWhenBarsAreInserted)
{
    auto s = score();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("marker"));
    jt::addTimelineMarker(s.get(), bar(s.get(), 3)->tick(), u"Solos");
    s->endCmd();
    const Fraction barLen = bar(s.get(), 1)->ticks();

    s->startCmd(muse::TranslatableString::untranslatable("insert"));
    s->insertMeasure(bar(s.get(), 1));   // a new bar before bar 1
    s->endCmd();
    const auto markers = jt::timelineMarkers(s.get());
    ASSERT_EQ(markers.size(), 1u);
    EXPECT_EQ(markers[0].tick, barLen * 3) << "still at the start of the same music, now bar 4";
}

TEST(Jubelle_TimelineMarkers, ANamesWithSpecialCharactersRoundTrip)
{
    auto s = score();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("marker"));
    StaffText* t = jt::addTimelineMarker(s.get(), bar(s.get(), 2)->tick(), u"Intro <soft> & slow");
    s->endCmd();
    EXPECT_EQ(jt::markerName(t), u"Intro <soft> & slow");
}
