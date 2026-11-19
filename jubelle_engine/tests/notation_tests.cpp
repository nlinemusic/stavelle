// SPDX-License-Identifier: GPL-3.0-only
// S1: the model represents professional notation: voices, tuplets (nested), grace notes, ties, articulations,
// lyrics, chord symbols, dynamics, repeats and voltas, jumps, instrument changes, slurs and hairpins, layouts.
#include <gtest/gtest.h>

#include <functional>
#include <string>
#include <vector>

#include "edit/history.h"
#include "edit/scorebuilder.h"
#include "edit/structure.h"
#include "model/invariants.h"

using namespace je;
using namespace je::edit;
using je::model::Kind;
using je::model::Text;
namespace F = je::model::fields;

namespace {
std::string problems(const model::Snapshot& s)
{
    std::string out;
    for (const std::string& p : model::checkInvariants(s)) {
        out += p + "\n";
    }
    return out;
}

struct Score {
    model::Model model;
    IdGenerator ids { 1 };
    ScoreBuilder b { model, ids };
    History history;
    ScoreBuilder::PlayerIds flute, horn;
    Id voice2;
    Id chordC, chordTupletA, tuplet, innerTuplet, grace, mainAfterGrace, lyric, slur, volta, change, layout;

    Id chord(Transaction& tx, Id voice, Fraction t, Fraction base, int dots, std::vector<int> pitches, Id inTuplet = Id(),
             Fraction factor = Fraction(1, 1))
    {
        Fraction d = base;
        Fraction add = base;
        for (int i = 0; i < dots; ++i) {
            add = add * Fraction(1, 2);
            d += add;
        }
        std::vector<std::pair<Field, model::Value> > f { { F::TIME, t }, { F::DURATION, d * factor }, { F::BASE_DURATION, base } };
        if (dots) {
            f.push_back({ F::DOTS, int64_t(dots) });
        }
        if (inTuplet.isValid()) {
            f.push_back({ F::TUPLET, inTuplet });
        }
        const Id c = create(tx, ids, voice, { Kind::Chord, f }, Placement::ByTime);
        for (int p : pitches) {
            create(tx, ids, c, { Kind::Note, { { F::PITCH, int64_t(p) } } });
        }
        return c;
    }

    void build()
    {
        Transaction tx(model, "Everything");
        b.newScore(tx, "Notation coverage");
        b.addMeasures(tx, 4);
        flute = b.addPlayer(tx, "Flute", "flute", "woodwinds", 1);
        horn = b.addPlayer(tx, "Horn in F", "horn", "brass", 1, -7);
        voice2 = create(tx, ids, flute.staff, { Kind::Voice, { { F::FLOW, b.flow() }, { F::NUMBER, int64_t(1) } } });

        // bar 1, voice 1: dotted quarter + eighth, then a triplet of eighths with a nested triplet of sixteenths
        chordC = chord(tx, flute.voice, Fraction(0, 1), Fraction(1, 4), 1, { 72 });
        chord(tx, flute.voice, Fraction(3, 8), Fraction(1, 8), 0, { 74 });
        tuplet = create(tx, ids, flute.voice, { Kind::Tuplet, { { F::TIME, Fraction(1, 2) }, { F::DURATION, Fraction(1, 4) },
                                                    { F::BASE_DURATION, Fraction(1, 8) }, { F::RATIO_NUM, int64_t(3) },
                                                    { F::RATIO_DEN, int64_t(2) } } }, Placement::ByTime);
        chordTupletA = chord(tx, flute.voice, Fraction(1, 2), Fraction(1, 8), 0, { 76 }, tuplet, Fraction(2, 3));
        chord(tx, flute.voice, Fraction(1, 2) + Fraction(1, 12), Fraction(1, 8), 0, { 77 }, tuplet, Fraction(2, 3));
        innerTuplet = create(tx, ids, flute.voice, { Kind::Tuplet, { { F::TIME, Fraction(1, 2) + Fraction(1, 6) },
                                                         { F::DURATION, Fraction(1, 12) }, { F::BASE_DURATION, Fraction(1, 16) },
                                                         { F::RATIO_NUM, int64_t(3) }, { F::RATIO_DEN, int64_t(2) },
                                                         { F::TUPLET, tuplet } } }, Placement::ByTime);
        for (int i = 0; i < 3; ++i) {
            chord(tx, flute.voice, Fraction(1, 2) + Fraction(1, 6) + Fraction(i, 36), Fraction(1, 16), 0, { 79 + i }, innerTuplet,
                  Fraction(4, 9));
        }
        // a grace note before the last quarter of bar 1, then the main chord
        grace = create(tx, ids, flute.voice, { Kind::Chord, { { F::TIME, Fraction(3, 4) }, { F::DURATION, Fraction(1, 8) },
                                                   { F::BASE_DURATION, Fraction(1, 8) }, { F::GRACE, true },
                                                   { F::GRACE_TYPE, Text("acciaccatura") } } }, Placement::Append);
        create(tx, ids, grace, { Kind::Note, { { F::PITCH, int64_t(83) } } });
        mainAfterGrace = chord(tx, flute.voice, Fraction(3, 4), Fraction(1, 4), 0, { 84 });

        // voice 2 in bar 1, a whole-measure rest
        create(tx, ids, voice2, { Kind::Rest, { { F::TIME, Fraction(0, 1) }, { F::DURATION, Fraction(1, 1) }, { F::MEASURE_REST, true } } },
               Placement::ByTime);

        // bar 2: tied half notes, articulations, a lyric, a slur over the bar
        const Id h1 = chord(tx, flute.voice, Fraction(1, 1), Fraction(1, 2), 0, { 72 });
        const Id h2 = chord(tx, flute.voice, Fraction(3, 2), Fraction(1, 2), 0, { 72 });
        tx.set(b.notesOf(h1).front(), F::TIE_TO, b.notesOf(h2).front());
        create(tx, ids, h1, { Kind::Attachment, { { F::SUBKIND, Text("accent") } } });
        create(tx, ids, h2, { Kind::Attachment, { { F::SUBKIND, Text("fermata") } } });
        lyric = create(tx, ids, h1, { Kind::Attachment, { { F::SUBKIND, Text("lyric") }, { F::VERSE, int64_t(0) },
                                          { F::SYLLABIC, Text("begin") }, { F::TEXT, Text("Hal") } } });
        slur = create(tx, ids, b.flow() == Id() ? Id() : model.score(), { Kind::Spanner, { { F::SUBKIND, Text("slur") },
                                                                             { F::ANCHOR, h1 }, { F::END_ANCHOR, h2 },
                                                                             { F::TIME, Fraction(1, 1) }, { F::DURATION, Fraction(1, 1) } } });

        // horn: notes in bars 1–4 (written pitch is derived from the player's transposition)
        for (int bar = 0; bar < 4; ++bar) {
            chord(tx, horn.voice, Fraction(bar, 1), Fraction(1, 1), 0, { 65 });
        }
        // a hairpin, a dynamic, a chord symbol, staff text, tempo, key, clef and an instrument change
        create(tx, ids, model.score(), { Kind::Spanner, { { F::SUBKIND, Text("hairpin") }, { F::ANCHOR, horn.staff },
                                             { F::TIME, Fraction(0, 1) }, { F::DURATION, Fraction(2, 1) }, { F::INT_VALUE, int64_t(0) } } });
        auto item = [&](std::vector<std::pair<Field, model::Value> > f) {
            return create(tx, ids, b.flow(), { Kind::FlowItem, f }, Placement::ByTime);
        };
        item({ { F::SUBKIND, Text("dynamic") }, { F::ANCHOR, horn.staff }, { F::TIME, Fraction(0, 1) }, { F::TEXT, Text("mf") } });
        item({ { F::SUBKIND, Text("harmony") }, { F::ANCHOR, flute.staff }, { F::TIME, Fraction(0, 1) }, { F::TEXT, Text("Cmaj7") } });
        item({ { F::SUBKIND, Text("tempo") }, { F::TIME, Fraction(0, 1) }, { F::REAL_VALUE, 2.0 }, { F::TEXT, Text("♩ = 120") } });
        item({ { F::SUBKIND, Text("keySig") }, { F::TIME, Fraction(0, 1) }, { F::INT_VALUE, int64_t(-1) } });
        item({ { F::SUBKIND, Text("clef") }, { F::ANCHOR, horn.staff }, { F::TIME, Fraction(0, 1) }, { F::TEXT, Text("G") } });
        change = item({ { F::SUBKIND, Text("instrumentChange") }, { F::ANCHOR, flute.staff }, { F::TIME, Fraction(2, 1) },
                        { F::INSTRUMENT, Text("piccolo") }, { F::NAME, Text("Piccolo") }, { F::TRANSPOSE_CHROMATIC, int64_t(12) } });
        item({ { F::SUBKIND, Text("rehearsalMark") }, { F::TIME, Fraction(2, 1) }, { F::TEXT, Text("A") } });
        item({ { F::SUBKIND, Text("jump") }, { F::TIME, Fraction(4, 1) }, { F::TEXT, Text("D.C. al Fine") } });

        // repeats: bars 3–4 repeated, with first and second endings
        const auto& measures = model.find(b.flow())->children().ids();
        std::vector<Id> bars;
        for (Id m : measures) {
            if (model.find(m)->kind() == Kind::Measure) {
                bars.push_back(m);
            }
        }
        tx.set(bars[2], F::REPEAT_START, true);
        tx.set(bars[3], F::REPEAT_END, int64_t(2));
        volta = create(tx, ids, model.score(), { Kind::Spanner, { { F::SUBKIND, Text("volta") }, { F::ANCHOR, bars[3] },
                                                     { F::TIME, Fraction(3, 1) }, { F::DURATION, Fraction(1, 1) },
                                                     { F::INT_VALUE, int64_t(1) }, { F::TEXT, Text("1.") } } });
        // a part layout for the horn
        layout = create(tx, ids, model.score(), { Kind::Layout, { { F::NAME, Text("Horn in F") },
                                                      { F::PLAYERS, model::IdList({ horn.player }) } } });
        history.push(tx.commit());
    }
};
}

TEST(Notation, EverythingFitsTheModelAndIsConsistent)
{
    Score s;
    s.build();
    EXPECT_EQ(problems(s.model.snapshot()), "");
    EXPECT_EQ(s.model.find(s.lyric)->text(F::TEXT), "Hal");
    EXPECT_EQ(s.model.find(s.change)->text(F::INSTRUMENT), "piccolo");
}

TEST(Notation, EverythingUndoesAndRedoesExactly)
{
    Score s;
    s.build();
    const std::string full = s.model.snapshot().dump();
    s.history.undo(s.model);
    EXPECT_EQ(s.model.snapshot().objectCount(), 0u);
    s.history.redo(s.model);
    EXPECT_EQ(s.model.snapshot().dump(), full);
}

TEST(Notation, InvariantsCatchNotationMistakes)
{
    auto expectProblem = [](const std::function<void(Score&, Transaction&)>& breakIt, const std::string& what) {
        Score s;
        s.build();
        Transaction tx(s.model, "Break");
        breakIt(s, tx);
        tx.commit();
        EXPECT_NE(problems(s.model.snapshot()).find(what), std::string::npos) << what;
    };
    expectProblem([](Score& s, Transaction& tx) { tx.set(s.chordC, F::DOTS, int64_t(0)); }, "don't add up");
    expectProblem([](Score& s, Transaction& tx) { tx.set(s.tuplet, F::RATIO_NUM, int64_t(4)); }, "don't add up");
    expectProblem([](Score& s, Transaction& tx) { tx.set(s.chordTupletA, F::TIME, Fraction(3, 8)); }, "outside its tuplet");
    expectProblem([](Score& s, Transaction& tx) { removeTree(tx, s.mainAfterGrace); }, "no main chord after it");
    expectProblem([](Score& s, Transaction& tx) { tx.set(s.lyric, F::SYLLABIC, model::Text("sideways")); }, "invalid verse or syllable");
    expectProblem([](Score& s, Transaction& tx) { tx.set(s.change, F::ANCHOR, s.horn.player); }, "isn't a staff");
    expectProblem([](Score& s, Transaction& tx) { tx.set(s.slur, F::END_ANCHOR, Id(987654321)); }, "ends at a missing object");
    expectProblem([](Score& s, Transaction& tx) { tx.set(s.layout, F::PLAYERS, model::IdList({ s.horn.staff })); }, "isn't a player");
}
