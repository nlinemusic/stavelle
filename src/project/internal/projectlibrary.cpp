/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 * Music Composition & Notation
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "projectlibrary.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace mu::project::projectlibrary {
static const char* BACKUPS = "Backups";
static const char* PDF_EXPORTS = "PDF Exports";
static const char* FULL_SCORE = "Full Score";
static const char* INDIVIDUAL_PARTS = "Individual Parts";
static const char* COMBINED_PARTS = "Combined Parts";
static const char* AUDIO_EXPORTS = "Audio Exports";
static const char* VIDEO_EXPORTS = "Video Exports";
static const char* OTHER_EXPORTS = "Other Exports";
static const char* CATEGORY_ORDER_FILE = ".jubelle-category-order.txt";
static const char* SCORE_SUFFIX = ".mscz";
static const char* BACKUP_SUFFIX = ".mscz~";

static fs::path toPath(const std::string& utf8)
{
    return fs::u8path(utf8);
}

static std::string toUtf8(const fs::path& path)
{
    return path.u8string();
}

static Result fail(const std::string& error)
{
    Result r;
    r.error = error;
    return r;
}

static Result success(const fs::path& path)
{
    Result r;
    r.ok = true;
    r.path = toUtf8(path);
    return r;
}

static bool isDir(const fs::path& p)
{
    std::error_code ec;
    return fs::is_directory(p, ec);
}

static bool pathExists(const fs::path& p)
{
    std::error_code ec;
    return fs::exists(p, ec);
}

//! True if `a` and `b` name the same existing file or folder (e.g. a rename
//! that only changes upper/lower case on a case-insensitive disk).
static bool sameEntry(const fs::path& a, const fs::path& b)
{
    std::error_code ec;
    return fs::equivalent(a, b, ec);
}

static void setHidden(const fs::path& p)
{
#ifdef _WIN32
    DWORD attrs = GetFileAttributesW(p.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES) {
        SetFileAttributesW(p.c_str(), attrs | FILE_ATTRIBUTE_HIDDEN);
    }
#else
    (void)p; // a leading dot already hides the file on macOS
#endif
}

static std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string mainFolderName()
{
    return "Jubelle Projects";
}

std::vector<std::string> defaultCategories()
{
    return { "Big Band - Jazz Band", "Concert Band", "Orchestra", "Small Ensembles", "Other" };
}

std::string validateName(const std::string& name)
{
    if (name.empty()) {
        return "Please enter a name.";
    }

    if (name.size() > 120) {
        return "This name is too long. Please use a shorter name.";
    }

    if (name.front() == ' ' || name.back() == ' ') {
        return "The name can't begin or end with a space.";
    }

    if (name.front() == '.') {
        return "The name can't begin with a period.";
    }

    if (name.back() == '.') {
        return "The name can't end with a period.";
    }

    for (unsigned char c : name) {
        if (c < 32) {
            return "The name contains a character that can't be used.";
        }
        if (std::string("<>:\"/\\|?*").find(static_cast<char>(c)) != std::string::npos) {
            return "The name can't contain any of these characters: < > : \" / \\ | ? *";
        }
    }

    // Names Windows reserves for devices, also with an extension ("con.txt").
    std::string base = lower(name.substr(0, name.find('.')));
    while (!base.empty() && base.back() == ' ') {
        base.pop_back();
    }
    static const std::vector<std::string> reserved = {
        "con", "prn", "aux", "nul",
        "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"
    };
    if (std::find(reserved.begin(), reserved.end(), base) != reserved.end()) {
        return "This name is reserved by Windows. Please choose a different name.";
    }

    return std::string();
}

// ---------------------------------------------------------------------------
// Main folder and categories
// ---------------------------------------------------------------------------

static std::vector<std::string> readCategoryOrder(const fs::path& main)
{
    std::vector<std::string> order;
    std::ifstream in(main / CATEGORY_ORDER_FILE, std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            order.push_back(line);
        }
    }
    return order;
}

static bool writeCategoryOrder(const fs::path& main, const std::vector<std::string>& order)
{
    const fs::path file = main / CATEGORY_ORDER_FILE;
    const fs::path temp = main / (std::string(CATEGORY_ORDER_FILE) + ".tmp");

#ifdef _WIN32
    // A hidden file can't be replaced or truncated by a plain write on Windows.
    SetFileAttributesW(temp.c_str(), FILE_ATTRIBUTE_NORMAL);
#endif
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        for (const std::string& name : order) {
            out << name << '\n';
        }
        out.flush();
        if (!out) {
            return false;
        }
    }

#ifdef _WIN32
    if (!MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return false;
    }
#else
    std::error_code ec;
    fs::rename(temp, file, ec);
    if (ec) {
        return false;
    }
#endif

    setHidden(file);
    return true;
}

//! Replaces `oldName` by `newName` (or removes it when `newName` is empty)
//! in the stored order. Missing order file = nothing to update.
static void updateCategoryOrder(const fs::path& main, const std::string& oldName, const std::string& newName)
{
    std::vector<std::string> order = readCategoryOrder(main);
    auto it = std::find(order.begin(), order.end(), oldName);
    if (it == order.end()) {
        return;
    }
    if (newName.empty()) {
        order.erase(it);
    } else {
        *it = newName;
    }
    writeCategoryOrder(main, order);
}

Result ensureMainFolder(const std::string& mainFolder)
{
    const fs::path main = toPath(mainFolder);
    if (isDir(main)) {
        return success(main);
    }

    if (pathExists(main)) {
        return fail("A file with the same name as the projects folder is in the way: " + mainFolder);
    }

    std::error_code ec;
    fs::create_directories(main, ec);
    if (ec || !isDir(main)) {
        return fail("The projects folder could not be created: " + mainFolder + " (" + ec.message() + ")");
    }

    for (const std::string& category : defaultCategories()) {
        fs::create_directory(main / toPath(category), ec);
    }
    writeCategoryOrder(main, defaultCategories());

    return success(main);
}

std::vector<std::string> categories(const std::string& mainFolder)
{
    const fs::path main = toPath(mainFolder);
    std::vector<std::string> found;

    std::error_code ec;
    for (fs::directory_iterator it(main, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code dirEc;
        if (!it->is_directory(dirEc)) {
            continue;
        }
        std::string name = toUtf8(it->path().filename());
        if (name.empty() || name.front() == '.') {
            continue; // hidden folders are not categories
        }
        found.push_back(name);
    }

    std::sort(found.begin(), found.end(), [](const std::string& a, const std::string& b) {
        return lower(a) < lower(b);
    });

    std::vector<std::string> result;
    for (const std::string& name : readCategoryOrder(main)) {
        auto it = std::find(found.begin(), found.end(), name);
        if (it != found.end()) {
            result.push_back(name);
            found.erase(it);
        }
    }
    result.insert(result.end(), found.begin(), found.end());
    return result;
}

Result createCategory(const std::string& mainFolder, const std::string& name)
{
    std::string problem = validateName(name);
    if (!problem.empty()) {
        return fail(problem);
    }

    const fs::path main = toPath(mainFolder);
    if (!isDir(main)) {
        return fail("The projects folder was not found: " + mainFolder);
    }

    const fs::path dir = main / toPath(name);
    if (pathExists(dir)) {
        return fail("A category named \"" + name + "\" already exists.");
    }

    std::error_code ec;
    if (!fs::create_directory(dir, ec) || ec) {
        return fail("The category could not be created (" + ec.message() + ").");
    }

    std::vector<std::string> order = categories(mainFolder);
    writeCategoryOrder(main, order); // keeps the new category last in the stored order

    return success(dir);
}

Result renameCategory(const std::string& mainFolder, const std::string& oldName, const std::string& newName)
{
    std::string problem = validateName(newName);
    if (!problem.empty()) {
        return fail(problem);
    }

    const fs::path main = toPath(mainFolder);
    const fs::path from = main / toPath(oldName);
    const fs::path to = main / toPath(newName);

    if (!isDir(from)) {
        return fail("The category \"" + oldName + "\" was not found.");
    }

    if (oldName == newName) {
        return success(from);
    }

    if (pathExists(to) && !sameEntry(from, to)) {
        return fail("A category named \"" + newName + "\" already exists.");
    }

    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec) {
        return fail("The category could not be renamed. A file inside it may be open in another program. (" + ec.message() + ")");
    }

    updateCategoryOrder(main, oldName, newName);
    return success(to);
}

Result deleteCategory(const std::string& mainFolder, const std::string& name)
{
    const fs::path main = toPath(mainFolder);
    const fs::path dir = main / toPath(name);

    if (!isDir(dir)) {
        return fail("The category \"" + name + "\" was not found.");
    }

    std::error_code ec;
    if (!fs::is_empty(dir, ec) || ec) {
        return fail("The category \"" + name + "\" still contains projects or files. "
                    "Move them to another category first, then delete it.");
    }

    fs::remove(dir, ec);
    if (ec) {
        return fail("The category could not be deleted (" + ec.message() + ").");
    }

    updateCategoryOrder(main, name, std::string());
    return success(dir);
}

Result setCategoryOrder(const std::string& mainFolder, const std::vector<std::string>& orderedNames)
{
    const fs::path main = toPath(mainFolder);
    if (!isDir(main)) {
        return fail("The projects folder was not found: " + mainFolder);
    }

    std::vector<std::string> existing = categories(mainFolder);
    std::vector<std::string> order;
    for (const std::string& name : orderedNames) {
        if (std::find(existing.begin(), existing.end(), name) != existing.end()
            && std::find(order.begin(), order.end(), name) == order.end()) {
            order.push_back(name);
        }
    }
    for (const std::string& name : existing) {
        if (std::find(order.begin(), order.end(), name) == order.end()) {
            order.push_back(name);
        }
    }

    if (!writeCategoryOrder(main, order)) {
        return fail("The category order could not be saved.");
    }
    return success(main);
}

// ---------------------------------------------------------------------------
// Projects
// ---------------------------------------------------------------------------

static const std::vector<fs::path>& projectSubfolders()
{
    static const std::vector<fs::path> dirs = {
        fs::u8path(BACKUPS),
        fs::u8path(PDF_EXPORTS) / fs::u8path(FULL_SCORE),
        fs::u8path(PDF_EXPORTS) / fs::u8path(INDIVIDUAL_PARTS),
        fs::u8path(PDF_EXPORTS) / fs::u8path(COMBINED_PARTS),
        fs::u8path(AUDIO_EXPORTS),
        fs::u8path(VIDEO_EXPORTS),
        fs::u8path(OTHER_EXPORTS),
    };
    return dirs;
}

std::string picturesFolder(const std::string& projectFolder, bool create)
{
    const fs::path dir = toPath(projectFolder) / "Pictures";
    std::error_code ec;
    if (isDir(dir)) {
        return toUtf8(dir);
    }
    if (!create || !isProjectFolder(projectFolder)) {
        return std::string();
    }
    fs::create_directories(dir, ec);
    return ec ? std::string() : toUtf8(dir);
}

bool isProjectFolder(const std::string& folder)
{
    const fs::path dir = toPath(folder);
    return isDir(dir / BACKUPS) && isDir(dir / PDF_EXPORTS);
}

std::vector<std::string> projects(const std::string& mainFolder, const std::string& category)
{
    std::vector<std::string> result;
    const fs::path dir = toPath(mainFolder) / toPath(category);

    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code dirEc;
        if (it->is_directory(dirEc) && isProjectFolder(toUtf8(it->path()))) {
            result.push_back(toUtf8(it->path().filename()));
        }
    }

    std::sort(result.begin(), result.end(), [](const std::string& a, const std::string& b) {
        return lower(a) < lower(b);
    });
    return result;
}

std::string availableProjectName(const std::string& mainFolder, const std::string& category, const std::string& name)
{
    const fs::path categoryDir = toPath(mainFolder) / toPath(category);
    std::string finalName = name;
    for (int n = 2; pathExists(categoryDir / toPath(finalName)); ++n) {
        finalName = name + " (" + std::to_string(n) + ")";
    }
    return finalName;
}

Result removeUnusedProject(const std::string& projectFolder)
{
    const fs::path dir = toPath(projectFolder);
    if (!isProjectFolder(projectFolder)) {
        return fail("This is not a project folder.");
    }

    std::error_code ec;
    std::vector<fs::path> folders;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (it->is_symlink(ec) || !it->is_directory(ec)) {
            return fail("The project folder contains files, so it was kept.");
        }
        folders.push_back(it->path());
    }
    if (ec) {
        return fail("The project folder could not be checked (" + ec.message() + ").");
    }

    // Deepest first, and only empty folders: fs::remove never deletes a non-empty folder.
    std::sort(folders.begin(), folders.end(), [](const fs::path& a, const fs::path& b) {
        return a.native().size() > b.native().size();
    });
    for (const fs::path& f : folders) {
        fs::remove(f, ec);
        if (ec) {
            return fail("The project folder could not be removed (" + ec.message() + ").");
        }
    }
    fs::remove(dir, ec);
    if (ec) {
        return fail("The project folder could not be removed (" + ec.message() + ").");
    }
    return success(dir);
}

Result createProject(const std::string& mainFolder, const std::string& category, const std::string& name)
{
    std::string problem = validateName(name);
    if (!problem.empty()) {
        return fail(problem);
    }

    const fs::path categoryDir = toPath(mainFolder) / toPath(category);
    if (!isDir(categoryDir)) {
        return fail("The category \"" + category + "\" was not found.");
    }

    // Never reuse an existing folder: number the new one instead.
    const std::string finalName = availableProjectName(mainFolder, category, name);

    const fs::path projectDir = categoryDir / toPath(finalName);
    std::error_code ec;
    if (!fs::create_directory(projectDir, ec) || ec) {
        return fail("The project folder could not be created (" + ec.message() + ").");
    }

    for (const fs::path& sub : projectSubfolders()) {
        fs::create_directories(projectDir / sub, ec);
        if (ec) {
            return fail("The project's subfolders could not be created (" + ec.message() + ").");
        }
    }

    return success(projectDir);
}

std::string projectFolderForScore(const std::string& scorePath)
{
    const fs::path dir = toPath(scorePath).parent_path();
    if (dir.empty() || !isProjectFolder(toUtf8(dir))) {
        return std::string();
    }
    return toUtf8(dir);
}

std::string mainScorePath(const std::string& projectFolder)
{
    const fs::path dir = toPath(projectFolder);
    return toUtf8(dir / toPath(toUtf8(dir.filename()) + SCORE_SUFFIX));
}

Result renameProject(const std::string& projectFolder, const std::string& newName)
{
    std::string problem = validateName(newName);
    if (!problem.empty()) {
        return fail(problem);
    }

    const fs::path from = toPath(projectFolder);
    if (!isProjectFolder(projectFolder)) {
        return fail("This folder is not a Jubelle project: " + projectFolder);
    }

    const std::string oldName = toUtf8(from.filename());
    if (oldName == newName) {
        return success(from);
    }

    const fs::path to = from.parent_path() / toPath(newName);
    if (pathExists(to) && !sameEntry(from, to)) {
        return fail("A project named \"" + newName + "\" already exists in this category.");
    }

    // Rename the files inside first, so a failure can be fully undone.
    struct Step {
        fs::path from;
        fs::path to;
    };
    std::vector<Step> done;
    auto undo = [&done]() {
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            std::error_code ignore;
            fs::rename(it->to, it->from, ignore);
        }
    };

    const std::vector<Step> fileSteps = {
        { from / toPath(oldName + SCORE_SUFFIX), from / toPath(newName + SCORE_SUFFIX) },
        { from / BACKUPS / toPath(oldName + BACKUP_SUFFIX), from / BACKUPS / toPath(newName + BACKUP_SUFFIX) },
    };

    for (const Step& step : fileSteps) {
        if (!pathExists(step.from)) {
            continue;
        }
        if (pathExists(step.to) && !sameEntry(step.from, step.to)) {
            undo();
            return fail("The project contains a file named \"" + toUtf8(step.to.filename())
                        + "\" already, so it can't be renamed to \"" + newName + "\".");
        }
        std::error_code ec;
        fs::rename(step.from, step.to, ec);
        if (ec) {
            undo();
            return fail("The score file could not be renamed. It may be open in another program. (" + ec.message() + ")");
        }
        done.push_back(step);
    }

    std::error_code ec;
    fs::rename(from, to, ec);
    if (ec) {
        undo();
        return fail("The project folder could not be renamed. A file inside it may be open in another program. ("
                    + ec.message() + ")");
    }

    return success(to);
}

//! Total size and number of files below `dir`, used to verify a copy.
static bool measure(const fs::path& dir, uintmax_t& files, uintmax_t& bytes)
{
    files = 0;
    bytes = 0;
    std::error_code ec;
    for (fs::recursive_directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code fileEc;
        if (it->is_regular_file(fileEc)) {
            ++files;
            bytes += it->file_size(fileEc);
        }
    }
    return !ec;
}

Result moveProject(const std::string& projectFolder, const std::string& mainFolder, const std::string& newCategory)
{
    const fs::path from = toPath(projectFolder);
    if (!isProjectFolder(projectFolder)) {
        return fail("This folder is not a Jubelle project: " + projectFolder);
    }

    const fs::path categoryDir = toPath(mainFolder) / toPath(newCategory);
    if (!isDir(categoryDir)) {
        return fail("The category \"" + newCategory + "\" was not found.");
    }

    const fs::path to = categoryDir / from.filename();
    if (sameEntry(from, to)) {
        return success(from); // already there
    }

    if (pathExists(to)) {
        return fail("The category \"" + newCategory + "\" already has a project named \""
                    + toUtf8(from.filename()) + "\". Rename one of them first.");
    }

    std::error_code ec;
    fs::rename(from, to, ec);
    if (!ec) {
        return success(to);
    }

    // A plain move is not possible (for example, the folders are on different
    // drives). Copy everything, check the copy is complete, then remove the original.
    uintmax_t srcFiles = 0, srcBytes = 0;
    if (!measure(from, srcFiles, srcBytes)) {
        return fail("The project could not be read, so it was not moved.");
    }

    std::error_code copyEc;
    fs::copy(from, to, fs::copy_options::recursive, copyEc);

    uintmax_t dstFiles = 0, dstBytes = 0;
    if (copyEc || !measure(to, dstFiles, dstBytes) || dstFiles != srcFiles || dstBytes != srcBytes) {
        std::error_code ignore;
        fs::remove_all(to, ignore); // remove the incomplete copy; the original is untouched
        return fail("The project could not be moved. A file inside it may be open in another program. Nothing was changed.");
    }

    std::error_code removeEc;
    fs::remove_all(from, removeEc);
    if (removeEc) {
        // The complete copy is in place; the original could only be partly removed.
        Result r = success(to);
        r.error = "The project was moved, but some files could not be removed from its old location: "
                  + toUtf8(from);
        return r;
    }

    return success(to);
}

std::string exportFolder(const std::string& projectFolder, ExportKind kind)
{
    fs::path dir = toPath(projectFolder);
    switch (kind) {
    case ExportKind::FullScorePdf: dir = dir / PDF_EXPORTS / FULL_SCORE;
        break;
    case ExportKind::IndividualPartsPdf: dir = dir / PDF_EXPORTS / INDIVIDUAL_PARTS;
        break;
    case ExportKind::CombinedPartsPdf: dir = dir / PDF_EXPORTS / COMBINED_PARTS;
        break;
    case ExportKind::Audio: dir = dir / AUDIO_EXPORTS;
        break;
    case ExportKind::Video: dir = dir / VIDEO_EXPORTS;
        break;
    case ExportKind::Other: dir = dir / OTHER_EXPORTS;
        break;
    }

    std::error_code ec;
    fs::create_directories(dir, ec); // recreate if the user deleted it
    return toUtf8(dir);
}

std::string backupFolder(const std::string& projectFolder)
{
    const fs::path dir = toPath(projectFolder) / BACKUPS;
    std::error_code ec;
    fs::create_directories(dir, ec);
    return toUtf8(dir);
}

ExportKind exportKindFor(const std::string& suffix, bool combinedIntoOneFile, bool includesParts)
{
    const std::string s = lower(suffix);
    if (s == "pdf") {
        if (combinedIntoOneFile) {
            return ExportKind::CombinedPartsPdf;
        }
        return includesParts ? ExportKind::IndividualPartsPdf : ExportKind::FullScorePdf;
    }
    if (s == "wav" || s == "mp3" || s == "ogg" || s == "flac") {
        return ExportKind::Audio;
    }
    if (s == "mp4") {
        return ExportKind::Video;
    }
    return ExportKind::Other;
}

bool isSameFolder(const std::string& a, const std::string& b)
{
    if (a.empty() || b.empty()) {
        return false;
    }
    const fs::path pa = toPath(a);
    const fs::path pb = toPath(b);
    return isDir(pa) && isDir(pb) && sameEntry(pa, pb);
}

// ---- Jubelle R1: bringing existing scores into Projects ----

static constexpr const char* IMPORT_RECORD = ".jubelle/imported-from.txt";

bool isInsideMainFolder(const std::string& path, const std::string& mainFolder)
{
    if (path.empty() || mainFolder.empty()) {
        return false;
    }
    auto normal = [](const std::string& in) {
        std::error_code ec;
        fs::path p = fs::weakly_canonical(toPath(in), ec);
        std::string s = toUtf8(ec ? toPath(in).lexically_normal() : p);
        std::replace(s.begin(), s.end(), '\\', '/');
        while (s.size() > 1 && s.back() == '/') {
            s.pop_back();
        }
#if defined(_WIN32) || defined(__APPLE__)
        // Windows and (by default) macOS file names ignore letter case
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
        return s;
    };
    const std::string p = normal(path);
    const std::string m = normal(mainFolder);
    return p.size() > m.size() && p.compare(0, m.size(), m) == 0 && p[m.size()] == '/';
}

std::string fileFingerprint(const std::string& filePath)
{
    std::ifstream in(toPath(filePath), std::ios::binary);
    if (!in) {
        return std::string();
    }
    uint64_t hash = 1469598103934665603ULL;   // FNV-1a 64
    uint64_t size = 0;
    char buf[65536];
    while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
        const std::streamsize n = in.gcount();
        for (std::streamsize i = 0; i < n; ++i) {
            hash ^= static_cast<unsigned char>(buf[i]);
            hash *= 1099511628211ULL;
        }
        size += static_cast<uint64_t>(n);
    }
    std::ostringstream out;
    out << size << "-" << std::hex << hash;
    return out.str();
}

Result recordImport(const std::string& projectFolder, const std::string& sourcePath)
{
    const fs::path record = toPath(projectFolder) / fs::u8path(IMPORT_RECORD);
    std::error_code ec;
    fs::create_directories(record.parent_path(), ec);
    std::ofstream out(record, std::ios::binary | std::ios::trunc);
    if (!out) {
        return fail("The import record could not be written.");
    }
    out << "originalPath=" << sourcePath << "\n" << "fingerprint=" << fileFingerprint(sourcePath) << "\n";
    return out ? success(record) : fail("The import record could not be written.");
}

Result keepOriginal(const std::string& projectFolder, const std::string& sourcePath)
{
    const fs::path source = toPath(sourcePath);
    std::error_code ec;
    if (!fs::is_regular_file(source, ec)) {
        return fail("The file \"" + sourcePath + "\" was not found.");
    }
    const fs::path folder = toPath(projectFolder) / fs::u8path(IMPORTED_ORIGINALS_FOLDER);
    fs::create_directories(folder, ec);
    const fs::path target = folder / source.filename();
    fs::copy_file(source, target, fs::copy_options::overwrite_existing, ec);
    if (ec || fileFingerprint(toUtf8(target)) != fileFingerprint(sourcePath)) {
        return fail("The original file could not be copied into the project.");
    }
    return success(target);
}

Result importScore(const std::string& mainFolder, const std::string& category, const std::string& name,
                   const std::string& sourcePath)
{
    const fs::path source = toPath(sourcePath);
    std::error_code ec;
    if (!fs::is_regular_file(source, ec)) {
        return fail("The score \"" + sourcePath + "\" was not found.");
    }
    Result project = createProject(mainFolder, category, name);
    if (!project.ok) {
        return project;
    }
    const std::string target = mainScorePath(project.path);
    fs::copy_file(source, toPath(target), fs::copy_options::none, ec);
    if (ec || fileFingerprint(target) != fileFingerprint(sourcePath)) {
        std::error_code ignore;
        fs::remove(toPath(target), ignore);
        removeUnusedProject(project.path);
        return fail("The score could not be copied into the project (" + (ec ? ec.message() : std::string("copy differs")) + ").");
    }
    recordImport(project.path, sourcePath);
    return success(toPath(target));
}

std::string findImportedProject(const std::string& mainFolder, const std::string& sourcePath)
{
    const std::string fingerprint = fileFingerprint(sourcePath);
    std::string byPath;
    for (const std::string& category : categories(mainFolder)) {
        for (const std::string& name : projects(mainFolder, category)) {
            const fs::path folder = toPath(mainFolder) / toPath(category) / toPath(name);
            std::ifstream in(folder / fs::u8path(IMPORT_RECORD), std::ios::binary);
            std::string line, original, fp;
            while (std::getline(in, line)) {
                if (line.rfind("originalPath=", 0) == 0) {
                    original = line.substr(13);
                } else if (line.rfind("fingerprint=", 0) == 0) {
                    fp = line.substr(12);
                }
            }
            if (!fingerprint.empty() && fp == fingerprint) {
                return toUtf8(folder);
            }
            if (!original.empty() && original == sourcePath && byPath.empty()) {
                byPath = toUtf8(folder);
            }
        }
    }
    return byPath;
}

static constexpr const char* SILENCED_LIST = ".jubelle-dont-ask-to-add.txt";

bool shouldOfferAddToProjects(const std::string& scorePath, bool newlyCreated, const std::string& mainFolder,
                              const std::set<std::string>& dismissedThisSession)
{
    if (scorePath.empty() || newlyCreated || mainFolder.empty()) {
        return false;   // new scores are saved into Projects anyway
    }
    if (isInsideMainFolder(scorePath, mainFolder)) {
        return false;
    }
    return !dismissedThisSession.count(scorePath) && !isAddToProjectsSilenced(mainFolder, scorePath);
}

bool isAddToProjectsSilenced(const std::string& mainFolder, const std::string& scorePath)
{
    std::ifstream in(toPath(mainFolder) / fs::u8path(SILENCED_LIST), std::ios::binary);
    std::string line;
    while (std::getline(in, line)) {
        if (line == scorePath) {
            return true;
        }
    }
    return false;
}

Result silenceAddToProjects(const std::string& mainFolder, const std::string& scorePath)
{
    if (isAddToProjectsSilenced(mainFolder, scorePath)) {
        return success(toPath(mainFolder) / fs::u8path(SILENCED_LIST));
    }
    const fs::path list = toPath(mainFolder) / fs::u8path(SILENCED_LIST);
    std::ofstream out(list, std::ios::binary | std::ios::app);
    out << scorePath << "\n";
    return out ? success(list) : fail("The list could not be written.");
}
}
