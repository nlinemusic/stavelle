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
    Kind kind() const;

    //! The value, or an absent (monostate) value
    const Value& get(Field field) const;
    bool has(Field field) const;

    //! A copy with `field` set (an absent value removes the field)
    Object with(Field field, const Value& value) const;

    const std::vector<std::pair<Field, Value> >& properties() const { return m_props; }

    // typed helpers (absent → the default given)
    int64_t intOr(Field field, int64_t fallback) const;
    Fraction fractionOr(Field field, const Fraction& fallback) const;
    Id idOr(Field field, Id fallback = Id()) const;
    const IdList& children() const;
    Id parent() const { return idOr(fields::PARENT); }

private:
    Id m_id;
    std::vector<std::pair<Field, Value> > m_props;   // sorted by field
};

using ObjectPtr = std::shared_ptr<const Object>;
}
