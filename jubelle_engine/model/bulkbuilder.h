/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 *
 * BulkBuilder: builds a whole score at once, without transactions, for reading files (S2).
 * Children are collected per parent and ordered once at the end (by time where asked), so
 * building is linear rather than one transaction and one list copy per object. The result
 * is the same model the transaction path gives for the same objects in the same order
 * (tested), and it passes checkInvariants when the input is consistent.
 */
#pragma once

#include <unordered_map>
#include <utility>
#include <functional>
#include <vector>

#include "model.h"

namespace je::model {
class BulkBuilder
{
public:
    explicit BulkBuilder(IdGenerator& ids)
        : m_ids(ids) {}

    enum class Order {
        Append,   //!< children in the order they were added
        ByTime,   //!< children by TIME (stable: equal times keep their order)
    };

    //! Adds an object; an invalid parent means the score itself. `id` invalid = a new id.
    Id add(Kind kind, Id parent, std::vector<std::pair<Field, Value> > fields, Order order = Order::Append, Id id = Id());

    //! Sets a field of an object already added (a tie's target, known once both notes are read)
    void setField(Id id, Field field, Value value);

    //! Puts everything into the model (replacing what it held) and returns the number of objects. `adjust`, if
    //! given, sees each finished object (fields, kind, parent and children all set) and may return a changed copy
    size_t finish(Model& model, const std::function<Object(Object)>& adjust = nullptr);

private:
    struct Pending {
        Id id;
        Kind kind;
        Id parent;
        Order order;
        std::vector<std::pair<Field, Value> > fields;
    };
    IdGenerator& m_ids;
    std::vector<Pending> m_objects;
    std::unordered_map<Id, size_t> m_index;
    std::unordered_map<Id, std::vector<Id> > m_children;
    Id m_score;
};
}
