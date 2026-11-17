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

#include "engraving/dom/accidental.h"
#include "engraving/dom/chord.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/note.h"
#include "engraving/dom/segment.h"

#include "utils/scorerw.h"

using namespace mu::engraving;

//! Jubelle Phase 4: automatic courtesy accidentals (style "jubelleCourtesyAccidentals").
//! courtesy.mscx, C major, flute:
//!   bar 1: F#4   bar 2: F4 F4   bar 3: G#4   bar 4: G5   bar 5: F4
static const String DATA_DIR(u"jubelle_courtesy_data/");

class Engraving_JubelleCourtesyTests : public ::testing::Test
{
protected:
    static AccidentalType accidentalOf(MasterScore* score, int measureIndex, int chordIndex)
    {
        Measure* m = score->firstMeasure();
        for (int i = 0; i < measureIndex && m; ++i) {
            m = m->nextMeasure();
        }
        EXPECT_TRUE(m);
        if (!m) {
            return AccidentalType::NONE;
        }
        int n = 0;
        for (Segment* s = m->first(SegmentType::ChordRest); s; s = s->next(SegmentType::ChordRest)) {
            EngravingItem* e = s->element(0);
            if (!e || !e->isChord()) {
                continue;
            }
            if (n++ == chordIndex) {
                Note* note = toChord(e)->upNote();
                return note->accidental() ? note->accidental()->accidentalType() : AccidentalType::NONE;
            }
        }
        ADD_FAILURE() << "chord not found";
        return AccidentalType::NONE;
    }

    static void setStyle(MasterScore* score, bool courtesy, bool allOctaves)
    {
        score->style().set(Sid::jubelleCourtesyAccidentals, courtesy);
        score->style().set(Sid::jubelleCourtesyAccidentalsAllOctaves, allOctaves);
        score->update();
        score->doLayout();
    }
};

TEST_F(Engraving_JubelleCourtesyTests, DisabledByDefault_MuseScoreBehaviourUnchanged)
{
    MasterScore* score = ScoreRW::readScore(DATA_DIR + u"courtesy.mscx");
    ASSERT_TRUE(score);

    EXPECT_FALSE(score->style().styleB(Sid::jubelleCourtesyAccidentals));
    score->doLayout();

    EXPECT_EQ(accidentalOf(score, 0, 0), AccidentalType::SHARP);
    EXPECT_EQ(accidentalOf(score, 1, 0), AccidentalType::NONE); // no courtesy natural
    EXPECT_EQ(accidentalOf(score, 1, 1), AccidentalType::NONE);
    EXPECT_EQ(accidentalOf(score, 2, 0), AccidentalType::SHARP);
    EXPECT_EQ(accidentalOf(score, 3, 0), AccidentalType::NONE);
    EXPECT_EQ(accidentalOf(score, 4, 0), AccidentalType::NONE);

    delete score;
}

TEST_F(Engraving_JubelleCourtesyTests, Enabled_SameOctave)
{
    MasterScore* score = ScoreRW::readScore(DATA_DIR + u"courtesy.mscx");
    ASSERT_TRUE(score);

    setStyle(score, true, false);

    EXPECT_EQ(accidentalOf(score, 0, 0), AccidentalType::SHARP);
    EXPECT_EQ(accidentalOf(score, 1, 0), AccidentalType::NATURAL); // courtesy
    EXPECT_EQ(accidentalOf(score, 1, 1), AccidentalType::NONE);    // only the first note
    EXPECT_EQ(accidentalOf(score, 2, 0), AccidentalType::SHARP);
    EXPECT_EQ(accidentalOf(score, 3, 0), AccidentalType::NONE);    // other octave: not by default
    EXPECT_EQ(accidentalOf(score, 4, 0), AccidentalType::NONE);    // two bars later: no courtesy

    delete score;
}

TEST_F(Engraving_JubelleCourtesyTests, Enabled_AllOctaves)
{
    MasterScore* score = ScoreRW::readScore(DATA_DIR + u"courtesy.mscx");
    ASSERT_TRUE(score);

    setStyle(score, true, true);

    EXPECT_EQ(accidentalOf(score, 1, 0), AccidentalType::NATURAL);
    EXPECT_EQ(accidentalOf(score, 3, 0), AccidentalType::NATURAL); // G5 after G#4
    EXPECT_EQ(accidentalOf(score, 4, 0), AccidentalType::NONE);

    delete score;
}

TEST_F(Engraving_JubelleCourtesyTests, TurningOffRemovesCourtesyAccidentals)
{
    MasterScore* score = ScoreRW::readScore(DATA_DIR + u"courtesy.mscx");
    ASSERT_TRUE(score);

    setStyle(score, true, true);
    EXPECT_EQ(accidentalOf(score, 1, 0), AccidentalType::NATURAL);

    setStyle(score, false, true);
    EXPECT_EQ(accidentalOf(score, 1, 0), AccidentalType::NONE);
    EXPECT_EQ(accidentalOf(score, 3, 0), AccidentalType::NONE);
    EXPECT_EQ(accidentalOf(score, 0, 0), AccidentalType::SHARP); // real accidentals untouched

    delete score;
}

TEST_F(Engraving_JubelleCourtesyTests, CourtesyFollowsEdits)
{
    MasterScore* score = ScoreRW::readScore(DATA_DIR + u"courtesy.mscx");
    ASSERT_TRUE(score);
    setStyle(score, true, false);
    EXPECT_EQ(accidentalOf(score, 1, 0), AccidentalType::NATURAL);

    // Change bar 1's F#4 to F4: the courtesy natural in bar 2 must go away.
    Measure* m1 = score->firstMeasure();
    Chord* c = toChord(m1->first(SegmentType::ChordRest)->element(0));
    Note* n = c->upNote();
    score->startCmd(TranslatableString::untranslatable("test"));
    n->undoChangeProperty(Pid::PITCH, 65);
    n->undoChangeProperty(Pid::TPC1, 13);
    n->undoChangeProperty(Pid::TPC2, 13);
    score->endCmd();

    EXPECT_EQ(accidentalOf(score, 0, 0), AccidentalType::NONE);
    EXPECT_EQ(accidentalOf(score, 1, 0), AccidentalType::NONE);

    delete score;
}

//! G2 rules. courtesy_rules.mscx, C major, piano:
//!   right hand: bar 1 F#4 G4 | bar 2 Bb4 | bar 3 C5 | bar 4 F#4 | bar 5 G4 | bar 6 F4
//!   left hand:  bar 1 C3 F4  | bar 2 C3  | bar 3 B4 | bar 4 C3  | bar 5 C3 | bar 6 C3
class Engraving_JubelleCourtesyRulesTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        score = ScoreRW::readScore(DATA_DIR + u"courtesy_rules.mscx");
        ASSERT_TRUE(score);
        ASSERT_EQ(score->nstaves(), 2u);
    }

    void TearDown() override { delete score; }

    Accidental* accidentalAt(size_t staff, int measureIndex, int chordIndex)
    {
        Measure* m = score->firstMeasure();
        for (int i = 0; i < measureIndex && m; ++i) {
            m = m->nextMeasure();
        }
        int n = 0;
        for (Segment* s = m ? m->first(SegmentType::ChordRest) : nullptr; s; s = s->next(SegmentType::ChordRest)) {
            EngravingItem* e = s->element(staff * VOICES);
            if (e && e->isChord() && n++ == chordIndex) {
                return toChord(e)->upNote()->accidental();
            }
        }
        return nullptr;
    }

    AccidentalType typeAt(size_t staff, int measureIndex, int chordIndex)
    {
        Accidental* a = accidentalAt(staff, measureIndex, chordIndex);
        return a ? a->accidentalType() : AccidentalType::NONE;
    }

    void set(Sid id, const PropertyValue& v)
    {
        score->style().set(id, v);
        score->update();
        score->doLayout();
    }

    //! Every accidental in the score, as "staff:bar:chord:type:bracket"
    std::vector<std::string> allAccidentals()
    {
        std::vector<std::string> out;
        for (size_t staff = 0; staff < 2; ++staff) {
            for (int bar = 0; bar < 6; ++bar) {
                for (int c = 0; c < 2; ++c) {
                    if (Accidental* a = accidentalAt(staff, bar, c)) {
                        out.push_back(std::to_string(staff) + ":" + std::to_string(bar) + ":" + std::to_string(c) + ":"
                                      + std::to_string(int(a->accidentalType())) + ":" + std::to_string(int(a->displayBracket())));
                    }
                }
            }
        }
        return out;
    }

    MasterScore* score = nullptr;
};

TEST_F(Engraving_JubelleCourtesyRulesTests, NewRulesOffByDefault_NothingChanges)
{
    EXPECT_EQ(score->style().styleI(Sid::jubelleCourtesyAccidentalsBars), 1);
    EXPECT_FALSE(score->style().styleB(Sid::jubelleCourtesyAccidentalsOtherStaves));
    EXPECT_FALSE(score->style().styleB(Sid::jubelleCourtesyAccidentalsParentheses));
    score->doLayout();
    const std::vector<std::string> museScore = allAccidentals();
    // MuseScore's own accidentals only: F#4 bar 1, Bb4 bar 2, F#4 bar 4
    EXPECT_EQ(museScore.size(), 3u);

    // The new rules do nothing while courtesy accidentals are off
    set(Sid::jubelleCourtesyAccidentalsBars, 4);
    set(Sid::jubelleCourtesyAccidentalsOtherStaves, true);
    set(Sid::jubelleCourtesyAccidentalsParentheses, true);
    EXPECT_EQ(allAccidentals(), museScore);

    // With courtesy on and the new rules at their defaults: as before G2
    set(Sid::jubelleCourtesyAccidentalsBars, 1);
    set(Sid::jubelleCourtesyAccidentalsOtherStaves, false);
    set(Sid::jubelleCourtesyAccidentalsParentheses, false);
    set(Sid::jubelleCourtesyAccidentals, true);
    EXPECT_EQ(typeAt(1, 0, 1), AccidentalType::NONE) << "left hand: no courtesy from the right hand";
    EXPECT_EQ(typeAt(1, 2, 0), AccidentalType::NONE);
    EXPECT_EQ(typeAt(0, 5, 0), AccidentalType::NONE) << "two bars later: none with 1 bar";
}

TEST_F(Engraving_JubelleCourtesyRulesTests, LookBackSeveralBars)
{
    set(Sid::jubelleCourtesyAccidentals, true);
    set(Sid::jubelleCourtesyAccidentalsBars, 2);
    EXPECT_EQ(typeAt(0, 5, 0), AccidentalType::NATURAL) << "F#4 in bar 4, F4 in bar 6";
    EXPECT_EQ(typeAt(0, 4, 0), AccidentalType::NONE);
    EXPECT_EQ(typeAt(0, 3, 0), AccidentalType::SHARP);
    // Bar 2's courtesy: Bb in bar 2 doesn't touch F; bar 1 F# → nothing on F in bars 2-3
    EXPECT_EQ(typeAt(0, 2, 0), AccidentalType::NONE);
    set(Sid::jubelleCourtesyAccidentalsBars, 1);
    EXPECT_EQ(typeAt(0, 5, 0), AccidentalType::NONE) << "back to one bar";
}

TEST_F(Engraving_JubelleCourtesyRulesTests, OtherStavesOfTheInstrument)
{
    set(Sid::jubelleCourtesyAccidentals, true);
    set(Sid::jubelleCourtesyAccidentalsOtherStaves, true);
    EXPECT_EQ(typeAt(1, 0, 1), AccidentalType::NATURAL) << "same bar: F#4 in the right hand, then F4 in the left";
    EXPECT_EQ(typeAt(1, 2, 0), AccidentalType::NATURAL) << "previous bar: Bb4 in the right hand, then B4 in the left";
    EXPECT_EQ(typeAt(1, 0, 0), AccidentalType::NONE);
    EXPECT_EQ(typeAt(0, 0, 0), AccidentalType::SHARP) << "real accidentals untouched";
    EXPECT_EQ(typeAt(0, 1, 0), AccidentalType::FLAT);

    set(Sid::jubelleCourtesyAccidentalsOtherStaves, false);
    EXPECT_EQ(typeAt(1, 0, 1), AccidentalType::NONE);
    EXPECT_EQ(typeAt(1, 2, 0), AccidentalType::NONE);
}

TEST_F(Engraving_JubelleCourtesyRulesTests, ParenthesesAreDrawnNotStored)
{
    set(Sid::jubelleCourtesyAccidentals, true);
    set(Sid::jubelleCourtesyAccidentalsOtherStaves, true);
    set(Sid::jubelleCourtesyAccidentalsParentheses, true);
    Accidental* courtesy = accidentalAt(1, 0, 1);
    ASSERT_TRUE(courtesy);
    EXPECT_EQ(courtesy->displayBracket(), AccidentalBracket::PARENTHESIS);
    EXPECT_EQ(courtesy->bracket(), AccidentalBracket::NONE) << "nothing stored changes";
    Accidental* real = accidentalAt(0, 0, 0);
    ASSERT_TRUE(real);
    EXPECT_EQ(real->displayBracket(), AccidentalBracket::NONE) << "a needed accidental is never parenthesised";

    // A bracket you chose yourself is kept as it is
    score->startCmd(TranslatableString::untranslatable("bracket"));
    real->undoChangeProperty(Pid::ACCIDENTAL_BRACKET, int(AccidentalBracket::BRACKET));
    score->endCmd();
    EXPECT_EQ(accidentalAt(0, 0, 0)->displayBracket(), AccidentalBracket::BRACKET);

    set(Sid::jubelleCourtesyAccidentalsParentheses, false);
    EXPECT_EQ(accidentalAt(1, 0, 1)->displayBracket(), AccidentalBracket::NONE);
}

TEST_F(Engraving_JubelleCourtesyRulesTests, SavedAndReopenedWithTheRules)
{
    set(Sid::jubelleCourtesyAccidentals, true);
    set(Sid::jubelleCourtesyAccidentalsBars, 3);
    set(Sid::jubelleCourtesyAccidentalsOtherStaves, true);
    set(Sid::jubelleCourtesyAccidentalsParentheses, true);
    const std::vector<std::string> before = allAccidentals();

    ASSERT_TRUE(ScoreRW::saveScore(score, u"jubelle_courtesy_rules_saved.mscx"));
    MasterScore* reopened = ScoreRW::readScore(u"jubelle_courtesy_rules_saved.mscx", true);
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->style().styleI(Sid::jubelleCourtesyAccidentalsBars), 3);
    EXPECT_TRUE(reopened->style().styleB(Sid::jubelleCourtesyAccidentalsOtherStaves));
    EXPECT_TRUE(reopened->style().styleB(Sid::jubelleCourtesyAccidentalsParentheses));
    std::swap(score, reopened);
    score->doLayout();
    EXPECT_EQ(allAccidentals(), before);
    std::swap(score, reopened);
    delete reopened;
}

TEST_F(Engraving_JubelleCourtesyRulesTests, FollowsEditsAndUndo)
{
    set(Sid::jubelleCourtesyAccidentals, true);
    set(Sid::jubelleCourtesyAccidentalsOtherStaves, true);
    ASSERT_EQ(typeAt(1, 0, 1), AccidentalType::NATURAL);

    // Right hand bar 1: F#4 becomes G4 → the left hand's courtesy natural goes
    Note* n = toChord(score->firstMeasure()->first(SegmentType::ChordRest)->element(0))->upNote();
    score->startCmd(TranslatableString::untranslatable("edit"));
    n->undoChangeProperty(Pid::PITCH, 67);
    n->undoChangeProperty(Pid::TPC1, 15);
    n->undoChangeProperty(Pid::TPC2, 15);
    score->endCmd();
    EXPECT_EQ(typeAt(1, 0, 1), AccidentalType::NONE);

    score->undoRedo(true, nullptr);
    score->doLayout();
    EXPECT_EQ(typeAt(1, 0, 1), AccidentalType::NATURAL) << "undo brings it back";
    score->undoRedo(false, nullptr);
    score->doLayout();
    EXPECT_EQ(typeAt(1, 0, 1), AccidentalType::NONE) << "redo removes it again";
}
