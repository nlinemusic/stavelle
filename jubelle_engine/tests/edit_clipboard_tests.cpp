// SPDX-License-Identifier: GPL-3.0-only
// S3: range copy, paste, delete (edit/clipboard.h) with MuseScore's behaviour, and a property test of random
// copy-and-paste sequences.
#include <gtest/gtest.h>

#include <random>
#include <string>

#include "edit/clipboard.h"
#include "edit/history.h"
#include "edit/scorebuilder.h"
#include "io/mscxreader.h"
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
    Operations ops { model, ids };
    Clipboard clip { model, ids };
    ScoreBuilder::PlayerIds flute, horn;

    Doc()
    {
        Transaction tx(model, "New score");
        b.newScore(tx, "Clipboard");
        b.addMeasures(tx, 4);
        flute = b.addPlayer(tx, "Flute", "flute", "woodwinds", 1);
        horn = b.addPlayer(tx, "Horn in F", "horn", "brass", 1, -7);
        for (int i = 0; i < 4; ++i) {
            for (Id v : { flute.voice, horn.voice }) {
                create(tx, ids, v, { Kind::Rest, { { F::TIME, Fraction(i, 1) }, { F::DURATION, Fraction(1, 1) }, { F::MEASURE_REST, true } } },
                       Placement::ByTime);
            }
        }
        // bar 1 of the flute: a quarter with an accent and a lyric, a triplet, a half tied into bar 2, a slur
        // over the first two quarters' worth, a dynamic
        const Id c = ops.enterChord(tx, flute.voice, Fraction(0, 1), { Fraction(1, 4), 0 }, { 72 });
        create(tx, ids, c, { Kind::Attachment, { { F::SUBKIND, Text("accent") } } });
        create(tx, ids, c, { Kind::Attachment, { { F::SUBKIND, Text("lyric") }, { F::TEXT, Text("La") } } });
        ops.makeTuplet(tx, flute.voice, Fraction(1, 4), 3, 2, Fraction(1, 8));
        for (int i = 0; i < 3; ++i) {
            ops.enterChord(tx, flute.voice, Fraction(1, 4) + Fraction(i, 12), { Fraction(1, 8), 0 }, { 74 + i });
        }
        const Id h = ops.enterChord(tx, flute.voice, Fraction(1, 2), { Fraction(1, 2), 0 }, { 79 });
        const Id h2 = ops.enterChord(tx, flute.voice, Fraction(1, 1), { Fraction(1, 4), 0 }, { 79 });
        tx.set(b.notesOf(h).front(), F::TIE_TO, b.notesOf(h2).front());
        create(tx, ids, model.score(), { Kind::Spanner, { { F::SUBKIND, Text("slur") }, { F::ANCHOR, flute.staff }, { F::TIME, Fraction(0, 1) },
                                                          { F::DURATION, Fraction(1, 2) } } });
        create(tx, ids, b.flow(), { Kind::FlowItem, { { F::SUBKIND, Text("dynamic") }, { F::ANCHOR, flute.staff }, { F::TIME, Fraction(0, 1) },
                                                      { F::TEXT, Text("mf") } } },
               Placement::ByTime);
        history.push(tx.commit());
    }

    template<typename Fn>
    void edit(Fn&& fn)
    {
        Transaction tx(model, "Edit");
        fn(tx);
        history.push(tx.commit());
    }

    //! one voice from t0 to t1, times relative to t0
    std::string music(Id voice, const Fraction& t0, const Fraction& t1) const
    {
        std::string out;
        for (Id e : ops.eventsOf(voice)) {
            const model::Object* o = model.find(e);
            const Fraction t = o->fractionOr(F::TIME, Fraction());
            if (t < t0 || !(t < t1)) {
                continue;
            }
            out += (t - t0).toString() + " " + o->fractionOr(F::DURATION, Fraction()).toString();
            for (Id n : o->children()) {
                const model::Object* x = model.find(n);
                if (x->kind() == Kind::Note) {
                    out += " " + std::to_string(x->intOr(F::PITCH, 0)) + (x->has(F::TIE_TO) ? "~" : "");
                } else if (x->kind() == Kind::Attachment) {
                    out += " [" + x->text(F::SUBKIND) + "]";
                }
            }
            out += "\n";
        }
        return out;
    }

    int count(Kind kind, const std::string& subkind) const
    {
        int n = 0;
        model.snapshot().forEachObject([&](const model::Object& o) { n += o.kind() == kind && o.text(F::SUBKIND) == subkind; });
        return n;
    }

    std::string problems() const
    {
        std::string out;
        for (const auto& p : model::checkInvariants(model.snapshot())) {
            out += p + "\n";
        }
        return out;
    }

    std::string voice1Gaps() const
    {
        std::string out;
        for (Id v : { flute.voice, horn.voice }) {
            Fraction at;
            for (Id e : ops.eventsOf(v)) {
                const model::Object* o = model.find(e);
                if (o->has(F::GRACE)) {
                    continue;
                }
                if (o->fractionOr(F::TIME, Fraction()) != at) {
                    out += "gap at " + at.toString() + "\n";
                }
                at = o->fractionOr(F::TIME, Fraction()) + o->fractionOr(F::DURATION, Fraction());
            }
            if (at != Fraction(4, 1)) {
                out += "ends at " + at.toString() + "\n";
            }
        }
        return out;
    }
};
}

TEST(Clipboard, CopyAndPasteABar)
{
    Doc d;
    const std::string bar1 = d.music(d.flute.voice, Fraction(0, 1), Fraction(1, 1));
    const Clip c = d.clip.copy({ { d.flute.staff }, Fraction(0, 1), Fraction(1, 1) });
    d.edit([&](Transaction& tx) { d.clip.paste(tx, c, d.flute.staff, Fraction(2, 1)); });
    // the same music in bar 3, except the tie: its end (bar 2) wasn't copied
    std::string expected = bar1;
    expected.replace(expected.find("79~"), 3, "79");
    EXPECT_EQ(d.music(d.flute.voice, Fraction(2, 1), Fraction(3, 1)), expected);
    EXPECT_EQ(d.count(Kind::Spanner, "slur"), 2);
    EXPECT_EQ(d.count(Kind::FlowItem, "dynamic"), 2);
    int tuplets = 0;
    d.model.snapshot().forEachObject([&](const model::Object& o) { tuplets += o.kind() == Kind::Tuplet; });
    EXPECT_EQ(tuplets, 2);
    EXPECT_EQ(d.problems(), "");
    EXPECT_EQ(d.voice1Gaps(), "");
    // the copy with the tie's end too: tied in the paste
    const Clip c2 = d.clip.copy({ { d.flute.staff }, Fraction(1, 2), Fraction(5, 4) });
    d.edit([&](Transaction& tx) { d.clip.paste(tx, c2, d.flute.staff, Fraction(3, 1)); });
    EXPECT_EQ(d.music(d.flute.voice, Fraction(3, 1), Fraction(4, 1)), "0/1 1/2 79~\n1/2 1/4 79\n3/4 1/4\n");
    EXPECT_EQ(d.problems(), "");
    EXPECT_EQ(d.voice1Gaps(), "");
}

TEST(Clipboard, PasteAcrossABarlineAndToAnotherInstrument)
{
    Doc d;
    const Clip c = d.clip.copy({ { d.flute.staff }, Fraction(1, 2), Fraction(1, 1) });   // the half note
    d.edit([&](Transaction& tx) { d.clip.paste(tx, c, d.horn.staff, Fraction(7, 4)); });
    // on the horn from beat 4 of bar 2: a quarter tied into bar 3; written a fifth higher (in F)
    EXPECT_EQ(d.music(d.horn.voice, Fraction(7, 4), Fraction(9, 4)), "0/1 1/4 79~\n1/4 1/4 79\n");
    for (Id e : d.ops.eventsOf(d.horn.voice)) {
        for (Id n : d.model.find(e)->children()) {
            if (d.model.find(n)->kind() == Kind::Note) {
                EXPECT_EQ(d.model.find(n)->intOr(F::TPC2, 0), d.model.find(n)->intOr(F::TPC, 0) + 1);   // G → D
            }
        }
    }
    // a tuplet that would cross a barline there is left out, its place rests
    const Clip t = d.clip.copy({ { d.flute.staff }, Fraction(1, 4), Fraction(1, 2) });
    d.edit([&](Transaction& tx) { d.clip.paste(tx, t, d.flute.staff, Fraction(23, 8)); });
    EXPECT_EQ(d.clip.tupletsLeftOut(), 1);
    EXPECT_EQ(d.problems(), "");
    EXPECT_EQ(d.voice1Gaps(), "");
}

TEST(Clipboard, DeletingARangeLeavesRests)
{
    Doc d;
    d.edit([&](Transaction& tx) { d.clip.erase(tx, { { d.flute.staff }, Fraction(0, 1), Fraction(1, 2) }); });
    EXPECT_EQ(d.music(d.flute.voice, Fraction(0, 1), Fraction(1, 2)), "0/1 1/2\n");   // one half rest: MuseScore's rest values
    EXPECT_EQ(d.count(Kind::FlowItem, "dynamic"), 0);
    EXPECT_EQ(d.problems(), "");
    EXPECT_EQ(d.voice1Gaps(), "");
}

TEST(Clipboard, RandomCopyAndPasteStaysConsistentAndUndoesExactly)
{
    for (unsigned seed = 1; seed <= 10; ++seed) {
        Doc d;
        const std::string start = d.model.snapshot().dump();
        const size_t steps = d.history.index();
        std::mt19937 rng(seed);
        auto pick = [&rng](int n) { return int(rng() % unsigned(n)); };
        for (int step = 0; step < 40; ++step) {
            const Fraction a(pick(16), 4), len(1 + pick(8), 4);
            const Fraction b = std::min(a + len, Fraction(4, 1));
            if (!(a < b)) {
                continue;
            }
            const std::vector<Id> from = pick(2) ? std::vector<Id> { d.flute.staff } : std::vector<Id> { d.flute.staff, d.horn.staff };
            const Clip c = d.clip.copy({ from, a, b });
            const Id to = pick(2) ? d.flute.staff : d.horn.staff;
            const Fraction at(pick(32), 8);
            d.edit([&](Transaction& tx) {
                if (pick(5) == 0) {
                    d.clip.erase(tx, { from, a, b });
                } else {
                    d.clip.paste(tx, c, to, at);
                }
            });
            ASSERT_EQ(d.problems(), "") << "seed " << seed << " step " << step;
            ASSERT_EQ(d.voice1Gaps(), "") << "seed " << seed << " step " << step;
        }
        while (d.history.index() > steps) {
            d.history.undo(d.model);
        }
        EXPECT_EQ(d.model.snapshot().dump(), start) << "seed " << seed;
    }
}
