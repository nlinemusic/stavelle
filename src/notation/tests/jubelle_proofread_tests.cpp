/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle G5: Score Proofreading, on the real engine.
 */
#include <gtest/gtest.h>

#include <memory>

#include "engraving/dom/chord.h"
#include <algorithm>
#include "engraving/dom/part.h"
#include "engraving/dom/lyrics.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/instrchange.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/excerpt.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/hairpin.h"
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

TEST(Jubelle_Proofread, IntervalsMustLookTheWayTheySound)
{
    // lesson 8: C then D♯ sounds like a minor third but looks like a second; C then E♭ is fine
    for (int tpc : { int(Tpc::TPC_D_S), int(Tpc::TPC_E_B) }) {
        auto s = violinAndPiano();
        ASSERT_TRUE(s);
        s->startCmd(muse::TranslatableString::untranslatable("proofread test"));
        NoteVal c(72);
        c.tpc1 = c.tpc2 = int(Tpc::TPC_C);
        NoteVal x(75);
        x.tpc1 = x.tpc2 = tpc;
        Segment* seg = barStart(s.get(), 0);
        s->setNoteRest(seg, 0, c, Fraction(1, 2));
        Segment* half = s->tick2segment(Fraction(1, 2), true, SegmentType::ChordRest);
        s->setNoteRest(half, 0, x, Fraction(1, 2));
        s->endCmd();
        const auto found = jp::proofread(s.get(), just(jp::CheckCategory::Spelling));
        int looks = 0;
        for (const auto& f : found) {
            looks += f.message.contains(u"looks like") ? 1 : 0;
        }
        EXPECT_EQ(looks, tpc == int(Tpc::TPC_D_S) ? 1 : 0) << "tpc " << tpc;
    }
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

// ---------------------------------------------------------------------------
// Professional music-preparation practice (17 Nov 2026)
// ---------------------------------------------------------------------------
TEST(Jubelle_Proofread, ANoteThatHidesBeatThreeIsSuggested)
{
    auto s = violinAndPiano();
    ASSERT_TRUE(s);
    Measure* m = s->firstMeasure();
    ASSERT_EQ(m->timesig(), Fraction(4, 4));
    s->startCmd(muse::TranslatableString::untranslatable("proofread test"));
    // bar 1: eighth, then a dotted half starting on the "and" of 1: it runs across beat 3
    s->setNoteRest(barStart(s.get(), 0), 0, NoteVal(67), Fraction(1, 8));
    Segment* andOfOne = s->tick2segment(m->tick() + Fraction(1, 8), false, SegmentType::ChordRest);
    ASSERT_TRUE(andOfOne);
    s->setNoteRest(andOfOne, 0, NoteVal(69), Fraction(3, 4));
    // bar 2: quarter, half, quarter: the plain syncopation is fine
    s->setNoteRest(barStart(s.get(), 1), 0, NoteVal(67), Fraction(1, 4));
    Segment* beatTwo = s->tick2segment(barStart(s.get(), 1)->tick() + Fraction(1, 4), false, SegmentType::ChordRest);
    ASSERT_TRUE(beatTwo);
    s->setNoteRest(beatTwo, 0, NoteVal(69), Fraction(1, 2));
    s->endCmd();

    jp::ProofreadOptions o = just(jp::CheckCategory::MetricGrouping);
    int onViolin = 0;
    for (const jp::Finding& f : jp::proofread(s.get(), o)) {
        if (f.staffIdx == 0) {
            EXPECT_EQ(f.bar, 1);
            ++onViolin;
        }
    }
    EXPECT_GE(onViolin, 1) << "the long note in bar 1";
}

TEST(Jubelle_Proofread, InstrumentalDynamicsBelongBelow)
{
    auto s = violinAndPiano();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("proofread test"));
    put(s.get(), 0, 67);
    dynamic(s.get(), 0, DynamicType::MF);
    s->endCmd();
    s->doLayout();
    EXPECT_TRUE(jp::proofread(s.get(), just(jp::CheckCategory::Placement)).empty()) << "below the violin staff: as expected";

    Dynamic* d = toDynamic(barStart(s.get(), 0)->findAnnotation(ElementType::DYNAMIC, 0, 0));
    ASSERT_TRUE(d);
    d->setProperty(Pid::DIRECTION, PropertyValue(DirectionV::UP));
    s->doLayout();
    ASSERT_TRUE(d->placeAbove());
    const auto found = jp::proofread(s.get(), just(jp::CheckCategory::Placement));
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].item, d);
}

TEST(Jubelle_Proofread, PreparationChecks)
{
    auto s = violinAndPiano();
    ASSERT_TRUE(s);
    // make the score long enough: 40 bars
    s->startCmd(muse::TranslatableString::untranslatable("proofread test"));
    while (s->nmeasures() < 40) {
        s->insertMeasure(nullptr);
    }
    // the violin rests 20 bars, then enters
    put(s.get(), 20, 67);
    rehearsal(s.get(), 0, u"A");
    s->endCmd();
    s->doLayout();

    const auto found = jp::proofread(s.get(), just(jp::CheckCategory::Preparation));
    auto has = [&found](const char* keyStart) {
        for (const jp::Finding& f : found) {
            if (f.key.find(keyStart) != std::string::npos) {
                return true;
            }
        }
        return false;
    };
    EXPECT_TRUE(has(":cue:")) << "an entry after 20 bars of rest with no cue";
    EXPECT_TRUE(has(":mark:")) << "more than 16 bars without a rehearsal mark";
    EXPECT_TRUE(has(":tempo:start"));

    // a cue (small notes) before the entry answers the first finding
    s->startCmd(muse::TranslatableString::untranslatable("cue"));
    Note* cue = put(s.get(), 19, 72);
    cue->chord()->setSmall(true);
    cue->setPlay(false);
    s->endCmd();
    for (const jp::Finding& f : jp::proofread(s.get(), just(jp::CheckCategory::Preparation))) {
        EXPECT_EQ(f.key.find(":cue:"), std::string::npos) << f.message.toStdString();
    }
}

TEST(Jubelle_Proofread, HairpinsWithoutADynamicAndPartChecks)
{
    auto s = violinAndPiano();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("hairpins"));
    put(s.get(), 0, 67);
    put(s.get(), 1, 67);
    put(s.get(), 2, 67);
    // a crescendo over bar 1 with no dynamic at either end
    Hairpin* bare = Factory::createHairpin(s->dummy()->segment());
    bare->setHairpinType(HairpinType::CRESC_HAIRPIN);
    bare->setTrack(0);
    bare->setTrack2(0);
    bare->setTick(barStart(s.get(), 0)->tick());
    bare->setTick2(barStart(s.get(), 1)->tick());
    s->undoAddElement(bare);
    // one that ends on a dynamic is fine
    Hairpin* toF = Factory::createHairpin(s->dummy()->segment());
    toF->setHairpinType(HairpinType::CRESC_HAIRPIN);
    toF->setTrack(0);
    toF->setTrack2(0);
    toF->setTick(barStart(s.get(), 1)->tick());
    toF->setTick2(barStart(s.get(), 2)->tick());
    s->undoAddElement(toF);
    dynamic(s.get(), 2, DynamicType::F);
    s->endCmd();
    s->doLayout();

    int bareFound = 0, otherFound = 0;
    for (const jp::Finding& f : jp::proofread(s.get(), just(jp::CheckCategory::Preparation))) {
        if (f.key.find(":hairpin:") != std::string::npos) {
            (f.item == bare ? bareFound : otherFound)++;
        }
        EXPECT_EQ(f.key.find(":turn:"), std::string::npos) << "the full score is not checked for page turns";
    }
    EXPECT_EQ(bareFound, 1);
    EXPECT_EQ(otherFound, 0);

    // in a part: multi-measure rests off is pointed out
    for (Excerpt* ex : Excerpt::createExcerptsFromParts(s->parts(), s.get())) {
        s->initAndAddExcerpt(ex, false);
    }
    ASSERT_FALSE(s->excerpts().empty());
    Score* part = s->excerpts().front()->excerptScore();
    part->style().set(Sid::createMultiMeasureRests, false);
    part->doLayout();
    bool mmrest = false;
    for (const jp::Finding& f : jp::proofread(part, just(jp::CheckCategory::Preparation))) {
        mmrest = mmrest || f.key.find(":mmrest") != std::string::npos;
    }
    EXPECT_TRUE(mmrest);
}

// ---------------------------------------------------------------------------
// Professional preparation, part 2 (18 Nov 2026)
// ---------------------------------------------------------------------------

namespace {
std::unique_ptr<MasterScore> bigBand()
{
    jubelle_templates::loadInstrumentData();
    return std::unique_ptr<MasterScore>(jubelle_templates::readTemplate(
                                            ROOT + "share/templates/05-Jazz/02-Big_Band/02-Big_Band.mscx"));
}

std::vector<Part*> trumpets(MasterScore* s)
{
    std::vector<Part*> out;
    for (Part* p : s->parts()) {
        if (p->instrument()->id().contains(u"trumpet")) {
            out.push_back(p);
        }
    }
    return out;
}

void chordFor(MasterScore* s, int bar, Part* p, int pitch)
{
    s->setNoteRest(barStart(s, bar), p->startTrack(), NoteVal(pitch), Fraction(1, 4));
}

size_t countWith(const std::vector<jp::Finding>& f, const std::string& kind)
{
    return size_t(std::count_if(f.begin(), f.end(), [&kind](const jp::Finding& x) { return x.key.find(":" + kind + ":") != std::string::npos; }));
}
}

TEST(Jubelle_Proofread, SectionVoicing)
{
    auto s = bigBand();
    ASSERT_TRUE(s);
    const std::vector<Part*> tpt = trumpets(s.get());
    ASSERT_EQ(tpt.size(), 4u);
    s->startCmd(muse::TranslatableString::untranslatable("proofread test"));
    // bar 1: a good closed voicing (C5 A4 F4 C4 over F major): nothing to say
    chordFor(s.get(), 0, tpt[0], 72);
    chordFor(s.get(), 0, tpt[1], 69);
    chordFor(s.get(), 0, tpt[2], 65);
    chordFor(s.get(), 0, tpt[3], 60);
    // bar 2: trumpet 2 above trumpet 1
    chordFor(s.get(), 1, tpt[0], 72);
    chordFor(s.get(), 1, tpt[1], 74);
    chordFor(s.get(), 1, tpt[2], 65);
    chordFor(s.get(), 1, tpt[3], 60);
    // bar 3: only a semitone under the lead, and trumpet 3 doubling the lead an octave down
    chordFor(s.get(), 2, tpt[0], 72);
    chordFor(s.get(), 2, tpt[1], 71);
    chordFor(s.get(), 2, tpt[2], 60);
    chordFor(s.get(), 2, tpt[3], 55);
    // bar 4: more than an octave between trumpets 1 and 2
    chordFor(s.get(), 3, tpt[0], 84);
    chordFor(s.get(), 3, tpt[1], 69);
    s->endCmd();

    const auto found = jp::proofread(s.get(), just(jp::CheckCategory::Voicing));
    for (const auto& f : found) {
        EXPECT_NE(f.bar, 1) << f.message.toStdString();   // the good voicing is left alone
    }
    EXPECT_EQ(countWith(found, "cross"), 1u);
    EXPECT_EQ(countWith(found, "close"), 1u);
    EXPECT_EQ(countWith(found, "double"), 1u);
    EXPECT_EQ(countWith(found, "gap"), 1u);
    for (const auto& f : found) {
        EXPECT_EQ(f.severity, jp::Severity::Suggestion);
    }
}

TEST(Jubelle_Proofread, TimeToChangeInstrument)
{
    auto s = bigBand();
    ASSERT_TRUE(s);
    Part* alto = s->parts().front();
    s->startCmd(muse::TranslatableString::untranslatable("proofread test"));
    // a note in bar 1, then a change to flute in bar 2 with a note straight away
    s->setNoteRest(barStart(s.get(), 0), alto->startTrack(), NoteVal(70), Fraction(1, 1));
    s->setNoteRest(barStart(s.get(), 1), alto->startTrack(), NoteVal(77), Fraction(1, 1));
    const InstrumentTemplate* flute = searchTemplate(u"flute");
    ASSERT_TRUE(flute);
    Segment* seg = barStart(s.get(), 1);
    InstrumentChange* ic = Factory::createInstrumentChange(seg, Instrument::fromTemplate(flute));
    ic->setParent(seg);
    ic->setTrack(alto->startTrack());
    s->undoAddElement(ic);
    s->endCmd();
    const auto found = jp::proofread(s.get(), just(jp::CheckCategory::Orchestration));
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].severity, jp::Severity::Warning);
    EXPECT_EQ(found[0].item, ic);
}

TEST(Jubelle_Proofread, MelismaWithoutExtender)
{
    auto s = violinAndPiano();
    ASSERT_TRUE(s);
    s->startCmd(muse::TranslatableString::untranslatable("proofread test"));
    put(s.get(), 0, 67);
    put(s.get(), 1, 69);
    Chord* first = toChord(barStart(s.get(), 0)->element(0));
    Lyrics* l = Factory::createLyrics(first);
    l->setPlainText(u"love");
    l->setSyllabic(LyricsSyllabic::SINGLE);
    l->setParent(first);
    l->setTrack(0);
    s->undoAddElement(l);
    s->endCmd();
    auto found = jp::proofread(s.get(), just(jp::CheckCategory::Copying));
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].item, l);

    // with an extender line there is nothing to say
    l->setTicks(Fraction(1, 1));
    found = jp::proofread(s.get(), just(jp::CheckCategory::Copying));
    EXPECT_TRUE(found.empty());
}
