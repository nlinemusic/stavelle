/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

#include <gtest/gtest.h>

#include "engraving/dom/articulation.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/textline.h"
#include "engraving/rw/xmlreader.h"
#include "engraving/types/symnames.h"

#include "notation/internal/jubelle/linkedpassages.h"

#include "utils/scorerw.h"

using namespace mu::engraving;
namespace jlp = mu::notation::jubelle;
using mu::notation::passagelink::SyncOptions;

//! Jubelle linked passages on the real notation engine. linked.mscx: Trumpets
//! 1–4 (pitches 72, 67, 64, 60), 8 bars of quarter notes. Bars 1–4 of
//! Trumpet 1 are linked to Trumpets 2–4.
namespace {
constexpr int Q = 480;
constexpr int BAR = 4 * Q;

class Jubelle_LinkedPassages : public ::testing::Test
{
protected:
    void SetUp() override
    {
        jlp::installLinkedPassagesHook();
        const muse::String path = muse::String::fromUtf8(std::string(MU_ROOT_DIR) + "/src/notation/tests/jubelle_chordid_data/linked.mscx");
        score = ScoreRW::readScore(path, true);
        ASSERT_TRUE(score);
        ASSERT_EQ(score->nstaves(), 4u);
    }

    void TearDown() override
    {
        delete score;
        Score::jubelleCmdHook = nullptr;
    }

    int link(SyncOptions o = SyncOptions::all())
    {
        score->startCmd(muse::TranslatableString::untranslatable("link"));
        const int id = jlp::linkPassages(score, 0, 0, 4 * BAR, { 1, 2, 3 }, o);
        score->endCmd();
        return id;
    }

    //! "tick:duration:pitches" for voice 1 of a staff, bars 1–8
    std::vector<std::string> music(size_t staff, int from = 0, int to = 8 * BAR)
    {
        std::vector<std::string> out;
        for (Segment* s = score->tick2segment(Fraction::fromTicks(from), true, SegmentType::ChordRest); s && s->tick().ticks() < to;
             s = s->next1(SegmentType::ChordRest)) {
            EngravingItem* e = s->element(staff * VOICES);
            if (!e) {
                continue;
            }
            std::string t = std::to_string(s->tick().ticks()) + ":" + std::to_string(toChordRest(e)->actualTicks().ticks());
            if (e->isChord()) {
                for (Note* n : toChord(e)->notes()) {
                    t += ":" + std::to_string(n->pitch());
                }
            } else {
                t += ":rest";
            }
            out.push_back(t);
        }
        return out;
    }

    Chord* chordAt(size_t staff, int tick)
    {
        Segment* s = score->tick2segment(Fraction::fromTicks(tick), true, SegmentType::ChordRest);
        EngravingItem* e = s ? s->element(staff * VOICES) : nullptr;
        return e && e->isChord() ? toChord(e) : nullptr;
    }

    bool hasArticulation(size_t staff, int tick, SymId id)
    {
        Chord* c = chordAt(staff, tick);
        if (!c) {
            return false;
        }
        // Above and Below are the same articulation (MuseScore flips them by stem)
        auto base = [](SymId s) {
            std::string n(SymNames::nameForSymId(s).ascii());
            for (const std::string suffix : { "Above", "Below" }) {
                if (n.size() > suffix.size() && n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0) {
                    n.erase(n.size() - suffix.size());
                }
            }
            return n;
        };
        for (Articulation* a : c->articulations()) {
            if (base(a->symId()) == base(id)) {
                return true;
            }
        }
        return false;
    }

    void addArticulation(size_t staff, int tick, SymId id)
    {
        score->startCmd(muse::TranslatableString::untranslatable("articulation"));
        Chord* c = chordAt(staff, tick);
        Articulation* a = Factory::createArticulation(c);
        a->setSymId(id);
        a->setParent(c);
        a->setTrack(c->track());
        score->undoAddElement(a);
        score->endCmd();
    }

    //! Trumpet 1, bar 2: two quarters become a half note
    void halfNoteInBar2()
    {
        score->startCmd(muse::TranslatableString::untranslatable("rhythm"));
        Segment* s = score->tick2segment(Fraction::fromTicks(BAR), true, SegmentType::ChordRest);
        score->setNoteRest(s, 0, NoteVal(72), Fraction(1, 2));
        score->endCmd();
    }

    MasterScore* score = nullptr;
};
}

TEST_F(Jubelle_LinkedPassages, LinkingChangesNothingAndIsHidden)
{
    const std::vector<std::string> before = music(1);
    const int id = link();
    EXPECT_GT(id, 0);
    EXPECT_EQ(music(1), before);
    const std::vector<jlp::LinkGroup> groups = jlp::linkGroups(score);
    ASSERT_EQ(groups.size(), 1u);
    EXPECT_EQ(groups[0].targets.size(), 3u);
    EXPECT_FALSE(groups[0].source.line->visible()) << "never printed";
    for (const jlp::LinkMember& t : groups[0].targets) {
        EXPECT_FALSE(t.line->visible());
        EXPECT_EQ(t.startTick, 0);
        EXPECT_EQ(t.endTick, 4 * BAR);
    }
}

TEST_F(Jubelle_LinkedPassages, RhythmFollowsTargetsKeepTheirPitches)
{
    link();
    const std::vector<std::string> bar5to8 = music(2, 4 * BAR);
    halfNoteInBar2();

    // Each trumpet: a half note on beat 1 of bar 2, at its own pitch
    EXPECT_EQ(music(0, BAR, 2 * BAR), (std::vector<std::string> { "1920:960:72", "2880:480:72", "3360:480:72" }));
    EXPECT_EQ(music(1, BAR, 2 * BAR), (std::vector<std::string> { "1920:960:67", "2880:480:67", "3360:480:67" }));
    EXPECT_EQ(music(2, BAR, 2 * BAR), (std::vector<std::string> { "1920:960:64", "2880:480:64", "3360:480:64" }));
    EXPECT_EQ(music(3, BAR, 2 * BAR), (std::vector<std::string> { "1920:960:60", "2880:480:60", "3360:480:60" }));
    EXPECT_EQ(music(2, 4 * BAR), bar5to8) << "music outside the passage is untouched";

    // One undo puts every trumpet back; redo applies again
    score->undoRedo(true, nullptr);
    EXPECT_EQ(music(1, BAR, 2 * BAR).size(), 4u);
    EXPECT_EQ(music(3, BAR, 2 * BAR).front(), "1920:480:60");
    score->undoRedo(false, nullptr);
    EXPECT_EQ(music(3, BAR, 2 * BAR).front(), "1920:960:60");
}

TEST_F(Jubelle_LinkedPassages, ArticulationsAndDynamicsAndOptions)
{
    link();
    addArticulation(0, 0, SymId::articStaccatoAbove);
    for (size_t t : { 1, 2, 3 }) {
        EXPECT_TRUE(hasArticulation(t, 0, SymId::articStaccatoAbove)) << "staff " << t;
    }

    // A dynamic on Trumpet 1, bar 1 beat 2
    score->startCmd(muse::TranslatableString::untranslatable("dynamic"));
    Segment* seg = score->tick2segment(Fraction::fromTicks(Q), true, SegmentType::ChordRest);
    Dynamic* d = Factory::createDynamic(seg);
    d->setDynamicType(DynamicType::MF);
    d->setXmlText(Dynamic::dynamicText(DynamicType::MF));
    d->setParent(seg);
    d->setTrack(0);
    score->undoAddElement(d);
    score->endCmd();
    int dynamics = 0;
    for (EngravingItem* a : seg->annotations()) {
        dynamics += a->isDynamic() ? 1 : 0;
    }
    EXPECT_EQ(dynamics, 4) << "one dynamic per trumpet";

    // Articulations switched off for this link: no longer copied
    SyncOptions noArt = SyncOptions::all();
    noArt.articulations = false;
    score->startCmd(muse::TranslatableString::untranslatable("options"));
    jlp::setSyncOptions(score, jlp::linkGroups(score)[0].id, noArt);
    score->endCmd();
    addArticulation(0, 2 * Q, SymId::articAccentAbove);
    EXPECT_FALSE(hasArticulation(1, 2 * Q, SymId::articAccentAbove));
}

TEST_F(Jubelle_LinkedPassages, HandEditsAreYoursUntilResync)
{
    link();
    // Your own accent on Trumpet 2, bar 3 beat 1
    addArticulation(1, 2 * BAR, SymId::articAccentAbove);
    const jlp::LinkGroup g = jlp::linkGroups(score)[0];
    EXPECT_FALSE(g.targets[0].handEdited.empty()) << "marked as your own";
    EXPECT_TRUE(g.targets[1].handEdited.empty());

    // Trumpet 1 adds a staccato there: Trumpet 2 keeps its own note as it is
    addArticulation(0, 2 * BAR, SymId::articStaccatoAbove);
    EXPECT_FALSE(hasArticulation(1, 2 * BAR, SymId::articStaccatoAbove));
    EXPECT_TRUE(hasArticulation(1, 2 * BAR, SymId::articAccentAbove));
    EXPECT_TRUE(hasArticulation(2, 2 * BAR, SymId::articStaccatoAbove)) << "the others follow";

    // A rhythm change elsewhere doesn't touch your note either
    halfNoteInBar2();
    EXPECT_TRUE(hasArticulation(1, 2 * BAR, SymId::articAccentAbove));
    EXPECT_EQ(music(1, BAR, 2 * BAR).front(), "1920:960:67");

    // Re-sync: back in line with Trumpet 1 (pitch kept)
    score->startCmd(muse::TranslatableString::untranslatable("resync"));
    jlp::resyncTarget(score, g.targets[0].line);
    score->endCmd();
    EXPECT_TRUE(hasArticulation(1, 2 * BAR, SymId::articStaccatoAbove));
    EXPECT_FALSE(hasArticulation(1, 2 * BAR, SymId::articAccentAbove));
    EXPECT_EQ(chordAt(1, 2 * BAR)->notes().front()->pitch(), 67);
    EXPECT_TRUE(jlp::linkGroups(score)[0].targets[0].handEdited.empty());
}

TEST_F(Jubelle_LinkedPassages, UnlinkOneKeepsTheOthers)
{
    link();
    score->startCmd(muse::TranslatableString::untranslatable("unlink"));
    jlp::unlinkMember(score, jlp::linkGroups(score)[0].targets[1].line);   // Trumpet 3
    score->endCmd();
    ASSERT_EQ(jlp::linkGroups(score)[0].targets.size(), 2u);

    addArticulation(0, Q, SymId::articTenutoAbove);
    EXPECT_TRUE(hasArticulation(1, Q, SymId::articTenutoAbove));
    EXPECT_FALSE(hasArticulation(2, Q, SymId::articTenutoAbove)) << "unlinked";
    EXPECT_TRUE(hasArticulation(3, Q, SymId::articTenutoAbove));

    // Unlinking the source: Trumpet 2 becomes the source of what's left
    score->startCmd(muse::TranslatableString::untranslatable("unlink"));
    jlp::unlinkMember(score, jlp::linkGroups(score)[0].source.line);
    score->endCmd();
    const std::vector<jlp::LinkGroup> left = jlp::linkGroups(score);
    ASSERT_EQ(left.size(), 1u);
    EXPECT_EQ(left[0].source.staffIdx, 1u);
    EXPECT_EQ(left[0].targets.size(), 1u);
    EXPECT_EQ(left[0].targets[0].staffIdx, 3u);

    // Undo (source unlink, the tenuto, Trumpet 3's unlink): all four linked again
    score->undoRedo(true, nullptr);
    score->undoRedo(true, nullptr);
    score->undoRedo(true, nullptr);
    EXPECT_EQ(jlp::linkGroups(score)[0].targets.size(), 3u);
}

TEST_F(Jubelle_LinkedPassages, SurvivesSavingAndBarEdits)
{
    link();
    addArticulation(1, 2 * BAR, SymId::articAccentAbove);   // your own note in Trumpet 2

    const muse::String saved = u"jubelle_linked_test.mscx";
    ASSERT_TRUE(ScoreRW::saveScore(score, saved));
    MasterScore* reopened = ScoreRW::readScore(saved, true);
    ASSERT_TRUE(reopened);
    const std::vector<jlp::LinkGroup> g = jlp::linkGroups(reopened);
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(g[0].targets.size(), 3u);
    EXPECT_FALSE(g[0].targets[0].handEdited.empty()) << "your own notes are remembered";
    EXPECT_TRUE(g[0].sync.rhythm && g[0].sync.articulations);
    delete reopened;

    // A bar inserted before the passage: the links move with the music
    score->startCmd(muse::TranslatableString::untranslatable("insert"));
    score->insertMeasure(score->firstMeasure());
    score->endCmd();
    for (const jlp::LinkMember& t : jlp::linkGroups(score)[0].targets) {
        EXPECT_EQ(t.startTick, BAR);
        EXPECT_EQ(t.endTick, 5 * BAR);
    }
    // ...and still work there (Trumpet 1's bar 3 is now bar 4)
    addArticulation(0, 3 * BAR + Q, SymId::articMarcatoAbove);
    EXPECT_TRUE(hasArticulation(3, 3 * BAR + Q, SymId::articMarcatoAbove));
    // Your own note in Trumpet 2 is still recognised after the move
    addArticulation(0, 3 * BAR, SymId::articStaccatoAbove);
    EXPECT_FALSE(hasArticulation(1, 3 * BAR, SymId::articStaccatoAbove));
    EXPECT_TRUE(hasArticulation(2, 3 * BAR, SymId::articStaccatoAbove));

    // A bar deleted inside the passage: it shrinks and stays linked
    score->startCmd(muse::TranslatableString::untranslatable("delete"));
    Measure* m = score->tick2measure(Fraction::fromTicks(2 * BAR));
    score->deleteMeasures(m, m);
    score->endCmd();
    const jlp::LinkGroup after = jlp::linkGroups(score)[0];
    EXPECT_EQ(after.targets.size(), 3u);
    EXPECT_EQ(after.source.endTick - after.source.startTick, 3 * BAR);
}

TEST_F(Jubelle_LinkedPassages, DurationsCanStayIndependent)
{
    SyncOptions o = SyncOptions::all();
    o.durations = false;
    link(o);
    // Trumpet 4 plays bar 2 beat 1 as an eighth (its own length)
    score->startCmd(muse::TranslatableString::untranslatable("eighth"));
    Segment* s = score->tick2segment(Fraction::fromTicks(BAR), true, SegmentType::ChordRest);
    score->changeCRlen(toChordRest(s->element(3 * VOICES)), Fraction(1, 8));
    score->endCmd();
    // Trumpet 1 changes the rhythm later in the bar
    score->startCmd(muse::TranslatableString::untranslatable("rhythm"));
    Segment* s3 = score->tick2segment(Fraction::fromTicks(BAR + 2 * Q), true, SegmentType::ChordRest);
    score->setNoteRest(s3, 0, NoteVal(72), Fraction(1, 2));
    score->endCmd();
    // Trumpet 4's eighth is kept (it is also marked as your own); with
    // durations not synchronised, its beat 3 keeps its own quarter length
    // where Trumpet 1 now has a half note, and the rest is a rest
    EXPECT_EQ(music(3, BAR, BAR + Q).front(), "1920:240:60");
    EXPECT_EQ(music(3, BAR + 2 * Q, 2 * BAR), (std::vector<std::string> { "2880:480:60", "3360:480:rest" }));
    EXPECT_EQ(music(0, BAR + 2 * Q, 2 * BAR), (std::vector<std::string> { "2880:960:72" }));
}

TEST_F(Jubelle_LinkedPassages, MovingTheMusicByCutAndPasteMovesTheLinks)
{
    link();
    // Cut bars 1–4 of all four trumpets and paste them at bar 5 (MuseScore's
    // way of moving music: there is no separate "move bars" command)
    score->startCmd(muse::TranslatableString::untranslatable("cut"));
    score->select(score->firstMeasure(), SelectType::RANGE, 0);
    score->select(score->tick2measure(Fraction::fromTicks(3 * BAR)), SelectType::RANGE, 3);
    const muse::ByteArray data = score->selection().mimeData();
    score->cmdDeleteSelection();
    score->endCmd();
    EXPECT_TRUE(jlp::linkGroups(score).empty()) << "cutting the passage takes its link with it";

    score->startCmd(muse::TranslatableString::untranslatable("paste"));
    XmlReader e(data);
    ASSERT_TRUE(score->pasteStaff(e, score->tick2segment(Fraction::fromTicks(4 * BAR), true, SegmentType::ChordRest), 0));
    score->endCmd();

    const std::vector<jlp::LinkGroup> g = jlp::linkGroups(score);
    ASSERT_EQ(g.size(), 1u) << "the link arrives with the pasted music";
    EXPECT_EQ(g[0].source.staffIdx, 0u);
    EXPECT_EQ(g[0].source.startTick, 4 * BAR);
    EXPECT_EQ(g[0].targets.size(), 3u);

    // ...and works in its new place
    addArticulation(0, 5 * BAR, SymId::articTenutoAbove);
    EXPECT_TRUE(hasArticulation(2, 5 * BAR, SymId::articTenutoAbove));

    // One undo removes the paste; another restores the cut music and its link
    score->undoRedo(true, nullptr);
    score->undoRedo(true, nullptr);
    score->undoRedo(true, nullptr);
    const std::vector<jlp::LinkGroup> back = jlp::linkGroups(score);
    ASSERT_EQ(back.size(), 1u);
    EXPECT_EQ(back[0].source.startTick, 0);
}

TEST_F(Jubelle_LinkedPassages, ACopyPastedElsewhereIsNotASecondLink)
{
    link();
    score->startCmd(muse::TranslatableString::untranslatable("copy"));
    score->select(score->firstMeasure(), SelectType::RANGE, 0);
    score->select(score->tick2measure(Fraction::fromTicks(3 * BAR)), SelectType::RANGE, 0);
    const muse::ByteArray data = score->selection().mimeData();
    score->endCmd();
    score->startCmd(muse::TranslatableString::untranslatable("paste"));
    XmlReader e(data);
    ASSERT_TRUE(score->pasteStaff(e, score->tick2segment(Fraction::fromTicks(4 * BAR), true, SegmentType::ChordRest), 0));
    score->endCmd();

    const std::vector<jlp::LinkGroup> g = jlp::linkGroups(score);
    ASSERT_EQ(g.size(), 1u);
    EXPECT_EQ(g[0].source.startTick, 0) << "the original stays the linked one";
    // Editing the pasted copy changes nothing in the other trumpets
    const std::vector<std::string> before = music(1, 4 * BAR, 8 * BAR);
    addArticulation(0, 5 * BAR, SymId::articTenutoAbove);
    EXPECT_FALSE(hasArticulation(1, 5 * BAR, SymId::articTenutoAbove));
    EXPECT_EQ(music(1, 4 * BAR, 8 * BAR), before);
    // ...while the original still works
    addArticulation(0, BAR, SymId::articTenutoAbove);
    EXPECT_TRUE(hasArticulation(1, BAR, SymId::articTenutoAbove));
}
