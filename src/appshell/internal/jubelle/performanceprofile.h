/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#pragma once

// Jubelle: performance profiles (Preferences › General › Performance).
//
// A profile sets real settings:
//  - the audio buffer (smaller = lower latency, more work for the processor);
//  - the SoundFont low-pass filter (a little extra work per note);
//  - how often scores are saved automatically (each save briefly uses the disk).
// Automatic picks High performance, Balanced or Low resource from the number of
// processor cores and the memory of this computer, at each start. A setting you
// change yourself afterwards is kept: Automatic only replaces values it set.
//
// No Qt, so it is tested on its own.

#include <string>

namespace mu::appshell::jubelle {
enum class PerformanceProfile {
    Automatic = 0,
    HighPerformance,
    Balanced,
    LowResource,
};

struct MachineInfo {
    unsigned cores = 0;
    double memoryGb = 0.0;
};

struct ProfileSettings {
    PerformanceProfile resolved = PerformanceProfile::Balanced;   // never Automatic
    unsigned audioBufferSamples = 512;
    bool soundFontLowPass = true;
    int autosaveMinutes = 3;

    //! "512,1,3": what was applied, to tell later whether you changed it yourself
    std::string fingerprint() const;
};

//! What a profile sets on this machine
ProfileSettings settingsFor(PerformanceProfile profile, const MachineInfo& machine);

//! This computer's cores and memory
MachineInfo currentMachine();

//! True when the current values are still the ones Jubelle applied (or nothing was applied yet)
bool unchangedSinceApplied(const std::string& appliedFingerprint, unsigned buffer, bool lowPass, int autosaveMinutes);

//! Untranslated plain names, for the stored setting and the tests
const char* profileName(PerformanceProfile p);
}
