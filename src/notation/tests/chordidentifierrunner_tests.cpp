/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 * Music Composition & Notation
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include <gtest/gtest.h>

#include <map>
#include <tuple>
#include <vector>

#include "engraving/dom/chord.h"
#include "engraving/dom/factory.h"
#include "engraving/dom/harmony.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/select.h"

#include "notation/internal/jubelle/chordidentifierrunner.h"

#include "utils/scorerw.h"

using namespace mu::engraving;
namespace ci = mu::notation::chordidentifier;

//! Jubelle Chord Identifier on the real notation engine.
//! progression.mscx (Upper treble + Lower bass, C major):
//!   bar 1: C, then F/C (F A C over the held C3)   bar 2: G7 (upper alone: Bdim)
//!   bar 3: C in two voices, repeated on beat 3     bar 4: Dm7     bar 5: cluster
//!   bar 6: Gb (Gb Bb Db)   bar 7: F# (F# A# C#)
namespace {
MasterScore* load(const char* file)
{
    const muse::String path = muse::String::fromUtf8(std::string(MU_ROOT_DIR) + "/src/notation/tests/jubelle_chordid_data/" + file);
    MasterScore* score = ScoreRW::readScore(path, true);
    EXPECT_TRUE(score) << file;
    return score;
}

struct Placed {
    int bar;       //!< 1-based
    int beat;      //!< 1-based quarter beat in 4/4
    int staff;
    HarmonyType type;
    std::string text;
};

std::vector<Placed> symbols(Score* score)
{
    std::vector<Placed> out;
    for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        for (EngravingItem* a : s->annotations()) {
            if (!a->isHarmony()) {
                continue;
            }
            Harmony* h = toHarmony(a);
            const Measure* m = s->measure();
            const int beat = ((s->tick() - m->tick()).ticks() / Constants::DIVISION) + 1;
            out.push_back({ m->no() + 1, beat, int(h->staffIdx()), h->harmonyType(), h->harmonyName().toStdString() });
        }
    }
    return out;
}

std::vector<std::string> texts(Score* score, HarmonyType type = HarmonyType::STANDARD)
{
    std::vector<std::string> out;
    for (const Placed& s : symbols(score)) {
        if (s.type == type) {
            out.push_back(std::to_string(s.bar) + "." + std::to_string(s.beat) + " " + s.text);
        }
    }
    return out;
}

ci::Report runCmd(Score* score, const ci::Options& o)
{
    score->startCmd(muse::TranslatableString::untranslatable("Chord Identifier"));
    ci::Report r = ci::run(score, o);
    score->endCmd();
    return r;
}

void selectRange(Score* score, int firstBar, int lastBar, staff_idx_t staffStart, staff_idx_t staffEnd)
{
    Measure* first = score->firstMeasure();
    for (int i = 1; i < firstBar; ++i) {
        first = first->nextMeasure();
    }
    Measure* last = first;
    for (int i = firstBar; i < lastBar; ++i) {
        last = last->nextMeasure();
    }
    // As a user does it: click the first bar, shift-click the last bar on the last staff.
    score->deselectAll();
    score->select(first, SelectType::RANGE, staffStart);
    score->select(last, SelectType::RANGE, staffEnd - 1);
}

Note* noteAt(Score* score, int bar, int beat, staff_idx_t staff, int pitch)
{
    Measure* m = score->firstMeasure();
    for (int i = 1; i < bar; ++i) {
        m = m->nextMeasure();
    }
    const Fraction tick = m->tick() + Fraction((beat - 1), 4);
    for (voice_idx_t v = 0; v < VOICES; ++v) {
        Segment* s = m->findSegment(SegmentType::ChordRest, tick);
        EngravingItem* e = s ? s->element(staff * VOICES + v) : nullptr;
        if (e && e->isChord()) {
            for (Note* n : toChord(e)->notes()) {
                if (n->pitch() == pitch) {
                    return n;
                }
            }
        }
    }
    ADD_FAILURE() << "no note " << pitch << " at bar " << bar << " beat " << beat;
    return nullptr;
}

//! Everything musical about every note, except its colour.
std::vector<std::tuple<int, int, int, int, int>> musicOf(Score* score)
{
    std::vector<std::tuple<int, int, int, int, int>> out;
    for (Segment* s = score->firstSegment(SegmentType::ChordRest); s; s = s->next1(SegmentType::ChordRest)) {
        for (track_idx_t t = 0; t < score->ntracks(); ++t) {
            EngravingItem* e = s->element(t);
            if (e && e->isChordRest()) {
                ChordRest* cr = toChordRest(e);
                if (cr->isChord()) {
                    for (Note* n : toChord(cr)->notes()) {
                        out.emplace_back(s->tick().ticks(), int(t), n->pitch(), n->tpc1(), cr->actualTicks().ticks());
                    }
                } else {
                    out.emplace_back(s->tick().ticks(), int(t), -1, -1, cr->actualTicks().ticks());
                }
            }
        }
    }
    return out;
}

using Texts = std::vector<std::string>;
}

TEST(ChordIdentifierScoreTests, WholeScoreWhenNothingIsSelected)
{
    MasterScore* score = load("progression.mscx");
    ASSERT_TRUE(score);
    score->deselectAll();

    ci::Scope sc = ci::scope(score);
    EXPECT_TRUE(sc.wholeScore);
    EXPECT_EQ(sc.firstBar, 1);
    EXPECT_EQ(sc.lastBar, 7);
    EXPECT_EQ(sc.staffCount, 2);

    ci::Report r = runCmd(score, ci::Options());
    // Held bass note: beat 3 of bar 1 is F/C. Repeated C in bar 3 gets one symbol.
    // Bar 5 (cluster) gets none. Spelling follows the written notes (Gb vs F#).
    EXPECT_EQ(texts(score), (Texts { "1.1 C", "1.3 F/C", "2.1 G7", "3.1 C", "4.1 Dm7", "6.1 Gb", "7.1 F#" }));
    EXPECT_EQ(r.added, 7);
    EXPECT_EQ(r.unrecognised, 1);
    for (const Placed& s : symbols(score)) {
        EXPECT_EQ(s.staff, 0) << "chord symbols go above the top staff";
    }
    delete score;
}

TEST(ChordIdentifierScoreTests, SelectedPassageOnlyAnalysesThoseBarsAndStaves)
{
    MasterScore* score = load("progression.mscx");
    ASSERT_TRUE(score);

    selectRange(score, 2, 3, 0, 1); // bars 2-3, upper staff only
    ci::Scope sc = ci::scope(score);
    EXPECT_FALSE(sc.wholeScore);
    EXPECT_EQ(sc.firstBar, 2);
    EXPECT_EQ(sc.lastBar, 3);
    EXPECT_EQ(sc.staffCount, 1);
    EXPECT_EQ(sc.firstStaffName, "Upper");

    runCmd(score, ci::Options());
    // Without the lower staff, bar 2 is B-D-F (Bdim) and bar 3 is C/E (E in the upper voice is lowest).
    EXPECT_EQ(texts(score), (Texts { "2.1 Bdim", "3.1 C/G" }));

    // Both staves, same bars: G7 and C.
    score->undoRedo(true, nullptr);
    selectRange(score, 2, 3, 0, 2);
    runCmd(score, ci::Options());
    EXPECT_EQ(texts(score), (Texts { "2.1 G7", "3.1 C" }));
    delete score;
}

TEST(ChordIdentifierScoreTests, SelectedNotesOnlyUseThoseNotes)
{
    MasterScore* score = load("progression.mscx");
    ASSERT_TRUE(score);
    std::vector<EngravingItem*> notes {
        noteAt(score, 1, 3, 0, 65), noteAt(score, 1, 3, 0, 69), noteAt(score, 1, 3, 0, 72)
    };
    score->select(notes, SelectType::ADD, 0);
    EXPECT_TRUE(ci::scope(score).listSelection);
    EXPECT_EQ(ci::scope(score).noteCount, 3);

    runCmd(score, ci::Options());
    EXPECT_EQ(texts(score), (Texts { "1.3 F" })); // the held C3 isn't selected
    delete score;
}

TEST(ChordIdentifierScoreTests, MultipleVoicesAndRepeatedChords)
{
    MasterScore* score = load("progression.mscx");
    ASSERT_TRUE(score);
    ci::Options o;
    o.onlyWhenChordChanges = false;
    runCmd(score, o);
    // Bar 3: voice 1 (E5) + voice 2 (G4 C5) + lower C3 = C, on both beats.
    const Texts t = texts(score);
    EXPECT_NE(std::find(t.begin(), t.end(), "3.1 C"), t.end());
    EXPECT_NE(std::find(t.begin(), t.end(), "3.3 C"), t.end());
    delete score;
}

TEST(ChordIdentifierScoreTests, RepeatedRunsNeverDuplicateSymbols)
{
    MasterScore* score = load("progression.mscx");
    ASSERT_TRUE(score);
    runCmd(score, ci::Options());
    const Texts first = texts(score);

    ci::Report again = runCmd(score, ci::Options());
    EXPECT_EQ(again.added, 0);
    EXPECT_EQ(again.keptExisting, 7);
    EXPECT_EQ(texts(score), first);

    ci::Options replace;
    replace.existing = ci::ExistingSymbols::Replace;
    ci::Report r = runCmd(score, replace);
    EXPECT_EQ(r.added, 0);
    EXPECT_EQ(r.replaced, 0); // same text: nothing to replace
    EXPECT_EQ(texts(score), first);
    EXPECT_EQ(symbols(score).size(), first.size());
    delete score;
}

TEST(ChordIdentifierScoreTests, ExistingSymbolsAreKeptOrReplaced)
{
    MasterScore* score = load("progression.mscx");
    ASSERT_TRUE(score);
    // A chord symbol of your own on bar 1, beat 1.
    Segment* seg = score->firstMeasure()->first(SegmentType::ChordRest);
    score->startCmd(muse::TranslatableString::untranslatable("test"));
    Harmony* mine = Factory::createHarmony(seg);
    mine->setTrack(0);
    mine->setParent(seg);
    mine->setHarmony(u"Am");
    score->undoAddElement(mine);
    score->endCmd();

    ci::Report keep = runCmd(score, ci::Options());
    EXPECT_EQ(keep.keptExisting, 1);
    EXPECT_EQ(texts(score).front(), "1.1 Am");

    ci::Options replace;
    replace.existing = ci::ExistingSymbols::Replace;
    ci::Report r = runCmd(score, replace);
    EXPECT_EQ(r.replaced, 1);
    EXPECT_EQ(texts(score).front(), "1.1 C");
    int atBar1Beat1 = 0;
    for (const Placed& s : symbols(score)) {
        atBar1Beat1 += (s.bar == 1 && s.beat == 1) ? 1 : 0;
    }
    EXPECT_EQ(atBar1Beat1, 1);
    delete score;
}

TEST(ChordIdentifierScoreTests, UndoAndRedo)
{
    MasterScore* score = load("progression.mscx");
    ASSERT_TRUE(score);
    runCmd(score, ci::Options());
    const Texts added = texts(score);
    ASSERT_EQ(added.size(), 7u);

    score->undoRedo(true, nullptr);   // one undo removes everything the run added
    EXPECT_TRUE(texts(score).empty());

    score->undoRedo(false, nullptr);  // redo brings it back
    EXPECT_EQ(texts(score), added);
    delete score;
}

TEST(ChordIdentifierScoreTests, MusicIsNeverChanged)
{
    MasterScore* score = load("progression.mscx");
    ASSERT_TRUE(score);
    const auto before = musicOf(score);
    ci::Options all;
    all.romanNumerals = true;
    all.highlightChordTones = true;
    all.existing = ci::ExistingSymbols::Replace;
    runCmd(score, all);
    EXPECT_EQ(musicOf(score), before);
    delete score;
}

TEST(ChordIdentifierScoreTests, HighlightingOnColoursChordTones)
{
    MasterScore* score = load("progression.mscx");
    ASSERT_TRUE(score);
    ci::Options o;
    o.highlightChordTones = true;
    ci::Report r = runCmd(score, o);
    EXPECT_GT(r.notesColoured, 0);

    using H = ci::HighlightColors;
    EXPECT_EQ(noteAt(score, 1, 1, 1, 48)->color(), H::root());     // C3
    EXPECT_EQ(noteAt(score, 1, 1, 0, 64)->color(), H::third());    // E4
    EXPECT_EQ(noteAt(score, 1, 1, 0, 67)->color(), H::fifth());    // G4
    EXPECT_EQ(noteAt(score, 2, 1, 0, 77)->color(), H::seventh());  // F5 in G7
    // Bar 5 is not a chord: its notes keep their colour.
    EXPECT_FALSE(H::isHighlight(noteAt(score, 5, 1, 0, 60)->color()));

    // Remove highlighting: only Jubelle's colours are reset.
    score->startCmd(muse::TranslatableString::untranslatable("test"));
    ci::Report removed = ci::removeHighlight(score);
    score->endCmd();
    EXPECT_GT(removed.coloursRemoved, 0);
    EXPECT_FALSE(H::isHighlight(noteAt(score, 1, 1, 0, 64)->color()));
    delete score;
}

TEST(ChordIdentifierScoreTests, HighlightingOffNeverTouchesColours)
{
    MasterScore* score = load("progression.mscx");
    ASSERT_TRUE(score);
    // A colour of your own on E4 (bar 1).
    Note* e4 = noteAt(score, 1, 1, 0, 64);
    const muse::Color mine(0x12, 0x34, 0x56);
    score->startCmd(muse::TranslatableString::untranslatable("test"));
    e4->undoChangeProperty(Pid::COLOR, PropertyValue::fromValue(mine));
    score->endCmd();

    runCmd(score, ci::Options()); // highlighting off (the default)
    EXPECT_EQ(e4->color(), mine);
    EXPECT_FALSE(ci::HighlightColors::isHighlight(noteAt(score, 1, 1, 0, 67)->color()));

    // Remove highlighting never resets a colour you chose.
    score->startCmd(muse::TranslatableString::untranslatable("test"));
    ci::removeHighlight(score);
    score->endCmd();
    EXPECT_EQ(e4->color(), mine);
    delete score;
}

TEST(ChordIdentifierScoreTests, RomanNumeralsBelowTheBottomStaff)
{
    MasterScore* score = load("progression.mscx");
    ASSERT_TRUE(score);
    ci::Options o;
    o.chordSymbols = false;
    o.romanNumerals = true;
    runCmd(score, o);
    // C major: I, IV64 (F/C), V7, I, ii7, bV (Gb), #IV (F#).
    EXPECT_EQ(texts(score, HarmonyType::ROMAN), (Texts { "1.1 I", "1.3 IV64", "2.1 V7", "3.1 I", "4.1 ii7", "6.1 bV", "7.1 #IV" }));
    EXPECT_TRUE(texts(score, HarmonyType::STANDARD).empty());
    for (const Placed& s : symbols(score)) {
        EXPECT_EQ(s.staff, 1) << "Roman numerals go below the bottom staff";
    }

    // Both kinds at once, without figured bass.
    score->undoRedo(true, nullptr);
    ci::Options both;
    both.romanNumerals = true;
    both.figuredBass = false;
    runCmd(score, both);
    EXPECT_EQ(texts(score, HarmonyType::ROMAN).at(1), "1.3 IV");
    EXPECT_EQ(texts(score, HarmonyType::STANDARD).at(1), "1.3 F/C");
    delete score;
}

TEST(ChordIdentifierScoreTests, RomanNumeralsInAMinorKey)
{
    MasterScore* score = load("minor.mscx");
    ASSERT_TRUE(score);
    ci::Options o;
    o.romanNumerals = true;
    runCmd(score, o);
    EXPECT_EQ(texts(score, HarmonyType::ROMAN), (Texts { "1.1 i", "2.1 V65", "3.1 i" }));
    EXPECT_EQ(texts(score, HarmonyType::STANDARD), (Texts { "1.1 Am", "2.1 E7/G#", "3.1 Am" }));
    delete score;
}

TEST(ChordIdentifierScoreTests, SlashBassOption)
{
    MasterScore* score = load("progression.mscx");
    ASSERT_TRUE(score);
    ci::Options o;
    o.slashBass = false;
    runCmd(score, o);
    // F/C is shown as F; then C in bar 3 is still a change from G7.
    EXPECT_EQ(texts(score).at(1), "1.3 F");
    delete score;
}

TEST(ChordIdentifierScoreTests, TransposingStaffShowsWrittenPitch)
{
    MasterScore* score = load("trumpet.mscx");
    ASSERT_TRUE(score);
    ASSERT_FALSE(score->style().styleB(Sid::concertPitch));
    runCmd(score, ci::Options());
    // Concert C and C/E on a Bb trumpet are written D and D/F#.
    EXPECT_EQ(texts(score), (Texts { "1.1 D", "2.1 D/F#" }));

    score->undoRedo(true, nullptr);
    score->style().set(Sid::concertPitch, true);
    runCmd(score, ci::Options());
    EXPECT_EQ(texts(score), (Texts { "1.1 C", "2.1 C/E" }));
    delete score;
}
