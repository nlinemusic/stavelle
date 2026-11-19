// SPDX-License-Identifier: GPL-3.0-only
#include <gtest/gtest.h>

#include <thread>
#include <vector>

#include "model/cowvector.h"

using je::CowVector;

TEST(JE_CowVector, CopiesShareUntilChanged)
{
    CowVector<int, 4> a;
    for (int i = 0; i < 20; ++i) {
        a.push_back(i);
    }
    CowVector<int, 4> snapshot = a;
    EXPECT_EQ(a.sharedChunksWith(snapshot), 5u);
    a.set(9, 99);   // copies one chunk only
    EXPECT_EQ(a.sharedChunksWith(snapshot), 4u);
    EXPECT_EQ(a[9], 99);
    EXPECT_EQ(snapshot[9], 9);   // the snapshot never sees the change
}

TEST(JE_CowVector, PushPopAcrossChunks)
{
    CowVector<int, 3> v;
    for (int i = 0; i < 10; ++i) {
        v.push_back(i);
    }
    const auto copy = v;
    v.pop_back();
    v.pop_back();
    EXPECT_EQ(v.size(), 8u);
    EXPECT_EQ(copy.size(), 10u);
    EXPECT_EQ(copy.at(9), 9);
    EXPECT_THROW(v.at(8), std::out_of_range);
}

TEST(JE_CowVector, SnapshotsCanBeReadOnOtherThreadsWhileTheOriginalChanges)
{
    CowVector<int> v;
    for (int i = 0; i < 10000; ++i) {
        v.push_back(i);
    }
    std::vector<CowVector<int> > snaps(4, v);
    std::vector<long long> sums(4, 0);
    std::vector<std::thread> readers;
    for (int k = 0; k < 4; ++k) {
        readers.emplace_back([&, k] {
            for (int rep = 0; rep < 20; ++rep) {
                long long s = 0;
                for (size_t i = 0; i < snaps[k].size(); ++i) {
                    s += snaps[k][i];
                }
                sums[k] = s;
            }
        });
    }
    for (int i = 0; i < 10000; i += 7) {
        v.set(i, -1);   // the writer changes its own copy meanwhile
    }
    for (auto& r : readers) {
        r.join();
    }
    for (long long s : sums) {
        EXPECT_EQ(s, 10000LL * 9999 / 2);
    }
}
