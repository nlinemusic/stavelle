/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine: tests for the first shaping stage (S7): each note's staff line, notehead and accidental, as
 * MuseScore works them out (compared on the whole corpus by je_layout_reference.py; these tests pin the rules).
 */
#include <gtest/gtest.h>

#include "edit/changeset.h"
#include "edit/operations.h"
#include "edit/scorebuilder.h"
#include "layout/context.h"
#include "layout/notes.h"
#include "model/fields.h"
#include "model/model.h"
#include "model/packing.h"

using namespace je;
using namespace je::edit;
using je::style::SymId;
namespace F = je::model::fields;

namespace {
struct Score {
    model::Model m;
    IdGenerator ids { 1 };
    Id voice;
    std::vector<Id> measures;

    explicit Score(int bars = 3)
    {
        ScoreBuilder b(m, ids);
        Transaction tx(m, "New score");
        b.newScore(tx);
        b.addMeasures(tx, bars, 4, 4);
        b.addPlayer(tx, "Flute", "flute", "flutes", 1);
        tx.commit();
        m.snapshot().forEachObject([&](const model::Object& o) {
            if (o.kind() == model::Kind::Voice && !voice.isValid()) {
                voice = o.id();
            }
        });
    }
    void note(const Fraction& t, int pitch, const Fraction& value = Fraction(1, 4))
    {
        Operations ops(m, ids);
        Transaction tx(m, "Note");
        ops.enterChord(tx, voice, t, { value, 0 }, { pitch });
        tx.commit();
    }
    void style(const std::vector<std::pair<std::string, std::string> >& values)
    {
        Transaction tx(m, "Style");
        tx.set(m.score(), F::STYLE, model::Value(model::Text(model::packPairs(values))));
        tx.commit();
    }
    std::vector<layout::ChordLayout> layout()
    {
        snap = m.snapshot();
        ctx = std::make_unique<layout::ScoreContext>(snap);
        return layout::layoutNotes(*ctx);
    }
    model::Snapshot snap;
    std::unique_ptr<layout::ScoreContext> ctx;
};
}

TEST(LayoutNotes, LinesAndAccidentalsFollowTheMeasure)
{
    Score s;
    s.note(Fraction(0, 1), 66);      // F♯4
    s.note(Fraction(1, 4), 65);      // F4: a natural
    s.note(Fraction(2, 4), 65);      // F4 again: none
    s.note(Fraction(3, 4), 77);      // F5: another line, none
    s.note(Fraction(1, 1), 65);      // the next measure: none
    const auto chords = s.layout();
    ASSERT_EQ(chords.size(), 5u);
    EXPECT_EQ(chords[0].notes[0].line, 7);   // F4 in the treble clef: the bottom space
    EXPECT_EQ(chords[0].notes[0].accidental, SymId::accidentalSharp);
    EXPECT_EQ(chords[1].notes[0].accidental, SymId::accidentalNatural);
    EXPECT_EQ(chords[2].notes[0].accidental, SymId::noSym);
    EXPECT_EQ(chords[3].notes[0].line, 0);
    EXPECT_EQ(chords[3].notes[0].accidental, SymId::noSym);
    EXPECT_EQ(chords[4].notes[0].accidental, SymId::noSym);
    EXPECT_EQ(chords[0].notes[0].headUp, SymId::noteheadBlack);
}

TEST(LayoutNotes, JubellesCourtesyAccidentalsWhenSwitchedOn)
{
    Score s;
    s.note(Fraction(0, 1), 66);      // F♯4 in bar 1
    s.note(Fraction(1, 1), 65);      // F4 in bar 2
    auto chords = s.layout();
    ASSERT_EQ(chords.size(), 2u);
    EXPECT_EQ(chords[1].notes[0].accidental, SymId::noSym);   // MuseScore's rule: a new bar starts afresh
    s.style({ { "jubelleCourtesyAccidentals", "1" } });
    chords = s.layout();
    EXPECT_EQ(chords[1].notes[0].accidental, SymId::accidentalNatural);   // restated as a courtesy
    EXPECT_TRUE(chords[1].notes[0].courtesy);
}

TEST(LayoutNotes, NoteheadsFollowTheValue)
{
    Score s;
    s.note(Fraction(0, 1), 72, Fraction(1, 1));
    s.note(Fraction(1, 1), 72, Fraction(1, 2));
    s.note(Fraction(3, 2), 72, Fraction(1, 8));
    const auto chords = s.layout();
    ASSERT_GE(chords.size(), 3u);
    EXPECT_EQ(chords[0].notes[0].headUp, SymId::noteheadWhole);
    EXPECT_EQ(chords[1].notes[0].headUp, SymId::noteheadHalf);
    EXPECT_EQ(chords[2].notes[0].headDown, SymId::noteheadBlack);
    EXPECT_EQ(chords[0].notes[0].line, 3);   // C5: the third space
}
