/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore).
 *
 * Report a Bug (Help › Report a bug…): the report itself.
 *
 *  - You write the title, what happened, the steps, what you expected, what
 *    actually happened and any comments, and may add screenshots.
 *  - Jubelle adds the details that help fix a problem: its version and build,
 *    the system, the hardware, which notation engine is in use, the end of
 *    the log and the last crash, if any. Everything it adds is cleaned first:
 *    your user name, home folder, folder and file names (so score titles),
 *    e-mail addresses, passwords, keys and tokens are removed.
 *  - Your score is never added unless you tick "Include the open score"
 *    (off every time the window opens).
 *  - You see the whole report before anything is made. The result is a ZIP
 *    file on this computer that you send yourself; Jubelle uploads nothing.
 *
 * A future "send online" option fits in as another Submitter (see ISubmitter)
 * without changing the report. It must not carry a password or key inside
 * Jubelle: it would sign you in through your browser, or post to a service
 * that needs no secret in the app.
 *
 * Plain C++ (no Qt), so it is tested without the app.
 */
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "global/jubelle/diagnosticsregistry.h"

namespace mu::appshell::jubelle::bugreport {
constexpr const char* REPORT_SCHEMA = "jubelle.bugreport/1";
constexpr size_t MAX_SCREENSHOTS = 5;
constexpr size_t MAX_SCREENSHOT_BYTES = 10 * 1024 * 1024;
constexpr size_t MAX_SCORE_BYTES = 50 * 1024 * 1024;
constexpr size_t LOG_TAIL_LINES = 300;

using Bytes = std::vector<uint8_t>;

struct Attachment {
    std::string name;   // the name inside the package (made safe)
    Bytes data;
};

//! What you write (and choose) in the window
struct BugReportInput {
    std::string title;
    std::string description;
    std::string steps;
    std::string expected;
    std::string actual;
    std::string comments;

    std::vector<Attachment> screenshots;

    bool includeLogs = true;     // the cleaned end of the log and the last crash
    bool includeScore = false;   // never on unless you tick it
    Attachment score;            // only read when includeScore is on
};

//! Used to remove you from the report
struct PrivacyContext {
    std::string homeDir;
    std::string userName;
};

//! Raw details gathered when the report is made (cleaned by assemble())
struct RawDiagnostics {
    std::vector<muse::jubelle::DiagnosticSection> sections;
    std::string logTail;
    std::string lastCrash;
};

struct BugReport {
    std::string id;          // e.g. 20261118-153012-4f2a
    std::string createdAt;   // ISO 8601, UTC
    BugReportInput input;    // passwords and keys removed from your text
    std::vector<muse::jubelle::DiagnosticSection> diagnostics;   // cleaned
    std::string logTail;     // cleaned, empty when logs are not included
    std::string lastCrash;   // cleaned, empty when none or not included
};

//! Missing required fields, in words ("" when the report can be made)
std::string validate(const BugReportInput& input);

//! A screenshot must be a PNG or JPEG of at most MAX_SCREENSHOT_BYTES
bool isAcceptedScreenshot(const Bytes& data, std::string* reason = nullptr);

//! Removes passwords, keys, tokens and secrets written as name=value,
//! "name": "value", Authorization headers, private-key blocks and the usual
//! token formats. Keeps everything else as written.
std::string redactSecrets(const std::string& text);

//! Full cleaning for automatic details: sanitize() (you, folders, file
//! names, e-mail addresses, tokens) then redactSecrets()
std::string cleanDiagnosticText(const std::string& text, const PrivacyContext& privacy);

//! Letters, digits, '.', '-' and '_' only, at most 60 characters, never empty
std::string safeFileName(const std::string& name, const std::string& fallback);

//! Builds the report: cleans every automatic detail and the secrets in your text
BugReport assemble(const BugReportInput& input, const RawDiagnostics& raw, const PrivacyContext& privacy,
                   const std::string& createdAtIso, const std::string& id);

std::string toMarkdown(const BugReport& report);
std::string toJson(const BugReport& report);

//! The files the package will hold, in order (shown under "What is included")
std::vector<std::string> packageContents(const BugReport& report);

//! The package as ZIP bytes: report.md, report.json, diagnostics/log.txt,
//! diagnostics/last-crash.txt, screenshots/…, score/… (only when chosen)
Bytes makePackage(const BugReport& report);

//! Writes bytes to a file safely: to a temporary name first, then renamed,
//! readable only by you where the system allows it. Never overwrites.
bool writeFileSafely(const std::string& path, const Bytes& data, std::string* error = nullptr);

//! The newest log file's last lines (raw; assemble() cleans them)
std::string readNewestLogTail(const std::string& logsDir, size_t lines = LOG_TAIL_LINES);

//! "20261118-153012-4f2a" from a UTC time and a little randomness
std::string makeReportId(const std::string& createdAtIso, unsigned randomPart);

// ---------------------------------------------------------------------------
// Submitting
// ---------------------------------------------------------------------------

struct SubmitResult {
    bool ok = false;
    std::string location;   // the file, or later a link
    std::string error;
};

//! How a finished report leaves Jubelle. Today: saved as a ZIP for you to
//! send. Later: an online submitter can be added here.
class ISubmitter
{
public:
    virtual ~ISubmitter() = default;
    virtual std::string id() const = 0;
    //! False when it would need something Jubelle must not hold (a password or key)
    virtual bool available() const = 0;
    virtual SubmitResult submit(const BugReport& report) = 0;
};

//! Saves the package into a folder as Jubelle-bug-report-<id>.zip
class SaveToFolderSubmitter : public ISubmitter
{
public:
    explicit SaveToFolderSubmitter(std::string folder);
    std::string id() const override { return "save-zip"; }
    bool available() const override { return true; }
    SubmitResult submit(const BugReport& report) override;

private:
    std::string m_folder;
};
}
