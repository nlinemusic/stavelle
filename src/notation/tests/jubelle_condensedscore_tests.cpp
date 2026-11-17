/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle: the condensed full-score copy (G2), on the real notation engine.
 */
#include <gtest/gtest.h>

#include "engraving/engravingproject.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/part.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/slur.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafftext.h"
#include "engraving/dom/system.h"
#include "engraving/editing/undo.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/tie.h"

#include "notation/internal/jubelle/condensedscore.h"

#include "utils/scorerw.h"

using namespace mu::engraving;
namespace jc = mu::notation::jubelle;
using jc::CondenseOverride;

//! condense.mscx: Trumpet 1, Trumpet 2 (same instrument) and a Trombone,
//! 10 bars, line breaks after bars 4 and 8 (so 3 systems):
//!   1–2 unison (both mf in bar 1) · 3 same rhythm, different notes (chords) ·
//!   4 different rhythms (two voices; Trumpet 2 has p and a slur) ·
//!   5 only Trumpet 1 · 6 only Trumpet 2 · 7 both rest · 8 unison ·
//!   9 Trumpet 2 above Trumpet 1 (crossing → separate) · 10 unison
namespace {
constexpr int Q = 480;
constexpr int BAR = 4 * Q;

class Jubelle_CondensedScore : public ::testing::Test
{
protected:
    void SetUp() override
    {
        const muse::String path = muse::String::fromUtf8(std::string(MU_ROOT_DIR) + "/src/notation/tests/jubelle_chordid_data/condense.mscx");
        original = ScoreRW::readScore(path, true);
        ASSERT_TRUE(original);
        ASSERT_EQ(original->nstaves(), 3u);
    }

    void TearDown() override
    {
        project.reset();
        delete original;
    }

    MasterScore* make(const std::vector<CondenseOverride>& overrides = {})
    {
        project = jc::makeCondensedCopy(original, overrides, &report);
        return project ? project->masterScore() : nullptr;
    }

    static Measure* bar(Score* s, int index)
    {
        Measure* m = s->firstMeasure();
        for (int i = 0; i < index && m; ++i) {
            m = m->nextMeasure();
        }
        return m;
    }

    //! "tick:duration:pitches" for one track over one bar
    static std::vector<std::string> music(Score* s, track_idx_t track, int barIndex)
    {
        std::vector<std::string> out;
        Measure* m = bar(s, barIndex);
        for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
            EngravingItem* e = seg->element(track);
            if (!e) {
                continue;
            }
            std::string t = std::to_string((seg->tick() - m->tick()).ticks()) + ":" + std::to_string(toChordRest(e)->actualTicks().ticks());
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

    static std::vector<std::string> labels(Score* s, size_t staffIdx)
    {
        std::vector<std::string> out;
        int i = 0;
        for (Measure* m = s->firstMeasure(); m; m = m->nextMeasure(), ++i) {
            for (Segment* seg = m->first(); seg; seg = seg->next()) {
                for (EngravingItem* a : seg->annotations()) {
                    if (a->isStaffText() && a->staffIdx() == staffIdx) {
                        out.push_back(std::to_string(i + 1) + ":" + toStaffText(a)->plainText().toStdString());
                    }
                }
            }
        }
        return out;
    }

    //! Is the staff shown on the system that holds bar `barIndex`?
    static bool shown(Score* s, size_t staffIdx, int barIndex)
    {
        Measure* m = bar(s, barIndex);
        return m && m->system() && m->system()->staff(staffIdx)->show();
    }

    static std::string saved(MasterScore* s, const muse::String& name)
    {
        EXPECT_TRUE(ScoreRW::saveScore(s, name));
        FILE* f = std::fopen(name.toStdString().c_str(), "rb");
        std::string text;
        if (f) {
            char buf[4096];
            size_t n = 0;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
                text.append(buf, n);
            }
            std::fclose(f);
        }
        return text;
    }

    MasterScore* original = nullptr;
    std::shared_ptr<EngravingProject> project;
    jc::CondenseReport report;
};
}

TEST_F(Jubelle_CondensedScore, FindsTheTrumpetPair)
{
    const std::vector<jc::CondenseGroup> g = jc::condenseGroups(original);
    ASSERT_EQ(g.size(), 1u) << "the trombone has no partner";
    EXPECT_EQ(g[0].first, 0u);
    EXPECT_EQ(g[0].second, 1u);
    EXPECT_EQ(jc::combinedName(u"Trumpet 1", u"Trumpet 2"), u"Trumpet 1.2");
    EXPECT_EQ(jc::combinedName(u"Tpt. 1", u"Tpt. 2"), u"Tpt. 1.2");
    EXPECT_EQ(jc::combinedName(u"Flute", u"Flute"), u"Flute 1.2");
}

TEST_F(Jubelle_CondensedScore, TheOriginalIsNeverChanged)
{
    const std::string before = saved(original, u"jubelle_condense_original_before.mscx");
    const size_t undoBefore = original->undoStack()->size();
    MasterScore* copy = make();
    ASSERT_TRUE(copy);
    EXPECT_EQ(saved(original, u"jubelle_condense_original_after.mscx"), before);
    EXPECT_EQ(original->undoStack()->size(), undoBefore) << "nothing to undo in the original";
    EXPECT_FALSE(jc::isCondensedCopy(original));
    EXPECT_NE(copy, original);
}

TEST_F(Jubelle_CondensedScore, SharedStavesWhereTheMusicFits)
{
    MasterScore* copy = make();
    ASSERT_TRUE(copy);
    EXPECT_EQ(report.groups, 1);
    EXPECT_EQ(report.systems, 3);
    EXPECT_EQ(report.systemsCondensed, 2);
    EXPECT_EQ(report.systemsSeparate, 1);
    EXPECT_TRUE(jc::isCondensedCopy(copy));

    // Trumpet 2's staff is hidden on the two shared systems, shown on the third
    EXPECT_FALSE(shown(copy, 1, 0));
    EXPECT_FALSE(shown(copy, 1, 4));
    EXPECT_TRUE(shown(copy, 1, 8)) << "bar 9 crosses: both trumpets keep their staves on that system";
    EXPECT_TRUE(shown(copy, 0, 0));
    EXPECT_TRUE(shown(copy, 2, 0)) << "other instruments are untouched";

    // Unison: one voice
    EXPECT_EQ(music(copy, 0, 0), (std::vector<std::string> { "0:480:72", "480:480:72", "960:480:72", "1440:480:72" }));
    // Same rhythm: chords on one stem
    EXPECT_EQ(music(copy, 0, 2), (std::vector<std::string> { "0:480:72:76", "480:480:74:77", "960:960:76:79" }));
    EXPECT_TRUE(music(copy, 1, 2).empty()) << "no second voice for chords";
    // Different rhythms: two voices
    EXPECT_EQ(music(copy, 0, 3), (std::vector<std::string> { "0:1920:79" }));
    EXPECT_EQ(music(copy, 1, 3), (std::vector<std::string> { "0:480:72", "480:480:74", "960:480:76", "1440:480:72" }));
    // Only Trumpet 2 plays: its music on the shared staff
    EXPECT_EQ(music(copy, 0, 5), (std::vector<std::string> { "0:480:76", "480:480:76", "960:480:76", "1440:480:76" }));
    // Trumpet 2's own staff is empty in the shared bars
    for (int b = 0; b < 8; ++b) {
        for (const std::string& e : music(copy, VOICES, b)) {
            EXPECT_NE(e.find("rest"), std::string::npos) << "bar " << b + 1;
        }
    }
    // The separate system is exactly the original music
    EXPECT_EQ(music(copy, VOICES, 8), music(original, VOICES, 8));
    EXPECT_EQ(music(copy, 0, 8), music(original, 0, 8));
}

TEST_F(Jubelle_CondensedScore, LabelsNamesAndMarkings)
{
    MasterScore* copy = make();
    ASSERT_TRUE(copy);
    EXPECT_EQ(labels(copy, 0), (std::vector<std::string> { "1:a2", "5:1.", "6:2.", "8:a2" }));

    // The shared staff is named for both players; the separate system for its own
    Part* p = copy->staff(0)->part();
    EXPECT_EQ(p->instruments().size(), 2u) << "a hidden instrument change renames it";
    // The pair is still recognised in the copy (for the override commands there)
    const std::vector<jc::CondenseGroup> inCopy = jc::condenseGroups(copy);
    ASSERT_EQ(inCopy.size(), 1u);
    EXPECT_EQ(inCopy[0].first, 0u);
    EXPECT_EQ(p->longName(Fraction(0, 1)), u"Trumpet 1.2");
    EXPECT_EQ(p->shortName(Fraction(0, 1)), u"Tpt. 1.2");
    EXPECT_EQ(p->longName(Fraction::fromTicks(8 * BAR)), u"Trumpet 1");
    EXPECT_EQ(copy->staff(1)->part()->longName(Fraction(0, 1)), u"Trumpet 2");

    // Trumpet 2's p (two-voice bar) comes along, below in voice 2; bar 1's mf isn't doubled
    int mfBar1 = 0;
    bool pInVoice2 = false;
    for (Segment* s = bar(copy, 0)->first(); s; s = s->next()) {
        for (EngravingItem* a : s->annotations()) {
            if (a->isDynamic() && a->staffIdx() == 0) {
                ++mfBar1;
            }
        }
    }
    for (Segment* s = bar(copy, 3)->first(); s; s = s->next()) {
        for (EngravingItem* a : s->annotations()) {
            if (a->isDynamic() && a->track() == 1) {
                pInVoice2 = true;
            }
        }
    }
    EXPECT_EQ(mfBar1, 1);
    EXPECT_TRUE(pInVoice2);

    // Trumpet 2's slur moves with its notes into voice 2
    bool slurInVoice2 = false;
    for (const auto& [tick, sp] : copy->spanner()) {
        (void)tick;
        if (sp->isSlur() && sp->track() == 1 && sp->tick() == Fraction::fromTicks(3 * BAR)) {
            slurInVoice2 = true;
        }
        EXPECT_FALSE(sp->isSlur() && sp->staffIdx() == 1 && sp->tick() < Fraction::fromTicks(8 * BAR)) << "no slur left on the hidden staff";
    }
    EXPECT_TRUE(slurInVoice2);
}

TEST_F(Jubelle_CondensedScore, OverridesKeepSeparateOrCondense)
{
    // Keep bar 2 separate: its whole system stays separate
    MasterScore* copy = make({ { 0, 1, 1, CondenseOverride::Mode::Separate } });
    ASSERT_TRUE(copy);
    EXPECT_EQ(report.systemsCondensed, 1);
    EXPECT_TRUE(shown(copy, 1, 0));
    EXPECT_FALSE(shown(copy, 1, 4));

    // Condense bar 9 although the parts cross: two voices, the system is shared
    copy = make({ { 0, 8, 8, CondenseOverride::Mode::Condense } });
    ASSERT_TRUE(copy);
    EXPECT_EQ(report.systemsCondensed, 3);
    EXPECT_FALSE(shown(copy, 1, 8));
    EXPECT_EQ(music(copy, 1, 8), (std::vector<std::string> { "0:1920:79" }));

    // Overrides are kept in the copy, and editing them there is undoable
    EXPECT_EQ(jc::readOverrides(copy).size(), 1u);
    copy->startCmd(muse::TranslatableString::untranslatable("override"));
    jc::setOverride(copy, 0, 0, 3, CondenseOverride::Mode::Separate);
    copy->endCmd();
    EXPECT_EQ(jc::readOverrides(copy).size(), 2u);
    copy->undoRedo(true, nullptr);
    EXPECT_EQ(jc::readOverrides(copy).size(), 1u);
    // A new choice over the same bars replaces the old one
    copy->startCmd(muse::TranslatableString::untranslatable("override"));
    jc::setOverride(copy, 0, 8, 8, std::nullopt);
    copy->endCmd();
    EXPECT_TRUE(jc::readOverrides(copy).empty());
}

TEST_F(Jubelle_CondensedScore, SavedCopyReopensAndCantBeCondensedAgain)
{
    MasterScore* copy = make({ { 0, 1, 1, CondenseOverride::Mode::Separate } });
    ASSERT_TRUE(copy);
    ASSERT_TRUE(ScoreRW::saveScore(copy, u"jubelle_condensed_copy.mscx"));
    MasterScore* reopened = ScoreRW::readScore(u"jubelle_condensed_copy.mscx", true);
    ASSERT_TRUE(reopened);
    reopened->doLayout();
    EXPECT_TRUE(jc::isCondensedCopy(reopened));
    EXPECT_EQ(jc::readOverrides(reopened), jc::readOverrides(copy));
    EXPECT_TRUE(shown(reopened, 1, 0)) << "the override's system is separate";
    EXPECT_FALSE(shown(reopened, 1, 4)) << "hidden staves stay hidden after reopening";
    EXPECT_EQ(labels(reopened, 0), labels(copy, 0));

    jc::CondenseReport r;
    EXPECT_FALSE(jc::makeCondensedCopy(reopened, {}, &r)) << "a copy is updated from the original, not condensed again";
    EXPECT_FALSE(r.notes.empty());
    delete reopened;
}

TEST_F(Jubelle_CondensedScore, UpdatingReplacesTheCopy)
{
    MasterScore* copy = make();
    ASSERT_TRUE(copy);
    // An edit made in the copy...
    copy->startCmd(muse::TranslatableString::untranslatable("edit"));
    Note* n = toChord(bar(copy, 0)->first(SegmentType::ChordRest)->element(0))->upNote();
    n->undoChangeProperty(Pid::PITCH, 71);
    copy->endCmd();
    // ...is gone after the next update, which is made from the original again
    MasterScore* again = make(jc::readOverrides(copy));
    ASSERT_TRUE(again);
    EXPECT_EQ(music(again, 0, 0).front(), "0:480:72");
}

TEST_F(Jubelle_CondensedScore, NothingToCondense)
{
    // One flute: no pair of the same instrument, so nothing is made
    const muse::String path = muse::String::fromUtf8(std::string(MU_ROOT_DIR) + "/src/engraving/tests/jubelle_courtesy_data/courtesy.mscx");
    MasterScore* other = ScoreRW::readScore(path, true);
    ASSERT_TRUE(other);
    EXPECT_TRUE(jc::condenseGroups(other).empty());
    jc::CondenseReport r;
    EXPECT_FALSE(jc::makeCondensedCopy(other, {}, &r));
    EXPECT_FALSE(r.notes.empty());
    delete other;
}


TEST_F(Jubelle_CondensedScore, TiesMoveWithTheNotes)
{
    // Trumpet 2, bar 4 (two voices): tie beat 1 to beat 2 (both C5)
    original->startCmd(muse::TranslatableString::untranslatable("tie"));
    Measure* m4 = bar(original, 3);
    Segment* s1 = m4->first(SegmentType::ChordRest);
    Segment* s2 = s1->next(SegmentType::ChordRest);
    Note* n1 = toChord(s1->element(VOICES))->upNote();
    Note* n2 = toChord(s2->element(VOICES))->upNote();
    n2->undoChangeProperty(Pid::PITCH, 72);
    n2->undoChangeProperty(Pid::TPC1, 14);
    n2->undoChangeProperty(Pid::TPC2, 14);
    Tie* tie = Factory::createTie(n1);
    tie->setStartNote(n1);
    tie->setEndNote(n2);
    tie->setTrack(n1->track());
    tie->setTick(n1->tick());
    tie->setTick2(n2->tick());
    original->undoAddElement(tie);
    original->endCmd();

    MasterScore* copy = make();
    ASSERT_TRUE(copy);
    Measure* c4 = bar(copy, 3);
    Segment* t1 = c4->first(SegmentType::ChordRest);
    Segment* t2 = t1->next(SegmentType::ChordRest);
    ASSERT_TRUE(t1->element(1) && t1->element(1)->isChord());
    Note* a = toChord(t1->element(1))->upNote();
    ASSERT_TRUE(a->tieFor()) << "the tie moved into voice 2 with its notes";
    EXPECT_EQ(a->tieFor()->endNote()->chord(), toChord(t2->element(1)));
    // and the original still has its tie on Trumpet 2's own staff
    EXPECT_TRUE(n1->tieFor());
}

//! Real scores that come with MuseScore (demos, a large test score, band and
//! orchestra templates): condensing never crashes, the
//! original stays the same, and the copy saves and reopens. (The demos in the
//! old 2.x format can't be read by this Qt-free test tool: their chord-symbol
//! descriptions need Qt; so do Brassed_Up's and Reunion's chord symbols and
//! images. They are covered by the app checks instead.)
TEST(Jubelle_CondensedScoreDemos, RealScores)
{
    int condensedSomewhere = 0;
    for (const char* name : { "demos/Dawn.mscx", "demos/Dynamic_Strings.mscx", "demos/Fugue_1.mscx", "demos/Unclaimed_Gift.mscx",
                              "share/autobotscripts/data/Big_Score.mscz",
                              "share/templates/05-Jazz/02-Big_Band/02-Big_Band.mscx",
                              "share/templates/07-Band_and_Percussion/01-Concert_Band/01-Concert_Band.mscx",
                              "share/templates/08-Orchestral/02-Symphony_Orchestra/02-Symphony_Orchestra.mscx" }) {
        SCOPED_TRACE(name);
        const muse::String path = muse::String::fromUtf8(std::string(MU_ROOT_DIR) + "/" + name);
        MasterScore* original = ScoreRW::readScore(path, true);
        ASSERT_TRUE(original);
        const muse::String before = u"jubelle_demo_before.mscx";
        const muse::String after = u"jubelle_demo_after.mscx";
        ASSERT_TRUE(ScoreRW::saveScore(original, before));
        jc::CondenseReport report;
        std::shared_ptr<EngravingProject> project = jc::makeCondensedCopy(original, {}, &report);
        ASSERT_TRUE(ScoreRW::saveScore(original, after));
        FILE* a = std::fopen(before.toStdString().c_str(), "rb");
        FILE* b = std::fopen(after.toStdString().c_str(), "rb");
        ASSERT_TRUE(a && b);
        std::string ta, tb;
        char buf[4096];
        size_t n = 0;
        while ((n = std::fread(buf, 1, sizeof buf, a)) > 0) {
            ta.append(buf, n);
        }
        while ((n = std::fread(buf, 1, sizeof buf, b)) > 0) {
            tb.append(buf, n);
        }
        std::fclose(a);
        std::fclose(b);
        EXPECT_EQ(ta, tb) << "the original must not change";

        if (project) {
            if (report.systemsCondensed > 0) {
                ++condensedSomewhere;
            }
            ASSERT_TRUE(ScoreRW::saveScore(project->masterScore(), u"jubelle_demo_copy.mscx"));
            MasterScore* reopened = ScoreRW::readScore(u"jubelle_demo_copy.mscx", true);
            ASSERT_TRUE(reopened);
            EXPECT_TRUE(jc::isCondensedCopy(reopened));
            EXPECT_EQ(reopened->nstaves(), original->nstaves());
            delete reopened;
            std::fprintf(stderr, "%s: %d pair(s), %d systems, %d shared, %d separate, %zu note(s)\n", name, report.groups, report.systems,
                         report.systemsCondensed, report.systemsSeparate, report.notes.size());
            for (const muse::String& note : report.notes) {
                std::fprintf(stderr, "    %s\n", note.toStdString().c_str());
            }
        } else {
            std::fprintf(stderr, "%s: nothing to condense\n", name);
        }
        project.reset();
        delete original;
    }
    EXPECT_GT(condensedSomewhere, 0) << "at least one demo has players that share staves";
}


//! condense_long.mscx: four trumpets (two pairs), 48 bars of mixed writing
//! (unison, chords, different rhythms, one player, rests, crossing parts and a
//! player using two voices), line breaks every 4 bars. Separate systems are
//! expected exactly where a bar crosses or uses two voices: pair 1 on systems
//! 5 and 6, pair 2 on systems 1, 5 and 7.
TEST(Jubelle_CondensedScoreStress, NoMusicIsLost)
{
    const muse::String path = muse::String::fromUtf8(std::string(MU_ROOT_DIR) + "/src/notation/tests/jubelle_chordid_data/condense_long.mscx");
    MasterScore* original = ScoreRW::readScore(path, true);
    ASSERT_TRUE(original);
    jc::CondenseReport report;
    std::shared_ptr<EngravingProject> project = jc::makeCondensedCopy(original, {}, &report);
    ASSERT_TRUE(project);
    MasterScore* copy = project->masterScore();
    EXPECT_EQ(report.groups, 2);
    EXPECT_EQ(report.systems, 12);
    EXPECT_EQ(report.systemsSeparate, 5);
    EXPECT_EQ(report.systemsCondensed, 19);

    // Every note of each pair is still there, at the same time and pitch
    auto sounding = [](Score* s, size_t a, size_t b, Measure* m) {
        std::set<std::pair<int, int> > out;
        for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
            for (size_t st : { a, b }) {
                for (voice_idx_t v = 0; v < VOICES; ++v) {
                    EngravingItem* e = seg->element(st * VOICES + v);
                    if (e && e->isChord()) {
                        for (Note* n : toChord(e)->notes()) {
                            out.insert({ seg->tick().ticks(), n->pitch() });
                        }
                    }
                }
            }
        }
        return out;
    };
    const std::set<int> separate1 { 4, 5 };
    const std::set<int> separate2 { 0, 4, 6 };
    int barNo = 0;
    for (Measure* mo = original->firstMeasure(), * mc = copy->firstMeasure(); mo && mc;
         mo = mo->nextMeasure(), mc = mc->nextMeasure(), ++barNo) {
        SCOPED_TRACE("bar " + std::to_string(barNo + 1));
        for (size_t pair = 0; pair < 2; ++pair) {
            const size_t a = pair * 2;
            const size_t b = a + 1;
            EXPECT_EQ(sounding(copy, a, b, mc), sounding(original, a, b, mo));
            const int system = barNo / 4;
            const bool sep = (pair == 0 ? separate1 : separate2).count(system) > 0;
            ASSERT_TRUE(mc->system());
            EXPECT_EQ(mc->system()->staff(b)->show(), sep) << "pair " << pair + 1;
            if (!sep) {
                for (Segment* seg = mc->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
                    EngravingItem* e = seg->element(b * VOICES);
                    EXPECT_FALSE(e && e->isChord()) << "the second player's staff is empty on shared systems";
                }
            }
        }
    }
    delete original;
}

TEST_F(Jubelle_CondensedScore, WritesTheCopyNextToTheScoreAndNeverReplacesOtherFiles)
{
    EXPECT_EQ(jc::condensedPathFor(muse::io::path_t(u"/scores/Big Band/Shout.mscz")),
              muse::io::path_t(u"/scores/Big Band/Shout (condensed).mscz"));

    MasterScore* copy = make({ { 0, 1, 1, CondenseOverride::Mode::Separate } });
    ASSERT_TRUE(copy);
    const muse::io::path_t path(u"jubelle_condense_out (condensed).mscz");
    std::remove(path.toStdString().c_str());

    muse::String error;
    ASSERT_TRUE(jc::writeCondensedFile(*project, path, &error)) << error.toStdString();
    jc::ExistingCopy e = jc::readExistingCopy(path, original->iocContext());
    EXPECT_TRUE(e.exists);
    EXPECT_TRUE(e.isCondensedCopy);
    EXPECT_EQ(e.overrides.size(), 1u) << "overrides are read back for the next update";

    // Updating replaces the copy
    MasterScore* again = make(e.overrides);
    ASSERT_TRUE(again);
    EXPECT_TRUE(jc::writeCondensedFile(*project, path, &error)) << error.toStdString();

    // A different file with that name is never replaced
    ASSERT_TRUE(ScoreRW::saveScore(original, u"jubelle_condense_other.mscx"));
    const muse::io::path_t other(u"jubelle_condense_other.mscx");
    EXPECT_FALSE(jc::readExistingCopy(other, original->iocContext()).isCondensedCopy);
    EXPECT_FALSE(jc::writeCondensedFile(*project, other, &error));
    EXPECT_FALSE(error.empty());
    MasterScore* still = ScoreRW::readScore(u"jubelle_condense_other.mscx", true);
    ASSERT_TRUE(still);
    EXPECT_FALSE(jc::isCondensedCopy(still)) << "the other file is untouched";
    delete still;
}
