/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 */
#include "structure.h"

#include <algorithm>
#include <stdexcept>

using je::model::IdList;
namespace F = je::model::fields;

namespace je::edit {
namespace {
IdList childrenOf(const Transaction& tx, Id parent)
{
    const Value v = tx.target().get(parent, F::CHILDREN);
    return std::holds_alternative<IdList>(v) ? std::get<IdList>(v) : IdList();
}

Fraction timeOf(const Transaction& tx, Id id, const Fraction& fallback)
{
    const Value v = tx.target().get(id, F::TIME);
    return std::holds_alternative<Fraction>(v) ? std::get<Fraction>(v) : fallback;
}

Fraction timeIn(const NewObject& o)
{
    for (const auto& [field, value] : o.fields) {
        if (field == F::TIME && std::holds_alternative<Fraction>(value)) {
            return std::get<Fraction>(value);
        }
    }
    return Fraction();
}
}

std::vector<std::pair<Field, Value> > currentFields(const Transaction& tx, Id id)
{
    std::vector<std::pair<Field, Value> > out;
    for (Field f : tx.target().fieldsOf(id)) {
        out.emplace_back(f, tx.target().get(id, f));
    }
    return out;
}

void createWithId(Transaction& tx, Id id, Id parent, const NewObject& object, Placement where)
{
    if (std::holds_alternative<bool>(tx.target().get(id, F::EXISTS))) {
        throw std::logic_error("je::edit::create: the id is already used");
    }
    if (parent.isValid() && !std::holds_alternative<bool>(tx.target().get(parent, F::EXISTS))) {
        throw std::logic_error("je::edit::create: the parent doesn't exist");
    }
    tx.set(id, F::EXISTS, true);
    tx.set(id, F::KIND, int64_t(object.kind));
    for (const auto& [field, value] : object.fields) {
        if (field == F::EXISTS || field == F::KIND || field == F::PARENT || field == F::CHILDREN) {
            throw std::logic_error("je::edit::create: structural fields are set by the engine");
        }
        tx.set(id, field, value);
    }
    if (!parent.isValid()) {
        return;
    }
    tx.set(id, F::PARENT, parent);

    std::vector<Id> kids = childrenOf(tx, parent).ids();
    auto pos = kids.end();
    if (where == Placement::ByTime) {
        const Fraction t = timeIn(object);
        pos = std::upper_bound(kids.begin(), kids.end(), t, [&tx](const Fraction& time, Id other) {
            return time < timeOf(tx, other, Fraction());
        });
    }
    kids.insert(pos, id);
    tx.set(parent, F::CHILDREN, IdList(std::move(kids)));
}

void moveTo(Transaction& tx, Id id, Id newParent, Placement where)
{
    const Value parentValue = tx.target().get(id, F::PARENT);
    if (std::holds_alternative<Id>(parentValue)) {
        const Id old = std::get<Id>(parentValue);
        std::vector<Id> kids = childrenOf(tx, old).ids();
        kids.erase(std::remove(kids.begin(), kids.end(), id), kids.end());
        tx.set(old, F::CHILDREN, IdList(std::move(kids)));
    }
    tx.set(id, F::PARENT, newParent);
    std::vector<Id> kids = childrenOf(tx, newParent).ids();
    auto pos = kids.end();
    if (where == Placement::ByTime) {
        const Fraction t = timeOf(tx, id, Fraction());
        pos = std::upper_bound(kids.begin(), kids.end(), t, [&tx](const Fraction& time, Id other) {
            return time < timeOf(tx, other, Fraction());
        });
    }
    kids.insert(pos, id);
    tx.set(newParent, F::CHILDREN, IdList(std::move(kids)));
}

Id create(Transaction& tx, IdGenerator& ids, Id parent, const NewObject& object, Placement where)
{
    const Id id = ids.next();
    createWithId(tx, id, parent, object, where);
    return id;
}

namespace {
void removeSubtree(Transaction& tx, Id id)
{
    // children first (from the last, so each list shrinks at its end), then this object's fields
    const IdList kids = childrenOf(tx, id);
    for (size_t i = kids.size(); i-- > 0;) {
        removeSubtree(tx, kids[i]);
    }
    for (Field f : tx.target().fieldsOf(id)) {
        tx.set(id, f, Value());
    }
    tx.set(id, F::EXISTS, Value());
}
}

void removeTree(Transaction& tx, Id id)
{
    if (!std::holds_alternative<bool>(tx.target().get(id, F::EXISTS))) {
        return;
    }
    const Value parentValue = tx.target().get(id, F::PARENT);
    if (std::holds_alternative<Id>(parentValue)) {
        const Id parent = std::get<Id>(parentValue);
        std::vector<Id> kids = childrenOf(tx, parent).ids();
        kids.erase(std::remove(kids.begin(), kids.end(), id), kids.end());
        tx.set(parent, F::CHILDREN, IdList(std::move(kids)));
    }
    removeSubtree(tx, id);
}
}
