/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle: VST3 preset (.vstpreset) files.
 *
 * Reads the standard VST3 preset file format (documented by Steinberg in the
 * VST3 SDK, "Preset Format"): a 48-byte header ("VST3", version, the plugin's
 * 32-character class ID, the offset of the chunk list), the chunk data, and a
 * chunk list ("List", count, then id/offset/size per chunk). The "Comp" chunk
 * is the plugin's component state and "Cont" its controller state; these are
 * exactly what MuseScore already stores per track in the score, so loading a
 * preset is the same as restoring a track's saved plugin settings.
 *
 * Plain C++ (no SDK, no Qt) so it can be tested on its own.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace muse::vst::jubelle {
struct VstPresetData {
    std::string classId;         //!< 32 characters, as written in the file
    std::string componentState;  //!< "Comp" chunk (may be empty)
    std::string controllerState; //!< "Cont" chunk (may be empty)
};

//! Parses a whole .vstpreset file. Returns nothing if it isn't one, or if
//! any chunk lies outside the file.
std::optional<VstPresetData> parseVstPreset(const std::string& bytes);

//! Reads and parses a .vstpreset file from disk (at most 256 MB)
std::optional<VstPresetData> readVstPresetFile(const std::filesystem::path& path);

//! Builds a .vstpreset file (used by the tests and to save presets later).
std::string makeVstPreset(const std::string& classId, const std::string& componentState, const std::string& controllerState);

//! true if two class IDs name the same plugin class (case and any
//! separators such as "-", "{", "}" are ignored)
bool sameClassId(const std::string& a, const std::string& b);

struct VstPresetEntry {
    std::string title;           //!< file name without ".vstpreset"
    std::string group;           //!< sub-folder under the plugin's preset folder ("" at the top)
    std::filesystem::path path;
};

//! The standard VST3 preset folders on this system (user, all users,
//! Jubelle's own installation folder), in that order; only folders that exist.
std::vector<std::filesystem::path> presetRoots(const std::filesystem::path& appFolder = {});

//! Presets for one plugin: every .vstpreset in a "<vendor>/<plugin name>"
//! folder under the roots (any vendor folder if the vendor doesn't match),
//! whose class ID matches the plugin's when both are known. Sorted by group,
//! then title; at most `limit` entries.
std::vector<VstPresetEntry> findPresets(const std::vector<std::filesystem::path>& roots, const std::string& vendor,
                                        const std::string& pluginName, const std::string& classId, size_t limit = 2000);
}
