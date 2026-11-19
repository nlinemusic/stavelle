/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 *
 * The value vocabulary shared by the model and by change sets (je::edit): every stored
 * property of every object is a Value in a numbered Field, so a change is always
 * (object, field, before, after) and undo needs no per-type code.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

#include "fraction.h"
#include "id.h"

namespace je::model {
//! An ordered list of ids (children of an object), immutable and shared. Compared by content.
class IdList
{
public:
    IdList() = default;
    explicit IdList(std::vector<Id> ids)
        : m_ids(ids.empty() ? nullptr : std::make_shared<const std::vector<Id> >(std::move(ids))) {}

    size_t size() const { return m_ids ? m_ids->size() : 0; }
    bool empty() const { return size() == 0; }
    Id operator[](size_t i) const { return (*m_ids)[i]; }
    const std::vector<Id>& ids() const { static const std::vector<Id> none; return m_ids ? *m_ids : none; }
    std::vector<Id>::const_iterator begin() const { return ids().begin(); }
    std::vector<Id>::const_iterator end() const { return ids().end(); }

    bool operator==(const IdList& o) const { return m_ids == o.m_ids || ids() == o.ids(); }
    bool operator!=(const IdList& o) const { return !(*this == o); }

private:
    std::shared_ptr<const std::vector<Id> > m_ids;
};

//! A field value. monostate = absent (the object or the property isn't there)
using Value = std::variant<std::monostate, bool, int64_t, double, std::string, je::Fraction, je::Id, IdList>;

inline bool isAbsent(const Value& v) { return std::holds_alternative<std::monostate>(v); }

//! Field numbers. The engine has its own vocabulary (model/fields.h); MuseScore's property ids are mapped to
//! it by name when files are read and written, so the engine doesn't depend on MuseScore's numbering.
using Field = int32_t;

//! Anything that stores (object, field) → value: the model, or a test store
class ChangeTarget
{
public:
    virtual ~ChangeTarget() = default;
    virtual Value get(je::Id object, Field field) const = 0;
    virtual void set(je::Id object, Field field, const Value& value) = 0;
    //! The fields an object has (so a removal can record them all, and undo restore them)
    virtual std::vector<Field> fieldsOf(je::Id /*object*/) const { return {}; }
};
}
