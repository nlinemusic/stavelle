/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore). R3 crash reporting (D7).
 */
#include "crashguard.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include "crashreport.h"

namespace fs = std::filesystem;

namespace mu::appshell::jubelle {
std::string CrashGuard::folder(const std::string& appDataDir)
{
    return (fs::u8path(appDataDir) / "jubelle-crash").u8string();
}

std::string CrashGuard::sentSignaturesFile(const std::string& appDataDir)
{
    return (fs::u8path(folder(appDataDir)) / "sent-signatures.txt").u8string();
}

std::string CrashGuard::pendingReport(const std::string& appDataDir)
{
    const fs::path p = fs::u8path(folder(appDataDir)) / "pending-report.txt";
    std::error_code ec;
    return fs::exists(p, ec) ? p.u8string() : std::string();
}

// The last lines of the previous session's log (the newest log file but one)
static std::string previousLogTail(const std::string& appDataDir, size_t lines)
{
    std::error_code ec;
    std::vector<fs::directory_entry> logs;
    for (const auto& e : fs::directory_iterator(fs::u8path(appDataDir) / "logs", ec)) {
        if (e.is_regular_file(ec) && e.path().extension() == ".log") {
            logs.push_back(e);
        }
    }
    if (logs.size() < 2) {
        return std::string();
    }
    std::sort(logs.begin(), logs.end(), [](const auto& a, const auto& b) {
        std::error_code e1, e2;
        return a.last_write_time(e1) > b.last_write_time(e2);
    });
    std::ifstream in(logs[1].path());
    std::vector<std::string> all;
    std::string line;
    while (std::getline(in, line)) {
        all.push_back(line);
    }
    std::string out;
    for (size_t i = all.size() > lines ? all.size() - lines : 0; i < all.size(); ++i) {
        out += all[i] + '\n';
    }
    return out;
}

void CrashGuard::start(const std::string& appDataDir, const std::string& version, const std::string& os)
{
    std::error_code ec;
    const fs::path dir = fs::u8path(folder(appDataDir));
    fs::create_directories(dir, ec);
    const fs::path crashFile = dir / "crash-report.txt";
    const fs::path pending = dir / "pending-report.txt";
    const fs::path marker = dir / "session-open.txt";

    std::string lastVersion;
    const bool leftOpen = sessionWasLeftOpen(marker.u8string(), &lastVersion);
    if (fs::exists(crashFile, ec)) {
        // last time's crash: add the end of its log, keep it for the offer
        std::ifstream in(crashFile, std::ios::binary);
        std::stringstream s;
        s << in.rdbuf();
        CrashReport r = CrashReport::fromText(s.str());
        in.close();
        r.logTail = previousLogTail(appDataDir, 40);
        std::ofstream(pending, std::ios::trunc) << r.toText();
        fs::remove(crashFile, ec);
    } else if (leftOpen && !fs::exists(pending, ec)) {
        // closed without a crash report: killed, power loss, or a hang
        CrashReport r;
        r.version = lastVersion.empty() ? version : lastVersion;
        r.os = os;
        r.kind = "Unexpected shutdown (no crash details)";
        const std::time_t now = std::time(nullptr);
        char when[32];
        std::strftime(when, sizeof(when), "%Y-%m-%d", std::localtime(&now));
        r.when = std::string("before ") + when;
        r.logTail = previousLogTail(appDataDir, 40);
        std::ofstream(pending, std::ios::trunc) << r.toText();
    }

    markSessionOpen(marker.u8string(), version);
    installCrashHandler(crashFile.u8string(), version, os);
}

void CrashGuard::stop(const std::string& appDataDir)
{
    markSessionClosed((fs::u8path(folder(appDataDir)) / "session-open.txt").u8string());
}
}
