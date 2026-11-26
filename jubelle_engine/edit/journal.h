/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * The autosave journal (S4): every committed edit, undo and redo is appended to a small file as it happens, so that
 * after a crash the score can be brought back to the moment of the crash, with its undo history, by replaying the
 * journal. The journal begins with a copy of the whole model as it was when the journal started (on opening, saving
 * or recovering a score): every object with its id and fields. Recovery builds the model from that copy, not from the
 * score's file, because the ids of a model read from a file are given in reading order, so the file saved from an
 * edited model doesn't read back with the ids the history refers to. It also means the work is recovered even if
 * the file was changed or lost since; the journal names the file (its path, size and a hash of its bytes) so the
 * user can be told. Each record carries its length and a checksum: a record cut short by the crash is recognised
 * and left out, and everything before it is recovered. Saving starts the journal again; closing removes it.
 *
 * Format (little-endian): "JEJ1", format version (u32), the file's path, size (u64) and hash (u64); then records,
 * each [length u32][crc32 u32][kind u8 …]: the model (score id, then each object: id u64, field count, fields), the
 * undo history already made (its steps; how many are applied), a step (its action name and change records: object
 * u64, field i32, before and after values), an undo, a redo.
  */
#pragma once

#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

#include "changeset.h"
#include "history.h"
#include "model/model.h"

namespace je::edit {
//! The file a journal applies to
struct JournalBase {
    std::string file;
    uint64_t size = 0;
    uint64_t hash = 0;   // FNV-1a of the file's bytes
    bool operator==(const JournalBase& o) const { return size == o.size && hash == o.hash; }
};

//! The size and hash of a file (false if it can't be read)
bool describeFile(const std::string& path, JournalBase& out);

class Journal
{
public:
    Journal() = default;
    ~Journal();
    Journal(const Journal&) = delete;
    Journal& operator=(const Journal&) = delete;

    //! Starts a new journal at `path` (replacing any there) for a score whose file is `base`: the model as it is now
    //! and its undo history are written first
    bool start(const std::string& path, const JournalBase& base, const model::Snapshot& model, const History& history,
               std::string& error);
    //! Goes on writing a journal already there (after a recovery has written it again)
    bool append(const std::string& path, std::string& error);
    bool isOpen() const { return m_file != nullptr; }
    const std::string& path() const { return m_path; }

    //! A committed step, an undo, a redo (written and flushed to the system at once)
    bool recordStep(const ChangeSet& step);
    bool recordUndo();
    bool recordRedo();
    //! Puts what was written on the disk, waiting for it (when the journal starts)
    bool sync();
    //! The same, done in the background so that editing never waits for the disk (from time to time while editing)
    void syncLater();
    //! A background sync failed (the disk is full or gone): the journal may not survive the computer failing
    bool syncFailed() const { return m_syncFailed; }
    //! After the score was saved as `base`: the journal starts again from the model as it is, keeping the history
    bool restart(const JournalBase& base, const model::Snapshot& model, const History& history, std::string& error);
    //! The score was closed: the journal is removed (removeFile) or just closed
    void close(bool removeFile = true);

    //! Bytes written since the journal (re)started
    uint64_t bytes() const { return m_bytes; }

private:
    bool write(const std::string& payload);
    bool writeStart(const JournalBase& base, const model::Snapshot& model, const History& history);

    void syncLoop();
    void stopSyncer();

    std::FILE* m_file = nullptr;
    std::string m_path;
    uint64_t m_bytes = 0;

    std::thread m_syncer;
    std::mutex m_mutex;
    std::condition_variable m_wake;
    bool m_pending = false;
    bool m_stop = false;
    int m_fd = -1;
    std::atomic<bool> m_syncFailed { false };
};

struct RecoveryReport {
    bool ok = false;
    std::string error;
    JournalBase base;           // the file the journal belongs to (as it was when the journal started)
    bool baseChanged = false;   // the file is no longer as it was then (changed elsewhere, or missing)
    size_t objects = 0;         // in the model the journal starts from
    size_t historySteps = 0;    // undo steps made before the journal started
    size_t steps = 0, undos = 0, redos = 0;
    bool truncated = false;     // a last record cut short (by the crash) was left out
};

//! What a journal holds, without applying it (which file it belongs to, how many records)
RecoveryReport inspectJournal(const std::string& path);

//! Replays a journal: `model` and `history` (replaced) end as they were at the last whole record, and `ids` is moved
//! past every id in them. If it fails (a journal that isn't whole at its start), they are left empty
RecoveryReport replayJournal(const std::string& path, model::Model& model, History& history, IdGenerator& ids);
}
