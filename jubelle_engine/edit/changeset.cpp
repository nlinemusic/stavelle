/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 */
#include "changeset.h"

#include <algorithm>
#include <map>
#include <stdexcept>
#include <unordered_set>

namespace je::edit {
ChangeSet ChangeSet::inverse() const
{
    ChangeSet inv;
    inv.actionName = actionName;
    inv.records.reserve(records.size());
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        inv.records.push_back({ it->object, it->field, it->after, it->before });
    }
    return inv;
}

std::vector<je::Id> ChangeSet::objects() const
{
    std::vector<je::Id> out;
    std::unordered_set<je::Id> seen;
    for (const ChangeRecord& r : records) {
        if (seen.insert(r.object).second) {
            out.push_back(r.object);
        }
    }
    return out;
}

void apply(ChangeTarget& target, const ChangeSet& changes)
{
    for (const ChangeRecord& r : changes.records) {
        target.set(r.object, r.field, r.after);
    }
}

Transaction::Transaction(ChangeTarget& target, std::string actionName, std::vector<Hook> hooks)
    : m_target(target), m_hooks(std::move(hooks))
{
    m_changes.actionName = std::move(actionName);
}

Transaction::~Transaction()
{
    if (m_open) {
        rollback();
    }
}

void Transaction::set(je::Id object, Field field, const Value& value)
{
    if (!m_open) {
        throw std::logic_error("je::edit::Transaction: set after commit or rollback");
    }
    Value before = m_target.get(object, field);
    if (before == value) {
        return;
    }
    m_target.set(object, field, value);
    m_changes.records.push_back({ object, field, std::move(before), value });
}

ChangeSet Transaction::commit()
{
    if (!m_open) {
        throw std::logic_error("je::edit::Transaction: commit twice");
    }
    if (!m_hooks.empty()) {
        const ChangeSet soFar = m_changes;
        std::vector<Hook> hooks;
        hooks.swap(m_hooks);   // hooks run once, and don't re-trigger on their own changes
        for (const Hook& h : hooks) {
            h(*this, soFar);
        }
    }
    m_open = false;

    // merge repeated changes to the same (object, field): keep the first "before" and the
    // last "after", at the position of the first change; drop changes that end unchanged
    std::map<std::pair<uint64_t, Field>, size_t> first;
    std::vector<ChangeRecord> merged;
    for (const ChangeRecord& r : m_changes.records) {
        auto key = std::make_pair(r.object.value(), r.field);
        auto it = first.find(key);
        if (it == first.end()) {
            first.emplace(key, merged.size());
            merged.push_back(r);
        } else {
            merged[it->second].after = r.after;
        }
    }
    merged.erase(std::remove_if(merged.begin(), merged.end(), [](const ChangeRecord& r) { return r.before == r.after; }),
                 merged.end());
    ChangeSet out;
    out.actionName = m_changes.actionName;
    out.records = std::move(merged);
    return out;
}

void Transaction::rollback()
{
    if (!m_open) {
        throw std::logic_error("je::edit::Transaction: rollback after finish");
    }
    m_open = false;
    apply(m_target, m_changes.inverse());
}
}
