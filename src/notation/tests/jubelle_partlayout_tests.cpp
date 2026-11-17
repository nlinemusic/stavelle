/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

#include <gtest/gtest.h>

#include "engraving/dom/chord.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/note.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/dom/rest.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/spacer.h"
#include "engraving/dom/systemlock.h"
#include "engraving/editing/editsystemlocks.h"

#include "notation/internal/jubelle/partlayout.h"

#include "utils/scorerw.h"

using namespace mu::engraving;
namespace jpl = mu::notation::jubelle;

//! Jubelle Phase 5E: Apply Part Layout, on the real notation engine.
//! partlayout.mscx: 16 bars; Trumpet plays throughout, Alto Sax rests in bars
//! 9–12, Piano has two staves. Each becomes a part.
namespace {
struct Parts {
    MasterScore* master = nullptr;
    Score* trumpet = nullptr;
    Score* sax = nullptr;
    Score* piano = nullptr;
};

Parts loadParts()
{
    Parts p;
    const muse::String path = muse::String::fromUtf8(std::string(MU_ROOT_DIR) + "/src/notation/tests/jubelle_chordid_data/partlayout.mscx");
    p.master = ScoreRW::readScore(path, true);
    if (!p.master) {
        return p;
    }
    for (Excerpt* ex : Excerpt::createExcerptsFromParts(p.master->parts(), p.master)) {
        p.master->initAndAddExcerpt(ex, false);
    }
    for (Excerpt* ex : p.master->excerpts()) {
        const muse::String name = ex->excerptScore()->parts().front()->partName();
        if (name == u"Trumpet") {
            p.trumpet = ex->excerptScore();
        } else if (name == u"Alto Sax") {
            p.sax = ex->excerptScore();
        } else if (name == u"Piano") {
            p.piano = ex->excerptScore();
        }
    }
    return p;
}

Measure* bar(Score* s, int n)
{
    Measure* m = s->firstMeasure();
    for (int i = 1; i < n && m; ++i) {
        m = m->nextMeasure();
    }
    return m;
}

std::vector<int> barsWith(Score* s, bool (MeasureBase::*has)() const)
{
    std::vector<int> out;
    for (Measure* m = s->firstMeasure(); m; m = m->nextMeasure()) {
        if ((m->*has)()) {
            out.push_back(m->no() + 1);
        }
    }
    return out;
}

//! A fingerprint of the music: every chord and rest with its notes
std::string music(Score* s)
{
    std::string out;
    for (Segment* seg = s->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        for (track_idx_t t = 0; t < s->ntracks(); ++t) {
            EngravingItem* e = seg->element(t);
            if (!e) {
                continue;
            }
            out += std::to_string(seg->tick().ticks()) + ":" + std::to_string(t) + ":" + toChordRest(e)->ticks().toString().toStdString();
            if (e->isChord()) {
                for (const Note* n : toChord(e)->notes()) {
                    out += "/" + std::to_string(n->pitch());
                }
            }
            out += ";";
        }
    }
    return out;
}

void formatTrumpet(Parts& p)
{
    Score* t = p.trumpet;
    t->startCmd(muse::TranslatableString::untranslatable("format"));
    bar(t, 4)->undoSetBreak(true, LayoutBreakType::LINE);
    bar(t, 8)->undoSetBreak(true, LayoutBreakType::PAGE);
    bar(t, 12)->undoSetBreak(true, LayoutBreakType::LINE);
    bar(t, 13)->undoSetBreak(true, LayoutBreakType::NOBREAK);
    bar(t, 2)->undoChangeProperty(Pid::USER_STRETCH, 1.5);
    Spacer* sp = Factory::createSpacer(bar(t, 3));
    sp->setSpacerType(SpacerType::DOWN);
    sp->setGap(Spatium(6));
    sp->setTrack(0);
    sp->setParent(bar(t, 3));
    t->undoAddElement(sp, false);
    EditSystemLocks::undoAddSystemLock(t, new SystemLock(bar(t, 5), bar(t, 7)));
    t->endCmd();

    // The sax part has its own break, which must be replaced
    p.sax->startCmd(muse::TranslatableString::untranslatable("format"));
    bar(p.sax, 2)->undoSetBreak(true, LayoutBreakType::LINE);
    p.sax->endCmd();
}

std::vector<int> v(std::initializer_list<int> l) { return l; }
}

TEST(Jubelle_PartLayout, CopiesLayoutReplacesOldAndLeavesMusicAlone)
{
    Parts p = loadParts();
    ASSERT_TRUE(p.master && p.trumpet && p.sax && p.piano);
    formatTrumpet(p);
    const std::string masterMusic = music(p.master), saxMusic = music(p.sax), pianoMusic = music(p.piano);
    const std::string trumpetMusic = music(p.trumpet);

    jpl::PartLayoutOptions o;
    const jpl::PartLayoutCheck pianoCheck = jpl::checkPartLayout(p.trumpet, p.piano, o);
    EXPECT_EQ(pianoCheck.issues.size(), 1u);   // spacers skipped: 2 staves

    p.master->startCmd(muse::TranslatableString::untranslatable("Apply part layout"));
    const auto reports = jpl::applyPartLayout(p.trumpet, { p.sax, p.piano }, o);
    p.master->endCmd();
    ASSERT_EQ(reports.size(), 2u);

    for (Score* dest : { p.sax, p.piano }) {
        EXPECT_EQ(barsWith(dest, &MeasureBase::lineBreak), v({ 4, 12 }));   // bar 2's old break is gone
        EXPECT_EQ(barsWith(dest, &MeasureBase::pageBreak), v({ 8 }));
        EXPECT_EQ(barsWith(dest, &MeasureBase::noBreak), v({ 13 }));
        EXPECT_DOUBLE_EQ(bar(dest, 2)->userStretch(), 1.5);
        const SystemLock* lock = dest->systemLocks()->lockStartingAt(bar(dest, 5));
        ASSERT_TRUE(lock);
        EXPECT_EQ(lock->endMB(), bar(dest, 7));
    }
    ASSERT_TRUE(bar(p.sax, 3)->vspacerDown(0));
    EXPECT_EQ(bar(p.sax, 3)->vspacerDown(0)->gap(), Spatium(6));
    EXPECT_FALSE(bar(p.piano, 3)->vspacerDown(0));
    EXPECT_TRUE(reports[1].spacersSkipped);
    // Bars 4 and 12, plus the break after the part's title frame (MuseScore
    // gives every part one), which the destination's title frame receives
    EXPECT_EQ(reports[0].systemBreaks, 3);
    EXPECT_TRUE(p.sax->first()->isVBox() && p.sax->first()->lineBreak());
    EXPECT_EQ(reports[0].pageBreaks, 1);
    EXPECT_EQ(reports[0].keepTogether, 1);

    // The full score and the source part keep their own layout; no music changed anywhere
    EXPECT_TRUE(barsWith(p.master, &MeasureBase::lineBreak).empty());
    EXPECT_TRUE(barsWith(p.master, &MeasureBase::pageBreak).empty());
    EXPECT_DOUBLE_EQ(bar(p.master, 2)->userStretch(), 1.0);
    EXPECT_FALSE(bar(p.master, 3)->vspacerDown(0));
    EXPECT_EQ(barsWith(p.trumpet, &MeasureBase::lineBreak), v({ 4, 12 }));
    EXPECT_EQ(music(p.master), masterMusic);
    EXPECT_EQ(music(p.trumpet), trumpetMusic);
    EXPECT_EQ(music(p.sax), saxMusic);
    EXPECT_EQ(music(p.piano), pianoMusic);

    // One undo restores every part; redo applies again
    p.master->undoRedo(true, nullptr);
    EXPECT_EQ(barsWith(p.sax, &MeasureBase::lineBreak), v({ 2 }));
    EXPECT_TRUE(barsWith(p.piano, &MeasureBase::lineBreak).empty());
    EXPECT_DOUBLE_EQ(bar(p.sax, 2)->userStretch(), 1.0);
    EXPECT_FALSE(bar(p.sax, 3)->vspacerDown(0));
    EXPECT_FALSE(p.sax->systemLocks()->lockStartingAt(bar(p.sax, 5)));
    p.master->undoRedo(false, nullptr);
    EXPECT_EQ(barsWith(p.sax, &MeasureBase::lineBreak), v({ 4, 12 }));
    EXPECT_EQ(barsWith(p.piano, &MeasureBase::pageBreak), v({ 8 }));
    delete p.master;
}

TEST(Jubelle_PartLayout, OptionsLeaveTheDestinationUnchanged)
{
    Parts p = loadParts();
    ASSERT_TRUE(p.master && p.trumpet && p.sax && p.piano);
    formatTrumpet(p);

    // The source has different page settings
    p.trumpet->startCmd(muse::TranslatableString::untranslatable("style"));
    p.trumpet->undoChangeStyleVal(Sid::spatium, 2.0);
    p.trumpet->undoChangeStyleVal(Sid::minSystemDistance, Spatium(12.0));
    p.trumpet->endCmd();
    const PropertyValue saxSpatium = p.sax->style().styleV(Sid::spatium);

    jpl::PartLayoutOptions o;
    o.systemLocks = false;
    o.measureWidths = false;
    o.spacers = false;
    o.pageAndSpacing = false;
    const jpl::PartLayoutCheck check = jpl::checkPartLayout(p.trumpet, p.sax, o);
    EXPECT_FALSE(check.compatible);   // the staff size differs
    ASSERT_FALSE(check.issues.empty());

    p.master->startCmd(muse::TranslatableString::untranslatable("Apply part layout"));
    jpl::applyPartLayout(p.trumpet, { p.sax }, o);
    p.master->endCmd();
    EXPECT_EQ(barsWith(p.sax, &MeasureBase::lineBreak), v({ 4, 12 }));   // breaks always
    EXPECT_DOUBLE_EQ(bar(p.sax, 2)->userStretch(), 1.0);                 // widths: off
    EXPECT_FALSE(bar(p.sax, 3)->vspacerDown(0));                         // spacers: off
    EXPECT_FALSE(p.sax->systemLocks()->lockStartingAt(bar(p.sax, 5)));   // locks: off
    EXPECT_EQ(p.sax->style().styleV(Sid::spatium), saxSpatium);          // page settings: off

    // With page and spacing settings on, they arrive (and only in the sax part)
    o.pageAndSpacing = true;
    p.master->startCmd(muse::TranslatableString::untranslatable("Apply part layout"));
    jpl::applyPartLayout(p.trumpet, { p.sax }, o);
    p.master->endCmd();
    EXPECT_EQ(p.sax->style().styleV(Sid::spatium), p.trumpet->style().styleV(Sid::spatium));
    EXPECT_EQ(p.sax->style().styleV(Sid::minSystemDistance), p.trumpet->style().styleV(Sid::minSystemDistance));
    EXPECT_NE(p.piano->style().styleV(Sid::spatium), p.trumpet->style().styleV(Sid::spatium));
    delete p.master;
}

TEST(Jubelle_PartLayout, MultiMeasureRestsAreReportedAndCanBeSkipped)
{
    Parts p = loadParts();
    ASSERT_TRUE(p.master && p.trumpet && p.sax);

    // A break after bar 10 in the trumpet part; the sax rests in bars 9–12
    p.trumpet->startCmd(muse::TranslatableString::untranslatable("format"));
    bar(p.trumpet, 10)->undoSetBreak(true, LayoutBreakType::LINE);
    p.trumpet->endCmd();
    p.sax->startCmd(muse::TranslatableString::untranslatable("mmrests"));
    p.sax->undoChangeStyleVal(Sid::createMultiMeasureRests, true);
    p.sax->endCmd();
    p.sax->doLayout();
    ASSERT_TRUE(bar(p.sax, 10)->coveringMMRestOrThis() != bar(p.sax, 10)) << "bars 9-12 are a multi-measure rest in the sax part";

    jpl::PartLayoutOptions o;
    o.pageAndSpacing = false;   // keep the sax part's multi-measure rests
    const jpl::PartLayoutCheck check = jpl::checkPartLayout(p.trumpet, p.sax, o);
    EXPECT_FALSE(check.compatible);
    bool mentioned = false;
    for (const muse::String& s : check.issues) {
        mentioned = mentioned || s.contains(u"Bars 9–12 are a multi-measure rest");
    }
    EXPECT_TRUE(mentioned);

    // Default: copied anyway (warned); option off: skipped
    o.breaksInsideMMRests = false;
    p.master->startCmd(muse::TranslatableString::untranslatable("Apply part layout"));
    const auto reports = jpl::applyPartLayout(p.trumpet, { p.sax }, o);
    p.master->endCmd();
    EXPECT_EQ(reports[0].skippedInMMRests, 1);
    EXPECT_TRUE(barsWith(p.sax, &MeasureBase::lineBreak).empty());

    o.breaksInsideMMRests = true;
    p.master->startCmd(muse::TranslatableString::untranslatable("Apply part layout"));
    jpl::applyPartLayout(p.trumpet, { p.sax }, o);
    p.master->endCmd();
    EXPECT_EQ(barsWith(p.sax, &MeasureBase::lineBreak), v({ 10 }));
    delete p.master;
}
