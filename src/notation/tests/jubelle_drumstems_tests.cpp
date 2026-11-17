/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle G9: drum-component stems, on the real engine.
 */
#include <gtest/gtest.h>

#include <memory>

#include "engraving/dom/chord.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "notation/internal/jubelle/drumstems.h"

#include "templatebuilder.h"

using namespace mu::engraving;
namespace jd = mu::notation::jubelle;

namespace {
const std::string ROOT = std::string(MU_ROOT_DIR) + "/";

struct Kit {
    std::unique_ptr<MasterScore> score;
    Part* drums = nullptr;
};

//! Jazz combo; the drum kit plays, bar by bar: kick, snare, closed hat, open hat, crash, ride, cowbell (56), kick
Kit combo()
{
    Kit k;
    jubelle_templates::loadInstrumentData();
    k.score.reset(jubelle_templates::readTemplate(ROOT + "share/templates/05-Jazz/03-Jazz_Combo/03-Jazz_Combo.mscx"));
    if (!k.score) {
        return k;
    }
    const std::vector<Part*> kits = jd::drumParts(k.score.get());
    if (kits.empty()) {
        return k;
    }
    k.drums = kits.front();
    MasterScore* s = k.score.get();
    s->startCmd(muse::TranslatableString::untranslatable("drum stems test"));
    while (s->nmeasures() < 8) {
        s->insertMeasure(nullptr);
    }
    const std::vector<int> pitches { 36, 38, 42, 46, 49, 51, 56, 36 };
    Measure* m = s->firstMeasure();
    for (size_t i = 0; i < pitches.size() && m; ++i, m = m->nextMeasure()) {
        s->setNoteRest(m->first(SegmentType::ChordRest), k.drums->startTrack(), NoteVal(pitches[i]), Fraction(1, 1));
        // something for the other instruments too
        s->setNoteRest(m->first(SegmentType::ChordRest), 0, NoteVal(60), Fraction(1, 1));
    }
    s->endCmd();
    return k;
}

std::vector<int> playingDrums(const Kit& k)
{
    std::vector<int> out;
    for (Segment* seg = k.score->firstSegment(SegmentType::ChordRest); seg; seg = seg->next1(SegmentType::ChordRest)) {
        EngravingItem* e = seg->element(k.drums->startTrack());
        if (e && e->isChord() && toChord(e)->upNote()->play()) {
            out.push_back(toChord(e)->upNote()->pitch());
        }
    }
    return out;
}
}

TEST(Jubelle_DrumStems, TheKitIsFoundAndSplitIntoItsComponents)
{
    Kit k = combo();
    ASSERT_TRUE(k.score);
    ASSERT_TRUE(k.drums) << "the jazz combo has a drum kit";
    ASSERT_NE(k.drums->startTrack(), 0u) << "the test puts other music on the first staff";

    const auto comps = jd::drumComponents(k.score.get(), k.drums);
    std::vector<std::string> names;
    for (const jd::DrumComponent& c : comps) {
        names.push_back(c.name.toStdString());
    }
    ASSERT_EQ(comps.size(), 7u) << ::testing::PrintToString(names);
    EXPECT_EQ(names[0], "Kick");
    EXPECT_EQ(comps[0].notes, 2);
    EXPECT_EQ(names[1], "Snare");
    EXPECT_EQ(names[2], "Closed hi-hat");
    EXPECT_EQ(names[3], "Open hi-hat");
    EXPECT_EQ(names[4], "Ride cymbal");
    EXPECT_EQ(names[5], "Crash cymbals");
    EXPECT_EQ(comps[6].kind, jd::DrumKind::Other) << "the cowbell is a component of its own, under the kit's name for it";
    EXPECT_FALSE(comps[6].name.isEmpty());
}

TEST(Jubelle_DrumStems, SuggestedGroupsFollowTheUsualMix)
{
    Kit k = combo();
    ASSERT_TRUE(k.drums);
    const auto comps = jd::drumComponents(k.score.get(), k.drums);
    const auto groups = jd::suggestedGroups(comps);
    std::vector<std::string> names;
    for (const jd::StemGroup& g : groups) {
        names.push_back(g.name.toStdString());
    }
    EXPECT_EQ(names, (std::vector<std::string> { "Kick", "Snare", "Hi-hat", "Cymbals", "Percussion" })) << "no toms are played, so no Toms group";
    EXPECT_EQ(jd::pitchesOf(groups[2], comps), (std::set<int> { 42, 46 }));
    EXPECT_EQ(jd::pitchesOf(groups[3], comps), (std::set<int> { 49, 51 }));
}

TEST(Jubelle_DrumStems, IsolatingLeavesOnlyThatGroupPlayingAndOneUndoRestoresIt)
{
    Kit k = combo();
    ASSERT_TRUE(k.drums);
    const std::vector<int> all = playingDrums(k);
    ASSERT_EQ(all.size(), 8u);

    k.score->startCmd(muse::TranslatableString::untranslatable("isolate"));
    const int playing = jd::isolate(k.score.get(), k.drums, { 42, 46 });
    k.score->endCmd();
    EXPECT_EQ(playing, 2);
    EXPECT_EQ(playingDrums(k), (std::vector<int> { 42, 46 }));

    // the other instruments are not touched (they are silenced by soloing the kit, not by changing their notes)
    Chord* first = toChord(k.score->firstMeasure()->first(SegmentType::ChordRest)->element(0));
    ASSERT_TRUE(first);
    EXPECT_TRUE(first->upNote()->play());

    k.score->undoRedo(true, nullptr);
    EXPECT_EQ(playingDrums(k), all) << "everything plays again";
}

TEST(Jubelle_DrumStems, FileNamesAreSafe)
{
    EXPECT_EQ(jd::stemFileName(u"Kick").toStdString(), "Kick");
    EXPECT_EQ(jd::stemFileName(u"Hi-hat: open/closed?").toStdString(), "Hi-hat_ open_closed_");
    EXPECT_EQ(jd::stemFileName(u"  ...").toStdString(), "Stem");
}

TEST(Jubelle_DrumStems, GroupsSurviveBeingSavedAsAPreset)
{
    const std::vector<jd::StemGroup> groups { { u"Kick + snare", { 0, 1 } }, { u"Metal", { 2, 3, 8, 9, 1056 } }, { u"Empty", {} } };
    const auto back = jd::groupsFromText(jd::groupsToText(groups));
    ASSERT_EQ(back.size(), 3u);
    EXPECT_EQ(back[0].name.toStdString(), "Kick + snare");
    EXPECT_EQ(back[0].componentIds, (std::vector<int> { 0, 1 }));
    EXPECT_EQ(back[1].componentIds, (std::vector<int> { 2, 3, 8, 9, 1056 }));
    EXPECT_TRUE(back[2].componentIds.empty());
}
