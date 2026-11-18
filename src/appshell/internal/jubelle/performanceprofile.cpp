/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "performanceprofile.h"

#include <thread>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#else
#include <unistd.h>
#endif

namespace mu::appshell::jubelle {
std::string ProfileSettings::fingerprint() const
{
    return std::to_string(audioBufferSamples) + "," + (soundFontLowPass ? "1" : "0") + "," + std::to_string(autosaveMinutes);
}

ProfileSettings settingsFor(PerformanceProfile profile, const MachineInfo& machine)
{
    PerformanceProfile p = profile;
    if (p == PerformanceProfile::Automatic) {
        if (machine.cores >= 8 && machine.memoryGb >= 15.5) {
            p = PerformanceProfile::HighPerformance;
        } else if (machine.cores >= 4 && machine.memoryGb >= 7.5) {
            p = PerformanceProfile::Balanced;
        } else {
            p = PerformanceProfile::LowResource;
        }
    }
    ProfileSettings s;
    s.resolved = p;
    switch (p) {
    case PerformanceProfile::HighPerformance:
        s.audioBufferSamples = 256;
        s.soundFontLowPass = true;
        s.autosaveMinutes = 2;
        break;
    case PerformanceProfile::LowResource:
        s.audioBufferSamples = 1024;
        s.soundFontLowPass = false;
        s.autosaveMinutes = 5;
        break;
    case PerformanceProfile::Balanced:
    case PerformanceProfile::Automatic:
        s.audioBufferSamples = 512;
        s.soundFontLowPass = true;
        s.autosaveMinutes = 3;
        break;
    }
    return s;
}

MachineInfo currentMachine()
{
    MachineInfo m;
    m.cores = std::thread::hardware_concurrency();
#if defined(_WIN32)
    MEMORYSTATUSEX st;
    st.dwLength = sizeof(st);
    if (GlobalMemoryStatusEx(&st)) {
        m.memoryGb = double(st.ullTotalPhys) / (1024.0 * 1024.0 * 1024.0);
    }
#elif defined(__APPLE__)
    int64_t bytes = 0;
    size_t len = sizeof(bytes);
    if (sysctlbyname("hw.memsize", &bytes, &len, nullptr, 0) == 0) {
        m.memoryGb = double(bytes) / (1024.0 * 1024.0 * 1024.0);
    }
#else
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0) {
        m.memoryGb = double(pages) * double(pageSize) / (1024.0 * 1024.0 * 1024.0);
    }
#endif
    return m;
}

bool unchangedSinceApplied(const std::string& applied, unsigned buffer, bool lowPass, int autosaveMinutes)
{
    if (applied.empty()) {
        return true;
    }
    ProfileSettings now;
    now.audioBufferSamples = buffer;
    now.soundFontLowPass = lowPass;
    now.autosaveMinutes = autosaveMinutes;
    return now.fingerprint() == applied;
}

const char* profileName(PerformanceProfile p)
{
    switch (p) {
    case PerformanceProfile::Automatic: return "Automatic";
    case PerformanceProfile::HighPerformance: return "High performance";
    case PerformanceProfile::Balanced: return "Balanced";
    case PerformanceProfile::LowResource: return "Low resource";
    }
    return "Automatic";
}
}
