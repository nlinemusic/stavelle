/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 */
#include "history.h"

#include <stdexcept>

namespace je::edit {
History::History(std::size_t limit)
    : m_limit(limit)
{
}

void History::push(ChangeSet changes)
{
    if (changes.empty()) {
        return;
    }
    m_steps.resize(m_index);   // drop the redo branch
    m_steps.push_back(std::move(changes));
    m_index = m_steps.size();
    if (m_limit && m_steps.size() > m_limit) {
        m_steps.erase(m_steps.begin(), m_steps.begin() + static_cast<std::ptrdiff_t>(m_steps.size() - m_limit));
        m_index = m_steps.size();
    }
}

ChangeSet History::undo(ChangeTarget& target)
{
    if (!canUndo()) {
        throw std::logic_error("je::edit::History: nothing to undo");
    }
    ChangeSet inv = m_steps[--m_index].inverse();
    apply(target, inv);
    return inv;
}

ChangeSet History::redo(ChangeTarget& target)
{
    if (!canRedo()) {
        throw std::logic_error("je::edit::History: nothing to redo");
    }
    const ChangeSet& step = m_steps[m_index++];
    apply(target, step);
    return step;
}
}
