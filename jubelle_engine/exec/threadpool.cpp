/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::exec). Not part of MuseScore.
 */
#include "threadpool.h"

#include <algorithm>
#include <atomic>
#include <exception>
#include <memory>

namespace je::exec {
ThreadPool::ThreadPool(std::size_t threads)
{
    if (threads == 0) {
        threads = std::max<std::size_t>(1, std::thread::hardware_concurrency());
    }
    m_workers.reserve(threads);
    for (std::size_t i = 0; i < threads; ++i) {
        m_workers.emplace_back([this] { workerLoop(); });
    }
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stopping = true;
    }
    m_wake.notify_all();
    for (std::thread& t : m_workers) {
        t.join();
    }
}

void ThreadPool::workerLoop()
{
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_wake.wait(lock, [this] { return m_stopping || !m_tasks.empty(); });
            if (m_stopping && m_tasks.empty()) {
                return;
            }
            task = std::move(m_tasks.front());
            m_tasks.pop();
        }
        task();
    }
}

void ThreadPool::parallelFor(std::size_t count, const std::function<void(std::size_t)>& body)
{
    if (count == 0) {
        return;
    }
    // Shared by the caller and the helpers. A helper may start after all the work is
    // done (it then finds no index left and returns at once); the shared pointer keeps
    // this state alive for it. `body` is only called for indices below `count`, which
    // all finish before this function returns, so no helper uses `body` afterwards.
    struct Job {
        std::atomic<std::size_t> next { 0 };
        std::atomic<std::size_t> done { 0 };
        std::mutex mutex;
        std::condition_variable finished;
        std::exception_ptr error;
    };
    auto job = std::make_shared<Job>();
    const std::function<void(std::size_t)>* work = &body;

    auto drain = [job, work, count]() {
        for (std::size_t i = job->next.fetch_add(1); i < count; i = job->next.fetch_add(1)) {
            try {
                (*work)(i);
            } catch (...) {
                std::lock_guard<std::mutex> lock(job->mutex);
                if (!job->error) {
                    job->error = std::current_exception();
                }
            }
            if (job->done.fetch_add(1) + 1 == count) {
                std::lock_guard<std::mutex> lock(job->mutex);
                job->finished.notify_all();
            }
        }
    };

    const std::size_t helpers = std::min(count - 1, m_workers.size());
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (std::size_t h = 0; h < helpers; ++h) {
            m_tasks.push(drain);
        }
    }
    m_wake.notify_all();
    drain();   // the calling thread works too

    std::unique_lock<std::mutex> lock(job->mutex);
    job->finished.wait(lock, [&] { return job->done.load() == count; });
    if (job->error) {
        std::rethrow_exception(job->error);
    }
}
}
