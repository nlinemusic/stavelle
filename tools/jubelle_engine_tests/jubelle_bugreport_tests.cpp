/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

// Report a Bug: the report, its privacy rules and the package.
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "appshell/internal/jubelle/bugreport/bugreport.h"
#include "global/io/buffer.h"
#include "global/jubelle/diagnosticsregistry.h"
#include "global/serialization/json.h"
#include "global/serialization/zipreader.h"

using namespace mu::appshell::jubelle::bugreport;
using muse::jubelle::DiagnosticSection;
using muse::jubelle::DiagnosticsRegistry;
namespace fs = std::filesystem;

namespace {
fs::path tempDir(const char* name)
{
    const fs::path d = fs::temp_directory_path() / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}

Bytes png(size_t extra = 16)
{
    Bytes b = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    b.resize(b.size() + extra, 0x42);
    return b;
}

BugReportInput sampleInput()
{
    BugReportInput in;
    in.title = "Slur disappears after undo";
    in.description = "When I undo a slur in bar 3 the next slur vanishes too.";
    in.steps = "1. Add two slurs\n2. Undo once";
    in.expected = "Only the last slur is removed";
    in.actual = "Both slurs are removed";
    return in;
}

RawDiagnostics sampleRaw()
{
    RawDiagnostics raw;
    DiagnosticSection app { "application", "Jubelle", { { "Version", "4.7.7.261118172" }, { "Channel", "testing" } }, 10 };
    DiagnosticSection engine { "engine", "Notation engine", { { "Engine", "MuseScore Studio 4.7.7 engine (current)" } }, 30 };
    raw.sections = { engine, app };
    raw.logTail
        = "12:00 | INFO | opened C:\\Users\\Nick Smith\\Documents\\Scores\\Blues for Nick.mscz\n"
          "12:01 | INFO | login password=hunter2 token: abcdefghijkl api_key='sk-abcdefghijklmnopqrstuvwxyz'\n"
          "12:02 | INFO | Authorization: Bearer abc.def.ghi\n"
          "12:03 | INFO | mail nick.smith@example.com, cloud ya29.a0AfH6SMBxyzxyzxyzxyz\n"
          "12:04 | INFO | {\"refresh_token\": \"1//0gabcdefghijklmnopqrstuvwxyz\", \"client_secret\": \"GOCSPX-abc\"}\n"
          "12:05 | INFO | https://nick:supersecret@example.com/path\n";
    raw.lastCrash = "kind: EXCEPTION_ACCESS_VIOLATION\nframe: Jubelle.exe+0x1a2b\nuser Nick Smith\n";
    return raw;
}

PrivacyContext privacy()
{
    return { "C:\\Users\\Nick Smith", "Nick Smith" };
}

std::map<std::string, std::string> unzip(const Bytes& zip)
{
    muse::io::Buffer buf(zip.data(), zip.size());
    buf.open(muse::io::IODevice::ReadOnly);
    muse::ZipReader reader(&buf);
    std::map<std::string, std::string> files;
    for (const auto& info : reader.fileInfoList()) {
        if (info.isFile) {
            const muse::ByteArray d = reader.fileData(info.filePath.toStdString());
            files[info.filePath.toStdString()] = std::string(reinterpret_cast<const char*>(d.constData()), d.size());
        }
    }
    return files;
}

const std::vector<std::string> SECRETS = {
    "hunter2", "abcdefghijkl", "sk-abcdefghijklmnopqrstuvwxyz", "abc.def.ghi", "ya29.", "1//0gabcdef",
    "GOCSPX-abc", "supersecret", "nick.smith@example.com", "Nick Smith", "Blues for Nick", "C:\\Users",
};
}

TEST(Jubelle_BugReport, RequiredFieldsAreChecked)
{
    BugReportInput in;
    EXPECT_NE(validate(in).find("a title"), std::string::npos);
    EXPECT_NE(validate(in).find("description"), std::string::npos);
    in.title = "  ";
    in.description = "x";
    EXPECT_NE(validate(in).find("a title"), std::string::npos);
    EXPECT_EQ(validate(sampleInput()), "");
}

TEST(Jubelle_BugReport, OnlyPngAndJpegScreenshots)
{
    std::string why;
    EXPECT_TRUE(isAcceptedScreenshot(png(), &why));
    EXPECT_TRUE(isAcceptedScreenshot({ 0xFF, 0xD8, 0xFF, 0xE0, 0, 0 }, &why));
    EXPECT_FALSE(isAcceptedScreenshot({ 'P', 'K', 3, 4 }, &why));         // a ZIP (or a score) renamed .png
    EXPECT_FALSE(isAcceptedScreenshot({}, &why));
    EXPECT_FALSE(isAcceptedScreenshot(Bytes(MAX_SCREENSHOT_BYTES + 1, 0xFF), &why));

    BugReportInput in = sampleInput();
    in.screenshots.push_back({ "fake.png", { 'M', 'Z', 0, 0 } });
    EXPECT_NE(validate(in).find("fake.png"), std::string::npos);
    in.screenshots.clear();
    for (size_t i = 0; i <= MAX_SCREENSHOTS; ++i) {
        in.screenshots.push_back({ "s.png", png() });
    }
    EXPECT_NE(validate(in).find("At most"), std::string::npos);
}

TEST(Jubelle_BugReport, SecretsAreRedacted)
{
    const std::string out = redactSecrets(
        "password=hunter2 PASSWD: \"open sesame\" token=abc&x=1 api-key = 'zzz'\n"
        "Authorization: Basic dXNlcjpwYXNz\n"
        "{\"access_token\": \"secret-value\", \"name\": \"keep me\"}\n"
        "-----BEGIN OPENSSH PRIVATE KEY-----\nAAAAB3Nza\n-----END OPENSSH PRIVATE KEY-----\n"
        "AKIAABCDEFGHIJKLMNOP eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NTY3In0.abcdefghijkl ghp_abcdefghijklmnopqrstuvwx\n"
        "The test passes and the tokenizer works.");
    for (const char* s : { "hunter2", "open sesame", "abc&", "zzz", "dXNlcjpwYXNz", "secret-value", "AAAAB3Nza", "AKIAABCDEFGHIJKLMNOP",
                           "eyJhbGciOiJIUzI1NiJ9", "ghp_abc" }) {
        EXPECT_EQ(out.find(s), std::string::npos) << s << "\n" << out;
    }
    // ordinary words stay
    EXPECT_NE(out.find("\"name\": \"keep me\""), std::string::npos);
    EXPECT_NE(out.find("The test passes and the tokenizer works."), std::string::npos);
    EXPECT_NE(out.find("x=1"), std::string::npos);
}

TEST(Jubelle_BugReport, AutomaticDetailsAreCleaned)
{
    const BugReport r = assemble(sampleInput(), sampleRaw(), privacy(), "2026-11-18T15:30:12Z", "20261118-153012-00aa");
    const std::string all = toMarkdown(r) + toJson(r) + r.logTail + r.lastCrash;
    for (const std::string& s : SECRETS) {
        EXPECT_EQ(all.find(s), std::string::npos) << s;
    }
    // what helps fixing stays
    EXPECT_NE(all.find("4.7.7.261118172"), std::string::npos);
    EXPECT_NE(all.find("EXCEPTION_ACCESS_VIOLATION"), std::string::npos);
    EXPECT_NE(all.find("Jubelle.exe+0x1a2b"), std::string::npos);
    EXPECT_NE(all.find("MuseScore Studio 4.7.7 engine (current)"), std::string::npos);
}

TEST(Jubelle_BugReport, YourTextKeepsItsWordsButNotItsPasswords)
{
    BugReportInput in = sampleInput();
    in.comments = "My cloud login password: hunter2 failed, see bar 3 of my score";
    const BugReport r = assemble(in, {}, privacy(), "2026-11-18T15:30:12Z", "x");
    EXPECT_EQ(r.input.comments.find("hunter2"), std::string::npos);
    EXPECT_NE(r.input.comments.find("see bar 3 of my score"), std::string::npos);
    EXPECT_EQ(r.input.title, "Slur disappears after undo");
}

TEST(Jubelle_BugReport, ScoreIsNeverIncludedUnlessChosen)
{
    BugReportInput in = sampleInput();
    in.score = { "My Secret Song.mscz", { 'P', 'K', 3, 4, 1, 2, 3 } };   // given, but not chosen
    ASSERT_FALSE(in.includeScore);   // off by default
    BugReport r = assemble(in, sampleRaw(), privacy(), "2026-11-18T15:30:12Z", "id1");
    EXPECT_TRUE(r.input.score.data.empty());
    for (const std::string& f : packageContents(r)) {
        EXPECT_EQ(f.rfind("score/", 0), std::string::npos) << f;
    }
    auto files = unzip(makePackage(r));
    for (const auto& f : files) {
        EXPECT_EQ(f.first.rfind("score/", 0), std::string::npos) << f.first;
    }
    EXPECT_EQ(toMarkdown(r).find("Secret Song"), std::string::npos);

    in.includeScore = true;   // chosen
    r = assemble(in, sampleRaw(), privacy(), "2026-11-18T15:30:12Z", "id2");
    files = unzip(makePackage(r));
    ASSERT_EQ(files.count("score/My_Secret_Song.mscz"), 1u);
    EXPECT_EQ(files["score/My_Secret_Song.mscz"].size(), 7u);
}

TEST(Jubelle_BugReport, PackageHoldsWhatThePreviewSays)
{
    BugReportInput in = sampleInput();
    in.screenshots = { { "../../evil name.png", png() }, { "jpeg shot", { 0xFF, 0xD8, 0xFF, 0xE0, 1 } } };
    const BugReport r = assemble(in, sampleRaw(), privacy(), "2026-11-18T15:30:12Z", "20261118-153012-00aa");
    const std::vector<std::string> listed = packageContents(r);
    const auto files = unzip(makePackage(r));
    std::set<std::string> inZip;
    for (const auto& f : files) {
        inZip.insert(f.first);
        EXPECT_EQ(f.first.find(".."), std::string::npos) << f.first;   // nothing can land outside the folder
    }
    EXPECT_EQ(inZip, std::set<std::string>(listed.begin(), listed.end()));
    EXPECT_EQ(inZip.count("screenshots/01-evil_name.png"), 1u);
    EXPECT_EQ(inZip.count("screenshots/02-jpeg_shot.jpg"), 1u);
    EXPECT_EQ(inZip.count("diagnostics/log.txt"), 1u);
    EXPECT_EQ(inZip.count("diagnostics/last-crash.txt"), 1u);
    EXPECT_EQ(files.at("report.md"), toMarkdown(r));

    // the JSON is readable and says what it is
    const std::string json = files.at("report.json");
    std::string err;
    const muse::JsonDocument doc = muse::JsonDocument::fromJson(muse::ByteArray(json.data(), json.size()), &err);
    ASSERT_TRUE(err.empty()) << err;
    const muse::JsonObject root = doc.rootObject();
    EXPECT_EQ(root.value("schema").toStdString(), REPORT_SCHEMA);
    EXPECT_EQ(root.value("report").toObject().value("title").toStdString(), "Slur disappears after undo");
    EXPECT_TRUE(root.value("attachments").toObject().value("score").isNull());
    EXPECT_EQ(root.value("diagnostics").toArray().size(), 2u);
    // sorted by order: Jubelle first, engine second
    EXPECT_EQ(root.value("diagnostics").toArray().at(0).toObject().value("id").toStdString(), "application");
}

TEST(Jubelle_BugReport, LogsCanBeLeftOut)
{
    BugReportInput in = sampleInput();
    in.includeLogs = false;
    const BugReport r = assemble(in, sampleRaw(), privacy(), "2026-11-18T15:30:12Z", "id");
    EXPECT_TRUE(r.logTail.empty());
    EXPECT_TRUE(r.lastCrash.empty());
    const auto files = unzip(makePackage(r));
    EXPECT_EQ(files.count("diagnostics/log.txt"), 0u);
    EXPECT_NE(toMarkdown(r).find("_(not included)_"), std::string::npos);
}

TEST(Jubelle_BugReport, MarkdownCannotBeBrokenByCodeFences)
{
    RawDiagnostics raw = sampleRaw();
    raw.logTail = "line ```\n## injected heading\n";
    const BugReport r = assemble(sampleInput(), raw, privacy(), "t", "id");
    const std::string md = toMarkdown(r);
    EXPECT_EQ(md.find("line ```"), std::string::npos);
}

TEST(Jubelle_BugReport, FilesAreWrittenSafely)
{
    const fs::path dir = tempDir("jubelle_bugreport_write");
    const std::string path = (dir / "a.zip").u8string();
    std::string err;
    ASSERT_TRUE(writeFileSafely(path, { 1, 2, 3 }, &err)) << err;
    EXPECT_FALSE(fs::exists(path + ".partial"));
    EXPECT_EQ(fs::file_size(path), 3u);
    // never overwrites
    EXPECT_FALSE(writeFileSafely(path, { 9 }, &err));
    EXPECT_EQ(fs::file_size(path), 3u);
#ifndef _WIN32
    const fs::perms p = fs::status(path).permissions();
    EXPECT_EQ(p & (fs::perms::group_all | fs::perms::others_all), fs::perms::none);
#endif
}

TEST(Jubelle_BugReport, SaveToFolderSubmitterMakesTheZip)
{
    const fs::path dir = tempDir("jubelle_bugreport_submit");
    SaveToFolderSubmitter submitter(dir.u8string());
    EXPECT_TRUE(submitter.available());
    const BugReport r = assemble(sampleInput(), sampleRaw(), privacy(), "2026-11-18T15:30:12Z", "20261118-153012-00aa");
    const SubmitResult a = submitter.submit(r);
    ASSERT_TRUE(a.ok) << a.error;
    EXPECT_EQ(fs::path(a.location).filename().u8string(), "Jubelle-bug-report-20261118-153012-00aa.zip");
    const SubmitResult b = submitter.submit(r);   // same id again: a new name, nothing overwritten
    ASSERT_TRUE(b.ok) << b.error;
    EXPECT_NE(a.location, b.location);
}

TEST(Jubelle_BugReport, NewestLogTailIsRead)
{
    const fs::path dir = tempDir("jubelle_bugreport_logs");
    {
        std::ofstream(dir / "Jubelle_261117_090000.log") << "old\n";
    }
    fs::last_write_time(dir / "Jubelle_261117_090000.log", fs::file_time_type::clock::now() - std::chrono::hours(5));
    {
        std::ofstream out(dir / "Jubelle_261118_090000.log");
        for (int i = 0; i < 1000; ++i) {
            out << "line " << i << "\r\n";
        }
    }
    {
        std::ofstream(dir / "notes.txt") << "not a log\n";
    }
    const std::string tail = readNewestLogTail(dir.u8string(), 3);
    EXPECT_EQ(tail, "line 997\nline 998\nline 999\n");
    EXPECT_EQ(readNewestLogTail((dir / "missing").u8string()), "");
}

TEST(Jubelle_BugReport, SafeNamesAndIds)
{
    EXPECT_EQ(safeFileName("../../etc/passwd", "x"), "etcpasswd");
    EXPECT_EQ(safeFileName("...", "fallback"), "fallback");
    EXPECT_EQ(safeFileName("My Song (final)", "x"), "My_Song_final");
    EXPECT_LE(safeFileName(std::string(500, 'a'), "x").size(), 60u);
    EXPECT_EQ(makeReportId("2026-11-18T15:30:12Z", 0x4f2a), "20261118-153012-4f2a");
}

TEST(Jubelle_DiagnosticsRegistry, AnyEngineCanDescribeItself)
{
    DiagnosticsRegistry reg;
    reg.add("engine", [] {
        return DiagnosticSection { "engine", "Notation engine", { { "Engine", "MuseScore Studio 4.7.7 engine (current)" } }, 30 };
    });
    reg.add("application", [] { return DiagnosticSection { "application", "Jubelle", { { "Version", "1" } }, 10 }; });
    reg.add("broken", []() -> DiagnosticSection { throw std::runtime_error("x"); });
    auto all = reg.collectAll();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[0].id, "application");
    EXPECT_EQ(all[1].items[0].value, "MuseScore Studio 4.7.7 engine (current)");
    EXPECT_EQ(all[2].id, "broken");   // a failing source is reported, never stops the report

    // a future engine replaces the entry under the same id
    reg.add("engine", [] {
        return DiagnosticSection { "engine", "Notation engine", { { "Engine", "Jubelle engine 1 (future)" } }, 30 };
    });
    all = reg.collectAll();
    ASSERT_EQ(all.size(), 3u);
    EXPECT_EQ(all[1].items[0].value, "Jubelle engine 1 (future)");
    reg.remove("broken");
    EXPECT_FALSE(reg.has("broken"));
}
