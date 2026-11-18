/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */
#include <gtest/gtest.h>

#include "appshell/internal/jubelle/performanceprofile.h"

using namespace mu::appshell::jubelle;

TEST(Jubelle_PerformanceProfile, AutomaticFollowsTheMachine)
{
    EXPECT_EQ(settingsFor(PerformanceProfile::Automatic, { 16, 32.0 }).resolved, PerformanceProfile::HighPerformance);
    EXPECT_EQ(settingsFor(PerformanceProfile::Automatic, { 8, 15.9 }).resolved, PerformanceProfile::HighPerformance);  // "16 GB" machines report a little less
    EXPECT_EQ(settingsFor(PerformanceProfile::Automatic, { 8, 8.0 }).resolved, PerformanceProfile::Balanced);
    EXPECT_EQ(settingsFor(PerformanceProfile::Automatic, { 4, 8.0 }).resolved, PerformanceProfile::Balanced);
    EXPECT_EQ(settingsFor(PerformanceProfile::Automatic, { 2, 8.0 }).resolved, PerformanceProfile::LowResource);
    EXPECT_EQ(settingsFor(PerformanceProfile::Automatic, { 8, 4.0 }).resolved, PerformanceProfile::LowResource);
    EXPECT_EQ(settingsFor(PerformanceProfile::Automatic, { 0, 0.0 }).resolved, PerformanceProfile::LowResource);
}

TEST(Jubelle_PerformanceProfile, ProfilesSetRealValues)
{
    const MachineInfo any { 8, 16.0 };
    const ProfileSettings high = settingsFor(PerformanceProfile::HighPerformance, any);
    const ProfileSettings bal = settingsFor(PerformanceProfile::Balanced, any);
    const ProfileSettings low = settingsFor(PerformanceProfile::LowResource, any);
    EXPECT_LT(high.audioBufferSamples, bal.audioBufferSamples);
    EXPECT_LT(bal.audioBufferSamples, low.audioBufferSamples);
    EXPECT_TRUE(high.soundFontLowPass);
    EXPECT_FALSE(low.soundFontLowPass);
    EXPECT_LT(high.autosaveMinutes, low.autosaveMinutes);
    // a chosen profile ignores the machine
    EXPECT_EQ(settingsFor(PerformanceProfile::LowResource, { 32, 64.0 }).resolved, PerformanceProfile::LowResource);
}

TEST(Jubelle_PerformanceProfile, YourOwnChangesAreKept)
{
    const ProfileSettings bal = settingsFor(PerformanceProfile::Balanced, { 8, 16.0 });
    EXPECT_EQ(bal.fingerprint(), "512,1,3");
    EXPECT_TRUE(unchangedSinceApplied("", 1024, false, 10));          // nothing applied yet
    EXPECT_TRUE(unchangedSinceApplied(bal.fingerprint(), 512, true, 3));
    EXPECT_FALSE(unchangedSinceApplied(bal.fingerprint(), 128, true, 3));   // you changed the buffer
    EXPECT_FALSE(unchangedSinceApplied(bal.fingerprint(), 512, true, 1));   // or the autosave time
}

TEST(Jubelle_PerformanceProfile, ThisMachineIsMeasured)
{
    const MachineInfo m = currentMachine();
    EXPECT_GE(m.cores, 1u);
    EXPECT_GT(m.memoryGb, 0.2);
}
