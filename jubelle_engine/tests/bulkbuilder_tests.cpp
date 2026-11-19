// SPDX-License-Identifier: GPL-3.0-only
#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "edit/scorebuilder.h"
#include "model/bulkbuilder.h"
#include "model/invariants.h"

using namespace je;
using je::model::Kind;
namespace F = je::model::fields;

namespace {
//! The same score built twice: through transactions (ScoreBuilder) and at once (BulkBuilder), with the same
//! ids in the same order. Events are given out of time order on purpose.
void buildBoth(model::Model& viaTx, model::Model& viaBulk, int players, int bars, unsigned seed)
{
    std::mt19937 rng(seed);
    std::vector<std::vector<std::pair<int, std::vector<int> > > > plan { size_t(players) };
    for (auto& p : plan) {
        std::vector<int> slots;
        for (int s = 0; s < bars * 4; ++s) {
            if (rng() % 3) {
                slots.push_back(s);
            }
        }
        std::shuffle(slots.begin(), slots.end(), rng);
        for (int s : slots) {
            std::vector<int> pitches { 50 + int(rng() % 30) };
            if (rng() % 4 == 0) {
                pitches.push_back(pitches[0] + 3);
            }
            p.push_back({ s, pitches });
        }
    }

    IdGenerator idsA(1);
    edit::ScoreBuilder b(viaTx, idsA);
    edit::Transaction tx(viaTx, "Build");
    b.newScore(tx, "Same");
    b.addMeasures(tx, bars);
    for (int p = 0; p < players; ++p) {
        const auto ids = b.addPlayer(tx, "Horn " + std::to_string(p + 1), "horn", "brass", p + 1, -7);
        for (const auto& [slot, pitches] : plan[size_t(p)]) {
            b.addChord(tx, ids.voice, Fraction(slot, 4), Fraction(1, 4), pitches);
        }
    }
    tx.commit();

    IdGenerator idsB(1);
    model::BulkBuilder bb(idsB);
    const Id score = bb.add(Kind::Score, Id(), { { F::NAME, std::string("Same") } });
    const Id flow = bb.add(Kind::Flow, score, { { F::NAME, std::string("Flow 1") } });
    for (int m = 0; m < bars; ++m) {
        bb.add(Kind::Measure, flow, { { F::TIME, Fraction(m, 1) }, { F::DURATION, Fraction(1, 1) },
                   { F::TIMESIG_NUM, int64_t(4) }, { F::TIMESIG_DEN, int64_t(4) } }, model::BulkBuilder::Order::ByTime);
    }
    for (int p = 0; p < players; ++p) {
        const Id player = bb.add(Kind::Player, score, { { F::NAME, "Horn " + std::to_string(p + 1) },
                                     { F::INSTRUMENT, std::string("horn") }, { F::FAMILY, std::string("brass") },
                                     { F::NUMBER, int64_t(p + 1) }, { F::TRANSPOSE_CHROMATIC, int64_t(-7) } });
        const Id staff = bb.add(Kind::Staff, player, {});
        const Id voice = bb.add(Kind::Voice, staff, { { F::FLOW, flow }, { F::NUMBER, int64_t(0) } });
        for (const auto& [slot, pitches] : plan[size_t(p)]) {
            const Id chord = bb.add(Kind::Chord, voice, { { F::TIME, Fraction(slot, 4) }, { F::DURATION, Fraction(1, 4) } },
                                    model::BulkBuilder::Order::ByTime);
            for (int pitch : pitches) {
                bb.add(Kind::Note, chord, { { F::PITCH, int64_t(pitch) } });
            }
        }
    }
    bb.finish(viaBulk);
}
}

TEST(BulkBuilder, GivesExactlyTheModelTheTransactionPathGives)
{
    for (unsigned seed : { 1u, 2u, 3u }) {
        model::Model a, b;
        buildBoth(a, b, 4, 12, seed);
        EXPECT_EQ(a.snapshot().dump(), b.snapshot().dump()) << "seed " << seed;
        EXPECT_TRUE(model::checkInvariants(b.snapshot()).empty());
    }
}

TEST(BulkBuilder, RejectsRepeatedIdsAndMissingParents)
{
    IdGenerator ids(1);
    model::BulkBuilder bb(ids);
    const Id score = bb.add(Kind::Score, Id(), {});
    EXPECT_THROW(bb.add(Kind::Flow, Id(999999), {}), std::logic_error);
    EXPECT_THROW(bb.add(Kind::Flow, score, {}, model::BulkBuilder::Order::Append, score), std::logic_error);
}

TEST(BulkBuilder, IdsFromAFileAreReserved)
{
    IdGenerator ids(1);
    model::BulkBuilder bb(ids);
    const Id fromFile(5000);
    bb.add(Kind::Score, Id(), {}, model::BulkBuilder::Order::Append, fromFile);
    const Id next = bb.add(Kind::Flow, Id(), {});
    EXPECT_NE(next, fromFile);
}
