/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * Undo/redo history of change sets. A new change after undoing discards the redo branch,
 * as in MuseScore. The history can be limited in size (oldest steps dropped). It knows where the score was last
 * saved (MuseScore's clean state: "modified" in the title bar and the question on closing), and the names of the
 * steps for the Undo and Redo menu items.
 */
#pragma once

#include <cstddef>
#include <string>
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

    //! The score is as it was saved (or opened) here
    void setClean() { m_clean = m_index; m_cleanValid = true; }
    //! Whether the score is as it was last saved: undoing back to that point makes it clean again, as in MuseScore
    bool isClean() const { return m_cleanValid && m_clean == m_index; }
    //! The name of the step Undo would take back, and the one Redo would make again (empty when none)
    std::string undoName() const { return canUndo() ? m_steps[m_index - 1].actionName : std::string(); }
    std::string redoName() const { return canRedo() ? m_steps[m_index].actionName : std::string(); }
    //! Forgets every step (a score opened again, or recovered)
    void clear();
    //! Takes over steps already in the score (a recovered journal): the first `index` are applied, the rest can be
    //! redone; the score is clean at `index` (as it was saved)
    void restore(std::vector<ChangeSet> steps, std::size_t index);

private:
    std::vector<ChangeSet> m_steps;
    std::size_t m_index = 0;
    std::size_t m_limit = 0;
    std::size_t m_clean = 0;      // the index at which the score was saved
    bool m_cleanValid = true;     // false once the saved state can't be reached again (its steps were replaced)
};
}
