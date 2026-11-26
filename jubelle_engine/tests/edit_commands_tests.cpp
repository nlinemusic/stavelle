// SPDX-License-Identifier: GPL-3.0-only
// S3: editing commands (edit/commands.h) with MuseScore's rules: enharmonic spelling, accidentals, add interval,
// articulations, voices, grace notes, lines, key signatures and clefs. Each is undone exactly, keeps the model
// consistent, and survives save and reopen.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <tuple>
#include <vector>

#include "edit/chordsymbol.h"
#include "edit/commands.h"
#include "edit/history.h"
#include "edit/links.h"
#include "edit/operations.h"
#include "edit/scorebuilder.h"
#include "io/mscxreader.h"
#include "io/mscxwriter.h"
#include "model/detailskey.h"
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

namespace {
std::string measuresOf(const Doc& d)
{
    std::string out;
    for (Id m : d.ops.measures()) {
        const model::Object* o = d.o(m);
        out += o->fractionOr(F::TIME, Fraction()).toString() + "+" + o->fractionOr(F::DURATION, Fraction()).toString() + " "
               + std::to_string(o->intOr(F::TIMESIG_NUM, 0)) + "/" + std::to_string(o->intOr(F::TIMESIG_DEN, 0)) + "\n";
    }
    return out;
}
}

TEST(Commands, ATimeSignatureRewritesTheMeasuresAsMuseScoreDoes)
{
    Doc d(2);
    int q = 0;
    for (int p : { 60, 62, 64, 65, 67, 69, 71, 72 }) {
        d.chord(d.flute.voice, q++, { p });
    }
    bool ok = false;
    d.edit("3/4", [&](Transaction& tx) { ok = d.cmd.changeTimeSignature(tx, 0, 3, 4); });
    ASSERT_TRUE(ok);
    // eight quarters in 3/4: three measures, the last filled with a rest
    EXPECT_EQ(measuresOf(d), "0/1+3/4 3/4\n3/4+3/4 3/4\n3/2+3/4 3/4\n");
    EXPECT_EQ(d.voice(d.flute.voice), "0/1 1/4 60\n1/4 1/4 62\n1/2 1/4 64\n3/4 1/4 65\n1/1 1/4 67\n5/4 1/4 69\n"
                                      "3/2 1/4 71\n7/4 1/4 72\n2/1 1/4\n");
    // the horn had only measure rests: measure rests of the new length
    EXPECT_EQ(d.voice(d.horn.voice), "0/1 3/4\n3/4 3/4\n3/2 3/4\n");
    d.undoRedo();
    d.consistent();
    d.roundTrip();
}

TEST(Commands, NotesAcrossANewBarlineAreSplitAndTied)
{
    Doc d(1);
    d.chord(d.flute.voice, 0, { 60 });
    d.chord(d.flute.voice, 1, { 62 });
    d.chord(d.flute.voice, 2, { 64 }, 2);   // a half note on beat 3
    d.edit("Staccato", [&](Transaction& tx) {
        for (Id e : d.ops.eventsOf(d.flute.voice)) {
            if (d.o(e)->fractionOr(F::TIME, Fraction()) == Fraction(1, 2)) {
                d.cmd.toggleArticulation(tx, { e }, "articStaccatoAbove");
            }
        }
    });
    bool ok = false;
    d.edit("3/4", [&](Transaction& tx) { ok = d.cmd.changeTimeSignature(tx, 0, 3, 4); });
    ASSERT_TRUE(ok);
    EXPECT_EQ(d.voice(d.flute.voice), "0/1 1/4 60\n1/4 1/4 62\n1/2 1/4 64~\n3/4 1/4 64\n1/1 1/2\n");
    // the articulation stays on the first part only
    int articulations = 0;
    for (Id e : d.ops.eventsOf(d.flute.voice)) {
        for (Id x : d.o(e)->children()) {
            articulations += d.o(x)->kind() == Kind::Attachment;
        }
    }
    EXPECT_EQ(articulations, 1);
    d.consistent();
    d.roundTrip();
}

TEST(Commands, TheSameLengthOnlyRenamesTheMeasures)
{
    Doc d(2);
    d.chord(d.flute.voice, 1, { 67 }, 2);
    const std::string before = d.voice(d.flute.voice);
    bool ok = false;
    d.edit("2/2", [&](Transaction& tx) { ok = d.cmd.changeTimeSignature(tx, 0, 2, 2); });
    ASSERT_TRUE(ok);
    EXPECT_EQ(measuresOf(d), "0/1+1/1 2/2\n1/1+1/1 2/2\n");
    EXPECT_EQ(d.voice(d.flute.voice), before);
    d.roundTrip();
}

TEST(Commands, ATimeSignatureInTheMiddleRewritesFromThere)
{
    Doc d(3);
    d.chord(d.flute.voice, 4, { 60 }, 2);
    d.chord(d.flute.voice, 6, { 62 }, 2);
    bool ok = false;
    d.edit("6/8", [&](Transaction& tx) { ok = d.cmd.changeTimeSignature(tx, 1, 6, 8); });
    ASSERT_TRUE(ok);
    // measures 2–3 (8 quarters) in 6/8: three measures of 3/4, a quarter filled; the first measure untouched
    EXPECT_EQ(measuresOf(d), "0/1+1/1 4/4\n1/1+3/4 6/8\n7/4+3/4 6/8\n5/2+3/4 6/8\n");
    EXPECT_EQ(d.voice(d.flute.voice), "0/1 1/1\n1/1 1/2 60\n3/2 1/4 62~\n7/4 1/4 62\n2/1 1/2\n5/2 3/4\n");
    d.consistent();
    d.roundTrip();
}

TEST(Commands, ATupletAcrossANewBarlineIsSplitAtItsMiddleOrRefused)
{
    Doc d(1);
    d.edit("Triplet", [&](Transaction& tx) { d.ops.makeTuplet(tx, d.flute.voice, Fraction(1, 2), 3, 2, Fraction(1, 4)); });
    const std::string before = d.listing() + measuresOf(d);
    bool ok = true;
    d.edit("5/8", [&](Transaction& tx) { ok = d.cmd.changeTimeSignature(tx, 0, 5, 8); });
    EXPECT_FALSE(ok);   // the barline would fall at 5/8, not the triplet's middle
    EXPECT_EQ(d.listing() + measuresOf(d), before);
    // at its middle (3/4) it is split into two halves, as MuseScore splits it
    d.edit("3/4", [&](Transaction& tx) { ok = d.cmd.changeTimeSignature(tx, 0, 3, 4); });
    EXPECT_TRUE(ok);
    int tuplets = 0;
    for (Id c : d.o(d.flute.voice)->children()) {
        if (d.o(c)->kind() == Kind::Tuplet) {
            ++tuplets;
            EXPECT_EQ(d.o(c)->fractionOr(F::DURATION, Fraction()), Fraction(1, 4));
        }
    }
    EXPECT_EQ(tuplets, 2);
    d.consistent();
    d.roundTrip();
}

TEST(Commands, ChangingTheTimeSignatureBackJoinsTheTiedPiecesAgain)
{
    // as in MuseScore: 4/4 → 3/4 splits the half note, 3/4 → 4/4 joins it again (ties made by the rewrite)
    Doc d(1);
    d.chord(d.flute.voice, 0, { 60 });
    d.chord(d.flute.voice, 1, { 62 });
    d.chord(d.flute.voice, 2, { 64 }, 2);
    const std::string before = d.voice(d.flute.voice);
    bool ok = false;
    d.edit("3/4", [&](Transaction& tx) { ok = d.cmd.changeTimeSignature(tx, 0, 3, 4); });
    ASSERT_TRUE(ok);
    d.edit("4/4", [&](Transaction& tx) { ok = d.cmd.changeTimeSignature(tx, 0, 4, 4); });
    ASSERT_TRUE(ok);
    // 2 measures of 4/4 now (5/4 of music filled to 2/1): the first as it was, the second a measure rest
    EXPECT_EQ(measuresOf(d), "0/1+1/1 4/4\n1/1+1/1 4/4\n");
    EXPECT_EQ(d.voice(d.flute.voice), before + "1/1 1/1\n");
    d.consistent();
    d.roundTrip();
}

TEST(Commands, ANoteWithATieOfItsOwnIsNotTiedToItsPieces)
{
    // a laissez vibrer (kept as written) is the note's own tie: MuseScore doesn't add another, and the pieces after
    // it don't get it (two ties on one note make MuseScore fail to read the file)
    Doc d(1);
    d.chord(d.flute.voice, 0, { 60 });
    d.chord(d.flute.voice, 1, { 62 });
    const Id c = d.chord(d.flute.voice, 2, { 64 }, 2);
    const Id n = d.notes(c)[0];
    d.edit("l.v.", [&](Transaction& tx) { tx.set(n, F::DETAILS, model::Text("<x>|  <LaissezVib>|    </LaissezVib>|  </x>")); });
    bool ok = false;
    d.edit("3/4", [&](Transaction& tx) { ok = d.cmd.changeTimeSignature(tx, 0, 3, 4); });
    ASSERT_TRUE(ok);
    EXPECT_EQ(d.voice(d.flute.voice), "0/1 1/4 60\n1/4 1/4 62\n1/2 1/4 64\n3/4 1/4 64\n1/1 1/2\n");
    for (Id e : d.ops.eventsOf(d.flute.voice)) {
        if (e != c && d.o(e)->kind() == Kind::Chord && d.o(e)->fractionOr(F::TIME, Fraction()) == Fraction(3, 4)) {
            EXPECT_EQ(d.o(d.notes(e)[0])->text(F::DETAILS).find("LaissezVib"), std::string::npos);
        }
    }
    d.consistent();
    d.roundTrip();
}

TEST(Commands, SplittingAMeasureKeepsItsMusicInTwoMeasures)
{
    Doc d(2);
    d.chord(d.flute.voice, 0, { 60 });
    d.chord(d.flute.voice, 1, { 62 });
    d.chord(d.flute.voice, 2, { 64 }, 2);
    const std::string before = d.voice(d.flute.voice);
    bool ok = true;
    d.edit("Split", [&](Transaction& tx) { ok = d.cmd.splitMeasure(tx, Fraction(0, 1)); });
    EXPECT_FALSE(ok);   // the first beat
    d.edit("Split", [&](Transaction& tx) { ok = d.cmd.splitMeasure(tx, Fraction(1, 4)); });
    ASSERT_TRUE(ok);
    EXPECT_EQ(measuresOf(d), "0/1+1/4 4/4\n1/4+3/4 4/4\n1/1+1/1 4/4\n");
    EXPECT_EQ(d.voice(d.flute.voice), before);
    // the horn's measure rest becomes one in each part
    EXPECT_EQ(d.voice(d.horn.voice).substr(0, 16), "0/1 1/4\n1/4 3/4\n");
    d.undoRedo();
    d.consistent();
    d.roundTrip();
}

TEST(Commands, JoiningMeasuresMakesOneOfTheFirstsTimeSignature)
{
    Doc d(3);
    d.chord(d.flute.voice, 0, { 60 }, 2);
    d.chord(d.flute.voice, 2, { 62 }, 2);
    d.chord(d.flute.voice, 4, { 64 }, 2);
    bool ok = false;
    d.edit("Join", [&](Transaction& tx) { ok = d.cmd.joinMeasures(tx, 0, 1); });
    ASSERT_TRUE(ok);
    EXPECT_EQ(measuresOf(d), "0/1+2/1 4/4\n2/1+1/1 4/4\n");
    EXPECT_EQ(d.voice(d.flute.voice).substr(0, 33), "0/1 1/2 60\n1/2 1/2 62\n1/1 1/2 64\n");
    // the horn's two measure rests are one measure rest of the joined measure
    EXPECT_EQ(d.voice(d.horn.voice), "0/1 2/1\n2/1 1/1\n");
    d.undoRedo();
    d.consistent();
    d.roundTrip();
    // and split again
    d.edit("Split", [&](Transaction& tx) { ok = d.cmd.splitMeasure(tx, Fraction(1, 1)); });
    ASSERT_TRUE(ok);
    EXPECT_EQ(measuresOf(d), "0/1+1/1 4/4\n1/1+1/1 4/4\n2/1+1/1 4/4\n");
    d.consistent();
}

TEST(Commands, AMeasureIsNotSplitInsideATuplet)
{
    Doc d(1);
    d.edit("Triplet", [&](Transaction& tx) { d.ops.makeTuplet(tx, d.flute.voice, Fraction(0, 1), 3, 2, Fraction(1, 4)); });
    bool ok = true;
    d.edit("Split", [&](Transaction& tx) { ok = d.cmd.splitMeasure(tx, Fraction(1, 6)); });
    EXPECT_FALSE(ok);
    d.edit("Split", [&](Transaction& tx) { ok = d.cmd.splitMeasure(tx, Fraction(1, 2)); });
    EXPECT_TRUE(ok);
    d.consistent();
}

TEST(Commands, AStaffWithItsOwnTimeSignatureFollowsOnlyWhereItHoldsMeasureRests)
{
    // the flute has its own 3/4 in the 4/4 score; a 4/4 from bar 1 (no change to the measures) gives it the
    // global signature where it has only measure rests, and is refused where it has music (MuseScore's rule)
    auto make = [](Doc& d, bool music) {
        d.edit("Local time signature", [&](Transaction& tx) {
            Id flow;
            for (Id c : d.o(d.model.score())->children()) {
                flow = d.o(c)->kind() == Kind::Flow ? c : flow;
            }
            create(tx, d.ids, flow, { Kind::FlowItem, { { F::SUBKIND, model::Text("timeSig") }, { F::ANCHOR, d.flute.staff },
                                                        { F::TIME, Fraction(0, 1) }, { F::TIMESIG_NUM, int64_t(3) },
                                                        { F::TIMESIG_DEN, int64_t(4) } } },
                   Placement::ByTime);
            for (Id e : d.ops.eventsOf(d.flute.voice)) {
                tx.set(e, F::STRETCH, Fraction(3, 4));
            }
        });
        if (music) {
            d.chord(d.flute.voice, 0, { 60 });
        }
    };
    Doc rests(2);
    make(rests, false);
    bool ok = false;
    rests.edit("4/4", [&](Transaction& tx) { ok = rests.cmd.changeTimeSignature(tx, 0, 4, 4); });
    EXPECT_TRUE(ok);
    EXPECT_EQ(rests.ops.stretchAt(rests.flute.voice, Fraction(0, 1)), Fraction(1, 1));
    rests.consistent();
    rests.roundTrip();
    Doc notes(2);
    make(notes, true);
    const std::string before = notes.listing();
    notes.edit("4/4", [&](Transaction& tx) { ok = notes.cmd.changeTimeSignature(tx, 0, 4, 4); });
    EXPECT_FALSE(ok);
    EXPECT_EQ(notes.listing(), before);
}

TEST(Commands, ATupletInsideATupletIsSplitWithIt)
{
    // a triplet of half notes with a triplet inside its first part, across a barline at its middle
    Doc d(2);
    d.edit("Tuplets", [&](Transaction& tx) {
        const Id outer = d.ops.makeTuplet(tx, d.flute.voice, Fraction(1, 2), 3, 2, Fraction(1, 4));
        // its first half (a half rest, as the triplet was filled) becomes a triplet of eighths and a quarter rest
        for (Id e : d.ops.eventsOf(d.flute.voice)) {
            if (d.o(e)->idOr(F::TUPLET) == outer && d.o(e)->fractionOr(F::TIME, Fraction()) == Fraction(1, 2)) {
                removeTree(tx, e);
                break;
            }
        }
        const Id inner = create(tx, d.ids, d.flute.voice, { Kind::Tuplet, { { F::TIME, Fraction(1, 2) }, { F::DURATION, Fraction(1, 6) },
                                                                            { F::BASE_DURATION, Fraction(1, 8) }, { F::RATIO_NUM, int64_t(3) },
                                                                            { F::RATIO_DEN, int64_t(2) }, { F::TUPLET, outer } } },
                                Placement::ByTime);
        for (int i = 0; i < 3; ++i) {
            create(tx, d.ids, d.flute.voice, { Kind::Rest, { { F::TIME, Fraction(1, 2) + Fraction(i, 18) }, { F::DURATION, Fraction(1, 18) },
                                                             { F::BASE_DURATION, Fraction(1, 8) }, { F::TUPLET, inner } } },
                   Placement::ByTime);
        }
        create(tx, d.ids, d.flute.voice, { Kind::Rest, { { F::TIME, Fraction(2, 3) }, { F::DURATION, Fraction(1, 6) },
                                                         { F::BASE_DURATION, Fraction(1, 4) }, { F::TUPLET, outer } } },
               Placement::ByTime);
    });
    d.consistent();
    bool ok = false;
    d.edit("3/4", [&](Transaction& tx) { ok = d.cmd.changeTimeSignature(tx, 0, 3, 4); });
    ASSERT_TRUE(ok);
    // two halves of the outer triplet (one at the middle of the 3/4 barline), the inner one in the first
    std::string tuplets;
    for (Id c : d.o(d.flute.voice)->children()) {
        if (d.o(c)->kind() == Kind::Tuplet) {
            tuplets += d.o(c)->fractionOr(F::TIME, Fraction()).toString() + "+" + d.o(c)->fractionOr(F::DURATION, Fraction()).toString()
                       + (d.o(c)->has(F::TUPLET) ? " inner" : "") + "\n";
        }
    }
    EXPECT_EQ(tuplets, "1/2+1/4\n1/2+1/6 inner\n3/4+1/4\n");
    d.consistent();
    d.roundTrip();
}

TEST(Commands, ATupletInsideATupletAcrossTheBarlineGivesBothHalvesTheirParents)
{
    // the outer triplet's middle part is a triplet of eighths, and the 3/4 barline falls at the middle of both
    Doc d(2);
    Id outer;
    d.edit("Tuplets", [&](Transaction& tx) {
        outer = d.ops.makeTuplet(tx, d.flute.voice, Fraction(1, 2), 3, 2, Fraction(1, 4));
        for (Id e : d.ops.eventsOf(d.flute.voice)) {
            if (d.o(e)->idOr(F::TUPLET) == outer) {
                removeTree(tx, e);
            }
        }
        for (const Fraction& t : { Fraction(1, 2), Fraction(5, 6) }) {
            create(tx, d.ids, d.flute.voice, { Kind::Rest, { { F::TIME, t }, { F::DURATION, Fraction(1, 6) }, { F::BASE_DURATION, Fraction(1, 4) },
                                                             { F::TUPLET, outer } } },
                   Placement::ByTime);
        }
        const Id inner = create(tx, d.ids, d.flute.voice, { Kind::Tuplet, { { F::TIME, Fraction(2, 3) }, { F::DURATION, Fraction(1, 6) },
                                                                            { F::BASE_DURATION, Fraction(1, 8) }, { F::RATIO_NUM, int64_t(3) },
                                                                            { F::RATIO_DEN, int64_t(2) }, { F::TUPLET, outer } } },
                                Placement::ByTime);
        for (int i = 0; i < 3; ++i) {
            const Id c = create(tx, d.ids, d.flute.voice, { Kind::Chord, { { F::TIME, Fraction(2, 3) + Fraction(i, 18) }, { F::DURATION, Fraction(1, 18) },
                                                                           { F::BASE_DURATION, Fraction(1, 8) }, { F::TUPLET, inner } } },
                                Placement::ByTime);
            create(tx, d.ids, c, { Kind::Note, { { F::PITCH, int64_t(72 + i) }, { F::TPC, int64_t(14 + 2 * i) } } });
        }
    });
    d.consistent();
    bool ok = false;
    d.edit("3/4", [&](Transaction& tx) { ok = d.cmd.changeTimeSignature(tx, 0, 3, 4); });
    ASSERT_TRUE(ok);
    std::string tuplets;
    for (Id c : d.o(d.flute.voice)->children()) {
        const model::Object* t = d.o(c);
        if (t->kind() == Kind::Tuplet) {
            const model::Object* p = d.o(t->idOr(F::TUPLET));
            tuplets += t->fractionOr(F::TIME, Fraction()).toString() + "+" + t->fractionOr(F::DURATION, Fraction()).toString()
                       + (p ? " in " + p->fractionOr(F::TIME, Fraction()).toString() : "") + "\n";
        }
    }
    EXPECT_EQ(tuplets, "1/2+1/4\n2/3+1/12 in 1/2\n3/4+1/4\n3/4+1/12 in 3/4\n");
    // the middle note is split at the barline and tied
    EXPECT_NE(d.voice(d.flute.voice).find("13/18 1/36 73~\n3/4 1/36 73\n"), std::string::npos) << d.voice(d.flute.voice);
    d.consistent();
    d.roundTrip();
}

namespace {
//! A doc with two more players (clarinet in C, bassoon: not transposing)
struct Quartet : Doc {
    ScoreBuilder::PlayerIds clarinet, bassoon;
    Quartet()
        : Doc(2)
    {
        edit("More players", [&](Transaction& tx) {
            clarinet = b.addPlayer(tx, "Clarinet in C", "clarinet", "woodwinds", 1);
            bassoon = b.addPlayer(tx, "Bassoon", "bassoon", "woodwinds", 1);
            for (Id v : { clarinet.voice, bassoon.voice }) {
                for (int i = 0; i < 2; ++i) {
                    create(tx, ids, v, { Kind::Rest, { { F::TIME, Fraction(i, 1) }, { F::DURATION, Fraction(1, 1) },
                                                       { F::MEASURE_REST, true } } }, Placement::ByTime);
                }
            }
        });
    }
    Id voiceOf(Id staff, int n) const
    {
        for (Id v : o(staff)->children()) {
            if (o(v)->kind() == Kind::Voice && o(v)->intOr(F::NUMBER, 0) == n) {
                return v;
            }
        }
        return Id();
    }
};
}

TEST(Commands, ExplodingChordsGivesEachStaffOneNoteFromTheTop)
{
    Quartet d;   // staves: flute, horn, clarinet, bassoon
    d.chord(d.flute.voice, 0, { 60, 64, 67 });
    d.chord(d.flute.voice, 1, { 62, 65 });
    int last = 0;
    d.edit("Explode", [&](Transaction& tx) { last = d.cmd.explode(tx, 0, 1, Fraction(0, 1), Fraction(1, 1)); });
    EXPECT_EQ(last, 3);   // as many staves as the largest chord has notes
    EXPECT_EQ(d.voice(d.flute.voice).substr(0, 22), "0/1 1/4 67\n1/4 1/4 65\n");
    EXPECT_EQ(d.voice(d.horn.voice).substr(0, 22), "0/1 1/4 64\n1/4 1/4 62\n");
    EXPECT_EQ(d.voice(d.clarinet.voice).substr(0, 22), "0/1 1/4 60\n1/4 1/4 62\n");   // the two-note chord: its bottom note again
    EXPECT_EQ(d.voice(d.bassoon.voice), "0/1 1/1\n1/1 1/1\n");   // untouched
    d.undoRedo();
    d.consistent();
    d.roundTrip();
}

TEST(Commands, ExplodingVoicesMovesEachToTheNextStaff)
{
    Quartet d;
    d.chord(d.flute.voice, 0, { 72 }, 2);
    d.edit("Voice 2", [&](Transaction& tx) {
        const Id v2 = d.ops.voice(tx, d.flute.staff, 1);
        d.ops.enterChord(tx, v2, Fraction(0, 1), { Fraction(1, 2), 0 }, { 60 });
    });
    int last = 0;
    d.edit("Explode", [&](Transaction& tx) { last = d.cmd.explode(tx, 0, 1, Fraction(0, 1), Fraction(1, 1)); });
    EXPECT_EQ(last, 2);
    EXPECT_EQ(d.voice(d.flute.voice).substr(0, 11), "0/1 1/2 72\n");
    EXPECT_TRUE(d.voice(d.voiceOf(d.flute.staff, 1)).empty());
    EXPECT_EQ(d.voice(d.horn.voice).substr(0, 11), "0/1 1/2 60\n");
    d.consistent();
    d.roundTrip();
}

TEST(Commands, ExplodingVoicesKeepsTheDestinationsEarlierMusic)
{
    // voice 2's music starts in the second measure: the horn's first measure is kept (it is only checked, and only
    // written, from there); MuseScore keeps it too
    Quartet d;
    d.chord(d.flute.voice, 0, { 72 }, 4);
    d.chord(d.flute.voice, 4, { 74 }, 4);
    d.chord(d.horn.voice, 0, { 60 }, 4);
    d.edit("Voice 2", [&](Transaction& tx) {
        const Id v2 = d.ops.voice(tx, d.flute.staff, 1);
        d.ops.enterChord(tx, v2, Fraction(1, 1), { Fraction(1, 2), 0 }, { 65 });
    });
    int last = 0;
    d.edit("Explode", [&](Transaction& tx) { last = d.cmd.explode(tx, 0, 1, Fraction(0, 1), Fraction(2, 1)); });
    EXPECT_EQ(last, 2);
    EXPECT_EQ(d.voice(d.horn.voice).substr(0, 22), "0/1 1/1 60\n1/1 1/2 65\n");
    EXPECT_TRUE(d.voice(d.voiceOf(d.flute.staff, 1)).empty());
    d.undoRedo();
    d.consistent();
    d.roundTrip();
}

TEST(Commands, ExplodingAndImplodingRefuseLinkedStaves)
{
    // a staff and the staff linked to it show the same music: taking notes out of one takes them out of both
    Quartet d;
    d.chord(d.flute.voice, 0, { 60, 64 });
    d.edit("Link", [&](Transaction& tx) { tx.set(d.horn.staff, F::LINKED_STAFF, d.flute.staff); });
    int last = 0;
    bool ok = true;
    d.edit("Explode", [&](Transaction& tx) { last = d.cmd.explode(tx, 0, 2, Fraction(0, 1), Fraction(1, 1)); });
    d.edit("Implode", [&](Transaction& tx) { ok = d.cmd.implode(tx, 0, 2, Fraction(0, 1), Fraction(1, 1)); });
    EXPECT_EQ(last, -1);
    EXPECT_FALSE(ok);
    EXPECT_EQ(d.voice(d.flute.voice).substr(0, 14), "0/1 1/4 60 64\n");
}

TEST(Commands, ImplodingJoinsVoicesAndStaves)
{
    {
        // one staff: voice 2's chord of the same length joins voice 1's; a different length stays
        Doc d(1);
        d.chord(d.flute.voice, 0, { 72 });
        d.chord(d.flute.voice, 1, { 74 });
        d.edit("Voice 2", [&](Transaction& tx) {
            const Id v2 = d.ops.voice(tx, d.flute.staff, 1);
            d.ops.enterChord(tx, v2, Fraction(0, 1), { Fraction(1, 4), 0 }, { 64 });
            d.ops.enterChord(tx, v2, Fraction(1, 4), { Fraction(1, 8), 0 }, { 65 });
        });
        bool ok = false;
        d.edit("Implode", [&](Transaction& tx) { ok = d.cmd.implode(tx, 0, 1, Fraction(0, 1), Fraction(1, 1)); });
        ASSERT_TRUE(ok);
        EXPECT_EQ(d.voice(d.flute.voice).substr(0, 25), "0/1 1/4 64 72\n1/4 1/4 74\n");
        Id v2;
        for (Id v : d.o(d.flute.staff)->children()) {
            v2 = d.o(v)->kind() == Kind::Voice && d.o(v)->intOr(F::NUMBER, 0) == 1 ? v : v2;
        }
        EXPECT_EQ(d.voice(v2), "1/4 1/8 65\n");
        d.consistent();
        d.roundTrip();
    }
    {
        // several staves: the second staff's music becomes the first staff's voice 2
        Quartet d;
        d.chord(d.flute.voice, 0, { 72 }, 2);
        d.chord(d.horn.voice, 0, { 60 });
        bool ok = false;
        d.edit("Implode", [&](Transaction& tx) { ok = d.cmd.implode(tx, 0, 2, Fraction(0, 1), Fraction(1, 1)); });
        ASSERT_TRUE(ok);
        // with its rests, as MuseScore clones them (not the empty second measure's measure rest)
        EXPECT_EQ(d.voice(d.voiceOf(d.flute.staff, 1)), "0/1 1/4 60\n1/4 1/4\n1/2 1/2\n");
        EXPECT_EQ(d.voice(d.horn.voice).substr(0, 11), "0/1 1/4 60\n");   // the source stays
        d.undoRedo();
        d.consistent();
        d.roundTrip();
    }
}

namespace {
//! The listing with slashes and accent rests marked, after saving and reopening
std::string reopenedWithSlashes(const model::Model& m)
{
    const auto path = std::filesystem::temp_directory_path() / "je_commands_slashes.mscz";
    std::string error;
    EXPECT_TRUE(io::saveScore(m.snapshot(), path.string(), error)) << error;
    model::Model back;
    IdGenerator backIds { 1 };
    EXPECT_TRUE(io::openScore(path.string(), back, backIds, error)) << error;
    std::filesystem::remove(path);
    return io::musicListing(back.snapshot(), true);
}
}

TEST(Commands, SlashFillPutsSlashesOnEveryBeatInAFreeVoice)
{
    // MuseScore's cmdSlashFill: on the middle line (B in the treble clef), stemless and not played; in the first
    // measure voice 1 has music, so the slashes go to voice 2; in the second, voice 1 holds only a measure rest
    Doc d;
    d.chord(d.flute.voice, 0, { 72 }, 2);
    int made = 0;
    d.edit("Fill with slashes", [&](Transaction& tx) { made = d.cmd.slashFill(tx, 0, 1, Fraction(0, 1), Fraction(2, 1), false); });
    EXPECT_EQ(made, 8);
    const std::string expected = "0 0 0/1 1/2 72\n0 0 1/2 1/2\n0 0 1/1 1/4 71 /\n0 0 5/4 1/4 71 /\n0 0 3/2 1/4 71 /\n"
                                 "0 0 7/4 1/4 71 /\n0 1 0/1 1/4 71 /\n0 1 1/4 1/4 71 /\n0 1 1/2 1/4 71 /\n0 1 3/4 1/4 71 /\n";
    const std::string listing = io::musicListing(d.model.snapshot(), true);
    EXPECT_EQ(listing.substr(0, expected.size()), expected);
    const Id slash = d.ops.eventsOf(d.flute.voice)[2];
    const std::string& noteDetails = d.o(d.notes(slash)[0])->text(F::DETAILS_AFTER);   // after the pitch, as MuseScore writes them
    EXPECT_NE(noteDetails.find("<head>slash</head>"), std::string::npos);
    EXPECT_NE(noteDetails.find("<play>0</play>"), std::string::npos);
    EXPECT_NE(noteDetails.find("<fixedLine>4</fixedLine>"), std::string::npos);
    EXPECT_NE(d.o(slash)->text(F::DETAILS).find("<noStem>1</noStem>"), std::string::npos);
    EXPECT_TRUE(d.cmd.isSlash(slash));
    d.undoRedo();
    d.consistent();
    d.roundTrip();
    EXPECT_EQ(reopenedWithSlashes(d.model), listing);
}

TEST(Commands, SlashFillOnATransposingInstrumentIsWrittenOnTheMiddleLine)
{
    // horn in F: the written B on the middle line sounds a fifth lower (E)
    Doc d(1);
    d.edit("Fill with slashes", [&](Transaction& tx) { d.cmd.slashFill(tx, 1, 2, Fraction(0, 1), Fraction(1, 1), false); });
    const Id slash = d.ops.eventsOf(d.horn.voice)[0];
    EXPECT_EQ(d.pitch(d.notes(slash)[0]), 64);
    EXPECT_EQ(d.tpc2(d.notes(slash)[0]), 19);   // B written
    EXPECT_EQ(d.tpc(d.notes(slash)[0]), 18);    // E sounding
    d.consistent();
}

TEST(Commands, SlashRhythmTogglesSlashesAndAccentRests)
{
    Doc d(1);
    const Id c = d.chord(d.flute.voice, 0, { 60, 64 });
    d.edit("Toggle rhythmic slash notation", [&](Transaction& tx) { EXPECT_TRUE(d.cmd.slashRhythm(tx, { c })); });
    EXPECT_TRUE(d.cmd.isSlash(c));
    // stems kept; the upper note hidden; neither played
    EXPECT_EQ(d.o(c)->text(F::DETAILS).find("<noStem>"), std::string::npos);
    const std::vector<Id> n = d.notes(c);
    EXPECT_NE(d.o(n[1])->text(F::DETAILS).find("<visible>0</visible>"), std::string::npos);
    EXPECT_NE(d.o(n[0])->text(F::DETAILS_AFTER).find("<play>0</play>"), std::string::npos);
    d.undoRedo();
    d.edit("Toggle rhythmic slash notation", [&](Transaction& tx) { d.cmd.slashRhythm(tx, { c }); });
    EXPECT_FALSE(d.cmd.isSlash(c));
    EXPECT_TRUE(d.o(n[0])->text(F::DETAILS).empty() && d.o(n[0])->text(F::DETAILS_AFTER).empty());
    // a rest in voice 3 becomes an accent rest
    Id rest;
    d.edit("Voice 3", [&](Transaction& tx) {
        const Id v3 = d.ops.voice(tx, d.flute.staff, 2);
        rest = d.ops.enterRest(tx, v3, Fraction(0, 1), { Fraction(1, 4), 0 });
    });
    d.edit("Toggle rhythmic slash notation", [&](Transaction& tx) { d.cmd.slashRhythm(tx, { rest }); });
    EXPECT_NE(io::musicListing(d.model.snapshot(), true).find("0 2 0/1 1/4 acc"), std::string::npos);
    d.consistent();
    d.roundTrip();
    EXPECT_EQ(reopenedWithSlashes(d.model), io::musicListing(d.model.snapshot(), true));
}

TEST(Commands, AnInstrumentChangeGivesItsTranspositionToNotesAfterIt)
{
    // the flute player changes to a B♭ clarinet in bar 2: notes there are written a tone higher, slashes too
    Doc d;
    d.edit("Instrument change", [&](Transaction& tx) {
        Id flow;
        for (Id c : d.model.find(d.model.score())->children()) {
            if (d.model.find(c)->kind() == Kind::Flow) {
                flow = c;
            }
        }
        create(tx, d.ids, flow, { Kind::FlowItem, { { F::SUBKIND, model::Text("instrumentChange") }, { F::ANCHOR, d.flute.staff },
                                                   { F::TIME, Fraction(1, 1) }, { F::INSTRUMENT, model::Text("bb-clarinet") },
                                                   { F::TRANSPOSE_CHROMATIC, int64_t(-2) } } },
               Placement::ByTime);
    });
    const Id before = d.chord(d.flute.voice, 0, { 60 });
    const Id after = d.chord(d.flute.voice, 4, { 60 });
    EXPECT_EQ(d.tpc2(d.notes(before)[0]), 14);   // C
    EXPECT_EQ(d.tpc2(d.notes(after)[0]), 16);    // D written
    d.edit("Fill with slashes", [&](Transaction& tx) { d.cmd.slashFill(tx, 0, 1, Fraction(5, 4), Fraction(2, 1), false); });
    const std::vector<Id> events = d.ops.eventsOf(d.flute.voice);
    const Id slash = events[events.size() - 3];
    EXPECT_EQ(d.pitch(d.notes(slash)[0]), 69);   // the written B on the middle line sounds A
    d.consistent();
}

TEST(Commands, SlashFillShowsTheSlashesOnALinkedStaffToo)
{
    // a staff linked to the filled one (a tablature under its standard staff) gets the same slashes, once (MuseScore,
    // filling both, puts a second set in another voice)
    Doc d(1);
    d.edit("Link", [&](Transaction& tx) { tx.set(d.horn.staff, F::LINKED_STAFF, d.flute.staff); });
    d.edit("Fill with slashes", [&](Transaction& tx) { EXPECT_EQ(d.cmd.slashFill(tx, 0, 1, Fraction(0, 1), Fraction(1, 1), false), 4); });
    const std::vector<Id> copies = d.ops.eventsOf(d.horn.voice);
    ASSERT_EQ(copies.size(), 4u);
    for (Id c : copies) {
        EXPECT_TRUE(d.cmd.isSlash(c));
    }
    EXPECT_TRUE(linkedStaffDifferences(d.model).empty());
    d.consistent();
}

TEST(Commands, ASplitChordKeepsGraceNotesBeforeItsFirstPieceAndAfterItsLast)
{
    // 3/4 from bar 1: the half note on beat 3 is split at the new barline; its acciaccatura stays before the first
    // piece, its after-grace (tied from it) comes after the last piece, and the tie runs on from there
    Doc d;
    const Id c = d.chord(d.flute.voice, 2, { 67 }, 2);
    d.edit("Acciaccatura", [&](Transaction& tx) { d.cmd.addGraceNote(tx, d.notes(c)[0], "acciaccatura", Fraction(1, 8)); });
    d.edit("After", [&](Transaction& tx) {
        const Id g = d.cmd.addGraceNote(tx, d.notes(c)[0], "grace16after", Fraction(1, 16));
        tx.set(d.notes(c)[0], F::TIE_TO, d.notes(g)[0]);
    });
    d.edit("Time signature", [&](Transaction& tx) { EXPECT_TRUE(d.cmd.changeTimeSignature(tx, 0, 3, 4)); });
    const std::string v = d.voice(d.flute.voice);
    EXPECT_NE(v.find("1/2 1/8 g 67\n1/2 1/4 67~\n3/4 1/4 67~\n3/4 1/16 g 67\n"), std::string::npos) << v;
    d.consistent();
    d.undoRedo();
    d.roundTrip();
}

TEST(Commands, AppearanceDetailsStayWhenTheMusicMovesInTime)
{
    // a chord's kept appearance (a stem direction read from the file) is written again after a measure is inserted
    // before it: its place in time isn't part of its details' fingerprint
    Doc d;
    const Id c = d.chord(d.flute.voice, 4, { 72 });
    d.edit("Details", [&](Transaction& tx) {
        tx.set(c, F::DETAILS, model::Text("<x>\n<StemDirection>up</StemDirection>\n</x>\n"));
        tx.set(c, F::DETAILS_KEY, model::detailsKey(*d.o(c)));
    });
    d.edit("Insert", [&](Transaction& tx) { d.ops.insertMeasures(tx, 0, 1, 4, 4); });
    EXPECT_EQ(d.o(c)->fractionOr(F::TIME, Fraction()), Fraction(2, 1));
    const auto path = std::filesystem::temp_directory_path() / "je_details_in_time.mscx";
    std::string error;
    ASSERT_TRUE(io::saveScore(d.model.snapshot(), path.string(), error)) << error;
    std::string text;
    {
        std::ifstream in(path);   // closed before the file is removed (Windows)
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    EXPECT_NE(text.find("<StemDirection>up</StemDirection>"), std::string::npos);
    std::filesystem::remove(path);
}

TEST(Commands, ChordSymbolsKeepTheirBassAndLookWhenTransposed)
{
    // "C7/E" up a tone is "D7/F♯"; its kept look (an offset read from the file) stays; from 4.60 it is written in a
    // <harmonyInfo>, as MuseScore reads it
    Doc d;
    Id harmony;
    d.edit("Chord symbol", [&](Transaction& tx) {
        Id flow;
        for (Id c : d.model.find(d.model.score())->children()) {
            flow = d.model.find(c)->kind() == Kind::Flow ? c : flow;
        }
        harmony = create(tx, d.ids, flow, { Kind::FlowItem, { { F::SUBKIND, model::Text("harmony") }, { F::ANCHOR, d.flute.staff },
                                                             { F::TIME, Fraction(0, 1) }, { F::TEXT, model::Text("7") },
                                                             { F::INT_VALUE, int64_t(14) }, { F::BASS, int64_t(18) } } },
                         Placement::ByTime);
        tx.set(harmony, F::DETAILS, model::Text("<x>\n<offset x=\"0\" y=\"-3\"/>\n</x>\n"));
        tx.set(harmony, F::DETAILS_KEY, model::detailsKey(*d.o(harmony)));
    });
    d.edit("Transpose", [&](Transaction& tx) {
        d.ops.transposeRange(tx, { d.flute.staff }, Fraction(0, 1), Fraction(1, 1), 1, 2);
    });
    EXPECT_EQ(d.o(harmony)->intOr(F::INT_VALUE, -1), 16);   // D
    EXPECT_EQ(d.o(harmony)->intOr(F::BASS, -1), 20);        // F♯
    const auto path = std::filesystem::temp_directory_path() / "je_chord_symbol.mscx";
    std::string error;
    ASSERT_TRUE(io::saveScore(d.model.snapshot(), path.string(), error)) << error;
    std::string text;
    {
        std::ifstream in(path);   // closed before the file is removed (Windows)
        text.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
    EXPECT_NE(text.find("<offset x=\"0\" y=\"-3\"/>"), std::string::npos);
    EXPECT_NE(text.find("<harmonyInfo>"), std::string::npos);
    model::Model back;
    IdGenerator backIds { 1 };
    ASSERT_TRUE(io::openScore(path.string(), back, backIds, error)) << error;
    bool found = false;
    back.snapshot().forEachObject([&](const model::Object& o) {
        if (o.text(F::SUBKIND) == "harmony") {
            found = o.intOr(F::INT_VALUE, -1) == 16 && o.intOr(F::BASS, -1) == 20 && o.text(F::TEXT) == "7";
        }
    });
    EXPECT_TRUE(found);
    std::filesystem::remove(path);
}

TEST(Commands, ChordSymbolsAreUnderstoodAndVoicedAsMuseScoreDoes)
{
    chords::ParsedChord p;
    ASSERT_TRUE(p.parse("maj7(#11)"));
    EXPECT_EQ(p.quality, "major");
    EXPECT_EQ(p.extension, "7");
    EXPECT_EQ(p.modifiers, std::vector<std::string>({ "#11" }));
    ASSERT_TRUE(p.parse("7sus4"));
    EXPECT_EQ(p.quality, "dominant");
    EXPECT_EQ(p.modifiers, std::vector<std::string>({ "sus4" }));
    ASSERT_TRUE(p.parse("m7b5"));
    EXPECT_EQ(p.quality, "minor");
    EXPECT_EQ(p.modifiers, std::vector<std::string>({ "b5" }));
    // C7 in close position over its root; G7 before C adds a ninth in the jazz reading (literal off)
    p.parse("7");
    std::string close;
    for (const auto& [pitch, tpc] : chords::generateNotes(p, 14, -9, true, chords::Voicing::Close, 0, {})) {
        close += std::to_string(pitch) + " ";
    }
    EXPECT_EQ(close, "36 60 64 67 70 ");
    chords::Next next;
    next.present = true;
    next.rootTpc = 14;
    next.chord.parse("");
    std::string jazz;
    for (const auto& [pitch, tpc] : chords::generateNotes(p, 15, -9, false, chords::Voicing::Close, 0, next)) {
        jazz += std::to_string(pitch) + " ";
    }
    EXPECT_EQ(jazz, "43 62 65 67 69 71 ");   // G B D F and A in the octave above middle C, over G
}

TEST(Commands, RealizingChordSymbolsWritesThemAsNotesUntilTheNext)
{
    // C7 on beat 1 and G on beat 3: C7 for a half note, G until the end of the score (tied over the barline)
    Doc d;
    d.edit("Chord symbols", [&](Transaction& tx) {
        Id flow;
        for (Id c : d.model.find(d.model.score())->children()) {
            flow = d.model.find(c)->kind() == Kind::Flow ? c : flow;
        }
        for (auto [t, root, name] : { std::tuple<Fraction, int64_t, const char*> { Fraction(0, 1), 14, "7" }, { Fraction(1, 2), 15, "" } }) {
            create(tx, d.ids, flow, { Kind::FlowItem, { { F::SUBKIND, model::Text("harmony") }, { F::ANCHOR, d.flute.staff },
                                                       { F::TIME, t }, { F::TEXT, model::Text(name) }, { F::INT_VALUE, root } } },
                   Placement::ByTime);
        }
    });
    std::vector<Id> hs;
    for (const auto& cs : d.cmd.chordSymbols()) {
        hs.push_back(cs.item);
    }
    ASSERT_EQ(hs.size(), 2u);
    Commands::RealizeOptions o;
    o.override = true;
    o.voicing = int(chords::Voicing::Close);
    int made = 0;
    d.edit("Realize chord symbols", [&](Transaction& tx) { made = d.cmd.realizeChordSymbols(tx, hs, o, true); });
    EXPECT_EQ(made, 2);
    EXPECT_EQ(d.voice(d.flute.voice), "0/1 1/2 36 60 64 67 70\n1/2 1/2 43~ 62~ 67~ 71~\n1/1 1/1 43 62 67 71\n");
    d.consistent();
    d.undoRedo();
    d.roundTrip();
}

TEST(Commands, ChordsMoveToTheOtherStaffOfTheirInstrument)
{
    // a piano's right hand reaching into the left hand's staff (cross-staff), and back; one staff at most either way
    Doc d;
    d.edit("Second staff", [&](Transaction& tx) {
        const Id st = create(tx, d.ids, d.flute.player, { Kind::Staff, {} });
        const Id v = d.ops.voice(tx, st, 0);
        create(tx, d.ids, v, { Kind::Rest, { { F::TIME, Fraction(0, 1) }, { F::DURATION, Fraction(1, 1) }, { F::MEASURE_REST, true } } },
               Placement::ByTime);
        create(tx, d.ids, v, { Kind::Rest, { { F::TIME, Fraction(1, 1) }, { F::DURATION, Fraction(1, 1) }, { F::MEASURE_REST, true } } },
               Placement::ByTime);
    });
    const Id c = d.chord(d.flute.voice, 0, { 55 });
    int moved = 0;
    d.edit("Move down", [&](Transaction& tx) { moved = d.cmd.moveToStaff(tx, { c }, false); });
    EXPECT_EQ(moved, 1);
    EXPECT_EQ(d.o(c)->intOr(F::STAFF_MOVE, 0), 1);
    d.edit("Move down", [&](Transaction& tx) { moved = d.cmd.moveToStaff(tx, { c }, false); });
    EXPECT_EQ(moved, 0);   // there is no third staff
    d.roundTrip();
    EXPECT_NE(reopenedWithSlashes(d.model).find("0 0 0/1 1/4 55 sm:1"), std::string::npos);
    d.edit("Move up", [&](Transaction& tx) { moved = d.cmd.moveToStaff(tx, { c }, true); });
    EXPECT_FALSE(d.o(c)->has(F::STAFF_MOVE));
    d.edit("Move up", [&](Transaction& tx) { moved = d.cmd.moveToStaff(tx, { c }, true); });
    EXPECT_EQ(moved, 0);   // the top staff has none above
    d.consistent();
}

namespace {
std::vector<const model::Object*> itemsOf(const Doc& d, const std::string& subkind)
{
    std::vector<const model::Object*> out;
    d.model.snapshot().forEachObject([&](const model::Object& o) {
        if (o.kind() == Kind::FlowItem && o.text(F::SUBKIND) == subkind) {
            out.push_back(&o);
        }
    });
    return out;
}
}

TEST(Commands, TextIsAddedAsMuseScoreAddsIt)
{
    Doc d;
    const Id c = d.chord(d.flute.voice, 1, { 72 });
    d.edit("Text", [&](Transaction& tx) {
        EXPECT_TRUE(d.cmd.addText(tx, "staff", d.notes(c)[0], "espressivo").isValid());
        EXPECT_TRUE(d.cmd.addText(tx, "dynamics", c, "mf").isValid());
        EXPECT_FALSE(d.cmd.addText(tx, "expression", c, "").isValid());   // left empty: taken away
        EXPECT_TRUE(d.cmd.addText(tx, "tempo", c, "").isValid());
        EXPECT_TRUE(d.cmd.addText(tx, "fingering", d.notes(c)[0], "3").isValid());
        EXPECT_FALSE(d.cmd.addText(tx, "fingering", c, "3").isValid());   // a fingering needs a note
    });
    ASSERT_EQ(itemsOf(d, "staffText").size(), 1u);
    EXPECT_EQ(itemsOf(d, "staffText")[0]->text(F::TEXT), "espressivo");
    EXPECT_EQ(itemsOf(d, "staffText")[0]->idOr(F::ANCHOR), d.flute.staff);
    EXPECT_EQ(itemsOf(d, "staffText")[0]->fractionOr(F::TIME, Fraction()), Fraction(1, 4));
    EXPECT_EQ(itemsOf(d, "dynamic")[0]->text(F::TEXT), "mf");
    EXPECT_TRUE(itemsOf(d, "expression").empty());
    // the tempo: the beat of 4/4 at 80
    ASSERT_EQ(itemsOf(d, "tempo").size(), 1u);
    EXPECT_EQ(itemsOf(d, "tempo")[0]->text(F::TEXT), "<sym>metNoteQuarterUp</sym> = 80");
    EXPECT_NEAR(std::get<double>(itemsOf(d, "tempo")[0]->get(F::REAL_VALUE)), 80.0 / 60.0, 1e-9);
    d.consistent();
    d.undoRedo();
    d.roundTrip();
}

TEST(Commands, RehearsalMarksFollowTheirSequence)
{
    Doc d(4);
    std::vector<Id> c;
    for (int m = 0; m < 4; ++m) {
        c.push_back(d.chord(d.flute.voice, m * 4, { 72 }));
    }
    auto mark = [&](int i) {
        Id made;
        d.edit("Rehearsal mark", [&](Transaction& tx) { made = d.cmd.addText(tx, "rehearsal_mark", c[size_t(i)], ""); });
        return d.model.find(made)->text(F::TEXT);
    };
    EXPECT_EQ(mark(0), "A");
    EXPECT_EQ(mark(2), "B");
    EXPECT_EQ(mark(1), "A1");   // between A and B: B is taken
    EXPECT_EQ(mark(3), "C");
}
