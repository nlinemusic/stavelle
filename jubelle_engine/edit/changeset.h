/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * Transactions and change sets (decision D-E6, docs/03_engine_architecture.md §4).
 * Every change to the model is recorded as (object id, field, value before, value
 * after). A committed transaction is one ChangeSet = one undo step. Undo applies the
 * inverse change set. History never holds pointers, only ids and values.
 *
 * This first version works on any ChangeTarget (the model implements it in Phase 3.2;
 * tests use a simple property store). Structural operations (add/remove objects) are
 * recorded the same way: the "exists" field with before/after.
 */
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <variant>
#include <vector>

#include "model/fraction.h"
#include "model/id.h"
#include "model/value.h"

namespace je::edit {
using je::model::Value;
using je::model::Field;
using je::model::IdList;
using je::model::ChangeTarget;
constexpr Field FIELD_ENGINE_BASE = 100000;
constexpr Field FIELD_EXISTS = FIELD_ENGINE_BASE + 1;   // structural: object created (after=true) or removed

struct ChangeRecord {
    je::Id object;
    Field field = 0;
    Value before;
    Value after;
};

struct ChangeSet {
    std::string actionName;              // shown in the undo history, as MuseScore names the action
    std::vector<ChangeRecord> records;   // in the order they were made

    bool empty() const { return records.empty(); }
    //! The change set that undoes this one (records reversed, before/after swapped)
    ChangeSet inverse() const;
    //! The objects touched (for invalidation), without repeats, in first-touch order
    std::vector<je::Id> objects() const;
};

//! What a transaction changes: the model implements this

//! Applies a change set's "after" values in order
void apply(ChangeTarget& target, const ChangeSet& changes);

class Transaction
{
public:
    //! A hook runs once, just before commit, and may make more changes in the same
    //! transaction (linked passages, rules). Hooks don't see their own additions.
    using Hook = std::function<void(Transaction&, const ChangeSet& soFar)>;

    Transaction(ChangeTarget& target, std::string actionName, std::vector<Hook> hooks = {});
    ~Transaction();   // rolls back if neither committed nor rolled back

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void set(je::Id object, Field field, const Value& value);

    //! Runs the hooks, then returns the change set with repeated changes to the same
    //! field merged (first "before", last "after") and changes that end where they
    //! started removed. The transaction is finished.
    ChangeSet commit();

    //! Undoes everything this transaction did. The transaction is finished.
    void rollback();

    bool isOpen() const { return m_open; }

    //! The changes made so far (a command that brings linked staves in line before it finishes)
    const ChangeSet& changes() const { return m_changes; }

    //! What the transaction changes, for reading the current state while building it
    const ChangeTarget& target() const { return m_target; }

private:
    ChangeTarget& m_target;
    ChangeSet m_changes;
    std::vector<Hook> m_hooks;
    bool m_open = true;
};
}
