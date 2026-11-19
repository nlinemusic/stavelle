/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * Undo/redo history of change sets. A new change after undoing discards the redo branch,
 * as in MuseScore. The history can be limited in size (oldest steps dropped).
 */
#pragma once

#include <cstddef>
#include <vector>

#include "changeset.h"

namespace je::edit {
class History
{
public:
    explicit History(std::size_t limit = 0);   // 0: unlimited

    void push(ChangeSet changes);              // a committed transaction (empty ones are ignored)

    bool canUndo() const { return m_index > 0; }
    bool canRedo() const { return m_index < m_steps.size(); }

    //! Applies the inverse of the last step; returns the change set applied (for invalidation)
    ChangeSet undo(ChangeTarget& target);
    ChangeSet redo(ChangeTarget& target);

    std::size_t size() const { return m_steps.size(); }
    std::size_t index() const { return m_index; }        // steps currently applied
    const ChangeSet& step(std::size_t i) const { return m_steps.at(i); }

private:
    std::vector<ChangeSet> m_steps;
    std::size_t m_index = 0;
    std::size_t m_limit = 0;
};
}
