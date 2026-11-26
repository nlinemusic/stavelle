/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * An open score being edited (S4): the model, its ids, the undo history and the autosave journal kept together, so
 * that every committed edit, undo and redo is journalled as it happens and the score can be brought back after a
 * crash (see journal.h). Opening starts a journal; saving writes the file safely (a temporary file put on the disk,
 * then a rename) and starts the journal again, keeping the undo history; closing removes the journal. Opening a
 * score never overwrites a journal a crash left for it. A journal left behind is found with findJournals() and
 * replayed with recover().
 *
 * Journal writes never hold up editing: a failed write is reported by journalError() (the edit itself stands).
 * Records are handed to the system at once (they survive the program crashing); they are put on the disk (they
 * survive the computer failing) in the background at most once a second while editing, so editing never waits for
 * the disk, and at once when the journal starts (opening, saving, recovering).
 */
#pragma once

#include <chrono>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "changeset.h"
#include "history.h"
#include "journal.h"
#include "model/model.h"

namespace je::edit {
//! Where the journal of a score is kept in `folder`: a name made from the score's name and a hash of its full path,
//! so two scores of the same name in different folders don't share one
std::string journalPathFor(const std::string& scorePath, const std::string& folder);

//! Removes a journal that won't be recovered (the user chose to discard the changes)
bool discardJournal(const std::string& journalPath);

//! The journals in `folder` that can be recovered (each inspected: the file it belongs to and how much it holds)
std::vector<std::pair<std::string, RecoveryReport>> findJournals(const std::string& folder);

class EditSession
{
public:
    explicit EditSession(std::size_t historyLimit = 0);
    ~EditSession();
    EditSession(const EditSession&) = delete;
    EditSession& operator=(const EditSession&) = delete;

    //! Opens a score and starts its journal in `journalFolder` (none if empty)
    bool open(const std::string& path, const std::string& journalFolder, std::string& error);
    //! Replays a journal left by a crash: the score, with its undo history, as it was at the crash, to be saved to the
    //! file the journal names (report.baseChanged: that file has changed or gone since, which the user should be told
    //! before it is overwritten). The score is modified until saved, and the journal goes on.
    bool recover(const std::string& journalPath, RecoveryReport& report, std::string& error);

    model::Model& model() { return *m_model; }
    const model::Model& model() const { return *m_model; }
    IdGenerator& ids() { return *m_ids; }
    const History& history() const { return m_history; }

    //! A committed transaction: kept for undo and journalled. Returns the change set (for invalidation)
    const ChangeSet& commit(Transaction& tx);
    bool canUndo() const { return m_history.canUndo(); }
    bool canRedo() const { return m_history.canRedo(); }
    ChangeSet undo();
    ChangeSet redo();

    //! Saves to the file it was opened from, or to another (which it is then)
    bool save(std::string& error);
    bool saveAs(const std::string& path, std::string& error);
    //! Closes the score; the journal is removed (the score was saved or the changes were given up)
    void close();

    bool isOpen() const { return !m_path.empty(); }
    bool isModified() const { return !m_history.isClean(); }
    const std::string& path() const { return m_path; }
    const std::string& journalPath() const { return m_journal.path(); }
    //! The last journal write that failed (empty when all is well)
    const std::string& journalError() const { return m_journalError; }

private:
    void reset();
    void startJournal();
    void synced(bool ok);
    void maybeSync();

    std::unique_ptr<model::Model> m_model;
    std::unique_ptr<IdGenerator> m_ids;
    History m_history;
    std::size_t m_historyLimit = 0;
    Journal m_journal;
    std::string m_path;
    std::string m_journalFolder;
    std::string m_journalError;
    std::chrono::steady_clock::time_point m_lastSync;
    ChangeSet m_lastCommit;
};
}
