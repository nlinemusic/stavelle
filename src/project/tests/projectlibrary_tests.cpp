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

// ---- Jubelle R1: bringing existing scores into Projects ----

TEST_F(ProjectLibraryTests, ImportCopiesTheScoreAndNeverTouchesTheOriginal)
{
    ASSERT_TRUE(ensureMainFolder(m_main).ok);
    const fs::path outside = m_root / "Downloads" / "Old Chart.mscz";
    fs::create_directories(outside.parent_path());
    writeFile(outside, std::string("PK\x03\x04 score bytes", 18));
    const auto before = fs::last_write_time(outside);

    const Result r = importScore(m_main, defaultCategories().front(), "Old Chart", outside.u8string());
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(readFile(P(r.path)), readFile(outside)) << "byte for byte";
    EXPECT_TRUE(isProjectFolder(P(r.path).parent_path().u8string())) << "a normal project, with its subfolders";
    EXPECT_TRUE(fs::exists(outside)) << "the original stays";
    EXPECT_EQ(fs::last_write_time(outside), before) << "and is unchanged";
    EXPECT_TRUE(isInsideMainFolder(r.path, m_main));
    EXPECT_FALSE(isInsideMainFolder(outside.u8string(), m_main));
}

TEST_F(ProjectLibraryTests, TheSameFileIsntImportedTwice)
{
    ASSERT_TRUE(ensureMainFolder(m_main).ok);
    const fs::path outside = m_root / "Chart.mscz";
    writeFile(outside, "contents one");
    EXPECT_EQ(findImportedProject(m_main, outside.u8string()), "");
    const Result r = importScore(m_main, defaultCategories().front(), "Chart", outside.u8string());
    ASSERT_TRUE(r.ok);
    const std::string project = P(r.path).parent_path().u8string();
    EXPECT_EQ(findImportedProject(m_main, outside.u8string()), project) << "found by contents";

    // a copy of the same file elsewhere is recognised too
    const fs::path copy = m_root / "Copy of Chart.mscz";
    writeFile(copy, "contents one");
    EXPECT_EQ(findImportedProject(m_main, copy.u8string()), project);

    // the original changed since: still found by its path
    writeFile(outside, "contents two, edited in MuseScore");
    EXPECT_EQ(findImportedProject(m_main, outside.u8string()), project);

    // a different file is not
    const fs::path other = m_root / "Other.mscz";
    writeFile(other, "something else");
    EXPECT_EQ(findImportedProject(m_main, other.u8string()), "");
}

TEST_F(ProjectLibraryTests, FailedImportLeavesNoEmptyProject)
{
    ASSERT_TRUE(ensureMainFolder(m_main).ok);
    const std::string category = defaultCategories().front();
    const Result r = importScore(m_main, category, "Missing", (m_root / "does not exist.mscz").u8string());
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(projects(m_main, category).empty());
}

TEST_F(ProjectLibraryTests, InsideMainFolderIsNotFooledBySimilarNames)
{
    ASSERT_TRUE(ensureMainFolder(m_main).ok);
    EXPECT_FALSE(isInsideMainFolder(m_main + " (old)/Song/Song.mscz", m_main));
    EXPECT_FALSE(isInsideMainFolder(m_main, m_main)) << "the folder itself isn't inside itself";
    EXPECT_TRUE(isInsideMainFolder(m_main + "/Jazz/Song/Song.mscz", m_main));
}

TEST_F(ProjectLibraryTests, DontAskAgainIsRememberedPerFile)
{
    ASSERT_TRUE(ensureMainFolder(m_main).ok);
    const std::string a = (m_root / "a.mscz").u8string();
    const std::string b = (m_root / "b.mscz").u8string();
    EXPECT_FALSE(isAddToProjectsSilenced(m_main, a));
    ASSERT_TRUE(silenceAddToProjects(m_main, a).ok);
    ASSERT_TRUE(silenceAddToProjects(m_main, a).ok) << "twice is fine";
    EXPECT_TRUE(isAddToProjectsSilenced(m_main, a));
    EXPECT_FALSE(isAddToProjectsSilenced(m_main, b));
    EXPECT_TRUE(categories(m_main) == defaultCategories() || !categories(m_main).empty()) << "the hidden list isn't a category";
    for (const std::string& c : categories(m_main)) {
        EXPECT_NE(c.front(), '.');
    }
}

TEST_F(ProjectLibraryTests, NotInProjectsBarShowsOnlyWhenItShould)
{
    ASSERT_TRUE(ensureMainFolder(m_main).ok);
    const std::string outside = (m_root / "Downloads" / "Chart.mscz").u8string();
    const std::string inside = m_main + "/Jazz/Song/Song.mscz";
    std::set<std::string> dismissed;

    EXPECT_TRUE(shouldOfferAddToProjects(outside, false, m_main, dismissed));
    EXPECT_FALSE(shouldOfferAddToProjects(inside, false, m_main, dismissed)) << "already in Projects";
    EXPECT_FALSE(shouldOfferAddToProjects(outside, true, m_main, dismissed)) << "a new score";
    EXPECT_FALSE(shouldOfferAddToProjects("", false, m_main, dismissed));

    dismissed.insert(outside);   // "Not now"
    EXPECT_FALSE(shouldOfferAddToProjects(outside, false, m_main, dismissed));
    dismissed.clear();           // next session
    EXPECT_TRUE(shouldOfferAddToProjects(outside, false, m_main, dismissed));

    ASSERT_TRUE(silenceAddToProjects(m_main, outside).ok);   // "Don't ask for this file"
    EXPECT_FALSE(shouldOfferAddToProjects(outside, false, m_main, dismissed));
    EXPECT_TRUE(shouldOfferAddToProjects((m_root / "Other.mscz").u8string(), false, m_main, dismissed));
}

TEST_F(ProjectLibraryTests, PicturesFolderIsMadeOnlyWhenAskedFor)
{
    ASSERT_TRUE(ensureMainFolder(m_main).ok);
    const Result r = createProject(m_main, defaultCategories().front(), "Song");
    ASSERT_TRUE(r.ok) << r.error;
    const std::string project = r.path;   // the project folder
    EXPECT_EQ(picturesFolder(project, false), "") << "not made until you want it";
    EXPECT_FALSE(fs::exists(P(project) / "Pictures"));
    const std::string pics = picturesFolder(project, true);
    ASSERT_FALSE(pics.empty());
    EXPECT_TRUE(fs::is_directory(P(pics)));
    EXPECT_EQ(picturesFolder(project, false), pics) << "found afterwards";
    EXPECT_EQ(picturesFolder((m_root / "not a project").u8string(), true), "") << "only inside a project";
    EXPECT_TRUE(isProjectFolder(project)) << "still a normal project";
}
