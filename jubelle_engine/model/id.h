/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 *
 * Stable identities (decision D-E5, docs/09_model_mapping.md §4).
 *  - Every stored object has an Id. On import, MuseScore's <eid> values seed the ids
 *    where they are kept by MuseScore (see docs/13_mscz_storage_results.md).
 *  - Derived objects (presentation staves, condensed chords, system segments) get
 *    deterministic ids from their source and role, so a selection survives relayout.
 */
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace je {
class Id
{
public:
    constexpr Id() = default;
    constexpr explicit Id(uint64_t v)
        : m_value(v) {}

    constexpr uint64_t value() const { return m_value; }
    constexpr bool isValid() const { return m_value != 0; }

    constexpr bool operator==(const Id& o) const { return m_value == o.m_value; }
    constexpr bool operator!=(const Id& o) const { return m_value != o.m_value; }
    constexpr bool operator<(const Id& o) const { return m_value < o.m_value; }

    std::string toString() const;   // 16 hex digits

    //! A derived id: the same (source, role, index) always gives the same id; never 0
    static Id derived(Id source, std::string_view role, uint64_t index = 0);

    //! The id for a MuseScore <eid> text, so the same eid always maps to the same id
    static Id fromMuseScoreEid(std::string_view eid);

private:
    uint64_t m_value = 0;
};

//! Hands out new ids for one score. Ids from files are registered so new ones never collide.
class IdGenerator
{
public:
    explicit IdGenerator(uint64_t seed = 1);
    Id next();
    void reserve(Id used);   // an id read from a file

private:
    std::atomic<uint64_t> m_next;
};
}

template<>
struct std::hash<je::Id> {
    size_t operator()(const je::Id& id) const noexcept { return std::hash<uint64_t>()(id.value()); }
};
