/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 */
#include "session.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

#include "io/mscxwriter.h"

namespace fs = std::filesystem;

namespace je::edit {
namespace {
constexpr const char* EXTENSION = ".jej";
constexpr auto SYNC_INTERVAL = std::chrono::seconds(1);

//! A journal path not in use: a journal left by a crash is never overwritten by opening the score again
std::string freeJournalPath(const std::string& scorePath, const std::string& folder)
{
    const std::string first = journalPathFor(scorePath, folder);
    std::error_code ec;
    if (!fs::exists(fs::u8path(first), ec)) {
        return first;
    }
    const std::string stem = first.substr(0, first.size() - std::char_traits<char>::length(EXTENSION));
    for (int n = 2;; ++n) {
        const std::string p = stem + "-" + std::to_string(n) + EXTENSION;
        if (!fs::exists(fs::u8path(p), ec)) {
            return p;
        }
    }
}

std::string absolute(const std::string& path)
{
    std::error_code ec;
    const fs::path a = fs::absolute(fs::u8path(path), ec);
    return ec ? path : a.lexically_normal().u8string();
}
}

std::string journalPathFor(const std::string& scorePath, const std::string& folder)
{
    const std::string full = absolute(scorePath);
    uint64_t h = 1469598103934665603ull;
    for (unsigned char c : full) {
        h = (h ^ c) * 1099511628211ull;
    }
    char hex[17];
    std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(h));
    const std::string stem = fs::u8path(scorePath).stem().u8string();
    return (fs::u8path(folder) / fs::u8path(stem + "-" + hex + EXTENSION)).u8string();
}

std::vector<std::pair<std::string, RecoveryReport>> findJournals(const std::string& folder)
{
    std::vector<std::pair<std::string, RecoveryReport>> found;
    std::error_code ec;
    for (auto it = fs::directory_iterator(fs::u8path(folder), ec); !ec && it != fs::directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file(ec) && it->path().extension() == EXTENSION) {
            const std::string p = it->path().u8string();
            found.emplace_back(p, inspectJournal(p));
        }
    }
    std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    return found;
}

bool discardJournal(const std::string& journalPath)
{
    std::error_code ec;
    return fs::remove(fs::u8path(journalPath), ec) && !ec;
}

EditSession::EditSession(std::size_t historyLimit)
    : m_history(historyLimit)
    , m_historyLimit(historyLimit)
{
    reset();
}

EditSession::~EditSession()
{
    // Not closed: the program is ending without the score being closed, so the journal stays for recovery
    m_journal.close(false);
}

void EditSession::reset()
{
    m_journal.close(false);
    m_model = std::make_unique<model::Model>();
    m_ids = std::make_unique<IdGenerator>(1);   // the ids a file is read with must be the same every time (replay)
    m_history = History(m_historyLimit);
    m_path.clear();
    m_journalFolder.clear();
    m_journalError.clear();
}

bool EditSession::open(const std::string& path, const std::string& journalFolder, std::string& error)
{
    reset();
    if (!io::openScore(path, *m_model, *m_ids, error)) {
        reset();
        return false;
    }
    m_path = path;
    m_history.setClean();
    m_journalFolder = journalFolder;
    startJournal();
    return true;
}

void EditSession::startJournal()
{
    if (m_journalFolder.empty()) {
        return;
    }
    JournalBase base;
    std::string jerror;
    std::error_code ec;
    fs::create_directories(fs::u8path(m_journalFolder), ec);
    if (!describeFile(m_path, base)) {
        m_journalError = "the score's file can't be read for its journal: " + m_path;
        return;
    }
    base.file = absolute(m_path);   // recovery opens the file by this path
    if (!m_journal.start(freeJournalPath(m_path, m_journalFolder), base, m_model->snapshot(), m_history, jerror)) {
        m_journalError = jerror.empty() ? "the journal can't be written: " + m_journal.path() : jerror;
    }
    m_lastSync = std::chrono::steady_clock::now();
}

bool EditSession::recover(const std::string& journalPath, RecoveryReport& report, std::string& error)
{
    reset();
    report = replayJournal(journalPath, *m_model, m_history, *m_ids);
    if (!report.ok) {
        error = report.error;
        reset();
        return false;
    }
    m_path = report.base.file;
    // The journal goes on from here: written again from the recovered score and history (so a second crash before
    // saving loses nothing either), replacing the one recovered only once it is on the disk
    m_journalFolder = fs::u8path(journalPath).parent_path().u8string();
    const std::string next = journalPath + ".recovered";
    std::string jerror;
    if (!m_journal.start(next, report.base, m_model->snapshot(), m_history, jerror)) {
        m_journalError = jerror;
    } else {
        m_journal.close(false);
        std::error_code ec;
        fs::rename(fs::u8path(next), fs::u8path(journalPath), ec);   // replaces the recovered journal
        if (ec) {
            m_journalError = "the journal can't be written: " + journalPath;
            discardJournal(next);
        } else if (!m_journal.append(journalPath, jerror)) {
            m_journalError = jerror;
        }
    }
    m_lastSync = std::chrono::steady_clock::now();
    return true;
}

void EditSession::synced(bool ok)
{
    if (m_journal.isOpen() && (!ok || m_journal.syncFailed())) {
        m_journalError = "the journal can't be written: " + m_journal.path();
    }
}

void EditSession::maybeSync()
{
    const auto now = std::chrono::steady_clock::now();
    if (m_journal.isOpen() && now - m_lastSync >= SYNC_INTERVAL) {
        m_journal.syncLater();   // in the background: editing doesn't wait for the disk
        m_lastSync = now;
    }
}

const ChangeSet& EditSession::commit(Transaction& tx)
{
    m_lastCommit = tx.commit();
    if (!m_lastCommit.empty()) {
        m_history.push(m_lastCommit);
        if (m_journal.isOpen()) {
            synced(m_journal.recordStep(m_lastCommit));
            maybeSync();
        }
    }
    return m_lastCommit;
}

ChangeSet EditSession::undo()
{
    ChangeSet c = m_history.undo(*m_model);
    if (m_journal.isOpen()) {
        synced(m_journal.recordUndo());
        maybeSync();
    }
    return c;
}

ChangeSet EditSession::redo()
{
    ChangeSet c = m_history.redo(*m_model);
    if (m_journal.isOpen()) {
        synced(m_journal.recordRedo());
        maybeSync();
    }
    return c;
}

bool EditSession::save(std::string& error)
{
    return saveAs(m_path, error);
}

bool EditSession::saveAs(const std::string& path, std::string& error)
{
    if (path.empty()) {
        error = "no file to save to";
        return false;
    }
    if (!io::saveScore(m_model->snapshot(), path, error)) {
        return false;   // the old file and the journal are untouched: nothing is lost
    }
    const bool moved = absolute(path) != absolute(m_path);
    m_path = path;
    m_history.setClean();
    if (moved && !m_journalFolder.empty()) {
        // Saved as another file: its own journal (the old one is removed; the old file is as it was saved)
        m_journal.close(true);
        m_journalError.clear();
        startJournal();
        return true;
    }
    if (m_journal.isOpen()) {
        JournalBase base;
        std::string jerror;
        if (!describeFile(path, base)) {
            m_journalError = "the saved file can't be read for its journal";
            return true;
        }
        base.file = absolute(path);
        if (!m_journal.restart(base, m_model->snapshot(), m_history, jerror)) {
            m_journalError = jerror;
        } else {
            m_journalError.clear();
        }
        m_lastSync = std::chrono::steady_clock::now();
    }
    return true;
}

void EditSession::close()
{
    m_journal.close(true);
    reset();
}
}
