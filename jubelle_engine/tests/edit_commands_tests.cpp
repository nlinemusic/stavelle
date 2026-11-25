// SPDX-License-Identifier: GPL-3.0-only
// S3: editing commands (edit/commands.h) with MuseScore's rules: enharmonic spelling, accidentals, add interval,
// articulations, voices, grace notes, lines, key signatures and clefs. Each is undone exactly, keeps the model
// consistent, and survives save and reopen.
#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "edit/commands.h"
#include "edit/history.h"
#include "edit/operations.h"
#include "edit/scorebuilder.h"
#include "io/mscxreader.h"
#include "io/mscxwriter.h"
#include "model/invariants.h"
#include "model/notation.h"

using namespace je;
using namespace je::edit;
using je::model::Kind;
namespace F = je::model::fields;

namespace {
struct Doc {
    model::Model model;
    IdGenerator ids { 1 };
    ScoreBuilder b { model, ids };
    Operations ops { model, ids };
    Commands cmd { ops };
    History history;
    ScoreBuilder::PlayerIds flute, horn;

    explicit Doc(int measures = 2)
    {
        Transaction tx(model, "New score");
        b.newScore(tx, "Commands");
        b.addMeasures(tx, measures, 4, 4);
        flute = b.addPlayer(tx, "Flute", "flute", "woodwinds", 1);
        horn = b.addPlayer(tx, "Horn in F", "horn", "brass", 1, -7);
        for (int i = 0; i < measures; ++i) {
            for (Id v : { flute.voice, horn.voice }) {
                create(tx, ids, v, { Kind::Rest, { { F::TIME, Fraction(i, 1) }, { F::DURATION, Fraction(1, 1) },
                                                   { F::MEASURE_REST, true } } }, Placement::ByTime);
            }
        }
        history.push(tx.commit());
    }

    template<typename Fn>
    void edit(const std::string& name, Fn&& fn)
    {
        Transaction tx(model, name);
        fn(tx);
        history.push(tx.commit());
    }

    Id chord(Id voice, int quarter, std::vector<int> pitches, int quarters = 1)
    {
        Id c;
        edit("Enter", [&](Transaction& tx) {
            c = ops.enterChord(tx, voice, Fraction(quarter, 4), { Fraction(quarters, 4), 0 }, pitches);
        });
        return c;
    }
    std::vector<Id> notes(Id c) const { return b.notesOf(c); }
    const model::Object* o(Id id) const { return model.find(id); }
    int64_t pitch(Id n) const { return o(n)->intOr(F::PITCH, -1); }
    int64_t tpc(Id n) const { return o(n)->intOr(F::TPC, -1); }
    int64_t tpc2(Id n) const { return o(n)->intOr(F::TPC2, tpc(n)); }

    //! one voice: "time duration [g] pitches…" per event
    std::string voice(Id v) const
    {
        std::string out;
        for (Id e : ops.eventsOf(v)) {
            const model::Object* x = o(e);
            out += x->fractionOr(F::TIME, Fraction()).toString() + " " + x->fractionOr(F::DURATION, Fraction()).toString();
            if (x->has(F::GRACE)) {
                out += " g";
            }
            for (Id n : x->children()) {
                if (o(n)->kind() == Kind::Note) {
                    out += " " + std::to_string(pitch(n));
                    if (o(n)->has(F::TIE_TO)) {
                        out += "~";
                    }
                }
            }
            out += "\n";
        }
        return out;
    }
    std::string listing() const { return io::musicListing(model.snapshot()); }
    void consistent() const
    {
        const auto problems = model::checkInvariants(model.snapshot());
        EXPECT_TRUE(problems.empty()) << (problems.empty() ? "" : problems.front());
    }
    //! undo the last edit and redo it: exactly the state before and after
    void undoRedo()
    {
        const std::string after = io::modelDump(model.snapshot());
        history.undo(model);
        const std::string before = io::modelDump(model.snapshot());
        history.redo(model);
        EXPECT_EQ(io::modelDump(model.snapshot()), after);
        EXPECT_NE(before, after);
    }
    //! every note's pitch and both spellings, in tree order
    static std::string spellings(const model::Model& m)
    {
        std::string out;
        std::vector<Id> stack { m.score() };
        while (!stack.empty()) {
            const model::Object* x = m.find(stack.back());
            stack.pop_back();
            if (x->kind() == Kind::Note) {
                out += std::to_string(x->intOr(F::PITCH, -1)) + "/" + std::to_string(x->intOr(F::TPC, -1)) + "/"
                       + std::to_string(x->intOr(F::TPC2, x->intOr(F::TPC, -1))) + " ";
            }
            const auto& kids = x->children().ids();
            stack.insert(stack.end(), kids.rbegin(), kids.rend());
        }
        return out;
    }
    void roundTrip()
    {
        const auto path = std::filesystem::temp_directory_path() / "je_commands_roundtrip.mscz";
        std::string error;
        ASSERT_TRUE(io::saveScore(model.snapshot(), path.string(), error)) << error;
        model::Model back;
        IdGenerator backIds { 1 };
        ASSERT_TRUE(io::openScore(path.string(), back, backIds, error)) << error;
        EXPECT_EQ(io::musicListing(back.snapshot()), listing());
        EXPECT_EQ(spellings(back), spellings(model));
        std::filesystem::remove(path);
    }
};
}

TEST(Commands, EnharmonicSpellingCyclesAsMuseScoreDoes)
{
    Doc d;
    const Id c = d.chord(d.flute.voice, 0, { 61 });
    const Id n = d.notes(c)[0];
    EXPECT_EQ(d.tpc(n), 21);   // C♯
    d.edit("Respell", [&](Transaction& tx) { d.cmd.changeEnharmonicSpelling(tx, { n }, true, false); });
    EXPECT_EQ(d.tpc(n), 9);   // D♭
    EXPECT_EQ(d.pitch(n), 61);
    d.edit("Respell", [&](Transaction& tx) { d.cmd.changeEnharmonicSpelling(tx, { n }, true, false); });
    EXPECT_EQ(d.tpc(n), 21);
    // a natural cycles through its double accidentals: C → D𝄫 → B♯ → C
    const Id m = d.notes(d.chord(d.flute.voice, 1, { 60 }))[0];
    std::vector<int64_t> seen;
    for (int i = 0; i < 3; ++i) {
        d.edit("Respell", [&](Transaction& tx) { d.cmd.changeEnharmonicSpelling(tx, { m }, true, false); });
        seen.push_back(d.tpc(m));
    }
    EXPECT_EQ(seen, (std::vector<int64_t> { 2, 26, 14 }));
    d.undoRedo();
    d.consistent();
}

TEST(Commands, EnharmonicSpellingOfAWrittenNoteLeavesTheConcertSpellingUnlessBoth)
{
    Doc d;
    const Id n = d.notes(d.chord(d.horn.voice, 0, { 60 }))[0];   // concert C, written G
    EXPECT_EQ(d.tpc(n), 14);
    EXPECT_EQ(d.tpc2(n), 15);
    d.edit("Respell", [&](Transaction& tx) { d.cmd.changeEnharmonicSpelling(tx, { n }, false, false); });
    EXPECT_EQ(d.tpc2(n), 3);   // A𝄫
    EXPECT_EQ(d.tpc(n), 14);
    d.history.undo(d.model);
    d.edit("Respell", [&](Transaction& tx) { d.cmd.changeEnharmonicSpelling(tx, { n }, false, true); });
    EXPECT_EQ(d.tpc2(n), 3);
    EXPECT_EQ(d.tpc(n), 2);   // D𝄫, the same sound written as A𝄫 for horn in F
    d.consistent();
    d.roundTrip();
}

TEST(Commands, AccidentalsChangeThePitchAndToggleBackToTheKey)
{
    Doc d;
    const Id n = d.notes(d.chord(d.flute.voice, 0, { 60 }))[0];
    d.edit("Sharp", [&](Transaction& tx) { d.cmd.changeAccidental(tx, n, 1, true); });
    EXPECT_EQ(d.pitch(n), 61);
    EXPECT_EQ(d.tpc(n), 21);
    d.edit("Sharp", [&](Transaction& tx) { d.cmd.toggleAccidental(tx, n, 1, true); });
    EXPECT_EQ(d.pitch(n), 60);
    EXPECT_EQ(d.tpc(n), 14);
    d.edit("Double flat", [&](Transaction& tx) { d.cmd.changeAccidental(tx, n, -2, true); });
    EXPECT_EQ(d.pitch(n), 58);
    EXPECT_EQ(d.tpc(n), 0);   // C𝄫
    // in D major the toggled-off F♯ returns to F♯, not F
    Doc k;
    k.edit("Key", [&](Transaction& tx) { k.cmd.insertKeySignature(tx, Fraction(0, 1), 2); });
    const Id f = k.notes(k.chord(k.flute.voice, 0, { 66 }))[0];
    EXPECT_EQ(k.tpc(f), 20);
    k.edit("Natural", [&](Transaction& tx) { k.cmd.changeAccidental(tx, f, 0, true); });
    EXPECT_EQ(k.pitch(f), 65);
    k.edit("Natural", [&](Transaction& tx) { k.cmd.toggleAccidental(tx, f, 0, true); });
    EXPECT_EQ(k.pitch(f), 66);
    EXPECT_EQ(k.tpc(f), 20);
    k.consistent();
    k.roundTrip();
}

TEST(Commands, AddIntervalUsesTheKeyAndTheOctaveRule)
{
    Doc d;
    const Id c = d.chord(d.flute.voice, 0, { 60 });
    std::vector<Id> made;
    d.edit("Third", [&](Transaction& tx) { made = d.cmd.addInterval(tx, d.notes(c), 3); });
    ASSERT_EQ(made.size(), 1u);
    EXPECT_EQ(d.pitch(made[0]), 64);   // E
    d.edit("Third below", [&](Transaction& tx) { made = d.cmd.addInterval(tx, { d.notes(c)[0] }, -3); });
    EXPECT_EQ(d.pitch(made[0]), 57);   // A3
    d.edit("Octave", [&](Transaction& tx) { made = d.cmd.addInterval(tx, { d.notes(c).back() }, 8); });
    EXPECT_EQ(d.pitch(made[0]), 76);   // E5 above E4
    EXPECT_EQ(d.voice(d.flute.voice).substr(0, 18), "0/1 1/4 57 60 64 7");
    d.undoRedo();
    // the key gives the accidental: D major, a third above D is F♯; a sixth above B♭… stays in the key letters
    Doc k;
    k.edit("Key", [&](Transaction& tx) { k.cmd.insertKeySignature(tx, Fraction(0, 1), 2); });
    const Id kc = k.chord(k.flute.voice, 0, { 62 });
    k.edit("Third", [&](Transaction& tx) { made = k.cmd.addInterval(tx, k.notes(kc), 3); });
    EXPECT_EQ(k.pitch(made[0]), 66);
    EXPECT_EQ(k.tpc(made[0]), 20);
    // a B♯ is written in octave 3: the third above is D4, not D5
    Doc s;
    const Id sc = s.chord(s.flute.voice, 0, { 60 });
    s.edit("B sharp", [&](Transaction& tx) { s.cmd.changeEnharmonicSpelling(tx, s.notes(sc), true, false);
                                            s.cmd.changeEnharmonicSpelling(tx, s.notes(sc), true, false); });
    ASSERT_EQ(s.tpc(s.notes(sc)[0]), 26);
    s.edit("Third", [&](Transaction& tx) { made = s.cmd.addInterval(tx, s.notes(sc), 3); });
    ASSERT_EQ(made.size(), 1u);
    EXPECT_EQ(s.pitch(made[0]), 62);
    k.consistent();
}

TEST(Commands, AddIntervalContinuesAlongTiesAndTiesTheNewNotes)
{
    Doc d;
    const Id a = d.chord(d.flute.voice, 0, { 60 });
    const Id b = d.chord(d.flute.voice, 1, { 60 });
    d.edit("Tie", [&](Transaction& tx) { tx.set(d.notes(a)[0], F::TIE_TO, d.notes(b)[0]); });
    std::vector<Id> made;
    d.edit("Third", [&](Transaction& tx) { made = d.cmd.addInterval(tx, d.notes(a), 3); });
    ASSERT_EQ(made.size(), 2u);
    EXPECT_EQ(d.o(made[0])->idOr(F::TIE_TO), made[1]);
    EXPECT_FALSE(d.o(made[1])->has(F::TIE_TO));
    EXPECT_EQ(d.voice(d.flute.voice).substr(0, 30), "0/1 1/4 60~ 64~\n1/4 1/4 60 64\n");
    d.consistent();
    d.roundTrip();
}

TEST(Commands, ToggleArticulationAddsAndRemovesOne)
{
    Doc d;
    const Id c = d.chord(d.flute.voice, 0, { 60 });
    auto count = [&](const std::string& kind) {
        int n = 0;
        for (Id x : d.o(c)->children()) {
            n += d.o(x)->kind() == Kind::Attachment && model::articulationKind(d.o(x)->text(F::SUBKIND)) == kind;
        }
        return n;
    };
    d.edit("Staccato", [&](Transaction& tx) { d.cmd.toggleArticulation(tx, { c }, "articStaccatoAbove"); });
    EXPECT_EQ(count(model::articulationKind("articStaccatoAbove")), 1);
    d.edit("Accent", [&](Transaction& tx) { d.cmd.toggleArticulation(tx, { c }, "articAccentAbove"); });
    d.roundTrip();
    d.edit("Staccato", [&](Transaction& tx) { d.cmd.toggleArticulation(tx, { c }, "articStaccatoBelow"); });
    EXPECT_EQ(count(model::articulationKind("articStaccatoAbove")), 0);
    EXPECT_EQ(count(model::articulationKind("articAccentAbove")), 1);
    d.undoRedo();
    d.consistent();
}

TEST(Commands, ChangeVoiceMovesChordsAndNotes)
{
    Doc d;
    const Id c = d.chord(d.flute.voice, 0, { 60, 64 });
    d.chord(d.flute.voice, 1, { 62 });
    int moved = 0;
    d.edit("Voice 2", [&](Transaction& tx) { moved = d.cmd.changeVoice(tx, { c }, 1); });
    EXPECT_EQ(moved, 1);
    const std::string v1 = d.voice(d.flute.voice);
    EXPECT_EQ(v1.substr(0, 8 + 0), "0/1 1/4\n");   // a rest left in voice 1
    Id v2;
    for (Id v : d.o(d.flute.staff)->children()) {
        if (d.o(v)->kind() == Kind::Voice && d.o(v)->intOr(F::NUMBER, 0) == 1) {
            v2 = v;
        }
    }
    ASSERT_TRUE(v2.isValid());
    EXPECT_EQ(d.voice(v2), "0/1 1/4 60 64\n");
    d.undoRedo();
    // back: into the rest left in voice 1
    d.edit("Voice 1", [&](Transaction& tx) { moved = d.cmd.changeVoice(tx, { c }, 0); });
    EXPECT_EQ(moved, 1);
    EXPECT_EQ(d.voice(d.flute.voice).substr(0, 14), "0/1 1/4 60 64\n");
    // one note of a chord into voice 2: the chord keeps the other, voice 2 gets a chord of its own
    const Id e = d.notes(c)[1];
    d.edit("Note to voice 2", [&](Transaction& tx) { moved = d.cmd.changeVoice(tx, { e }, 1); });
    EXPECT_EQ(moved, 1);
    EXPECT_EQ(d.voice(d.flute.voice).substr(0, 11), "0/1 1/4 60\n");
    EXPECT_EQ(d.voice(v2), "0/1 1/4 64\n");
    d.consistent();
    d.roundTrip();
}

TEST(Commands, SwapVoicesExchangesTheRangeAndKeepsVoiceOneFull)
{
    Doc d;
    d.chord(d.flute.voice, 0, { 72 }, 2);
    d.chord(d.flute.voice, 2, { 74 }, 2);
    Id v2;
    d.edit("Voice 2", [&](Transaction& tx) {
        v2 = d.ops.voice(tx, d.flute.staff, 1);
        d.ops.enterChord(tx, v2, Fraction(0, 1), { Fraction(1, 4), 0 }, { 60 });
    });
    d.edit("Swap", [&](Transaction& tx) { d.cmd.swapVoices(tx, d.flute.staff, Fraction(0, 1), Fraction(1, 1), 0, 1); });
    EXPECT_EQ(d.voice(v2), "0/1 1/2 72\n1/2 1/2 74\n");
    const std::string v1 = d.voice(d.flute.voice);
    EXPECT_EQ(v1.substr(0, 11), "0/1 1/4 60\n");
    EXPECT_NE(v1.find("1/4 "), std::string::npos);   // the rest of the measure is filled
    d.undoRedo();
    d.consistent();
    d.roundTrip();
}

TEST(Commands, GraceNotesGoBeforeOrAfterTheirChord)
{
    Doc d;
    const Id c = d.chord(d.flute.voice, 0, { 67 });
    d.edit("Acciaccatura", [&](Transaction& tx) { d.cmd.addGraceNote(tx, d.notes(c)[0], "acciaccatura", Fraction(1, 8)); });
    d.edit("After", [&](Transaction& tx) { d.cmd.addGraceNote(tx, d.notes(c)[0], "grace16after", Fraction(1, 16)); });
    EXPECT_EQ(d.voice(d.flute.voice).substr(0, 38), "0/1 1/8 g 67\n0/1 1/4 67\n0/1 1/16 g 67\n");
    d.undoRedo();
    d.consistent();
    d.roundTrip();
}

TEST(Commands, LinesSlursAndNoteLinesAreSavedAndReopened)
{
    Doc d;
    const Id a = d.chord(d.flute.voice, 0, { 60 });
    const Id b = d.chord(d.flute.voice, 1, { 67 });
    d.chord(d.flute.voice, 2, { 64 });
    d.edit("Hairpin", [&](Transaction& tx) {
        EXPECT_TRUE(d.cmd.addLine(tx, "hairPin", d.flute.staff, 0, Fraction(0, 1), Fraction(1, 2), 0).isValid());
    });
    d.edit("Slur", [&](Transaction& tx) { EXPECT_TRUE(d.cmd.addSlur(tx, a, Id()).isValid()); });
    d.edit("Glissando", [&](Transaction& tx) {
        EXPECT_TRUE(d.cmd.addNoteLine(tx, "glissando", d.notes(a)[0], d.notes(b)[0]).isValid());
    });
    d.edit("Ottava", [&](Transaction& tx) {
        EXPECT_TRUE(d.cmd.addLine(tx, "ottava", d.flute.staff, 0, Fraction(0, 1), Fraction(1, 1), 0, "8va").isValid());
    });
    int lines = 0;
    for (Id x : d.o(d.model.score())->children()) {
        lines += d.o(x)->kind() == Kind::Spanner;
    }
    EXPECT_EQ(lines, 4);
    d.undoRedo();
    d.consistent();
    d.roundTrip();
}

TEST(Commands, KeySignaturesAndClefsAreInsertedOrChanged)
{
    Doc d;
    d.edit("Key", [&](Transaction& tx) { d.cmd.insertKeySignature(tx, Fraction(1, 1), -3); });
    EXPECT_EQ(d.ops.keyAt(d.flute.staff, Fraction(1, 1)), -3);
    EXPECT_EQ(d.ops.keyAt(d.flute.staff, Fraction(1, 2)), 0);
    d.edit("Key", [&](Transaction& tx) { d.cmd.insertKeySignature(tx, Fraction(1, 1), 4); });
    EXPECT_EQ(d.ops.keyAt(d.horn.staff, Fraction(3, 2)), 4);
    // a new note there is spelled in the key
    const Id n = d.notes(d.chord(d.flute.voice, 4, { 63 }))[0];
    EXPECT_EQ(d.tpc(n), 23);   // D♯ in E major
    d.edit("Clef", [&](Transaction& tx) { d.cmd.insertClef(tx, d.horn.staff, Fraction(1, 1), "F"); });
    d.undoRedo();
    d.consistent();
    d.roundTrip();
}

TEST(Commands, NoteEntryOnALocalTimeSignatureUsesItsOwnValues)
{
    // a 3/4 staff in a 4/4 score (stretch 3/4): a written quarter lasts a third of the measure, as in MuseScore
    Doc d(2);
    d.edit("Local time signature", [&](Transaction& tx) {
        Id flow;
        for (Id c : d.o(d.model.score())->children()) {
            flow = d.o(c)->kind() == Kind::Flow ? c : flow;
        }
        create(tx, d.ids, flow, { Kind::FlowItem, { { F::SUBKIND, model::Text("timeSig") }, { F::ANCHOR, d.flute.staff },
                                                    { F::TIME, Fraction(1, 1) }, { F::TIMESIG_NUM, int64_t(3) },
                                                    { F::TIMESIG_DEN, int64_t(4) } } },
               Placement::ByTime);
        for (Id e : d.ops.eventsOf(d.flute.voice)) {
            if (d.o(e)->fractionOr(F::TIME, Fraction()) == Fraction(1, 1)) {
                tx.set(e, F::STRETCH, Fraction(3, 4));
            }
        }
    });
    EXPECT_EQ(d.ops.stretchAt(d.flute.voice, Fraction(1, 1)), Fraction(3, 4));
    d.chord(d.flute.voice, 4, { 60 });
    const std::string v = d.voice(d.flute.voice);
    EXPECT_NE(v.find("1/1 1/3 60\n4/3 1/3\n5/3 1/3\n"), std::string::npos) << v;
    d.consistent();
    d.roundTrip();
}
