/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore).
 *
 * R3 crash reporting (decision D7, 15 Nov 2026). Private, cleaned and sent
 * only by you:
 *  - when Jubelle crashes, a small text report is written on this computer
 *    (version, Windows version, the kind of crash, and where in Jubelle's own
 *    code it happened as module + offset; no memory contents);
 *  - a "session open" marker notices when Jubelle didn't close normally;
 *  - at the next start Jubelle offers Review / Send / Don't send. Send opens
 *    your browser on a new issue in your private crash-report repository,
 *    already filled in; you look at it and submit it yourself. No token or
 *    password is stored in Jubelle.
 * Before anything is shown or sent, user names, folders and file names (so
 * score titles), e-mail addresses and anything that looks like a key or token
 * are removed.
 *
 * Plain C++, no Qt, so it is tested without the app.
 */
#pragma once

#include <set>
#include <string>
#include <vector>

namespace mu::appshell::jubelle {
//! Where reports go (a private repository you create; decision D7)
constexpr const char* CRASH_REPORT_REPOSITORY = "nlinemusic/jubelle-crash-reports";

struct CrashReport {
    std::string version;            // e.g. 4.7.7.261116010
    std::string os;                 // e.g. Windows 11 (10.0.26100)
    std::string kind;               // e.g. EXCEPTION_ACCESS_VIOLATION, or "Unexpected shutdown"
    std::vector<std::string> frames;   // "Jubelle.exe+0x1a2b3c", top first
    std::string logTail;            // the last lines of the log (cleaned before use)
    std::string when;               // ISO date and time

    std::string toText() const;
    static CrashReport fromText(const std::string& text);

    //! A short code from the kind and the top frames: the same crash gives the same code
    std::string signature() const;
    //! The issue title: "[crash 1a2b3c4d] <kind> at <top frame> (<version>)"
    std::string issueTitle() const;
    //! The issue body (Markdown), cleaned
    std::string issueBody(const std::string& homeDir, const std::string& userName) const;
};

//! Removes personal details: the user's name and home folder, every file path
//! and file name, e-mail addresses, and token-like strings
std::string sanitize(const std::string& text, const std::string& homeDir, const std::string& userName);

//! https://github.com/<repo>/issues/new?title=...&body=... (kept short enough for a browser)
std::string newIssueUrl(const std::string& repository, const std::string& title, const std::string& body);
//! https://github.com/<repo>/issues?q=<signature>, to add a +1 to a known crash
std::string searchIssuesUrl(const std::string& repository, const std::string& signature);

std::string urlEncode(const std::string& text);

//! Signatures you have already sent (one per line in a small file)
std::set<std::string> readSentSignatures(const std::string& file);
void rememberSentSignature(const std::string& file, const std::string& signature);

//! The session marker: written when Jubelle starts, removed when it closes normally
void markSessionOpen(const std::string& markerFile, const std::string& version);
void markSessionClosed(const std::string& markerFile);
bool sessionWasLeftOpen(const std::string& markerFile, std::string* version = nullptr);

//! Installs the crash handler (Windows: unhandled exceptions; elsewhere: fatal
//! signals). It writes `reportFile` when Jubelle crashes, then lets the
//! previous handler (for example crashpad's local dump) run as before.
void installCrashHandler(const std::string& reportFile, const std::string& version, const std::string& os);
}
