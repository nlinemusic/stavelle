// SPDX-License-Identifier: GPL-3.0-only
// S3: the operation vocabulary with MuseScore's note-entry rules (edit/operations.h). Exact expectations for the
// common cases, and a property test: random edit sequences keep the model consistent and voice 1 full, undo and
// redo exactly, and a score edited this way is saved and reopened with the same music.
#include <gtest/gtest.h>

#include <filesystem>
#include <random>
#include <string>
#include <vector>

#include "edit/history.h"
#include "edit/operations.h"
#include "edit/scorebuilder.h"
#include "io/mscxreader.h"
#include "io/mscxwriter.h"
#include "model/invariants.h"

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
    History history;
    ScoreBuilder::PlayerIds flute, horn;

    explicit Doc(int measures = 3, int num = 4, int den = 4)
    {
        Transaction tx(model, "New score");
        b.newScore(tx, "Edits");
        b.addMeasures(tx, measures, num, den);
        flute = b.addPlayer(tx, "Flute", "flute", "woodwinds", 1);
        horn = b.addPlayer(tx, "Horn in F", "horn", "brass", 1, -7);
        for (int i = 0; i < measures; ++i) {
            for (Id v : { flute.voice, horn.voice }) {
                create(tx, ids, v, { Kind::Rest, { { F::TIME, Fraction(i * num, den) }, { F::DURATION, Fraction(num, den) },
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

    std::string listing() const { return io::musicListing(model.snapshot()); }
    //! staff 0 voice 0 only
    std::string flute1() const
    {
        std::string out;
        for (Id e : ops.eventsOf(flute.voice)) {
            const model::Object* o = model.find(e);
            out += o->fractionOr(F::TIME, Fraction()).toString() + " " + o->fractionOr(F::DURATION, Fraction()).toString();
            if (o->has(F::GRACE)) {
                out += " g";
            }
            for (Id n : o->children()) {
                if (model.find(n)->kind() == Kind::Note) {
                    out += " " + std::to_string(model.find(n)->intOr(F::PITCH, 0));
                    if (model.find(n)->has(F::TIE_TO)) {
                        out += "~";
                    }
                }
            }
            out += "\n";
        }
        return out;
    }
};

std::string problems(const model::Snapshot& s)
{
    std::string out;
    for (const std::string& p : model::checkInvariants(s)) {
        out += p + "\n";
    }
    return out;
}

//! Voice 1 of every staff covers the music from the start to the end, without gaps or overlaps
std::string voice1Gaps(const Doc& d)
{
    const auto ms = d.ops.measures();
    const Fraction end = d.ops.measureEnd(ms.back());
    std::string out;
    for (Id v : { d.flute.voice, d.horn.voice }) {
        Fraction at;
        for (Id e : d.ops.eventsOf(v)) {
            const model::Object* o = d.model.find(e);
            if (o->has(F::GRACE)) {
                continue;
            }
            if (o->fractionOr(F::TIME, Fraction()) != at) {
                out += "voice 1 jumps at " + at.toString() + " to " + o->fractionOr(F::TIME, Fraction()).toString() + "\n";
            }
            at = o->fractionOr(F::TIME, Fraction()) + o->fractionOr(F::DURATION, Fraction());
        }
        if (at != end) {
            out += "voice 1 ends at " + at.toString() + "\n";
        }
    }
    return out;
}
}

TEST(EditOperations, NoteValuesAreWrittenAsMuseScoreWritesThem)
{
    // rests: undotted, on their own beat
    EXPECT_EQ(valuesFor(Fraction(1, 4), Fraction(3, 4), true), (std::vector<NoteValue> { { Fraction(1, 4), 0 }, { Fraction(1, 2), 0 } }));
    EXPECT_EQ(valuesFor(Fraction(1, 8), Fraction(7, 8), true),
              (std::vector<NoteValue> { { Fraction(1, 8), 0 }, { Fraction(1, 4), 0 }, { Fraction(1, 2), 0 } }));
    // notes: one dotted value where one fits
    EXPECT_EQ(valuesFor(Fraction(0, 1), Fraction(3, 8), false), (std::vector<NoteValue> { { Fraction(1, 4), 1 } }));
    EXPECT_EQ(valuesFor(Fraction(0, 1), Fraction(7, 8), false), (std::vector<NoteValue> { { Fraction(1, 2), 2 } }));
    EXPECT_EQ(valuesFor(Fraction(0, 1), Fraction(5, 8), false), (std::vector<NoteValue> { { Fraction(1, 2), 0 }, { Fraction(1, 8), 0 } }));
}

TEST(EditOperations, EnteringANoteOverwritesAndFillsWithRests)
{
    Doc d;
    d.edit("Enter", [&](Transaction& tx) { d.ops.enterChord(tx, d.flute.voice, Fraction(0, 1), { Fraction(1, 4), 0 }, { 72 }); });
    EXPECT_EQ(d.flute1(), "0/1 1/4 72\n1/4 1/4\n1/2 1/2\n1/1 1/1\n2/1 1/1\n");
    // a dotted half over the quarter and the rests after it
    d.edit("Enter", [&](Transaction& tx) { d.ops.enterChord(tx, d.flute.voice, Fraction(0, 1), { Fraction(1, 2), 1 }, { 74 }); });
    EXPECT_EQ(d.flute1(), "0/1 3/4 74\n3/4 1/4\n1/1 1/1\n2/1 1/1\n");
    // an eighth into the middle of it: the dotted half is cut to a quarter, the eighth, then rests to its old end
    d.edit("Enter", [&](Transaction& tx) { d.ops.enterChord(tx, d.flute.voice, Fraction(1, 4), { Fraction(1, 8), 0 }, { 76 }); });
    EXPECT_EQ(d.flute1(), "0/1 1/4 74\n1/4 1/8 76\n3/8 1/8\n1/2 1/4\n3/4 1/4\n1/1 1/1\n2/1 1/1\n");
    EXPECT_EQ(problems(d.model.snapshot()), "");
    EXPECT_EQ(voice1Gaps(d), "");
}

TEST(EditOperations, ANoteAcrossTheBarlineIsTied)
{
    Doc d;
    d.edit("Enter", [&](Transaction& tx) { d.ops.enterChord(tx, d.flute.voice, Fraction(3, 4), { Fraction(1, 2), 0 }, { 67, 72 }); });
    EXPECT_EQ(d.flute1(), "0/1 1/2\n1/2 1/4\n3/4 1/4 67~ 72~\n1/1 1/4 67 72\n5/4 1/4\n3/2 1/2\n2/1 1/1\n");
    EXPECT_EQ(problems(d.model.snapshot()), "");
    EXPECT_EQ(voice1Gaps(d), "");
}

TEST(EditOperations, DeletingAndChangingValues)
{
    Doc d;
    Id c;
    d.edit("Enter", [&](Transaction& tx) { c = d.ops.enterChord(tx, d.flute.voice, Fraction(0, 1), { Fraction(1, 2), 0 }, { 72 }); });
    d.edit("Shorter", [&](Transaction& tx) { d.ops.setValue(tx, c, { Fraction(1, 4), 0 }); });
    EXPECT_EQ(d.flute1(), "0/1 1/4 72\n1/4 1/4\n1/2 1/2\n1/1 1/1\n2/1 1/1\n");
    d.edit("Longer", [&](Transaction& tx) { d.ops.setValue(tx, c, { Fraction(1, 1), 0 }); });
    EXPECT_EQ(d.flute1(), "0/1 1/1 72\n1/1 1/1\n2/1 1/1\n");
    d.edit("Delete", [&](Transaction& tx) { d.ops.deleteEvent(tx, c); });
    EXPECT_EQ(d.flute1(), "0/1 1/1\n1/1 1/1\n2/1 1/1\n");
    // in voice 2 a deleted chord leaves a gap
    Id v2, c2;
    d.edit("Voice 2", [&](Transaction& tx) {
        v2 = d.ops.voice(tx, d.flute.staff, 1);
        c2 = d.ops.enterChord(tx, v2, Fraction(1, 2), { Fraction(1, 4), 0 }, { 60 });
    });
    EXPECT_EQ(d.ops.eventsOf(v2).size(), 1u);
    d.edit("Delete", [&](Transaction& tx) { d.ops.deleteEvent(tx, c2); });
    EXPECT_TRUE(d.ops.eventsOf(v2).empty());
    EXPECT_EQ(problems(d.model.snapshot()), "");
}

TEST(EditOperations, NotesInChordsAndTies)
{
    Doc d;
    Id c1, c2;
    d.edit("Enter", [&](Transaction& tx) {
        c1 = d.ops.enterChord(tx, d.flute.voice, Fraction(0, 1), { Fraction(1, 4), 0 }, { 72 });
        c2 = d.ops.enterChord(tx, d.flute.voice, Fraction(1, 4), { Fraction(1, 4), 0 }, { 72 });
    });
    Id n;
    d.edit("Add note", [&](Transaction& tx) { n = d.ops.addNote(tx, c1, 64); });
    d.edit("Tie", [&](Transaction& tx) { EXPECT_TRUE(d.ops.tieToNext(tx, d.b.notesOf(c1).back())); });
    EXPECT_EQ(d.flute1().substr(0, 26), "0/1 1/4 64 72~\n1/4 1/4 72\n");
    // a new pitch breaks the tie; removing the last note leaves a rest
    d.edit("Pitch", [&](Transaction& tx) { d.ops.setPitch(tx, d.b.notesOf(c2).front(), 74); });
    EXPECT_EQ(d.flute1().substr(0, 25), "0/1 1/4 64 72\n1/4 1/4 74\n");
    d.edit("Remove", [&](Transaction& tx) { d.ops.removeNote(tx, d.b.notesOf(c2).front()); });
    EXPECT_EQ(d.flute1().substr(0, 21), "0/1 1/4 64 72\n1/4 1/4");
    // spelled for the key (C major here): E natural
    EXPECT_EQ(d.model.find(n)->intOr(F::TPC, 0), 18);   // E
    EXPECT_EQ(problems(d.model.snapshot()), "");
}

TEST(EditOperations, TupletsAreEnteredIntoAndRemovedWhole)
{
    Doc d;
    Id tuplet;
    auto startsWith = [](const std::string& s, const std::string& p) { return s.compare(0, p.size(), p) == 0; };
    // a triplet of eighths on beat 2: MuseScore's rests in it, a quarter and an eighth
    d.edit("Triplet", [&](Transaction& tx) { tuplet = d.ops.makeTuplet(tx, d.flute.voice, Fraction(1, 4), 3, 2, Fraction(1, 8)); });
    EXPECT_TRUE(startsWith(d.flute1(), "0/1 1/4\n1/4 1/6\n5/12 1/12\n1/2 1/2\n")) << d.flute1();
    // notes entered at its positions go into it, in its values
    d.edit("Enter", [&](Transaction& tx) {
        d.ops.enterChord(tx, d.flute.voice, Fraction(1, 4), { Fraction(1, 8), 0 }, { 72 });
        d.ops.enterChord(tx, d.flute.voice, Fraction(1, 3), { Fraction(1, 4), 0 }, { 74 });   // a quarter in a triplet
    });
    EXPECT_TRUE(startsWith(d.flute1(), "0/1 1/4\n1/4 1/12 72\n1/3 1/6 74\n1/2 1/2\n")) << d.flute1();
    EXPECT_EQ(problems(d.model.snapshot()), "");
    // a quarter from before it over part of it removes the tuplet; what it held beyond becomes rests
    d.edit("Over", [&](Transaction& tx) { d.ops.enterChord(tx, d.flute.voice, Fraction(1, 8), { Fraction(1, 4), 0 }, { 60 }); });
    EXPECT_EQ(d.model.find(tuplet), nullptr);
    EXPECT_TRUE(startsWith(d.flute1(), "0/1 1/8\n1/8 1/4 60\n3/8 1/8\n1/2 1/2\n")) << d.flute1();
    EXPECT_EQ(problems(d.model.snapshot()), "");
    EXPECT_EQ(voice1Gaps(d), "");
}

TEST(EditOperations, InsertingAndRemovingMeasuresMovesTheMusic)
{
    Doc d;
    d.edit("Enter", [&](Transaction& tx) { d.ops.enterChord(tx, d.flute.voice, Fraction(1, 1), { Fraction(1, 1), 0 }, { 72 }); });
    d.edit("Insert", [&](Transaction& tx) { d.ops.insertMeasures(tx, 0, 2, 3, 4); });
    EXPECT_EQ(d.ops.measures().size(), 5u);
    EXPECT_NE(d.flute1().find("5/2 1/1 72"), std::string::npos);   // 3/4 + 3/4 + 1/1
    EXPECT_EQ(problems(d.model.snapshot()), "");
    EXPECT_EQ(voice1Gaps(d), "");
    d.edit("Remove", [&](Transaction& tx) { d.ops.removeMeasures(tx, 0, 3); });
    EXPECT_EQ(d.ops.measures().size(), 2u);
    EXPECT_EQ(d.flute1(), "0/1 1/1 72\n1/1 1/1\n");
    EXPECT_EQ(problems(d.model.snapshot()), "");
    EXPECT_EQ(voice1Gaps(d), "");
}

TEST(EditOperations, RandomEditsStayConsistentUndoExactlyAndSurviveSaving)
{
    for (unsigned seed = 1; seed <= 12; ++seed) {
        Doc d(4);
        std::mt19937 rng(seed);
        auto pick = [&rng](int n) { return int(rng() % unsigned(n)); };
        const std::vector<NoteValue> values { { Fraction(1, 1), 0 }, { Fraction(1, 2), 1 }, { Fraction(1, 2), 0 }, { Fraction(1, 4), 1 },
                                              { Fraction(1, 4), 0 }, { Fraction(1, 8), 0 }, { Fraction(1, 16), 0 } };
        for (int step = 0; step < 150; ++step) {
            const Id voice = pick(2) ? d.flute.voice : d.horn.voice;
            const auto events = d.ops.eventsOf(voice);
            const Id event = events[size_t(pick(int(events.size())))];
            const Fraction t = d.model.find(event)->fractionOr(F::TIME, Fraction());
            const int what = pick(9);
            const std::string name = "step " + std::to_string(step) + " op " + std::to_string(what);
            d.edit(name, [&](Transaction& tx) {
                switch (what) {
                case 0:
                case 1:
                case 2:
                    d.ops.enterChord(tx, voice, t, values[size_t(pick(int(values.size())))], { 55 + pick(30) });
                    break;
                case 3:
                    d.ops.enterRest(tx, voice, t, values[size_t(pick(int(values.size())))]);
                    break;
                case 4:
                    d.ops.deleteEvent(tx, event);
                    break;
                case 5:
                    d.ops.setValue(tx, event, values[size_t(pick(int(values.size())))]);
                    break;
                case 6:
                    if (d.model.find(event)->kind() == Kind::Chord) {
                        d.ops.addNote(tx, event, 55 + pick(30));
                    }
                    break;
                case 7:
                    if (d.model.find(event)->kind() == Kind::Chord) {
                        const auto notes = d.b.notesOf(event);
                        if (pick(2)) {
                            d.ops.removeNote(tx, notes[size_t(pick(int(notes.size())))]);
                        } else {
                            d.ops.tieToNext(tx, notes.front());
                        }
                    }
                    break;
                case 8:
                    if (!d.model.find(event)->has(F::TUPLET)) {
                        const Fraction ms = d.ops.measureStart(d.ops.measureAt(t));
                        const Fraction pos = t - ms;
                        if ((pos / Fraction(1, 4)).denominator() == 1 && t + Fraction(1, 4) <= d.ops.measureEnd(d.ops.measureAt(t))) {
                            d.ops.makeTuplet(tx, voice, t, 3, 2, Fraction(1, 8));
                        }
                    }
                    break;
                }
            });
            ASSERT_EQ(problems(d.model.snapshot()), "") << "seed " << seed << ", " << name;
            ASSERT_EQ(voice1Gaps(d), "") << "seed " << seed << ", " << name;
        }
        const std::string final = d.model.snapshot().dump();
        // saved and reopened: the same music
        const auto path = std::filesystem::temp_directory_path() / ("je_edit_random_" + std::to_string(seed) + ".mscz");
        std::string error;
        ASSERT_TRUE(io::saveScore(d.model.snapshot(), path.string(), error)) << error;
        model::Model back;
        IdGenerator ids(1);
        ASSERT_TRUE(io::openScore(path.string(), back, ids, error)) << error;
        EXPECT_EQ(io::musicListing(back.snapshot()), d.listing()) << "seed " << seed;
        EXPECT_EQ(problems(back.snapshot()), "") << "seed " << seed;
        // every step undoes and redoes exactly
        while (d.history.index() > 1) {
            d.history.undo(d.model);
        }
        d.history.undo(d.model);
        EXPECT_EQ(d.model.snapshot().objectCount(), 0u);
        while (d.history.canRedo()) {
            d.history.redo(d.model);
        }
        EXPECT_EQ(d.model.snapshot().dump(), final) << "seed " << seed;
    }
}

TEST(EditOperations, TranspositionMovesPitchesAndSpellings)
{
    Doc d;
    Id c;
    d.edit("Enter", [&](Transaction& tx) {
        c = d.ops.enterChord(tx, d.flute.voice, Fraction(0, 1), { Fraction(1, 2), 0 }, { 60, 66 });   // C, F sharp
        d.ops.enterChord(tx, d.flute.voice, Fraction(1, 2), { Fraction(1, 2), 0 }, { 60 });
        d.ops.tieToNext(tx, d.b.notesOf(c).front());
        create(tx, d.ids, d.b.flow(), { Kind::FlowItem, { { F::SUBKIND, model::Text("harmony") }, { F::ANCHOR, d.flute.staff },
                                                          { F::TIME, Fraction(0, 1) }, { F::INT_VALUE, int64_t(14) },
                                                          { F::TEXT, model::Text("maj7") } } },
               Placement::ByTime);
    });
    // up a major third: C → E, F♯ → A♯; the tie holds; the chord symbol's root C → E
    d.edit("Transpose", [&](Transaction& tx) { d.ops.transposeRange(tx, { d.flute.staff }, Fraction(0, 1), Fraction(1, 1), 2, 4); });
    const auto notes = d.b.notesOf(c);
    EXPECT_EQ(d.model.find(notes[0])->intOr(F::PITCH, 0), 64);
    EXPECT_EQ(d.model.find(notes[0])->intOr(F::TPC, 0), 18);   // E
    EXPECT_EQ(d.model.find(notes[1])->intOr(F::PITCH, 0), 70);
    EXPECT_EQ(d.model.find(notes[1])->intOr(F::TPC, 0), 24);   // A sharp
    EXPECT_TRUE(d.model.find(notes[0])->has(F::TIE_TO));
    EXPECT_EQ(problems(d.model.snapshot()), "");
    bool rootMoved = false;
    d.model.snapshot().forEachObject([&](const model::Object& o) {
        rootMoved = rootMoved || (o.text(F::SUBKIND) == "harmony" && o.intOr(F::INT_VALUE, 0) == 18);
    });
    EXPECT_TRUE(rootMoved);
    // the horn (another staff) is untouched
    EXPECT_EQ(d.listing().find("1 0 0/1 1/1 60"), std::string::npos);
}

TEST(Operations, OverwritingANoteTiedFromAnotherVoiceRemovesTheTie)
{
    Doc d;
    Id from;
    d.edit("Enter", [&](Transaction& tx) {
        const Id v2 = d.ops.voice(tx, d.flute.staff, 1);
        const Id a = d.ops.enterChord(tx, v2, Fraction(1, 2), { Fraction(1, 2), 0 }, { 67 });
        const Id b = d.ops.enterChord(tx, d.flute.voice, Fraction(1, 1), { Fraction(1, 4), 0 }, { 67 });
        from = d.b.notesOf(a).front();
        tx.set(from, F::TIE_TO, d.b.notesOf(b).front());   // a tie from voice 2 into voice 1 (as files have)
    });
    ASSERT_EQ(problems(d.model.snapshot()), "");
    d.edit("Overwrite", [&](Transaction& tx) { d.ops.enterChord(tx, d.flute.voice, Fraction(1, 1), { Fraction(1, 4), 0 }, { 72 }); });
    EXPECT_FALSE(d.model.find(from)->has(F::TIE_TO));
    EXPECT_EQ(problems(d.model.snapshot()), "");
}
