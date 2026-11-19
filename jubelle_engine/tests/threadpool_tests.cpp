// SPDX-License-Identifier: GPL-3.0-only
#include <gtest/gtest.h>

#include <atomic>
#include <numeric>
#include <stdexcept>
#include <vector>

#include "exec/threadpool.h"

using je::exec::ThreadPool;

TEST(JE_ThreadPool, EveryIndexRunsExactlyOnce)
{
    ThreadPool pool(4);
    std::vector<std::atomic<int> > hits(10000);
    pool.parallelFor(hits.size(), [&](size_t i) { hits[i].fetch_add(1); });
    for (auto& h : hits) {
        EXPECT_EQ(h.load(), 1);
    }
}

TEST(JE_ThreadPool, ResultsDontDependOnThreadOrder)
{
    // deterministic: results go into slots by index and are combined in index order
    auto run = [](size_t threads) {
        ThreadPool pool(threads);
        std::vector<double> slot(5000);
        pool.parallelFor(slot.size(), [&](size_t i) { slot[i] = 1.0 / double(i + 1); });
        return std::accumulate(slot.begin(), slot.end(), 0.0);
    };
    const double one = run(1);
    for (int k = 0; k < 5; ++k) {
        EXPECT_EQ(run(8), one);   // bit-identical
    }
}

TEST(JE_ThreadPool, ManySmallJobsAndNesting)
{
    ThreadPool pool(3);
    std::atomic<int> total { 0 };
    for (int j = 0; j < 500; ++j) {
        pool.parallelFor(3, [&](size_t) { total.fetch_add(1); });
    }
    EXPECT_EQ(total.load(), 1500);
    pool.parallelFor(0, [&](size_t) { FAIL(); });   // nothing to do
}

TEST(JE_ThreadPool, ExceptionsReachTheCaller)
{
    ThreadPool pool(4);
    std::atomic<int> ran { 0 };
    EXPECT_THROW(pool.parallelFor(100, [&](size_t i) {
        ran.fetch_add(1);
        if (i == 37) {
            throw std::runtime_error("x");
        }
    }), std::runtime_error);
    EXPECT_EQ(ran.load(), 100);   // the other items still ran
}
