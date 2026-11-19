/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore). Report a Bug: the automatic
 * details the application itself adds (version, system, hardware), and
 * gathering everything when a report is made. Nothing runs before then.
 */
#pragma once

#include <string>

#include "bugreport.h"

namespace mu::appshell::jubelle::bugreport {
//! Adds the "application", "system" and "hardware" sources
void registerApplicationDiagnostics(const std::string& version, const std::string& build, const std::string& revision);

//! Every source, the end of the newest log and the last crash (raw; assemble() cleans them)
RawDiagnostics gatherDiagnostics(const std::string& appDataDir);

//! Your home folder and user name, so they can be removed
PrivacyContext currentPrivacy();

//! Where reports are saved: <app data>/bug-reports
std::string reportsFolder(const std::string& appDataDir);

//! Now, as 2026-11-18T15:30:12Z
std::string nowIsoUtc();
}
