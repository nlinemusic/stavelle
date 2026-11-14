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
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <iterator>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "project/internal/projectlibrary.h"

namespace fs = std::filesystem;
using namespace mu::project::projectlibrary;

namespace {
class ProjectLibraryTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::random_device rd;
        m_root = fs::temp_directory_path() / ("jubelle_projectlibrary_" + std::to_string(rd()));
        fs::create_directories(m_root);
        m_main = (m_root / fs::u8path(mainFolderName())).u8string();
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }

    static void writeFile(const fs::path& p, const std::string& content)
    {
        std::ofstream out(p, std::ios::binary);
        out << content;
    }

    static std::string readFile(const fs::path& p)
    {
        std::ifstream in(p, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    static fs::path P(const std::string& utf8) { return fs::u8path(utf8); }

    fs::path m_root;
    std::string m_main;
};
}

TEST_F(ProjectLibraryTests, MainFolderIsCreatedWithDefaultCategoriesOnce)
{
    Result r = ensureMainFolder(m_main);
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(categories(m_main), defaultCategories());

    // The user deletes a default category; running setup again must not bring it back.
    ASSERT_TRUE(deleteCategory(m_main, "Other").ok);
    ASSERT_TRUE(ensureMainFolder(m_main).ok);
    for (const std::string& c : categories(m_main)) {
        EXPECT_NE(c, "Other");
    }
}

TEST_F(ProjectLibraryTests, CategoriesCanBeCreatedRenamedReorderedAndDeleted)
{
    ASSERT_TRUE(ensureMainFolder(m_main).ok);

    Result created = createCategory(m_main, "Choir");
    ASSERT_TRUE(created.ok) << created.error;
    EXPECT_EQ(categories(m_main).back(), "Choir");

    EXPECT_FALSE(createCategory(m_main, "Choir").ok) << "duplicate category must be refused";

    ASSERT_TRUE(renameCategory(m_main, "Choir", "Choral Works").ok);
    EXPECT_EQ(categories(m_main).back(), "Choral Works");

    ASSERT_TRUE(setCategoryOrder(m_main, { "Orchestra", "Choral Works" }).ok);
    std::vector<std::string> order = categories(m_main);
    ASSERT_GE(order.size(), 2u);
    EXPECT_EQ(order[0], "Orchestra");
    EXPECT_EQ(order[1], "Choral Works");
    EXPECT_EQ(order.size(), defaultCategories().size() + 1);

    ASSERT_TRUE(deleteCategory(m_main, "Choral Works").ok);
    EXPECT_FALSE(fs::exists(P(m_main) / "Choral Works"));
}

TEST_F(ProjectLibraryTests, CategoryRenameChangingOnlyCaseWorks)
{
    ASSERT_TRUE(ensureMainFolder(m_main).ok);
    Result r = renameCategory(m_main, "Orchestra", "ORCHESTRA");
    ASSERT_TRUE(r.ok) << r.error;
    auto cats = categories(m_main);
    EXPECT_NE(std::find(cats.begin(), cats.end(), "ORCHESTRA"), cats.end());
}

TEST_F(ProjectLibraryTests, NonEmptyCategoryIsNeverDeleted)
{
    ASSERT_TRUE(ensureMainFolder(m_main).ok);
    ASSERT_TRUE(createProject(m_main, "Orchestra", "Symphony").ok);

    Result r = deleteCategory(m_main, "Orchestra");
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(fs::exists(P(m_main) / "Orchestra" / "Symphony"));
}

TEST_F(ProjectLibraryTests, ProjectHasTheRequiredFoldersAndNoExports)
{
    ASSERT_TRUE(ensureMainFolder(m_main).ok);
    Result r = createProject(m_main, "Big Band - Jazz Band", "Song Name 1");
    ASSERT_TRUE(r.ok) << r.error;

    fs::path p = P(r.path);
    EXPECT_EQ(p.filename().u8string(), "Song Name 1");
    for (const char* sub : { "Backups", "PDF Exports/Full Score", "PDF Exports/Individual Parts",
                             "PDF Exports/Combined Parts", "Audio Exports", "Video Exports", "Other Exports" }) {
        EXPECT_TRUE(fs::is_directory(p / P(sub))) << sub;
    }

    // Folders only: no score, PDF, audio or other file is generated.
    size_t files = 0;
    for (auto& e : fs::recursive_directory_iterator(p)) {
        files += e.is_regular_file() ? 1 : 0;
    }
    EXPECT_EQ(files, 0u);

    EXPECT_TRUE(isProjectFolder(r.path));
    EXPECT_EQ(P(mainScorePath(r.path)).filename().u8string(), "Song Name 1.mscz");
    EXPECT_EQ(projects(m_main, "Big Band - Jazz Band"), std::vector<std::string>{ "Song Name 1" });
}

TEST_F(ProjectLibraryTests, DuplicateProjectNamesNeverOverwrite)
{
    ASSERT_TRUE(ensureMainFolder(m_main).ok);
    Result first = createProject(m_main, "Orchestra", "Overture");
    ASSERT_TRUE(first.ok);
    writeFile(mainScorePath(first.path), "original score");

    Result second = createProject(m_main, "Orchestra", "Overture");
    ASSERT_TRUE(second.ok);
    EXPECT_EQ(P(second.path).filename().u8string(), "Overture (2)");
    EXPECT_EQ(readFile(mainScorePath(first.path)), "original score");

    Result third = createProject(m_main, "Orchestra", "Overture");
    EXPECT_EQ(P(third.path).filename().u8string(), "Overture (3)");
}

TEST_F(ProjectLibraryTests, InvalidNamesAreRefused)
{
    for (const char* bad : { "", " Lead", "Trail ", ".hidden", "end.", "a/b", "a\\b", "a:b", "what?", "CON", "com1.txt", "nul" }) {
        EXPECT_FALSE(validateName(bad).empty()) << "should refuse: '" << bad << "'";
    }
    for (const char* good : { "Song Name 1", "Big Band - Jazz Band", "Préludes & Fugues", "交响曲", "Opus 3. No. 2", "Console" }) {
        EXPECT_TRUE(validateName(good).empty()) << "should accept: '" << good << "'";
    }
}

TEST_F(ProjectLibraryTests, RenameUpdatesFolderScoreAndBackupAndKeepsExports)
{
    ASSERT_TRUE(ensureMainFolder(m_main).ok);
    Result r = createProject(m_main, "Concert Band", "Working Title");
    ASSERT_TRUE(r.ok);
    writeFile(mainScorePath(r.path), "score data");
    writeFile(P(backupFolder(r.path)) / "Working Title.mscz~", "backup data");
    writeFile(P(exportFolder(r.path, ExportKind::FullScorePdf)) / "Working Title.pdf", "pdf");

    Result renamed = renameProject(r.path, "Final Title");
    ASSERT_TRUE(renamed.ok) << renamed.error;

    fs::path p = P(renamed.path);
    EXPECT_FALSE(fs::exists(P(r.path)));
    EXPECT_EQ(p.filename().u8string(), "Final Title");
    EXPECT_EQ(readFile(p / "Final Title.mscz"), "score data");
    EXPECT_EQ(readFile(p / "Backups" / "Final Title.mscz~"), "backup data");
    EXPECT_EQ(readFile(p / "PDF Exports" / "Full Score" / "Working Title.pdf"), "pdf");
    EXPECT_EQ(projectFolderForScore(mainScorePath(renamed.path)), renamed.path);
}

TEST_F(ProjectLibraryTests, RenameToExistingProjectIsRefusedAndChangesNothing)
{
    ASSERT_TRUE(ensureMainFolder(m_main).ok);
    Result a = createProject(m_main, "Orchestra", "First");
    Result b = createProject(m_main, "Orchestra", "Second");
    writeFile(mainScorePath(a.path), "first");
    writeFile(mainScorePath(b.path), "second");

    Result r = renameProject(a.path, "Second");
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(readFile(mainScorePath(a.path)), "first");
    EXPECT_EQ(readFile(mainScorePath(b.path)), "second");
}

TEST_F(ProjectLibraryTests, MoveKeepsEverythingAndRefusesCollisions)
{
    ASSERT_TRUE(ensureMainFolder(m_main).ok);
    Result r = createProject(m_main, "Other", "Suite");
    writeFile(mainScorePath(r.path), "score");
    writeFile(P(exportFolder(r.path, ExportKind::Audio)) / "Suite.wav", std::string(100000, 'a'));

    Result moved = moveProject(r.path, m_main, "Orchestra");
    ASSERT_TRUE(moved.ok) << moved.error;
    EXPECT_FALSE(fs::exists(P(r.path)));
    EXPECT_EQ(readFile(mainScorePath(moved.path)), "score");
    EXPECT_EQ(fs::file_size(P(moved.path) / "Audio Exports" / "Suite.wav"), 100000u);

    // Another "Suite" in Other cannot be moved on top of the first one.
    Result other = createProject(m_main, "Other", "Suite");
    writeFile(mainScorePath(other.path), "different");
    EXPECT_FALSE(moveProject(other.path, m_main, "Orchestra").ok);
    EXPECT_EQ(readFile(mainScorePath(moved.path)), "score");
    EXPECT_EQ(readFile(mainScorePath(other.path)), "different");
}

TEST_F(ProjectLibraryTests, ExportFoldersAreRecreatedIfDeleted)
{
    ASSERT_TRUE(ensureMainFolder(m_main).ok);
    Result r = createProject(m_main, "Other", "Piece");
    fs::remove_all(P(r.path) / "Video Exports");
    std::string v = exportFolder(r.path, ExportKind::Video);
    EXPECT_TRUE(fs::is_directory(P(v)));
    EXPECT_EQ(P(v).filename().u8string(), "Video Exports");
}

TEST_F(ProjectLibraryTests, ScoresOutsideProjectsAreNotTreatedAsProjects)
{
    fs::create_directories(m_root / "Loose");
    writeFile(m_root / "Loose" / "Loose.mscz", "x");
    EXPECT_EQ(projectFolderForScore((m_root / "Loose" / "Loose.mscz").u8string()), "");
}

TEST_F(ProjectLibraryTests, ExportsAreSortedIntoTheRightFolder)
{
    EXPECT_EQ(exportKindFor("pdf", false, false), ExportKind::FullScorePdf);
    EXPECT_EQ(exportKindFor("PDF", false, true), ExportKind::IndividualPartsPdf);
    EXPECT_EQ(exportKindFor("pdf", true, true), ExportKind::CombinedPartsPdf);
    for (const char* audio : { "wav", "mp3", "ogg", "flac" }) {
        EXPECT_EQ(exportKindFor(audio, false, true), ExportKind::Audio) << audio;
    }
    EXPECT_EQ(exportKindFor("mp4", false, false), ExportKind::Video);
    for (const char* other : { "png", "svg", "musicxml", "mxl", "mid", "brf", "mei" }) {
        EXPECT_EQ(exportKindFor(other, false, false), ExportKind::Other) << other;
    }
}

TEST_F(ProjectLibraryTests, SameFolderIsRecognised)
{
    ASSERT_TRUE(ensureMainFolder(m_main).ok);
    Result r = createProject(m_main, "Other", "Piece");
    const std::string parts = exportFolder(r.path, ExportKind::IndividualPartsPdf);
    EXPECT_TRUE(isSameFolder(parts, (P(r.path) / "PDF Exports" / "Individual Parts").u8string()));
    EXPECT_TRUE(isSameFolder(parts, (P(r.path) / "PDF Exports" / "." / "Individual Parts").u8string()));
    EXPECT_FALSE(isSameFolder(parts, exportFolder(r.path, ExportKind::FullScorePdf)));
    EXPECT_FALSE(isSameFolder(parts, ""));
}
