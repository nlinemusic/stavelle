/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle G5: Find and Select, on the real engine.
 */
#include <gtest/gtest.h>

#include <memory>

#include "engraving/dom/articulation.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/select.h"
#include "engraving/dom/staff.h"
#include "notation/internal/jubelle/findselect.h"

#include "templatebuilder.h"

using namespace mu::engraving;
namespace js = mu::notation::jubelle;

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

//! Violin + Piano: violin bars 1-4 with G3, A3, B5 (staccato) and C6 whole notes,
//! a dynamic in bars 1 and 3; piano untouched (whole rests)
struct Fixture {
    std::unique_ptr<MasterScore> s;
    Note* g3 = nullptr;
    Note* b5 = nullptr;
    Note* c6 = nullptr;

    Fixture()
    {
        jubelle_templates::loadInstrumentData();
        s.reset(jubelle_templates::readTemplate(ROOT + "share/templates/09-Solo_with_Piano/01-Violin_+_Piano/01-Violin_+_Piano.mscx"));
        if (!s) {
            return;
        }
        s->startCmd(muse::TranslatableString::untranslatable("find test"));
        const int pitches[] = { 55, 57, 83, 84 };
        for (int bar = 0; bar < 4; ++bar) {
            s->setNoteRest(barStart(s.get(), bar), 0, NoteVal(pitches[bar]), Fraction(1, 1));
        }
        g3 = toChord(barStart(s.get(), 0)->element(0))->upNote();
        b5 = toChord(barStart(s.get(), 2)->element(0))->upNote();
        c6 = toChord(barStart(s.get(), 3)->element(0))->upNote();
        Articulation* a = Factory::createArticulation(b5->chord());
        a->setSymId(SymId::articStaccatoAbove);
        a->setParent(b5->chord());
        a->setTrack(0);
        s->undoAddElement(a);
        for (int bar : { 0, 2 }) {
            Segment* seg = barStart(s.get(), bar);
            Dynamic* d = Factory::createDynamic(seg);
            d->setDynamicType(bar == 0 ? DynamicType::P : DynamicType::F);
            d->setParent(seg);
            d->setTrack(0);
            s->undoAddElement(d);
        }
        s->endCmd();
        s->doLayout();
    }

    std::vector<EngravingItem*> find(const js::FindQuery& q) { return js::findItems(s.get(), q); }
};
}

TEST(Jubelle_FindSelect, NotesAboveAPitch)
{
    Fixture f;
    ASSERT_TRUE(f.s);
    js::FindQuery q;          // notes, everywhere
    q.lowestPitch = 78;       // "every note above F5"
    const auto found = f.find(q);
    ASSERT_EQ(found.size(), 2u);
    EXPECT_EQ(found[0], f.b5);
    EXPECT_EQ(found[1], f.c6);
}

TEST(Jubelle_FindSelect, OnlyNotesWithAStaccato)
{
    Fixture f;
    ASSERT_TRUE(f.s);
    js::FindQuery q;
    q.withArticulation = { SymId::articStaccatoAbove, SymId::articStaccatoBelow };
    const auto found = f.find(q);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0], f.b5);

    // the staccato itself
    js::FindQuery a;
    a.kinds = { js::FindKind::Articulations };
    ASSERT_EQ(f.find(a).size(), 1u);
    EXPECT_TRUE(f.find(a)[0]->isArticulation());
}

TEST(Jubelle_FindSelect, KindsStavesAndBars)
{
    Fixture f;
    ASSERT_TRUE(f.s);
    js::FindQuery q;
    q.kinds = { js::FindKind::Dynamics };
    EXPECT_EQ(f.find(q).size(), 2u);

    // bars 2-3 only
    q.fromBar = 2;
    q.toBar = 3;
    ASSERT_EQ(f.find(q).size(), 1u);
    EXPECT_EQ(toDynamic(f.find(q)[0])->dynamicType(), DynamicType::F);

    // rests on the piano staves only (the violin has none in bars 1-4)
    js::FindQuery r;
    r.kinds = { js::FindKind::Rests };
    r.fromBar = 1;
    r.toBar = 4;
    r.staves = { 0 };
    EXPECT_TRUE(f.find(r).empty());
    r.staves = { 1, 2 };
    EXPECT_GE(f.find(r).size(), 8u);   // at least the four bars on both piano staves
    for (EngravingItem* e : f.find(r)) {
        EXPECT_NE(e->staffIdx(), 0u);
    }
}

TEST(Jubelle_FindSelect, NoteLengths)
{
    Fixture f;
    ASSERT_TRUE(f.s);
    js::FindQuery q;
    q.longest = Fraction(1, 2);   // nothing shorter than a whole note was written
    EXPECT_TRUE(f.find(q).empty());
    q.longest.reset();
    q.shortest = Fraction(1, 1);
    EXPECT_EQ(f.find(q).size(), 4u);
}

TEST(Jubelle_FindSelect, WrittenPitchForATransposingInstrument)
{
    jubelle_templates::loadInstrumentData();
    std::unique_ptr<MasterScore> s(jubelle_templates::readTemplate(
                                       ROOT + "share/templates/09-Solo_with_Piano/06-Clarinet_+_Piano/06-Clarinet_+_Piano.mscx"));
    if (!s) {
        GTEST_SKIP() << "Clarinet + Piano template not found";
    }
    s->startCmd(muse::TranslatableString::untranslatable("find test"));
    s->setNoteRest(barStart(s.get(), 0), 0, NoteVal(70), Fraction(1, 1));   // sounding B-flat 4
    s->endCmd();
    Note* n = toChord(barStart(s.get(), 0)->element(0))->upNote();
    ASSERT_EQ(n->transposition(), -2) << "a B-flat clarinet sounds a tone lower";

    js::FindQuery q;
    q.lowestPitch = 72;
    q.highestPitch = 72;           // written C5
    EXPECT_TRUE(js::findItems(s.get(), q).empty()) << "sounding pitch is B-flat 4";
    q.writtenPitch = true;
    ASSERT_EQ(js::findItems(s.get(), q).size(), 1u);
    EXPECT_EQ(js::findItems(s.get(), q)[0], n);
}

TEST(Jubelle_FindSelect, WithinTheCurrentSelection)
{
    Fixture f;
    ASSERT_TRUE(f.s);
    // range-select bars 2 and 3 of the violin, as with click and shift+click
    f.s->select(barStart(f.s.get(), 1)->element(0), SelectType::SINGLE, 0);
    f.s->select(barStart(f.s.get(), 2)->element(0), SelectType::RANGE, 0);
    ASSERT_TRUE(f.s->selection().isRange());
    js::FindQuery q;
    q.inSelection = true;
    const auto found = f.find(q);
    ASSERT_EQ(found.size(), 2u);   // bars 2 and 3
    EXPECT_EQ(found[1], f.b5);
}

TEST(Jubelle_FindSelect, SelectingSelectsExactlyTheMatches)
{
    Fixture f;
    ASSERT_TRUE(f.s);
    js::FindQuery q;
    q.lowestPitch = 78;
    EXPECT_EQ(js::selectItems(f.s.get(), f.find(q)), 2u);
    EXPECT_TRUE(f.b5->selected());
    EXPECT_TRUE(f.c6->selected());
    EXPECT_FALSE(f.g3->selected());
    EXPECT_EQ(f.s->selection().elements().size(), 2u);
}

TEST(Jubelle_FindSelect, SavedFiltersRoundTrip)
{
    js::FindQuery q;
    q.kinds = { js::FindKind::Notes, js::FindKind::Dynamics };
    q.staves = { 0, 3 };
    q.voices = 0x3;
    q.lowestPitch = 60;
    q.highestPitch = 84;
    q.writtenPitch = true;
    q.includeGraceNotes = false;
    q.skipTiedContinuations = true;
    q.shortest = Fraction(1, 16);
    q.longest = Fraction(1, 4);
    q.withArticulation = { SymId::articStaccatoAbove };
    q.articulationSymbols = { SymId::articAccentAbove };
    q.fromBar = 5;
    q.toBar = 12;
    q.inSelection = true;

    const js::FindQuery back = js::fromText(js::toText(q));
    EXPECT_EQ(back.kinds, q.kinds);
    EXPECT_EQ(back.staves, q.staves);
    EXPECT_EQ(back.voices, q.voices);
    EXPECT_EQ(back.lowestPitch, q.lowestPitch);
    EXPECT_EQ(back.highestPitch, q.highestPitch);
    EXPECT_EQ(back.writtenPitch, q.writtenPitch);
    EXPECT_EQ(back.includeGraceNotes, q.includeGraceNotes);
    EXPECT_EQ(back.skipTiedContinuations, q.skipTiedContinuations);
    EXPECT_EQ(back.shortest, q.shortest);
    EXPECT_EQ(back.longest, q.longest);
    EXPECT_EQ(back.withArticulation, q.withArticulation);
    EXPECT_EQ(back.articulationSymbols, q.articulationSymbols);
    EXPECT_EQ(back.fromBar, q.fromBar);
    EXPECT_EQ(back.toBar, q.toBar);
    EXPECT_EQ(back.inSelection, q.inSelection);
}
