/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore). R3 crash reporting (D7).
 */
#pragma once

#include <string>

namespace mu::appshell::jubelle {
//! Folder for crash reports and the session marker: <app data>/jubelle-crash
struct CrashGuard {
    //! At start: keeps last time's report (or notes an unexpected shutdown),
    //! marks this session open, installs the crash handler
    static void start(const std::string& appDataDir, const std::string& version, const std::string& os);
    //! At a normal close
    static void stop(const std::string& appDataDir);

    static std::string folder(const std::string& appDataDir);
    static std::string pendingReport(const std::string& appDataDir);   // "" if none
    static std::string sentSignaturesFile(const std::string& appDataDir);
};
}
