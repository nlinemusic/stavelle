/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 */
#include "id.h"

#include <cstdio>

namespace je {
namespace {
// 64-bit FNV-1a, then a final mix (splitmix64) so nearby inputs spread well
uint64_t fnv1a(std::string_view s, uint64_t h = 0xcbf29ce484222325ull)
{
    for (unsigned char c : s) {
        h ^= c;
        h *= 0x100000001b3ull;
    }
    return h;
}

uint64_t mix(uint64_t z)
{
    z += 0x9e3779b97f4a7c15ull;
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

// Derived and imported ids live in the upper half of the space; generated ids count
// up from 1 in the lower half, so the two never collide
constexpr uint64_t HIGH_BIT = 1ull << 63;
}

std::string Id::toString() const
{
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(m_value));
    return buf;
}

Id Id::derived(Id source, std::string_view role, uint64_t index)
{
    uint64_t h = mix(source.value());
    h = fnv1a(role, h);
    h = mix(h ^ mix(index));
    return Id(h | HIGH_BIT);
}

Id Id::fromMuseScoreEid(std::string_view eid)
{
    return Id(mix(fnv1a(eid)) | HIGH_BIT);
}

IdGenerator::IdGenerator(uint64_t seed)
    : m_next(seed == 0 ? 1 : seed)
{
}

Id IdGenerator::next()
{
    return Id(m_next.fetch_add(1));
}

void IdGenerator::reserve(Id used)
{
    if (used.value() & HIGH_BIT) {
        return;   // derived/imported space never collides with generated ids
    }
    uint64_t cur = m_next.load();
    while (used.value() >= cur && !m_next.compare_exchange_weak(cur, used.value() + 1)) {
    }
}
}
