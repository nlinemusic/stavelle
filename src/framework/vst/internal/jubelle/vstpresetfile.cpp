/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle: VST3 preset (.vstpreset) files. See vstpresetfile.h.
 */
#include "vstpresetfile.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <system_error>

namespace fs = std::filesystem;

namespace muse::vst::jubelle {
namespace {
constexpr size_t HEADER_SIZE = 4 + 4 + 32 + 8;    // "VST3", version, class ID, list offset
constexpr int32_t FORMAT_VERSION = 1;

int64_t readInt64(const std::string& b, size_t at)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) {
        v = (v << 8) | uint8_t(b[at + size_t(i)]);
    }
    return int64_t(v);
}

int32_t readInt32(const std::string& b, size_t at)
{
    uint32_t v = 0;
    for (int i = 3; i >= 0; --i) {
        v = (v << 8) | uint8_t(b[at + size_t(i)]);
    }
    return int32_t(v);
}

void writeInt64(std::string& out, int64_t value)
{
    const uint64_t v = uint64_t(value);
    for (int i = 0; i < 8; ++i) {
        out.push_back(char((v >> (8 * i)) & 0xFF));
    }
}

void writeInt32(std::string& out, int32_t value)
{
    const uint32_t v = uint32_t(value);
    for (int i = 0; i < 4; ++i) {
        out.push_back(char((v >> (8 * i)) & 0xFF));
    }
}

std::string hexOnly(const std::string& s)
{
    std::string out;
    for (char c : s) {
        if (std::isxdigit(static_cast<unsigned char>(c))) {
            out.push_back(char(std::toupper(static_cast<unsigned char>(c))));
        }
    }
    return out;
}

// UTF-8 text of a path on every system (std::filesystem's string() can throw
// on Windows for names outside the local code page)
std::string utf8(const fs::path& p)
{
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}

bool readFile(const fs::path& path, std::string& out, size_t maxBytes)
{
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec || size > maxBytes) {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return false;
    }
    out.assign(size_t(size), '\0');
    in.read(out.data(), std::streamsize(size));
    return bool(in);
}

std::string headerClassId(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    char header[HEADER_SIZE];
    if (!in.read(header, HEADER_SIZE) || std::string(header, 4) != "VST3") {
        return {};
    }
    return std::string(header + 8, 32);
}
}

std::optional<VstPresetData> parseVstPreset(const std::string& b)
{
    if (b.size() < HEADER_SIZE || b.compare(0, 4, "VST3") != 0) {
        return std::nullopt;
    }
    VstPresetData data;
    data.classId = b.substr(8, 32);

    const int64_t listOffset = readInt64(b, 40);
    if (listOffset < int64_t(HEADER_SIZE) || uint64_t(listOffset) + 8 > b.size()) {
        return std::nullopt;
    }
    const size_t at = size_t(listOffset);
    if (b.compare(at, 4, "List") != 0) {
        return std::nullopt;
    }
    const int32_t count = readInt32(b, at + 4);
    constexpr size_t ENTRY = 4 + 8 + 8;
    if (count < 0 || at + 8 + size_t(count) * ENTRY > b.size()) {
        return std::nullopt;
    }
    for (int32_t i = 0; i < count; ++i) {
        const size_t e = at + 8 + size_t(i) * ENTRY;
        const std::string id = b.substr(e, 4);
        const int64_t offset = readInt64(b, e + 4);
        const int64_t size = readInt64(b, e + 12);
        if (offset < 0 || size < 0 || uint64_t(offset) + uint64_t(size) > b.size()) {
            return std::nullopt;
        }
        if (id == "Comp") {
            data.componentState = b.substr(size_t(offset), size_t(size));
        } else if (id == "Cont") {
            data.controllerState = b.substr(size_t(offset), size_t(size));
        }
    }
    return data;
}

std::string makeVstPreset(const std::string& classId, const std::string& componentState, const std::string& controllerState)
{
    std::string id = classId;
    id.resize(32, ' ');

    std::string out = "VST3";
    writeInt32(out, FORMAT_VERSION);
    out += id;
    const size_t listOffsetAt = out.size();
    writeInt64(out, 0);

    struct Chunk {
        const char* id;
        int64_t offset;
        int64_t size;
    };
    std::vector<Chunk> chunks;
    if (!componentState.empty()) {
        chunks.push_back({ "Comp", int64_t(out.size()), int64_t(componentState.size()) });
        out += componentState;
    }
    if (!controllerState.empty()) {
        chunks.push_back({ "Cont", int64_t(out.size()), int64_t(controllerState.size()) });
        out += controllerState;
    }

    const int64_t listOffset = int64_t(out.size());
    out += "List";
    writeInt32(out, int32_t(chunks.size()));
    for (const Chunk& c : chunks) {
        out += std::string(c.id, 4);
        writeInt64(out, c.offset);
        writeInt64(out, c.size);
    }

    std::string offsetBytes;
    writeInt64(offsetBytes, listOffset);
    out.replace(listOffsetAt, 8, offsetBytes);
    return out;
}

bool sameClassId(const std::string& a, const std::string& b)
{
    const std::string x = hexOnly(a);
    return !x.empty() && x == hexOnly(b);
}

std::vector<fs::path> presetRoots(const fs::path& appFolder)
{
    std::vector<fs::path> candidates;
    auto env = [](const char* name) -> std::string {
        const char* v = std::getenv(name);
        return v ? std::string(v) : std::string();
    };
#if defined(_WIN32)
    if (!env("USERPROFILE").empty()) {
        candidates.push_back(fs::path(env("USERPROFILE")) / "Documents" / "VST3 Presets");
    }
    if (!env("PROGRAMDATA").empty()) {
        candidates.push_back(fs::path(env("PROGRAMDATA")) / "VST3 Presets");
    }
#elif defined(__APPLE__)
    if (!env("HOME").empty()) {
        candidates.push_back(fs::path(env("HOME")) / "Library" / "Audio" / "Presets");
    }
    candidates.push_back("/Library/Audio/Presets");
#else
    if (!env("HOME").empty()) {
        candidates.push_back(fs::path(env("HOME")) / ".vst3" / "presets");
    }
    candidates.push_back("/usr/share/vst3/presets");
    candidates.push_back("/usr/local/share/vst3/presets");
#endif
    if (!appFolder.empty()) {
        candidates.push_back(appFolder / "VST3 Presets");
    }

    std::vector<fs::path> roots;
    for (const fs::path& p : candidates) {
        std::error_code ec;
        if (fs::is_directory(p, ec)) {
            roots.push_back(p);
        }
    }
    return roots;
}

std::vector<VstPresetEntry> findPresets(const std::vector<fs::path>& roots, const std::string& vendor, const std::string& pluginName,
                                        const std::string& classId, size_t limit)
{
    std::vector<VstPresetEntry> found;
    if (pluginName.empty()) {
        return found;
    }
    const std::string wantVendor = lower(vendor);
    const std::string wantPlugin = lower(pluginName);

    for (const fs::path& root : roots) {
        std::error_code ec;
        std::vector<fs::path> pluginFolders;
        for (const auto& vendorDir : fs::directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
            std::error_code ec2;
            if (!vendorDir.is_directory(ec2)) {
                continue;
            }
            for (const auto& pluginDir : fs::directory_iterator(vendorDir.path(), fs::directory_options::skip_permission_denied, ec2)) {
                std::error_code ec3;
                if (pluginDir.is_directory(ec3) && lower(utf8(pluginDir.path().filename())) == wantPlugin) {
                    // the matching vendor first
                    if (lower(utf8(vendorDir.path().filename())) == wantVendor) {
                        pluginFolders.insert(pluginFolders.begin(), pluginDir.path());
                    } else {
                        pluginFolders.push_back(pluginDir.path());
                    }
                }
            }
        }

        for (const fs::path& folder : pluginFolders) {
            std::error_code ec4;
            for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec4);
                 it != fs::recursive_directory_iterator(); it.increment(ec4)) {
                if (ec4 || found.size() >= limit) {
                    break;
                }
                std::error_code ec5;
                if (!it->is_regular_file(ec5) || lower(utf8(it->path().extension())) != ".vstpreset") {
                    continue;
                }
                if (!classId.empty()) {
                    const std::string fileId = headerClassId(it->path());
                    if (fileId.empty() || !sameClassId(fileId, classId)) {
                        continue;
                    }
                }
                VstPresetEntry e;
                e.title = utf8(it->path().stem());
                const fs::path rel = fs::relative(it->path().parent_path(), folder, ec5);
                if (!ec5 && rel != ".") {
                    const auto g = rel.generic_u8string(); // "/" between folders on every system
                    e.group = std::string(g.begin(), g.end());
                }
                e.path = it->path();
                found.push_back(std::move(e));
            }
        }
    }

    std::sort(found.begin(), found.end(), [](const VstPresetEntry& a, const VstPresetEntry& b) {
        if (a.group != b.group) {
            return lower(a.group) < lower(b.group);
        }
        return lower(a.title) < lower(b.title);
    });
    // the same preset can be in two roots; keep the first
    found.erase(std::unique(found.begin(), found.end(), [](const VstPresetEntry& a, const VstPresetEntry& b) {
        return a.group == b.group && a.title == b.title;
    }), found.end());
    return found;
}

std::optional<VstPresetData> readVstPresetFile(const fs::path& path)
{
    std::string bytes;
    if (!readFile(path, bytes, 256u * 1024u * 1024u)) {
        return std::nullopt;
    }
    return parseVstPreset(bytes);
}
}
