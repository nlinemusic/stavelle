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

// Jubelle: key switches and a level trim for each VST3 instrument.
//
// Many sample libraries change articulation when they receive a low "key
// switch" note. For each instrument you can say which key selects which
// articulation; before every note Jubelle then sends the key for the note's
// articulation (or the "normal" key when it has none of them). The level trim
// evens out libraries that are much louder or quieter than the others.
//
// Both are kept in the instrument's plugin settings, so they are saved with the
// score. No Qt and no audio engine here, so this part is tested on its own.

#include <map>
#include <string>
#include <vector>

namespace muse::vst::jubelle {
//! Keys in the instrument's plugin settings (the plugin itself never reads them)
constexpr const char* KEYSWITCHES_SETTING = "jubelle.keyswitches";
constexpr const char* TRIM_SETTING = "jubelle.trimDb";

//! The articulations a key can select, in priority order (when a note has several,
//! the first one listed here wins). "normal" is for notes with none of them.
const std::vector<std::string>& keySwitchGroups();
//! A plain name for a group ("Staccato", "Mute"...); untranslated
std::string keySwitchGroupTitle(const std::string& group);

struct KeySwitchMap {
    std::map<std::string, int> keys;   // group -> MIDI note 0-127

    bool empty() const { return keys.empty(); }
    //! The key for a note with these groups, or -1 to send none
    int keyFor(const std::vector<std::string>& groupsOnNote) const;
};

//! "normal=24; staccato=26; mute=29" (MIDI numbers, or note names such as C1 or F#0)
KeySwitchMap parseKeySwitches(const std::string& text, bool middleCIsC4 = true);
std::string keySwitchesToString(const KeySwitchMap& map);

//! "C1", "F#0", "Bb-1": MIDI note number, or -1. With middleCIsC4, C4 = 60; otherwise C3 = 60.
int noteNameToMidi(const std::string& name, bool middleCIsC4 = true);
std::string midiToNoteName(int midi, bool middleCIsC4 = true);

//! The trim in decibels, limited to -24 ... +12 (empty or unreadable = 0)
double parseTrimDb(const std::string& text);
//! As a gain factor
float trimGain(double db);
}
