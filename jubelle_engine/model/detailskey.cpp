/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 */
#include "detailskey.h"

#include <set>

#include "fields.h"

namespace F = je::model::fields;

namespace je::model {
namespace {
bool excluded(Field f)
{
    // (an element's place in time isn't part of it: its details stay when measures are inserted before it)
    static const std::set<Field> skip { F::TIME, F::KIND, F::CHILDREN, F::PARENT, F::ORIGIN_EID, F::DETAILS, F::DETAILS_KEY,
                                                F::DETAILS_2, F::DETAILS_AFTER, F::TIE_EID, F::LINK_EID, F::LINK_MARK, F::TIE_LINK, F::SEQ, F::AT_END, F::TAG, F::HOME_STAFF, F::TIE_TO,
                                                F::LINKED_TO, F::LINKED_STAFF, F::TIE_MARK, F::LINE_DETAILS, F::TIE_GENERATED };
    return skip.count(f) > 0;
}

void feedBytes(uint64_t& h, const void* data, size_t n)
{
    const unsigned char* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < n; ++i) {   // FNV-1a
        h ^= p[i];
        h *= 1099511628211ULL;
    }
}

template<typename T>
void feedValue(uint64_t& h, const T& x)
{
    feedBytes(h, &x, sizeof(x));
}

//! One field's contribution: its number, the kind of value and the value itself (hashed directly, without making
//! text of it: this runs for every object when a file is read and written)
uint64_t fieldHash(Field f, const Value& v)
{
    uint64_t h = 1469598103934665603ULL;
    feedValue(h, f);
    const uint8_t kind = uint8_t(v.index());
    feedValue(h, kind);
    std::visit([&h](const auto& x) {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
        } else if constexpr (std::is_same_v<T, Text>) {
            feedBytes(h, x.str().data(), x.str().size());
        } else if constexpr (std::is_same_v<T, Fraction>) {
            feedValue(h, x.numerator());
            feedValue(h, x.denominator());
        } else if constexpr (std::is_same_v<T, Id>) {
            feedValue(h, x.value());
        } else if constexpr (std::is_same_v<T, IdList>) {
            feedValue(h, x.size());
        } else {
            feedValue(h, x);
        }
    }, v);
    // mixed, so that summing the fields' hashes (in any order) still tells sets of fields apart
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    return h;
}
}

int64_t detailsKey(const std::vector<std::pair<Field, Value> >& fields)
{
    // a field given twice keeps its last value (as the object does)
    uint64_t sum = 0;
    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& [f, v] = fields[i];
        if (excluded(f)) {
            continue;
        }
        bool later = false;
        for (size_t j = i + 1; j < fields.size() && !later; ++j) {
            later = fields[j].first == f;
        }
        if (!later && !isAbsent(v)) {
            sum += fieldHash(f, v);
        }
    }
    return int64_t(sum & 0x7FFFFFFFFFFFFFFFULL);
}

int64_t detailsKey(const Object& object)
{
    uint64_t sum = 0;
    object.forEachField([&sum](Field f, const Value& v) {
        if (!excluded(f)) {
            sum += fieldHash(f, v);
        }
    });
    return int64_t(sum & 0x7FFFFFFFFFFFFFFFULL);
}

}
