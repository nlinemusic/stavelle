/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore). Report a Bug: the report.
 */
#include "bugreport.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <regex>
#include <sstream>

#include "global/io/buffer.h"
#include "global/serialization/json.h"
#include "global/serialization/zipwriter.h"

#include "../crashreport.h"

namespace fs = std::filesystem;
using muse::jubelle::DiagnosticSection;

namespace mu::appshell::jubelle::bugreport {
namespace {
std::string trim(const std::string& s)
{
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) {
        return std::string();
    }
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

bool endsWith(const std::string& s, const std::string& tail)
{
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}

//! Text in a Markdown code block must not end the block early
std::string fenceSafe(const std::string& s)
{
    std::string out = s;
    size_t pos = 0;
    while ((pos = out.find("```", pos)) != std::string::npos) {
        out.replace(pos, 3, "'''");
    }
    return out;
}

std::string orNone(const std::string& s)
{
    const std::string t = trim(s);
    return t.empty() ? std::string("_(none)_") : t;
}

std::string extensionOf(const std::string& name)
{
    const size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? std::string() : lower(name.substr(dot));
}

std::string screenshotName(size_t index, const Attachment& a)
{
    std::string ext = extensionOf(a.name);
    if (ext != ".png" && ext != ".jpg" && ext != ".jpeg") {
        ext = (a.data.size() >= 2 && a.data[0] == 0xFF && a.data[1] == 0xD8) ? ".jpg" : ".png";
    }
    std::string base = a.name;
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) {
        base = base.substr(0, dot);
    }
    char prefix[8];
    std::snprintf(prefix, sizeof(prefix), "%02u-", unsigned(index + 1));
    return std::string("screenshots/") + prefix + safeFileName(base, "screenshot") + ext;
}

std::string scoreName(const Attachment& a)
{
    std::string ext = extensionOf(a.name);
    if (ext.empty()) {
        ext = ".mscz";
    }
    std::string base = a.name;
    const size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) {
        base = base.substr(0, dot);
    }
    return "score/" + safeFileName(base, "score") + ext;
}

bool scoreIncluded(const BugReport& r)
{
    return r.input.includeScore && !r.input.score.data.empty();
}
}

// ---------------------------------------------------------------------------
// Checks
// ---------------------------------------------------------------------------

std::string validate(const BugReportInput& input)
{
    std::vector<std::string> missing;
    if (trim(input.title).empty()) {
        missing.push_back("a title");
    }
    if (trim(input.description).empty()) {
        missing.push_back("a description of the problem");
    }
    if (input.screenshots.size() > MAX_SCREENSHOTS) {
        return "At most " + std::to_string(MAX_SCREENSHOTS) + " screenshots can be added.";
    }
    for (const Attachment& a : input.screenshots) {
        std::string why;
        if (!isAcceptedScreenshot(a.data, &why)) {
            return "Screenshot \"" + a.name + "\": " + why;
        }
    }
    if (input.includeScore && input.score.data.size() > MAX_SCORE_BYTES) {
        return "The score is too large to include (more than 50 MB).";
    }
    if (missing.empty()) {
        return std::string();
    }
    std::string s = "Please add ";
    for (size_t i = 0; i < missing.size(); ++i) {
        s += (i == 0 ? "" : " and ") + missing[i];
    }
    return s + ".";
}

bool isAcceptedScreenshot(const Bytes& d, std::string* reason)
{
    auto fail = [reason](const char* why) {
        if (reason) {
            *reason = why;
        }
        return false;
    };
    if (d.empty()) {
        return fail("the file is empty");
    }
    if (d.size() > MAX_SCREENSHOT_BYTES) {
        return fail("the picture is larger than 10 MB");
    }
    static const uint8_t png[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    const bool isPng = d.size() >= 8 && std::equal(png, png + 8, d.begin());
    const bool isJpeg = d.size() >= 3 && d[0] == 0xFF && d[1] == 0xD8 && d[2] == 0xFF;
    if (!isPng && !isJpeg) {
        return fail("only PNG and JPEG pictures can be added");
    }
    return true;
}

// ---------------------------------------------------------------------------
// Privacy
// ---------------------------------------------------------------------------

std::string redactSecrets(const std::string& text)
{
    std::string s = text;

    // private-key blocks
    static const std::regex pem(R"(-----BEGIN [A-Z0-9 ]*PRIVATE KEY-----[\s\S]*?-----END [A-Z0-9 ]*PRIVATE KEY-----)");
    s = std::regex_replace(s, pem, "[private key removed]");

    // user:password@ in links
    static const std::regex urlCredentials(R"(([A-Za-z][A-Za-z0-9+.-]*://)[^/\s:@]+:[^/\s@]+@)");
    s = std::regex_replace(s, urlCredentials, "$1[redacted]@");

    // Authorization headers (the scheme and the value)
    static const std::regex authHeader(R"(\b(Proxy-Authorization|Authorization)\s*:\s*[^\r\n]+)", std::regex::icase);
    s = std::regex_replace(s, authHeader, "$1: [redacted]");

    static const std::string names
        = R"((?:password|passwd|passphrase|pwd|pass|secret|client_secret|token|access_token|refresh_token|id_token|auth_token|)"
          R"(api[_-]?key|apikey|app[_-]?key|private[_-]?key|signing[_-]?key|credentials?|session[_-]?id|cookie|set-cookie))";

    // "password": "value" (JSON and similar)
    static const std::regex quotedPair("(\"" + names + "\"\\s*:\\s*)\"[^\"]*\"", std::regex::icase);
    s = std::regex_replace(s, quotedPair, "$1\"[redacted]\"");

    // password=value, token: value, api_key = 'value' (also in links)
    static const std::regex pair("\\b(" + names + ")(\\s*[:=]\\s*)(\"[^\"]*\"|'[^']*'|[^\\s,;&\"']+)", std::regex::icase);
    s = std::regex_replace(s, pair, "$1$2[redacted]");

    // well-known token formats
    static const std::regex tokens(
        R"(\b(ya29\.[A-Za-z0-9_-]{10,}|1//[A-Za-z0-9_-]{20,}|AKIA[0-9A-Z]{16}|ASIA[0-9A-Z]{16}|AIza[0-9A-Za-z_-]{30,}|)"
        R"(sk-[A-Za-z0-9_-]{20,}|xox[abprs]-[A-Za-z0-9-]{10,}|glpat-[A-Za-z0-9_-]{20,}|gh[pousr]_[A-Za-z0-9]{20,}|)"
        R"(github_pat_[A-Za-z0-9_]{20,}|eyJ[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}))");
    s = std::regex_replace(s, tokens, "[token]");

    return s;
}

std::string cleanDiagnosticText(const std::string& text, const PrivacyContext& privacy)
{
    return redactSecrets(sanitize(redactSecrets(text), privacy.homeDir, privacy.userName));
}

std::string safeFileName(const std::string& name, const std::string& fallback)
{
    std::string out;
    for (unsigned char c : name) {
        if (std::isalnum(c) || c == '.' || c == '-' || c == '_') {
            out += char(c);
        } else if (c == ' ' && !out.empty() && out.back() != '_') {
            out += '_';
        }
        if (out.size() >= 60) {
            break;
        }
    }
    while (!out.empty() && (out.front() == '.' || out.front() == '_')) {   // no hidden files, no ".."
        out.erase(out.begin());
    }
    while (!out.empty() && (out.back() == '.' || out.back() == '_')) {
        out.pop_back();
    }
    return out.empty() ? fallback : out;
}

// ---------------------------------------------------------------------------
// The report
// ---------------------------------------------------------------------------

BugReport assemble(const BugReportInput& input, const RawDiagnostics& raw, const PrivacyContext& privacy,
                   const std::string& createdAtIso, const std::string& id)
{
    BugReport r;
    r.id = id;
    r.createdAt = createdAtIso;
    r.input = input;

    // your own words: passwords and keys removed, nothing else changed
    for (std::string* f : { &r.input.title, &r.input.description, &r.input.steps, &r.input.expected, &r.input.actual,
                            &r.input.comments }) {
        *f = redactSecrets(trim(*f));
    }

    if (!r.input.includeScore) {
        r.input.score = Attachment();   // never kept unless chosen
    }

    for (DiagnosticSection s : raw.sections) {
        s.title = cleanDiagnosticText(s.title, privacy);
        for (auto& item : s.items) {
            item.label = cleanDiagnosticText(item.label, privacy);
            item.value = cleanDiagnosticText(item.value, privacy);
        }
        r.diagnostics.push_back(std::move(s));
    }
    std::stable_sort(r.diagnostics.begin(), r.diagnostics.end(), [](const DiagnosticSection& a, const DiagnosticSection& b) {
        return a.order < b.order;
    });

    if (input.includeLogs) {
        r.logTail = cleanDiagnosticText(raw.logTail, privacy);
        r.lastCrash = cleanDiagnosticText(raw.lastCrash, privacy);
    }
    return r;
}

std::string toMarkdown(const BugReport& r)
{
    std::ostringstream o;
    o << "# " << (r.input.title.empty() ? std::string("Bug report") : r.input.title) << "\n\n";
    o << "Jubelle bug report " << r.id << ", made " << r.createdAt << "\n\n";
    o << "## What happened\n\n" << orNone(r.input.description) << "\n\n";
    o << "## Steps to reproduce\n\n" << orNone(r.input.steps) << "\n\n";
    o << "## Expected result\n\n" << orNone(r.input.expected) << "\n\n";
    o << "## Actual result\n\n" << orNone(r.input.actual) << "\n\n";
    if (!trim(r.input.comments).empty()) {
        o << "## Comments\n\n" << r.input.comments << "\n\n";
    }

    o << "## Attachments\n\n";
    if (r.input.screenshots.empty() && !scoreIncluded(r)) {
        o << "_(none)_\n";
    }
    for (size_t i = 0; i < r.input.screenshots.size(); ++i) {
        o << "- " << screenshotName(i, r.input.screenshots[i]) << "\n";
    }
    if (scoreIncluded(r)) {
        o << "- " << scoreName(r.input.score) << " (included because you chose to)\n";
    }
    o << "\n## System and Jubelle\n\n";
    for (const DiagnosticSection& s : r.diagnostics) {
        o << "### " << s.title << "\n\n";
        for (const auto& item : s.items) {
            o << "- " << item.label << ": " << item.value << "\n";
        }
        o << "\n";
    }
    if (!r.input.includeLogs) {
        o << "## Log\n\n_(not included)_\n";
    } else {
        if (!trim(r.lastCrash).empty()) {
            o << "## Last crash\n\n```\n" << fenceSafe(r.lastCrash) << (endsWith(r.lastCrash, "\n") ? "" : "\n") << "```\n\n";
        }
        o << "## End of the log\n\n";
        if (trim(r.logTail).empty()) {
            o << "_(empty)_\n";
        } else {
            o << "```\n" << fenceSafe(r.logTail) << (endsWith(r.logTail, "\n") ? "" : "\n") << "```\n";
        }
    }
    return o.str();
}

std::string toJson(const BugReport& r)
{
    using namespace muse;
    JsonObject root;
    root.set("schema", REPORT_SCHEMA);
    root.set("id", r.id);
    root.set("createdAt", r.createdAt);

    JsonObject report;
    report.set("title", r.input.title);
    report.set("description", r.input.description);
    report.set("steps", r.input.steps);
    report.set("expected", r.input.expected);
    report.set("actual", r.input.actual);
    report.set("comments", r.input.comments);
    root.set("report", report);

    JsonObject attachments;
    JsonArray shots;
    for (size_t i = 0; i < r.input.screenshots.size(); ++i) {
        shots.append(screenshotName(i, r.input.screenshots[i]));
    }
    attachments.set("screenshots", shots);
    if (scoreIncluded(r)) {
        attachments.set("score", scoreName(r.input.score));
    } else {
        attachments.set("score", JsonValue());
    }
    attachments.set("logIncluded", r.input.includeLogs);
    root.set("attachments", attachments);

    JsonArray sections;
    for (const DiagnosticSection& s : r.diagnostics) {
        JsonObject so;
        so.set("id", s.id);
        so.set("title", s.title);
        JsonArray items;
        for (const auto& item : s.items) {
            JsonObject io;
            io.set("label", item.label);
            io.set("value", item.value);
            items.append(io);
        }
        so.set("items", items);
        sections.append(so);
    }
    root.set("diagnostics", sections);

    const ByteArray json = JsonDocument(root).toJson(JsonDocument::Format::Indented);
    return std::string(reinterpret_cast<const char*>(json.constData()), json.size());
}

std::vector<std::string> packageContents(const BugReport& r)
{
    std::vector<std::string> files = { "report.md", "report.json" };
    if (r.input.includeLogs) {
        files.push_back("diagnostics/log.txt");
        if (!trim(r.lastCrash).empty()) {
            files.push_back("diagnostics/last-crash.txt");
        }
    }
    for (size_t i = 0; i < r.input.screenshots.size(); ++i) {
        files.push_back(screenshotName(i, r.input.screenshots[i]));
    }
    if (scoreIncluded(r)) {
        files.push_back(scoreName(r.input.score));
    }
    return files;
}

Bytes makePackage(const BugReport& r)
{
    muse::ByteArray zipData;
    {
        muse::io::Buffer buf(&zipData);
        buf.open(muse::io::IODevice::WriteOnly);
        muse::ZipWriter zip(&buf);
        auto text = [&zip](const std::string& name, const std::string& content) {
            zip.addFile(name, muse::ByteArray(content.data(), content.size()));
        };
        auto bytes = [&zip](const std::string& name, const Bytes& content) {
            zip.addFile(name, muse::ByteArray(content.data(), content.size()));
        };
        text("report.md", toMarkdown(r));
        text("report.json", toJson(r));
        if (r.input.includeLogs) {
            text("diagnostics/log.txt", r.logTail);
            if (!trim(r.lastCrash).empty()) {
                text("diagnostics/last-crash.txt", r.lastCrash);
            }
        }
        for (size_t i = 0; i < r.input.screenshots.size(); ++i) {
            bytes(screenshotName(i, r.input.screenshots[i]), r.input.screenshots[i].data);
        }
        if (scoreIncluded(r)) {
            bytes(scoreName(r.input.score), r.input.score.data);
        }
        zip.close();
        if (zip.hasError()) {
            return Bytes();
        }
    }
    return Bytes(zipData.constData(), zipData.constData() + zipData.size());
}

// ---------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------

bool writeFileSafely(const std::string& path, const Bytes& data, std::string* error)
{
    auto fail = [error](const std::string& why) {
        if (error) {
            *error = why;
        }
        return false;
    };
    std::error_code ec;
    const fs::path target = fs::u8path(path);
    if (fs::exists(target, ec)) {
        return fail("a file with this name already exists");
    }
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
    }
    const fs::path partial = fs::u8path(path + ".partial");
    {
        std::ofstream out(partial, std::ios::binary | std::ios::trunc);
        if (!out) {
            return fail("the file could not be created");
        }
        // only you can read it (where the system has such permissions)
        fs::permissions(partial, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::replace, ec);
        out.write(reinterpret_cast<const char*>(data.data()), std::streamsize(data.size()));
        out.flush();
        if (!out) {
            out.close();
            fs::remove(partial, ec);
            return fail("the file could not be written (is the disk full?)");
        }
    }
    fs::rename(partial, target, ec);
    if (ec) {
        fs::remove(partial, ec);
        return fail("the file could not be saved");
    }
    return true;
}

std::string readNewestLogTail(const std::string& logsDir, size_t lines)
{
    std::error_code ec;
    fs::path newest;
    fs::file_time_type newestTime {};
    for (const auto& e : fs::directory_iterator(fs::u8path(logsDir), ec)) {
        std::error_code e1;
        if (!e.is_regular_file(e1) || e.path().extension() != ".log") {
            continue;
        }
        const auto t = e.last_write_time(e1);
        if (newest.empty() || t > newestTime) {
            newest = e.path();
            newestTime = t;
        }
    }
    if (newest.empty()) {
        return std::string();
    }
    std::ifstream in(newest, std::ios::binary);
    if (!in) {
        return std::string();
    }
    // only the end of the file is read
    const std::streamoff window = 512 * 1024;
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    in.seekg(size > window ? size - window : 0);
    std::string chunk((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<std::string> all;
    std::istringstream s(chunk);
    std::string line;
    while (std::getline(s, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        all.push_back(line);
    }
    if (size > window && !all.empty()) {
        all.erase(all.begin());   // the first line may be cut
    }
    std::string out;
    for (size_t i = all.size() > lines ? all.size() - lines : 0; i < all.size(); ++i) {
        out += all[i] + '\n';
    }
    return out;
}

std::string makeReportId(const std::string& iso, unsigned randomPart)
{
    std::string digits;
    for (char c : iso) {
        if (c >= '0' && c <= '9') {
            digits += c;
        }
        if (digits.size() == 14) {
            break;
        }
    }
    while (digits.size() < 14) {
        digits += '0';
    }
    char tail[8];
    std::snprintf(tail, sizeof(tail), "%04x", randomPart & 0xFFFFu);
    return digits.substr(0, 8) + "-" + digits.substr(8, 6) + "-" + tail;
}

// ---------------------------------------------------------------------------
// Submitters
// ---------------------------------------------------------------------------

SaveToFolderSubmitter::SaveToFolderSubmitter(std::string folder)
    : m_folder(std::move(folder))
{
}

SubmitResult SaveToFolderSubmitter::submit(const BugReport& report)
{
    SubmitResult res;
    const Bytes zip = makePackage(report);
    if (zip.empty()) {
        res.error = "the package could not be made";
        return res;
    }
    std::string base = (fs::u8path(m_folder) / ("Jubelle-bug-report-" + safeFileName(report.id, "report"))).u8string();
    std::string path = base + ".zip";
    std::error_code ec;
    for (int n = 2; fs::exists(fs::u8path(path), ec) && n < 100; ++n) {
        path = base + "-" + std::to_string(n) + ".zip";
    }
    std::string err;
    if (!writeFileSafely(path, zip, &err)) {
        res.error = err;
        return res;
    }
    res.ok = true;
    res.location = path;
    return res;
}
}
