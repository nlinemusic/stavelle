/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 */
#include "bulkbuilder.h"

#include <algorithm>
#include <stdexcept>

namespace F = je::model::fields;

namespace je::model {
namespace {
Fraction timeOf(const std::vector<std::pair<Field, Value> >& fields)
{
    for (const auto& [field, value] : fields) {
        if (field == F::TIME && std::holds_alternative<Fraction>(value)) {
            return std::get<Fraction>(value);
        }
    }
    return Fraction();
}
}

Id BulkBuilder::add(Kind kind, Id parent, std::vector<std::pair<Field, Value> > fields, Order order, Id id)
{
    if (!id.isValid()) {
        id = m_ids.next();
    } else {
        m_ids.reserve(id);
    }
    if (m_index.count(id)) {
        throw std::logic_error("je::model::BulkBuilder: id used twice");
    }
    if (kind == Kind::Score) {
        m_score = id;
    } else if (!parent.isValid()) {
        parent = m_score;
    }
    if (parent.isValid() && !m_index.count(parent)) {
        throw std::logic_error("je::model::BulkBuilder: the parent must be added first");
    }
    m_index.emplace(id, m_objects.size());
    m_objects.push_back({ id, kind, parent, order, std::move(fields) });
    if (parent.isValid()) {
        m_children[parent].push_back(id);
    }
    return id;
}

void BulkBuilder::setField(Id id, Field field, Value value)
{
    auto it = m_index.find(id);
    if (it == m_index.end()) {
        throw std::logic_error("je::model::BulkBuilder::setField: unknown object");
    }
    m_objects[it->second].fields.emplace_back(field, std::move(value));   // a later value wins (Object's constructor)
}

size_t BulkBuilder::finish(Model& model)
{
    // times first (fields are moved into the objects below)
    std::vector<Fraction> times(m_objects.size());
    for (size_t i = 0; i < m_objects.size(); ++i) {
        times[i] = timeOf(m_objects[i].fields);
    }
    auto timeAt = [this, &times](Id id) { return times[m_index.at(id)]; };

    std::vector<Object> objects;
    objects.reserve(m_objects.size());
    for (Pending& p : m_objects) {
        std::vector<std::pair<Field, Value> > fields = std::move(p.fields);
        fields.emplace_back(F::KIND, int64_t(p.kind));
        if (p.parent.isValid()) {
            fields.emplace_back(F::PARENT, p.parent);
        }
        auto kids = m_children.find(p.id);
        if (kids != m_children.end() && !kids->second.empty()) {
            // the same rule as je::edit::create: appended, or placed after the children with the same or an
            // earlier time (in files the events come in time order, so this is a search at the end)
            std::vector<Id> list;
            list.reserve(kids->second.size());
            for (Id c : kids->second) {
                if (m_objects[m_index.at(c)].order == Order::Append) {
                    list.push_back(c);
                } else {
                    const Fraction t = timeAt(c);
                    auto pos = std::upper_bound(list.begin(), list.end(), t,
                                                [&timeAt](const Fraction& time, Id other) { return time < timeAt(other); });
                    list.insert(pos, c);
                }
            }
            fields.emplace_back(F::CHILDREN, IdList(std::move(list)));
        }
        objects.emplace_back(p.id, std::move(fields));
    }
    const size_t count = objects.size();
    model.replaceAll(std::move(objects), m_score);
    m_objects.clear();
    m_index.clear();
    m_children.clear();
    return count;
}
}
