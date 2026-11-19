/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 *
 * A copy-on-write vector in chunks, the building block of model snapshots
 * (docs/03_engine_architecture.md §3): copying is O(number of chunks) and shares every
 * chunk; changing an element copies only its chunk. A copy taken for another thread is
 * never affected by later changes to the original, and vice versa.
 *
 * Thread safety: a CowVector object is not shared between threads while being
 * changed; copies of it may be read on any thread at the same time.
 */
#pragma once

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <vector>

namespace je {
template<typename T, std::size_t ChunkSize = 64>
class CowVector
{
    static_assert(ChunkSize > 0, "chunk size");
    using Chunk = std::vector<T>;

public:
    std::size_t size() const { return m_size; }
    bool empty() const { return m_size == 0; }

    const T& operator[](std::size_t i) const { return (*m_chunks[i / ChunkSize])[i % ChunkSize]; }
    const T& at(std::size_t i) const
    {
        if (i >= m_size) {
            throw std::out_of_range("je::CowVector::at");
        }
        return (*this)[i];
    }

    void push_back(T value)
    {
        if (m_size % ChunkSize == 0) {
            auto chunk = std::make_shared<Chunk>();
            chunk->reserve(ChunkSize);
            m_chunks.push_back(std::move(chunk));
        }
        mutableChunk(m_chunks.size() - 1).push_back(std::move(value));
        ++m_size;
    }

    void set(std::size_t i, T value)
    {
        if (i >= m_size) {
            throw std::out_of_range("je::CowVector::set");
        }
        mutableChunk(i / ChunkSize)[i % ChunkSize] = std::move(value);
    }

    void pop_back()
    {
        if (m_size == 0) {
            throw std::out_of_range("je::CowVector::pop_back");
        }
        mutableChunk(m_chunks.size() - 1).pop_back();
        --m_size;
        if (m_size % ChunkSize == 0) {
            m_chunks.pop_back();
        }
    }

    //! how many chunks this vector shares with another (for tests and memory accounting)
    std::size_t sharedChunksWith(const CowVector& o) const
    {
        std::size_t n = 0;
        for (std::size_t c = 0; c < m_chunks.size() && c < o.m_chunks.size(); ++c) {
            n += m_chunks[c] == o.m_chunks[c] ? 1 : 0;
        }
        return n;
    }

private:
    Chunk& mutableChunk(std::size_t c)
    {
        if (m_chunks[c].use_count() > 1) {
            m_chunks[c] = std::make_shared<Chunk>(*m_chunks[c]);   // copy on write
        }
        return *m_chunks[c];
    }

    std::vector<std::shared_ptr<Chunk> > m_chunks;
    std::size_t m_size = 0;
};
}
