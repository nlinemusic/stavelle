// SPDX-License-Identifier: GPL-3.0-only
#include <gtest/gtest.h>

#include <map>
#include <random>
#include <vector>

#include "model/idmap.h"

using je::Id;
using je::model::IdMap;

namespace {
std::map<uint64_t, int> contents(const IdMap<int>& m)
{
    std::map<uint64_t, int> out;
    m.forEach([&out](Id k, const int& v) { out[k.value()] = v; });
    return out;
}
}

TEST(IdMap, EmptyFindSetErase)
{
    IdMap<int> m;
    EXPECT_TRUE(m.empty());
    EXPECT_EQ(m.find(Id(1)), nullptr);
    IdMap<int> a = m.set(Id(1), 10);
    EXPECT_EQ(a.size(), 1u);
    ASSERT_NE(a.find(Id(1)), nullptr);
    EXPECT_EQ(*a.find(Id(1)), 10);
    EXPECT_TRUE(m.empty());   // the old version is untouched
    IdMap<int> b = a.set(Id(1), 11);
    EXPECT_EQ(b.size(), 1u);
    EXPECT_EQ(*b.find(Id(1)), 11);
    EXPECT_EQ(*a.find(Id(1)), 10);
    IdMap<int> c = b.erase(Id(1));
    EXPECT_TRUE(c.empty());
    EXPECT_EQ(c.find(Id(1)), nullptr);
    EXPECT_TRUE(b.erase(Id(2)).sameVersion(b));   // erasing an absent key changes nothing
}

// Property test: random sets and erases against std::map, keeping old versions and checking them all at the end
TEST(IdMap, RandomOperationsMatchStdMapAndOldVersionsStayIntact)
{
    std::mt19937_64 rng(20261118);
    IdMap<int> m;
    std::map<uint64_t, int> ref;
    std::vector<std::pair<IdMap<int>, std::map<uint64_t, int> > > versions;
    // keys from a small range (many replacements and erasures) and from the full 64-bit range
    for (int i = 0; i < 30000; ++i) {
        const uint64_t key = (rng() % 3) ? 1 + rng() % 5000 : (rng() | 1);
        if (rng() % 4 == 0) {
            m = m.erase(Id(key));
            ref.erase(key);
        } else {
            const int v = int(rng() % 1000000);
            m = m.set(Id(key), v);
            ref[key] = v;
        }
        ASSERT_EQ(m.size(), ref.size());
        if (i % 1500 == 0) {
            versions.emplace_back(m, ref);
        }
    }
    EXPECT_EQ(contents(m), ref);
    for (const auto& [key, v] : ref) {
        ASSERT_NE(m.find(Id(key)), nullptr);
        EXPECT_EQ(*m.find(Id(key)), v);
    }
    for (const auto& [old, oldRef] : versions) {
        EXPECT_EQ(contents(old), oldRef);
        EXPECT_EQ(old.size(), oldRef.size());
    }
    // erase everything
    for (const auto& [key, v] : ref) {
        m = m.erase(Id(key));
    }
    EXPECT_TRUE(m.empty());
    EXPECT_TRUE(contents(m).empty());
}

TEST(IdMap, IterationOrderDependsOnlyOnKeys)
{
    std::vector<uint64_t> keys;
    for (uint64_t k = 1; k <= 2000; ++k) {
        keys.push_back(k * 7919);
    }
    IdMap<int> a, b;
    for (uint64_t k : keys) {
        a = a.set(Id(k), 1);
    }
    for (auto it = keys.rbegin(); it != keys.rend(); ++it) {
        b = b.set(Id(*it), 1);
    }
    std::vector<uint64_t> oa, ob;
    a.forEach([&oa](Id k, const int&) { oa.push_back(k.value()); });
    b.forEach([&ob](Id k, const int&) { ob.push_back(k.value()); });
    EXPECT_EQ(oa, ob);
}

// One change to a large map copies only the path to the changed entry (at most 13 nodes)
TEST(IdMap, OneChangeSharesAlmostEverything)
{
    IdMap<int> m;
    for (uint64_t k = 1; k <= 20000; ++k) {
        m = m.set(Id(k), int(k));
    }
    const IdMap<int> changed = m.set(Id(777), -1);
    EXPECT_LE(changed.nodesNotSharedWith(m), 13u);
    EXPECT_EQ(*m.find(Id(777)), 777);
    EXPECT_EQ(*changed.find(Id(777)), -1);
}
