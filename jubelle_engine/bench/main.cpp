// SPDX-License-Identifier: GPL-3.0-only
// Jubelle engine benchmarks (Phase 3 skeleton). Writes JSON lines: {"name":..,"ms":..}.
// The score benchmarks (read, layout, edit, undo, save against MuseScore's engine)
// are added with the converter (Phase 3.3), using the same scores as
// "Jubelle - Engine Baselines (15 Nov 2026)".
#include <chrono>
#include <cstdio>
#include <vector>

#include "edit/changeset.h"
#include "edit/history.h"
#include "exec/threadpool.h"
#include "model/cowvector.h"

namespace {
template<typename F>
double timeMs(F&& f, int reps = 5)
{
    double best = 1e30;
    for (int r = 0; r < reps; ++r) {
        const auto t0 = std::chrono::steady_clock::now();
        f();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        best = ms < best ? ms : best;
    }
    return best;
}
}

int main()
{
    je::exec::ThreadPool pool;
    std::vector<double> out(100000);
    std::printf("{\"name\":\"parallelFor 100k items (%zu threads)\",\"ms\":%.3f}\n", pool.size(),
                timeMs([&] { pool.parallelFor(out.size(), [&](size_t i) { out[i] = double(i) * 0.5; }); }));

    je::CowVector<int> v;
    for (int i = 0; i < 1000000; ++i) {
        v.push_back(i);
    }
    std::printf("{\"name\":\"snapshot of 1M-element CowVector\",\"ms\":%.3f}\n", timeMs([&] { auto s = v; (void)s; }));
    return 0;
}
