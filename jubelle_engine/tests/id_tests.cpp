// SPDX-License-Identifier: GPL-3.0-only
#include <gtest/gtest.h>

#include <set>
#include <thread>
#include <vector>

#include "model/id.h"

using je::Id;
using je::IdGenerator;

TEST(JE_Id, DerivedIdsAreStableAndDistinct)
{
    const Id src(42);
    EXPECT_EQ(Id::derived(src, "condensed-staff", 1), Id::derived(src, "condensed-staff", 1));
    EXPECT_NE(Id::derived(src, "condensed-staff", 1), Id::derived(src, "condensed-staff", 2));
    EXPECT_NE(Id::derived(src, "condensed-staff", 1), Id::derived(src, "system-segment", 1));
    EXPECT_NE(Id::derived(src, "x"), Id::derived(Id(43), "x"));
    std::set<uint64_t> seen;
    for (uint64_t i = 0; i < 100000; ++i) {
        EXPECT_TRUE(seen.insert(Id::derived(src, "segment", i).value()).second);
    }
}

TEST(JE_Id, MuseScoreEidsMapToTheSameIdEveryTime)
{
    EXPECT_EQ(Id::fromMuseScoreEid("B_B"), Id::fromMuseScoreEid("B_B"));
    EXPECT_NE(Id::fromMuseScoreEid("B_B"), Id::fromMuseScoreEid("C_C"));
    EXPECT_TRUE(Id::fromMuseScoreEid("").isValid());
}

TEST(JE_Id, GeneratedIdsNeverCollideWithReservedOrDerived)
{
    IdGenerator gen;
    gen.reserve(Id(500));
    const Id a = gen.next();
    EXPECT_GT(a.value(), 500u);
    EXPECT_NE(a, Id::derived(Id(1), "x"));
    EXPECT_EQ(a.toString().size(), 16u);
}

TEST(JE_Id, GeneratorIsThreadSafe)
{
    IdGenerator gen;
    std::vector<std::vector<uint64_t> > got(8);
    std::vector<std::thread> t;
    for (int k = 0; k < 8; ++k) {
        t.emplace_back([&, k] {
            for (int i = 0; i < 10000; ++i) {
                got[k].push_back(gen.next().value());
            }
        });
    }
    for (auto& th : t) {
        th.join();
    }
    std::set<uint64_t> all;
    for (auto& v : got) {
        all.insert(v.begin(), v.end());
    }
    EXPECT_EQ(all.size(), 80000u);
}
