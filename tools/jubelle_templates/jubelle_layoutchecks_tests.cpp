/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

// Jubelle G7 step 1: the overlap checker, on the real engine with real symbol
// sizes (second pass of the engine tool).
#include <gtest/gtest.h>

#include <iostream>
#include <map>
#include <memory>

#include "engraving/dom/chord.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/stafftext.h"
#include "engraving/dom/rehearsalmark.h"
#include "engraving/dom/system.h"
#include "engraving/rendering/score/jubelle/overlapchecker.h"

#include "fontproviderstub.h"
#include "templatebuilder.h"

using namespace mu::engraving;
namespace jl = mu::engraving::jubelle;

namespace {
const std::string ROOT = std::string(MU_ROOT_DIR) + "/";

#define REQUIRE_REALISTIC() \
    if (!muse::draw::FontProviderStub::realisticMetrics()) { GTEST_SKIP() << "needs realistic metrics"; }

std::unique_ptr<MasterScore> violinAndPiano()
{
    jubelle_templates::loadInstrumentData();
    return std::unique_ptr<MasterScore>(jubelle_templates::readTemplate(
                                            ROOT + "share/templates/09-Solo_with_Piano/01-Violin_+_Piano/01-Violin_+_Piano.mscx"));
}

template<typename T>
T* addFixed(MasterScore* s, Segment* seg, T* item)
{
    item->setParent(seg);
    item->setTrack(0);
    item->setAutoplace(false);          // stays exactly where it is put
    s->undoAddElement(item);
    return item;
}
}

TEST(Jubelle_TemplateLayout, OverlapCheckerFindsAndRanksOverlaps)
{
    REQUIRE_REALISTIC();
    auto s = violinAndPiano();
    ASSERT_TRUE(s);
    Segment* seg = s->firstMeasure()->first(SegmentType::ChordRest);
    s->startCmd(muse::TranslatableString::untranslatable("overlap test"));
    s->setNoteRest(seg, 0, NoteVal(76), Fraction(1, 1));
    StaffText* t = Factory::createStaffText(seg);
    t->setXmlText(u"espressivo");
    addFixed(s.get(), seg, t);
    RehearsalMark* r = Factory::createRehearsalMark(seg);
    r->setXmlText(u"A");
    addFixed(s.get(), seg, r);
    s->endCmd();
    s->doLayout();
    // put the rehearsal mark right on the text (both fixed: automatic placement off)
    r->setOffset(r->offset() + (t->pagePos() - r->pagePos()));
    s->doLayout();
    ASSERT_GT(jl::checkedItemCount(s->systems().back()), 0u);

    bool found = false;
    for (const jl::Overlap& o : jl::findOverlaps(s.get())) {
        if (o.stays == t && o.moves == r) {
            found = true;   // priority table: text stays, the rehearsal mark moves
        }
        EXPECT_FALSE(o.stays == r && o.moves == t);
    }
    EXPECT_TRUE(found);

    // Moved apart: no overlap
    r->setOffset(r->offset() + PointF(0, -10 * s->style().spatium()));
    s->doLayout();
    for (const jl::Overlap& o : jl::findOverlaps(s.get())) {
        EXPECT_NE(o.moves, r);
    }

    // Invisible items don't count
    r->setOffset(r->offset() + PointF(0, 10 * s->style().spatium()));
    r->setVisible(false);
    s->doLayout();
    for (const jl::Overlap& o : jl::findOverlaps(s.get())) {
        EXPECT_NE(o.moves, r);
    }
}

TEST(Jubelle_TemplateLayout, OverlapCheckerIgnoresMarkingsOnTheirOwnChord)
{
    REQUIRE_REALISTIC();
    auto s = violinAndPiano();
    ASSERT_TRUE(s);
    Segment* seg = s->firstMeasure()->first(SegmentType::ChordRest);
    s->startCmd(muse::TranslatableString::untranslatable("overlap test"));
    s->setNoteRest(seg, 0, NoteVal(79), Fraction(1, 4));
    Chord* c = toChord(seg->element(0));
    for (SymId id : { SymId::articAccentAbove, SymId::articStaccatoAbove, SymId::articTenutoAbove }) {
        Articulation* a = Factory::createArticulation(c);
        a->setSymId(id);
        a->setParent(c);
        a->setTrack(0);
        s->undoAddElement(a);
    }
    s->endCmd();
    s->doLayout();
    for (const jl::Overlap& o : jl::findOverlaps(s.get())) {
        EXPECT_FALSE(o.stays->isNote() && o.moves->isArticulation()) << "an articulation on its own chord isn't a collision";
    }
}

TEST(Jubelle_TemplateLayout, OverlapReportForTheBigBandChart)
{
    // Diagnostic: what MuseScore's own placement leaves overlapping in real
    // big-band writing. This is the starting point G7's collision avoidance
    // is measured against.
    REQUIRE_REALISTIC();
    jubelle_templates::loadInstrumentData();
    std::unique_ptr<MasterScore> s(jubelle_templates::readTemplate(ROOT + "tools/jubelle_templates/testscores/Big_Band_test_music.mscz"));
    ASSERT_TRUE(s);
    const std::vector<jl::Overlap> found = jl::findOverlaps(s.get());
    std::map<std::string, int> byKind;
    for (const jl::Overlap& o : found) {
        byKind[std::string(o.stays->typeName()) + " / " + o.moves->typeName()]++;
    }
    std::cout << "[ Overlaps ] Big Band chart: " << found.size() << " (text widths estimated)\n";
    for (const auto& [kind, n] : byKind) {
        std::cout << "[ Overlaps ]   " << kind << ": " << n << "\n";
    }
    SUCCEED();
}
