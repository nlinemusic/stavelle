// SPDX-License-Identifier: GPL-3.0-only
// S3: note input (edit/noteinput.h) behaves as MuseScore's: letters in the nearest octave and the key, chords,
// values and dots, rests, ties, arrows, re-pitch, tuplets; every command is one undo step.
#include <gtest/gtest.h>

#include <string>

#include "edit/noteinput.h"
#include "edit/scorebuilder.h"
#include "model/invariants.h"

using namespace je;
using namespace je::edit;
using je::model::Kind;
using je::model::Text;
namespace F = je::model::fields;

namespace {
struct Doc {
    model::Model model;
    IdGenerator ids { 1 };
    ScoreBuilder b { model, ids };
    History history;
    ScoreBuilder::PlayerIds flute;
    Operations ops { model, ids };

    explicit Doc(int key = 0)
    {
        Transaction tx(model, "New score");
        b.newScore(tx, "Input");
        b.addMeasures(tx, 3);
        flute = b.addPlayer(tx, "Flute", "flute", "woodwinds", 1);
        for (int i = 0; i < 3; ++i) {
            create(tx, ids, flute.voice, { Kind::Rest, { { F::TIME, Fraction(i, 1) }, { F::DURATION, Fraction(1, 1) }, { F::MEASURE_REST, true } } },
                   Placement::ByTime);
        }
        if (key) {
            create(tx, ids, b.flow(), { Kind::FlowItem, { { F::SUBKIND, Text("keySig") }, { F::TIME, Fraction(0, 1) }, { F::INT_VALUE, int64_t(key) } } },
                   Placement::ByTime);
        }
        history.push(tx.commit());
    }

    //! voice 1: "time duration pitch/tpc… (~ tied)" per event
    std::string music() const
    {
        std::string out;
        for (Id e : ops.eventsOf(flute.voice)) {
            const model::Object* o = model.find(e);
            out += o->fractionOr(F::TIME, Fraction()).toString() + " " + o->fractionOr(F::DURATION, Fraction()).toString();
            for (Id n : o->children()) {
                const model::Object* note = model.find(n);
                if (note->kind() == Kind::Note) {
                    out += " " + std::to_string(note->intOr(F::PITCH, 0)) + "/" + std::to_string(note->intOr(F::TPC, 0))
                           + (note->has(F::TIE_TO) ? "~" : "");
                }
            }
            out += "\n";
        }
        return out;
    }
};

bool startsWith(const std::string& s, const std::string& p)
{
    return s.compare(0, p.size(), p) == 0;
}

bool consistent(const Doc& d)
{
    return model::checkInvariants(d.model.snapshot()).empty();
}
}

TEST(NoteInput, LettersInTheNearestOctaveAndAcrossTheBarline)
{
    Doc d;
    NoteInput in(d.model, d.ids, d.history);
    in.start(d.flute.staff, Fraction(0, 1));
    for (char c : std::string("CDEFG")) {
        EXPECT_TRUE(in.letter(c));
    }
    EXPECT_EQ(d.music(), "0/1 1/4 60/14\n1/4 1/4 62/16\n1/2 1/4 64/18\n3/4 1/4 65/13\n1/1 1/4 67/15\n5/4 1/4\n3/2 1/2\n2/1 1/1\n");
    EXPECT_EQ(in.position(), Fraction(5, 4));
    // the nearest octave: B after G is above it, then C above that
    in.letter('B');
    in.letter('C');
    EXPECT_NE(d.music().find("5/4 1/4 71/19\n3/2 1/4 72/14"), std::string::npos) << d.music();
    // a half note from beat 4 of bar 2 continues, tied, into bar 3
    in.setValueByKey(6);
    in.letter('D');
    EXPECT_NE(d.music().find("7/4 1/4 74/16~\n2/1 1/4 74/16"), std::string::npos) << d.music();
    EXPECT_EQ(in.position(), Fraction(9, 4));
    EXPECT_TRUE(consistent(d));
}

TEST(NoteInput, KeySignaturesAndAccidentals)
{
    Doc d(2);   // D major: F sharp, C sharp
    NoteInput in(d.model, d.ids, d.history);
    in.start(d.flute.staff, Fraction(0, 1));
    in.letter('F');
    in.letter('C');
    in.setAccidental(0);   // a natural for the next note
    in.letter('C');
    in.setAccidental(-1);
    in.letter('B');
    EXPECT_TRUE(startsWith(d.music(), "0/1 1/4 66/20\n1/4 1/4 61/21\n1/2 1/4 60/14\n3/4 1/4 58/12\n")) << d.music();
    // arrows: a semitone up is spelled with a sharp, down with a flat
    in.upDown(true);
    EXPECT_NE(d.music().find("3/4 1/4 59/19"), std::string::npos);
    in.upDown(true);
    EXPECT_NE(d.music().find("3/4 1/4 60/14"), std::string::npos);
    in.upDown(false);
    in.upDown(false);
    EXPECT_NE(d.music().find("3/4 1/4 58/12"), std::string::npos);
    in.upDown(true, true);   // an octave keeps the spelling
    EXPECT_NE(d.music().find("3/4 1/4 70/12"), std::string::npos);
    EXPECT_TRUE(consistent(d));
}

TEST(NoteInput, ChordsValuesRestsAndTies)
{
    Doc d;
    NoteInput in(d.model, d.ids, d.history);
    in.start(d.flute.staff, Fraction(0, 1));
    in.letter('C');
    in.letter('E', true);   // added above
    in.letter('C', true);   // the C above the E
    EXPECT_TRUE(startsWith(d.music(), "0/1 1/4 60/14 64/18 72/14\n")) << d.music();
    in.setValueByKey(6);
    in.toggleDot();
    in.rest();
    EXPECT_NE(d.music().find("1/4 3/4\n"), std::string::npos) << d.music();
    EXPECT_EQ(in.position(), Fraction(1, 1));
    in.setValueByKey(5);
    in.letter('G');
    in.tie();
    in.tie();
    EXPECT_NE(d.music().find("1/1 1/4 67/15~\n5/4 1/4 67/15~\n3/2 1/4 67/15\n"), std::string::npos) << d.music();
    in.deleteLast();
    EXPECT_NE(d.music().find("5/4 1/4 67/15\n3/2 1/4\n"), std::string::npos) << d.music();
    EXPECT_TRUE(consistent(d));
}

TEST(NoteInput, RepitchKeepsTheRhythm)
{
    Doc d;
    NoteInput in(d.model, d.ids, d.history);
    in.start(d.flute.staff, Fraction(0, 1));
    in.setValueByKey(4);
    in.letter('C');
    in.letter('C');
    in.setValueByKey(5);
    in.rest();
    in.setValueByKey(6);
    in.letter('C');
    const std::string rhythm = "0/1 1/8\n1/8 1/8\n1/4 1/4\n1/2 1/2\n";
    in.start(d.flute.staff, Fraction(0, 1));
    in.setRepitch(true);
    for (char c : std::string("EGA")) {
        in.letter(c);
    }
    EXPECT_TRUE(startsWith(d.music(), "0/1 1/8 64/18\n1/8 1/8 67/15\n1/4 1/4\n1/2 1/2 69/17\n")) << d.music();
    (void)rhythm;
    EXPECT_TRUE(consistent(d));
}

TEST(NoteInput, TupletsAndUndo)
{
    Doc d;
    const std::string empty = d.model.snapshot().dump();
    const size_t steps = d.history.size();
    NoteInput in(d.model, d.ids, d.history);
    in.start(d.flute.staff, Fraction(0, 1));
    in.setValueByKey(4);
    EXPECT_TRUE(in.tuplet(3, 2));
    for (char c : std::string("CDE")) {
        in.letter(c);
    }
    EXPECT_TRUE(startsWith(d.music(), "0/1 1/12 60/14\n1/12 1/12 62/16\n1/6 1/12 64/18\n")) << d.music();
    EXPECT_EQ(in.position(), Fraction(1, 4));
    in.letter('F');
    EXPECT_NE(d.music().find("1/4 1/8 65/13"), std::string::npos);
    EXPECT_TRUE(consistent(d));
    // each command one step; undoing them all gives the empty score back
    EXPECT_EQ(d.history.size(), steps + 5);
    while (d.history.size() > steps && d.history.index() > steps) {
        d.history.undo(d.model);
    }
    EXPECT_EQ(d.model.snapshot().dump(), empty);
}
