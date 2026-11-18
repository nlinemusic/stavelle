/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */
#include <gtest/gtest.h>

#include "framework/vst/internal/jubelle/vstkeyswitches.h"

using namespace muse::vst::jubelle;

TEST(Jubelle_VstKeySwitches, NoteNames)
{
    EXPECT_EQ(noteNameToMidi("C4"), 60);
    EXPECT_EQ(noteNameToMidi("C3", false), 60);   // libraries that call middle C "C3"
    EXPECT_EQ(noteNameToMidi("C-1"), 0);
    EXPECT_EQ(noteNameToMidi("F#0"), 18);
    EXPECT_EQ(noteNameToMidi("Bb1"), 34);
    EXPECT_EQ(noteNameToMidi("c1"), 24);
    EXPECT_EQ(noteNameToMidi("24"), 24);
    EXPECT_EQ(noteNameToMidi("128"), -1);
    EXPECT_EQ(noteNameToMidi("H2"), -1);
    EXPECT_EQ(noteNameToMidi("C"), -1);
    EXPECT_EQ(noteNameToMidi(""), -1);
    for (int m = 0; m < 128; ++m) {
        EXPECT_EQ(noteNameToMidi(midiToNoteName(m)), m);
        EXPECT_EQ(noteNameToMidi(midiToNoteName(m, false), false), m);
    }
}

TEST(Jubelle_VstKeySwitches, ParseAndWrite)
{
    const KeySwitchMap m = parseKeySwitches(" Normal = C1 ; staccato=26; mute=F#1; nonsense=3; fall=; doit=200 ");
    EXPECT_EQ(m.keys.size(), 3u);
    EXPECT_EQ(m.keys.at("normal"), 24);
    EXPECT_EQ(m.keys.at("staccato"), 26);
    EXPECT_EQ(m.keys.at("mute"), 30);
    // written in priority order, as numbers, and read back the same
    EXPECT_EQ(keySwitchesToString(m), "mute=30; staccato=26; normal=24");
    EXPECT_EQ(parseKeySwitches(keySwitchesToString(m)).keys, m.keys);
    EXPECT_TRUE(parseKeySwitches("").empty());
}

TEST(Jubelle_VstKeySwitches, KeyForANote)
{
    const KeySwitchMap m = parseKeySwitches("normal=24; staccato=26; mute=29; fall=31");
    EXPECT_EQ(m.keyFor({}), 24);                          // no articulation: the normal key
    EXPECT_EQ(m.keyFor({ "staccato" }), 26);
    EXPECT_EQ(m.keyFor({ "staccato", "mute" }), 29);      // mute outranks staccato
    EXPECT_EQ(m.keyFor({ "fall", "mute" }), 31);          // a fall outranks everything
    EXPECT_EQ(m.keyFor({ "tenuto" }), 24);                // no key for tenuto: normal
    const KeySwitchMap noNormal = parseKeySwitches("staccato=26");
    EXPECT_EQ(noNormal.keyFor({ "tenuto" }), -1);         // nothing to send
    EXPECT_EQ(KeySwitchMap().keyFor({ "staccato" }), -1);
}

TEST(Jubelle_VstKeySwitches, Trim)
{
    EXPECT_DOUBLE_EQ(parseTrimDb(""), 0.0);
    EXPECT_DOUBLE_EQ(parseTrimDb("-6"), -6.0);
    EXPECT_DOUBLE_EQ(parseTrimDb(" 3.5 "), 3.5);
    EXPECT_DOUBLE_EQ(parseTrimDb("-40"), -24.0);
    EXPECT_DOUBLE_EQ(parseTrimDb("30"), 12.0);
    EXPECT_DOUBLE_EQ(parseTrimDb("loud"), 0.0);
    EXPECT_NEAR(trimGain(0.0), 1.0f, 1e-6);
    EXPECT_NEAR(trimGain(-6.0), 0.501187f, 1e-5);
    EXPECT_NEAR(trimGain(6.0), 1.995262f, 1e-5);
}
