/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

// Jubelle fonts (F1, T2): every bundled music font loads in the engine, and a
// real score can switch font and back without anything in the music changing
// ("change the music font without rebuilding the score").
#include <gtest/gtest.h>

#include <memory>

#include "engraving/dom/chord.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/segment.h"
#include "engraving/iengravingfontsprovider.h"
#include "engraving/style/style.h"
#include "modularity/ioc.h"

#include "fontproviderstub.h"
#include "templatebuilder.h"

using namespace mu::engraving;
using muse::String;

namespace {
const std::string ROOT = std::string(MU_ROOT_DIR) + "/";
const std::vector<const char*> FONTS { "Leland", "Bravura", "MuseJazz", "Petaluma", "Sebastian", "Eugene", "Leipzig" };

struct Snapshot {
    size_t notes = 0;
    long long pitchSum = 0;
    size_t measures = 0;
};

Snapshot snapshot(const MasterScore* s)
{
    Snapshot r;
    r.measures = s->nmeasures();
    for (const Segment* seg = s->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (const EngravingItem* e : seg->elist()) {
            if (e && e->isChord()) {
                for (const Note* n : toChord(e)->notes()) {
                    ++r.notes;
                    r.pitchSum += n->pitch();
                }
            }
        }
    }
    return r;
}
}

TEST(Jubelle_TemplateLayout, EveryBundledMusicFontLoadsAndLaysOut)
{
    if (!muse::draw::FontProviderStub::realisticMetrics()) {
        GTEST_SKIP() << "needs realistic metrics";
    }
    auto fonts = muse::modularity::globalIoc()->resolve<IEngravingFontsProvider>("jubelle_engine_tests");
    ASSERT_TRUE(fonts);
    jubelle_templates::loadInstrumentData();
    std::unique_ptr<MasterScore> s(jubelle_templates::readTemplate(ROOT + "tools/jubelle_templates/testscores/Big_Band_test_music.mscz"));
    ASSERT_TRUE(s);
    for (const char* name : FONTS) {
        IEngravingFontPtr f = fonts->fontByName(name);
        ASSERT_TRUE(f) << name;
        EXPECT_EQ(f->name(), std::string(name)) << name << " is loaded, not replaced by the fallback";
        s->style().set(Sid::musicalSymbolFont, String::fromAscii(name));
        s->setEngravingFont(fonts->fontByName(name));
        s->setLayoutAll();
        s->doLayout();
        EXPECT_FALSE(s->pages().empty()) << name;
        // a notehead has a real size in this font
        const Chord* c = nullptr;
        for (const Segment* seg = s->firstSegment(SegmentType::ChordRest); seg && !c; seg = seg->next1(SegmentType::ChordRest)) {
            if (seg->element(0) && seg->element(0)->isChord()) {
                c = toChord(seg->element(0));
            }
        }
        ASSERT_TRUE(c);
        EXPECT_GT(c->upNote()->ldata()->bbox().width(), 0.0) << name;
    }
}

TEST(Jubelle_TemplateLayout, SwitchingTheFontChangesNothingInTheMusic)
{
    if (!muse::draw::FontProviderStub::realisticMetrics()) {
        GTEST_SKIP() << "needs realistic metrics";
    }
    jubelle_templates::loadInstrumentData();
    std::unique_ptr<MasterScore> s(jubelle_templates::readTemplate(ROOT + "tools/jubelle_templates/testscores/Big_Band_test_music.mscz"));
    ASSERT_TRUE(s);
    const Snapshot before = snapshot(s.get());
    ASSERT_GT(before.notes, 500u);

    const std::string path = "/tmp/jubelle_font_switch.mscz";
    for (const char* name : { "MuseJazz", "Sebastian", "Leland" }) {
        s->startCmd(muse::TranslatableString::untranslatable("font"));
        s->undoChangeStyleVal(Sid::musicalSymbolFont, String::fromAscii(name));
        s->endCmd();
        ASSERT_TRUE(jubelle_templates::writeMscz(s.get(), path));
        std::unique_ptr<MasterScore> reopened(jubelle_templates::readTemplate(path));
        ASSERT_TRUE(reopened) << name;
        EXPECT_EQ(reopened->style().styleSt(Sid::musicalSymbolFont), String::fromAscii(name)) << "the choice is saved";
        const Snapshot after = snapshot(reopened.get());
        EXPECT_EQ(after.notes, before.notes) << name;
        EXPECT_EQ(after.pitchSum, before.pitchSum) << name;
        EXPECT_EQ(after.measures, before.measures) << name;
    }
    // and undo goes back
    s->undoRedo(true, nullptr);
    EXPECT_EQ(s->style().styleSt(Sid::musicalSymbolFont), String(u"Sebastian"));
}
