/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 *
 * Object: one stored thing in a score (a note, a measure, a player…). Immutable: a change
 * makes a new Object, so snapshots that hold the old one are never affected. Properties are
 * a small sorted list of (Field, Value); absent fields read as monostate.
 */
#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "fields.h"
#include "value.h"

namespace je::model {
class Object
{
public:
    explicit Object(Id id)
        : m_id(id) {}
    //! With these properties (sorted here; absent values dropped; a repeated field keeps its last value)
    Object(Id id, std::vector<std::pair<Field, Value> > properties);

    Id id() const { return m_id; }
    Kind kind() const { return m_kind; }

    //! The value, or an absent (monostate) value
    Value get(Field field) const;
    bool has(Field field) const;

    //! A copy with `field` set (an absent value removes the field)
    Object with(Field field, const Value& value) const;

    //! Every stored field in field order (KIND and PARENT included), as (Field, Value)
    template<typename Fn>
    void forEachField(Fn&& fn) const
    {
        if (m_kind != Kind::None) {
            fn(fields::KIND, Value(int64_t(m_kind)));
        }
        if (m_parent.isValid()) {
            fn(fields::PARENT, Value(m_parent));
        }
        for (const auto& p : m_props) {
            fn(p.first, p.second);
        }
    }
    size_t fieldCount() const { return m_props.size() + (m_kind != Kind::None) + m_parent.isValid(); }
    std::string text(Field field) const;

    // typed helpers (absent → the default given)
    int64_t intOr(Field field, int64_t fallback) const;
    Fraction fractionOr(Field field, const Fraction& fallback) const;
    Id idOr(Field field, Id fallback = Id()) const;
    const IdList& children() const;
    Id parent() const { return m_parent; }

private:
    Id m_id;
    Id m_parent;                                      // fields::PARENT, kept out of the list (every object has one)
    Kind m_kind = Kind::None;                         // fields::KIND, likewise
    std::vector<std::pair<Field, Value> > m_props;   // the other fields, sorted, exact size
};

using ObjectPtr = std::shared_ptr<const Object>;
}
