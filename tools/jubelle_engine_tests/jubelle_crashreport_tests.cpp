/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

// Jubelle R3 crash reporting (D7): private, cleaned, sent only by you.
#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

#include "appshell/internal/jubelle/crashreport.h"

using namespace mu::appshell::jubelle;
namespace fs = std::filesystem;

namespace {
std::string readAll(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

fs::path tempDir(const char* name)
{
    const fs::path d = fs::temp_directory_path() / name;
    fs::remove_all(d);
    fs::create_directories(d);
    return d;
}
}

TEST(Jubelle_CrashReport, PersonalDetailsAreRemoved)
{
    const std::string home = "C:\\Users\\Nick Smith";
    const std::string text
        = "Opening C:\\Users\\Nick Smith\\Documents\\Jubelle Projects\\Big Band\\Blues for Nick\\Blues for Nick.mscz\n"
          "also C:/Users/Nick Smith/AppData/Local/Jubelle/logs\n"
          "saved /home/nick/Scores/Secret Song.mscx and \\\\server\\share\\x\n"
          "exported My Ballad.pdf; user Nick Smith; mail nick.smith@example.com\n"
          "token ghp_ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 and Bearer abcdefghijklmnopqrstuvwxyz\n"
          "key 0123456789abcdef0123456789abcdef0123456789abcdef\n"
          "frame Jubelle.exe+0x1a2b3c\nframe Qt6Core.dll+0x99\n";
    const std::string clean = sanitize(text, home, "Nick Smith");
    for (const char* secret : { "Nick", "Users", "Blues for", "Secret Song", "My Ballad", "example.com", "ghp_", "abcdefghijklmnop",
                                "0123456789abcdef0123", "server", "Jubelle Projects", "AppData" }) {
        EXPECT_EQ(clean.find(secret), std::string::npos) << secret << "\n" << clean;
    }
    EXPECT_NE(clean.find("Jubelle.exe+0x1a2b3c"), std::string::npos) << "where it crashed stays";
    EXPECT_NE(clean.find("Qt6Core.dll+0x99"), std::string::npos);
    EXPECT_NE(clean.find("[path]"), std::string::npos);
    EXPECT_NE(clean.find("[email]"), std::string::npos);
    EXPECT_NE(clean.find("[token]"), std::string::npos);
}

TEST(Jubelle_CrashReport, TextRoundTripSignatureAndIssue)
{
    CrashReport r;
    r.version = "4.7.7.261116010";
    r.os = "Windows 11 (10.0.26100)";
    r.kind = "EXCEPTION_ACCESS_VIOLATION";
    r.when = "2026-11-16 10:00:00";
    r.frames = { "Jubelle.exe+0x1a2b3c", "Jubelle.exe+0x4455", "Qt6Core.dll+0x99" };
    r.logTail = "line one C:\\Users\\Nick\\Song.mscz\nline two\n";
    const CrashReport back = CrashReport::fromText(r.toText());
    EXPECT_EQ(back.version, r.version);
    EXPECT_EQ(back.os, r.os);
    EXPECT_EQ(back.kind, r.kind);
    EXPECT_EQ(back.frames, r.frames);
    EXPECT_EQ(back.logTail, r.logTail);
    EXPECT_TRUE(CrashReport::fromText("not a report").frames.empty());

    // The same crash, the same signature (also on another day); a different place, a different one
    CrashReport again = r;
    again.when = "2026-12-01 09:00:00";
    again.logTail.clear();
    EXPECT_EQ(again.signature(), r.signature());
    CrashReport other = r;
    other.frames[0] = "Jubelle.exe+0x777";
    EXPECT_NE(other.signature(), r.signature());
    EXPECT_EQ(r.signature().size(), 8u);

    EXPECT_EQ(r.issueTitle(), "[crash " + r.signature() + "] EXCEPTION_ACCESS_VIOLATION at Jubelle.exe+0x1a2b3c (4.7.7.261116010)");
    const std::string body = r.issueBody("C:\\Users\\Nick", "Nick");
    EXPECT_EQ(body.find("Nick"), std::string::npos);
    EXPECT_EQ(body.find("Song"), std::string::npos);
    EXPECT_NE(body.find("Jubelle.exe+0x4455"), std::string::npos);

    const std::string url = newIssueUrl(CRASH_REPORT_REPOSITORY, r.issueTitle(), body);
    EXPECT_EQ(url.rfind("https://github.com/nlinemusic/jubelle-crash-reports/issues/new?", 0), 0u);
    EXPECT_EQ(url.find(' '), std::string::npos);
    EXPECT_EQ(url.find('\n'), std::string::npos);
    const std::string longUrl = newIssueUrl(CRASH_REPORT_REPOSITORY, r.issueTitle(), std::string(20000, 'x'));
    EXPECT_LE(longUrl.size(), 7800u) << "short enough for a browser";
    EXPECT_NE(searchIssuesUrl(CRASH_REPORT_REPOSITORY, r.signature()).find(r.signature()), std::string::npos);
}

TEST(Jubelle_CrashReport, SessionMarkerAndSentSignatures)
{
    const fs::path d = tempDir("jubelle_crash_marker");
    const std::string marker = (d / "session-open.txt").string();
    EXPECT_FALSE(sessionWasLeftOpen(marker));
    markSessionOpen(marker, "4.7.7.1");
    std::string v;
    EXPECT_TRUE(sessionWasLeftOpen(marker, &v));
    EXPECT_EQ(v, "4.7.7.1");
    markSessionClosed(marker);
    EXPECT_FALSE(sessionWasLeftOpen(marker));

    const std::string sent = (d / "sent.txt").string();
    EXPECT_TRUE(readSentSignatures(sent).empty());
    rememberSentSignature(sent, "1a2b3c4d");
    rememberSentSignature(sent, "1a2b3c4d");
    rememberSentSignature(sent, "deadbeef");
    EXPECT_EQ(readSentSignatures(sent), (std::set<std::string> { "1a2b3c4d", "deadbeef" }));
    fs::remove_all(d);
}

#ifndef _WIN32
//! A real crash, in a child process: the handler writes a report Jubelle can read
TEST(Jubelle_CrashReport, ARealCrashWritesAReport)
{
    const fs::path d = tempDir("jubelle_crash_real");
    const std::string report = (d / "crash-report.txt").string();
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        installCrashHandler(report, "4.7.7.42", "Linux test");
        volatile int* nothing = nullptr;
        *nothing = 1;   // crash
        _exit(0);
    }
    int status = 0;
    waitpid(child, &status, 0);
    EXPECT_TRUE(WIFSIGNALED(status)) << "it still crashes as before (the handler doesn't hide it)";
    const CrashReport r = CrashReport::fromText(readAll(report));
    EXPECT_EQ(r.version, "4.7.7.42");
    EXPECT_NE(r.kind.find("SIGSEGV"), std::string::npos);
    EXPECT_FALSE(r.frames.empty());
    for (const std::string& f : r.frames) {
        EXPECT_EQ(f.find('/'), std::string::npos) << "module names only, never folders: " << f;
    }
    fs::remove_all(d);
}
#endif
