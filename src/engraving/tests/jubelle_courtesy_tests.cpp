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
