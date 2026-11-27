/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine: tests for the first shaping stage (S7): each note's staff line, notehead and accidental, as
 * MuseScore works them out (compared on the whole corpus by je_layout_reference.py; these tests pin the rules).
 */
#include <gtest/gtest.h>

#include "edit/changeset.h"
#include "edit/operations.h"
#include "edit/scorebuilder.h"
#include "edit/structure.h"
#include "layout/beams.h"
#include "layout/context.h"
#include "layout/stems.h"
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
    void rest(const Fraction& t, const Fraction& value)
    {
        Operations ops(m, ids);
        Transaction tx(m, "Rest");
        ops.enterRest(tx, voice, t, { value, 0 });
        tx.commit();
    }
    //! Sets a chord's or rest's beam mode as a file gives it (MuseScore's <BeamMode>)
    void beamMode(Id event, const std::string& mode)
    {
        Transaction tx(m, "Beam mode");
        tx.set(event, F::DETAILS, model::Value(model::Text("<x>\n<BeamMode>" + mode + "</BeamMode>\n</x>\n")));
        tx.commit();
    }
    //! The measure at `t` made stemless on the flute's staff (MuseScore's <stemless>, kept as a measure detail)
    void stemless(const Fraction& t)
    {
        const model::Snapshot sn = m.snapshot();
        Id flow, staff;
        sn.forEachObject([&](const model::Object& o) {
            if (o.kind() == model::Kind::Flow && !flow.isValid()) {
                flow = o.id();
            } else if (o.kind() == model::Kind::Staff && !staff.isValid()) {
                staff = o.id();
            }
        });
        Transaction tx(m, "Stemless");
        create(tx, ids, flow, { model::Kind::FlowItem, { { F::SUBKIND, model::Value(model::Text(std::string("measureDetail"))) },
                                                       { F::ANCHOR, model::Value(staff) }, { F::TIME, model::Value(t) },
                                                       { F::DETAILS, model::Value(model::Text(std::string("<x>\n<stemless>1</stemless>\n</x>\n"))) } } },
               Placement::ByTime);
        tx.commit();
    }
    //! A chord in another voice of the flute's staff (1: voice 2)
    void noteInVoice(int number, const Fraction& t, int pitch, const Fraction& value = Fraction(1, 4))
    {
        Operations ops(m, ids);
        Transaction tx(m, "Note");
        const Id v = ops.voice(tx, ops.staffOf(voice), number);
        ops.enterChord(tx, v, t, { value, 0 }, { pitch });
        tx.commit();
    }
    //! Each chord's stem direction (true: up), in order
    std::vector<bool> stems()
    {
        auto chords = layout();
        layout::layoutStemDirections(*ctx, chords, layout::layoutBeams(*ctx, chords));
        std::vector<bool> out;
        for (const auto& c : chords) {
            out.push_back(c.up);
        }
        return out;
    }
    //! The beams as the number of chords and rests in each, in order
    std::vector<size_t> beams()
    {
        const auto chords = layout();
        std::vector<size_t> out;
        for (const layout::Beam& b : layout::layoutBeams(*ctx, chords)) {
            out.push_back(b.elements.size());
        }
        return out;
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

TEST(LayoutBeams, EighthsAreBeamedByTheTimeSignaturesGroups)
{
    Score s;
    for (int i = 0; i < 8; ++i) {
        s.note(Fraction(i, 8), 72, Fraction(1, 8));
    }
    // MuseScore's 4/4 default: eighths in two groups of four; a quarter is never beamed
    EXPECT_EQ(s.beams(), (std::vector<size_t> { 4, 4 }));
    s.note(Fraction(1, 1), 72, Fraction(1, 4));
    s.note(Fraction(5, 4), 72, Fraction(1, 8));
    s.note(Fraction(11, 8), 72, Fraction(1, 8));
    EXPECT_EQ(s.beams(), (std::vector<size_t> { 4, 4, 2 }));
}

TEST(LayoutBeams, ABeamIntoTheNextMeasureDoesNotStartOnAnUnbeamedRest)
{
    Score s;
    s.note(Fraction(0, 1), 72, Fraction(1, 2));
    s.rest(Fraction(1, 2), Fraction(1, 2));   // the bar ends with a half rest
    s.note(Fraction(1, 1), 72, Fraction(1, 16));
    s.note(Fraction(17, 16), 72, Fraction(1, 16));
    auto chords = s.layout();
    ASSERT_GE(chords.size(), 2u);
    s.beamMode(chords[1].chord, "mid");   // the next bar's first chord asks to continue a beam
    // MuseScore's vtest beams-2: the beam starts on that chord, not on the half rest before it
    const auto b = s.beams();
    ASSERT_EQ(b.size(), 1u);
    EXPECT_EQ(b[0], 2u);
}

TEST(LayoutBeams, AStemlessMeasureShowsNoBeams)
{
    Score s;
    for (int i = 0; i < 4; ++i) {
        s.note(Fraction(i, 8), 72, Fraction(1, 8));
    }
    s.stemless(Fraction(0, 1));
    const auto chords = s.layout();
    ASSERT_EQ(chords.size(), 4u);
    for (const auto& c : chords) {
        EXPECT_TRUE(c.stemless);   // ChordRest::beam shows none there
    }
}

TEST(LayoutStems, StemsFollowTheNotesAroundTheMiddleLine)
{
    Score s;
    s.note(Fraction(0, 1), 64);   // E4, below the middle line: up
    s.note(Fraction(1, 4), 71);   // B4, on it: down
    s.note(Fraction(2, 4), 76);   // E5: down
    s.note(Fraction(3, 4), 60);   // C4: up
    EXPECT_EQ(s.stems(), (std::vector<bool> { true, false, false, true }));
}

TEST(LayoutStems, ABeamTakesOneDirectionForAllItsChords)
{
    Score s;
    s.note(Fraction(0, 1), 64, Fraction(1, 8));   // E4 alone would be up
    s.note(Fraction(1, 8), 79, Fraction(1, 8));   // G5 alone would be down
    s.note(Fraction(2, 8), 81, Fraction(1, 8));
    s.note(Fraction(3, 8), 81, Fraction(1, 8));
    // MuseScore: the beam's distinct note lines balanced from the outside in (E4 and A5: A5 is further away)
    EXPECT_EQ(s.stems(), (std::vector<bool> { false, false, false, false }));
}

TEST(LayoutStems, VoicesOneAndTwoGoUpAndDown)
{
    Score s;
    s.note(Fraction(0, 1), 79);                      // G5 would be down on its own
    s.noteInVoice(1, Fraction(0, 1), 60);            // C4 in voice 2 would be up
    const auto up = s.stems();
    ASSERT_EQ(up.size(), 2u);
    EXPECT_TRUE(up[0]);
    EXPECT_FALSE(up[1]);
}
