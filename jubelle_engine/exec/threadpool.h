/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::exec). Not part of MuseScore.
 *
 * A fixed pool of worker threads for the engine's parallel stages (shaping per
 * measure-staff, vertical layout per system, playback events per measure).
 * Results never depend on thread order: parallelFor writes into slots indexed
 * by the item, and the caller combines them in index order.
 */
#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace je::exec {
class ThreadPool
{
public:
    //! threads = 0: one per hardware thread, at least 1
    explicit ThreadPool(std::size_t threads = 0);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    std::size_t size() const { return m_workers.size(); }

    //! Runs body(i) for every i in [0, count) and returns when all are done.
    //! The calling thread helps. Exceptions from body are rethrown (the first one).
    void parallelFor(std::size_t count, const std::function<void(std::size_t)>& body);

private:
    void workerLoop();

    std::vector<std::thread> m_workers;
    std::queue<std::function<void()> > m_tasks;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_stopping = false;
};
}
