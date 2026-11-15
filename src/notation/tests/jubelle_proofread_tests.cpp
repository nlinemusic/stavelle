/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle G5: Score Proofreading, on the real engine.
 */
#include <gtest/gtest.h>

#include <memory>

#include "engraving/dom/chord.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/pitchspelling.h"
#include "engraving/dom/rehearsalmark.h"
#include "engraving/dom/segment.h"
#include "engraving/editing/undo.h"
#include "notation/internal/jubelle/proofread.h"

#include "templatebuilder.h"

using namespace mu::engraving;
namespace jp = mu::notation::jubelle;

namespace {
const std::string ROOT = std::string(MU_ROOT_DIR) + "/";

Segment* barStart(MasterScore* s, int bar)
{
    Measure* m = s->firstMeasure();
    for (int i = 0; i < bar && m; ++i) {
        m = m->nextMeasure();
    }
    return m ? m->first(SegmentType::ChordRest) : nullptr;
}

std::unique_ptr<MasterScore> violinAndPiano()
{
    jubelle_templates::loadInstrumentData();
    return std::unique_ptr<MasterScore>(jubelle_templates::readTemplate(
                                            ROOT + "share/templates/09-Solo_with_Piano/01-Violin_+_Piano/01-Violin_+_Piano.mscx"));
}

Note* put(MasterScore* s, int bar, int pitch, track_idx_t track = 0)
{
    s->setNoteRest(barStart(s, bar), track, NoteVal(pitch), Fraction(1, 1));
    return toChord(barStart(s, bar)->element(track))->upNote();
}

void dynamic(MasterScore* s, int bar, DynamicType t)
{
    Segment* seg = barStart(s, bar);
    Dynamic* d = Factory::createDynamic(seg);
    d->setDynamicType(t);
    d->setParent(seg);
    d->setTrack(0);
    s->undoAddElement(d);
}

void rehearsal(MasterScore* s, int bar, const char16_t* text)
{
    Segment* seg = barStart(s, bar);
    RehearsalMark* r = Factory::createRehearsalMark(seg);
    r->setXmlText(muse::String(text));
    r->setParent(seg);
    r->setTrack(0);
    s->undoAddElement(r);
}

std::vector<jp::Finding> only(const std::vector<jp::Finding>& all, jp::CheckCategory c)
{
    std::vector<jp::Finding> out;
    for (const jp::Finding& f : all) {
        if (f.category == c) {
            out.push_back(f);
        }
    }
    return out;
}

jp::ProofreadOptions just(jp::CheckCategory c)
{
    jp::ProofreadOptions o;
    o.categories = { c };
    return o;
}
}

TEST(Jubelle_Proofread, RangeUsesMuseScoresLimits)
{
    auto s = violinAndPiano();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("proofread test"));
    Note* tooLow = put(s.get(), 0, 50);     // below the violin's G3
    Note* fine = put(s.get(), 1, 67);
    s->endCmd();
    const auto found = jp::proofread(s.get(), just(jp::CheckCategory::Range));
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].item, tooLow);
    EXPECT_EQ(found[0].severity, jp::Severity::Error);
    EXPECT_EQ(found[0].bar, 1);
    (void)fine;
}

TEST(Jubelle_Proofread, LargeLeapsByInstrumentGroup)
{
    auto s = violinAndPiano();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("proofread test"));
    put(s.get(), 0, 55);
    Note* high = put(s.get(), 1, 88);   // 33 semitones: more than two octaves for a string player
    put(s.get(), 2, 79);                // 9: fine
    s->endCmd();
    const auto found = jp::proofread(s.get(), just(jp::CheckCategory::Leaps));
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].item, high);
}

TEST(Jubelle_Proofread, DoubleSharpsAreSuggested)
{
    auto s = violinAndPiano();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("proofread test"));
    NoteVal nv(67);
    nv.tpc1 = int(Tpc::TPC_F_SS);   // G written as F double sharp
    nv.tpc2 = int(Tpc::TPC_F_SS);
    s->setNoteRest(barStart(s.get(), 0), 0, nv, Fraction(1, 1));
    s->endCmd();
    const auto found = jp::proofread(s.get(), just(jp::CheckCategory::Spelling));
    ASSERT_FALSE(found.empty());
    EXPECT_EQ(found[0].severity, jp::Severity::Suggestion);
}

TEST(Jubelle_Proofread, DynamicsMissingAtTheStartAndRepeated)
{
    auto s = violinAndPiano();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("proofread test"));
    put(s.get(), 0, 67);
    put(s.get(), 1, 69);
    put(s.get(), 2, 71);
    s->endCmd();
    auto found = only(jp::proofread(s.get(), just(jp::CheckCategory::Dynamics)), jp::CheckCategory::Dynamics);
    ASSERT_GE(found.size(), 1u);
    EXPECT_EQ(found[0].bar, 1) << "the violin starts without a dynamic";

    s->startCmd(muse::TranslatableString::untranslatable("proofread test"));
    dynamic(s.get(), 0, DynamicType::MF);
    dynamic(s.get(), 2, DynamicType::MF);   // mf again, nothing changed
    s->endCmd();
    found = jp::proofread(s.get(), just(jp::CheckCategory::Dynamics));
    int repeats = 0;
    for (const jp::Finding& f : found) {
        if (f.staffIdx == 0) {
            EXPECT_EQ(f.bar, 3);
            ++repeats;
        }
    }
    EXPECT_EQ(repeats, 1);
}

TEST(Jubelle_Proofread, RehearsalMarkGapsAndDuplicates)
{
    auto s = violinAndPiano();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("proofread test"));
    rehearsal(s.get(), 0, u"A");
    rehearsal(s.get(), 2, u"C");   // B is missing
    rehearsal(s.get(), 4, u"C");   // and C twice
    s->endCmd();
    const auto found = jp::proofread(s.get(), just(jp::CheckCategory::RehearsalMarks));
    ASSERT_EQ(found.size(), 3u);   // gap at C, duplicate C, and the second C doesn't follow
    EXPECT_EQ(found[0].bar, 3);
}

TEST(Jubelle_Proofread, VoiceCrossings)
{
    auto s = violinAndPiano();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("proofread test"));
    put(s.get(), 0, 64, 0);
    Note* above = put(s.get(), 0, 72, 1);   // voice 2 above voice 1
    s->endCmd();
    const auto found = jp::proofread(s.get(), just(jp::CheckCategory::VoiceCrossings));
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].item, above);
}

TEST(Jubelle_Proofread, MarkAsIntentionalIsRememberedInTheScore)
{
    auto s = violinAndPiano();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("proofread test"));
    put(s.get(), 0, 50);
    s->endCmd();
    auto found = jp::proofread(s.get(), just(jp::CheckCategory::Range));
    ASSERT_EQ(found.size(), 1u);

    s->startCmd(muse::TranslatableString::untranslatable("intentional"));
    jp::setIntentional(s.get(), found[0].key, true);
    s->endCmd();
    EXPECT_TRUE(jp::proofread(s.get(), just(jp::CheckCategory::Range)).empty());
    EXPECT_FALSE(s->metaTag(muse::String(jp::INTENTIONAL_TAG)).isEmpty());

    jp::ProofreadOptions all = just(jp::CheckCategory::Range);
    all.includeIntentional = true;
    EXPECT_EQ(jp::proofread(s.get(), all).size(), 1u);

    // undo brings it back
    s->undoRedo(true, nullptr);
    EXPECT_EQ(jp::proofread(s.get(), just(jp::CheckCategory::Range)).size(), 1u);
}

TEST(Jubelle_Proofread, ProofreadingNeverChangesTheScore)
{
    auto s = violinAndPiano();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("proofread test"));
    put(s.get(), 0, 50);
    put(s.get(), 1, 88);
    s->endCmd();
    const size_t undoBefore = s->undoStack()->size();
    jp::proofread(s.get());
    EXPECT_EQ(s->undoStack()->size(), undoBefore);
}
