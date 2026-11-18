/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "vstkeyswitches.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <sstream>

namespace muse::vst::jubelle {
const std::vector<std::string>& keySwitchGroups()
{
    static const std::vector<std::string> groups {
        "fall", "doit", "scoop", "mute", "open", "pizzicato", "harmonic", "tremolo", "trill",
        "staccato", "marcato", "accent", "tenuto", "normal",
    };
    return groups;
}

std::string keySwitchGroupTitle(const std::string& g)
{
    static const std::map<std::string, std::string> titles {
        { "fall", "Fall" }, { "doit", "Doit" }, { "scoop", "Scoop and plop" }, { "mute", "Mute" }, { "open", "Open" },
        { "pizzicato", "Pizzicato" }, { "harmonic", "Harmonic" }, { "tremolo", "Tremolo" }, { "trill", "Trill" },
        { "staccato", "Staccato" }, { "marcato", "Marcato" }, { "accent", "Accent" }, { "tenuto", "Tenuto" },
        { "normal", "Normal (no articulation)" },
    };
    auto it = titles.find(g);
    return it != titles.end() ? it->second : g;
}

int KeySwitchMap::keyFor(const std::vector<std::string>& groupsOnNote) const
{
    if (keys.empty()) {
        return -1;
    }
    for (const std::string& g : keySwitchGroups()) {
        if (g == "normal") {
            break;
        }
        if (std::find(groupsOnNote.begin(), groupsOnNote.end(), g) != groupsOnNote.end()) {
            auto it = keys.find(g);
            if (it != keys.end()) {
                return it->second;
            }
        }
    }
    auto it = keys.find("normal");
    return it != keys.end() ? it->second : -1;
}

namespace {
std::string ksTrim(const std::string& s)
{
    size_t a = 0;
    size_t b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) {
        ++a;
    }
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) {
        --b;
    }
    return s.substr(a, b - a);
}

std::string ksLower(std::string s)
{
    for (char& c : s) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
}
}

int noteNameToMidi(const std::string& raw, bool middleCIsC4)
{
    const std::string name = ksTrim(raw);
    if (name.empty()) {
        return -1;
    }
    // a plain number
    if (std::isdigit(static_cast<unsigned char>(name[0]))) {
        char* end = nullptr;
        const long v = std::strtol(name.c_str(), &end, 10);
        return (end && *end == '\0' && v >= 0 && v <= 127) ? int(v) : -1;
    }
    static const int steps[] = { 9, 11, 0, 2, 4, 5, 7 };   // A B C D E F G
    const char letter = char(std::toupper(static_cast<unsigned char>(name[0])));
    if (letter < 'A' || letter > 'G') {
        return -1;
    }
    int pc = steps[letter - 'A'];
    size_t i = 1;
    while (i < name.size() && (name[i] == '#' || name[i] == 'b')) {
        pc += name[i] == '#' ? 1 : -1;
        ++i;
    }
    if (i >= name.size()) {
        return -1;
    }
    char* end = nullptr;
    const long octave = std::strtol(name.c_str() + i, &end, 10);
    if (!end || *end != '\0') {
        return -1;
    }
    const int midi = int((octave + (middleCIsC4 ? 1 : 2)) * 12 + pc);
    return (midi >= 0 && midi <= 127) ? midi : -1;
}

std::string midiToNoteName(int midi, bool middleCIsC4)
{
    if (midi < 0 || midi > 127) {
        return std::string();
    }
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    const int octave = midi / 12 - (middleCIsC4 ? 1 : 2);
    return std::string(names[midi % 12]) + std::to_string(octave);
}

KeySwitchMap parseKeySwitches(const std::string& text, bool middleCIsC4)
{
    KeySwitchMap map;
    std::stringstream ss(text);
    std::string item;
    while (std::getline(ss, item, ';')) {
        const size_t eq = item.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string group = ksLower(ksTrim(item.substr(0, eq)));
        const auto& groups = keySwitchGroups();
        if (std::find(groups.begin(), groups.end(), group) == groups.end()) {
            continue;
        }
        const int midi = noteNameToMidi(item.substr(eq + 1), middleCIsC4);
        if (midi >= 0) {
            map.keys[group] = midi;
        }
    }
    return map;
}

std::string keySwitchesToString(const KeySwitchMap& map)
{
    std::string out;
    for (const std::string& g : keySwitchGroups()) {
        auto it = map.keys.find(g);
        if (it == map.keys.end()) {
            continue;
        }
        if (!out.empty()) {
            out += "; ";
        }
        out += g + "=" + std::to_string(it->second);
    }
    return out;
}

double parseTrimDb(const std::string& text)
{
    const std::string t = ksTrim(text);
    if (t.empty()) {
        return 0.0;
    }
    char* end = nullptr;
    const double v = std::strtod(t.c_str(), &end);
    if (!end || end == t.c_str() || !std::isfinite(v)) {
        return 0.0;
    }
    return std::clamp(v, -24.0, 12.0);
}

float trimGain(double db)
{
    return float(std::pow(10.0, db / 20.0));
}
}
