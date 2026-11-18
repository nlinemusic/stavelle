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
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "project/internal/projectinfo.h"
#include "project/internal/projectlibrary.h"

namespace fs = std::filesystem;
using namespace mu::project;
using namespace mu::project::projectinfo;

namespace {
class ProjectInfoTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::random_device rd;
        m_root = fs::temp_directory_path() / ("jubelle_projectinfo_" + std::to_string(rd()));
        fs::create_directories(m_root);
        m_main = (m_root / fs::u8path(projectlibrary::mainFolderName())).u8string();
        ASSERT_TRUE(projectlibrary::ensureMainFolder(m_main).ok);
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
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    //! Creates a project with a score file whose modification time is `minutesAgo` in the past.
    std::string makeProject(const std::string& category, const std::string& name, const std::string& score = "score",
                            int minutesAgo = 0)
    {
        projectlibrary::Result r = projectlibrary::createProject(m_main, category, name);
        EXPECT_TRUE(r.ok) << r.error;
        const fs::path scorePath = fs::u8path(projectlibrary::mainScorePath(r.path));
        writeFile(scorePath, score);
        setAge(scorePath, minutesAgo);
        return r.path;
    }

    static void setAge(const fs::path& p, int minutesAgo)
    {
        fs::last_write_time(p, fs::file_time_type::clock::now() - std::chrono::minutes(minutesAgo));
    }

    //! A snapshot of a score file: its bytes and modification time.
    struct Snapshot {
        std::string bytes;
        fs::file_time_type time;
        bool operator==(const Snapshot& o) const { return bytes == o.bytes && time == o.time; }
    };

    static Snapshot snapshot(const std::string& projectFolder)
    {
        const fs::path p = fs::u8path(projectlibrary::mainScorePath(projectFolder));
        return Snapshot { readFile(p), fs::last_write_time(p) };
    }

    std::string makeImage(const std::string& name, const std::string& content)
    {
        const fs::path p = m_root / name;
        writeFile(p, content);
        return p.u8string();
    }

    static std::vector<std::string> names(const std::vector<std::string>& folders)
    {
        std::vector<std::string> out;
        for (const std::string& f : folders) {
            out.push_back(fs::u8path(f).filename().u8string());
        }
        return out;
    }

    static size_t cardImageFiles(const std::string& projectFolder)
    {
        size_t n = 0;
        std::error_code ec;
        for (fs::directory_iterator it(fs::u8path(projectFolder) / ".jubelle", ec), end; !ec && it != end; it.increment(ec)) {
            if (it->path().filename().u8string().rfind("card-image", 0) == 0) {
                ++n;
            }
        }
        return n;
    }

    fs::path m_root;
    std::string m_main;
};
}

TEST_F(ProjectInfoTests, InfoRoundTripsAndMissingFileIsEmpty)
{
    const std::string p = makeProject("Other", "Piece");
    ProjectInfo empty = read(p);
    EXPECT_TRUE(empty.tags.empty());
    EXPECT_TRUE(empty.extraCategories.empty());
    EXPECT_FALSE(empty.cardImage.has_value());

    ProjectInfo info;
    info.tags = { "Vocal", "Ballad \"slow\"", "Ünïcödé" };
    info.extraCategories = { "Concert Band" };
    info.cardImage = CardImage { "card-image.png", 25.5, 80.0 };
    ASSERT_TRUE(write(p, info).ok);

    ProjectInfo back = read(p);
    EXPECT_EQ(back.tags, info.tags);
    EXPECT_EQ(back.extraCategories, info.extraCategories);
    ASSERT_TRUE(back.cardImage.has_value());
    EXPECT_EQ(back.cardImage->fileName, "card-image.png");
    EXPECT_DOUBLE_EQ(back.cardImage->x, 25.5);
    EXPECT_DOUBLE_EQ(back.cardImage->y, 80.0);
}

TEST_F(ProjectInfoTests, DamagedInfoFileIsIgnoredSafely)
{
    const std::string p = makeProject("Other", "Piece");
    fs::create_directories(fs::u8path(p) / ".jubelle");
    writeFile(fs::u8path(p) / ".jubelle" / "project.json", "{ not json");
    ProjectInfo info = read(p);
    EXPECT_TRUE(info.tags.empty());
    EXPECT_FALSE(info.cardImage.has_value());
}

TEST_F(ProjectInfoTests, ScoreFileIsNeverChangedByAnyInfoAction)
{
    const std::string p = makeProject("Big Band - Jazz Band", "Chart", "the score bytes", 30);
    const Snapshot before = snapshot(p);

    ASSERT_TRUE(setTags(p, m_main, { "Vocal" }).ok);
    ASSERT_TRUE(setCardImage(p, makeImage("a.png", "PNG-A"), 50, 50).ok);
    ASSERT_TRUE(setCardImagePosition(p, 10, 90).ok);
    ASSERT_TRUE(setCardImage(p, makeImage("b.jpg", "JPG-B"), 50, 50).ok);
    ASSERT_TRUE(removeCardImage(p).ok);
    Result cats = setCategories(p, m_main, { "Big Band - Jazz Band", "Small Ensembles" });
    ASSERT_TRUE(cats.ok) << cats.error;

    EXPECT_TRUE(snapshot(p) == before);
}

TEST_F(ProjectInfoTests, OnlyOneCardImageIsKeptAndReplacementWorks)
{
    const std::string p = makeProject("Other", "Piece");
    const std::string original = makeImage("photo.png", "ORIGINAL-PNG");

    ASSERT_TRUE(setCardImage(p, original, 50, 50).ok);
    EXPECT_EQ(cardImageFiles(p), 1u);
    EXPECT_EQ(readFile(fs::u8path(cardImagePath(p))), "ORIGINAL-PNG");

    ASSERT_TRUE(setCardImage(p, makeImage("other.jpg", "REPLACEMENT"), 30, 70).ok);
    EXPECT_EQ(cardImageFiles(p), 1u);
    EXPECT_EQ(readFile(fs::u8path(cardImagePath(p))), "REPLACEMENT");
    EXPECT_DOUBLE_EQ(read(p).cardImage->x, 30);

    // The user's own picture is never touched.
    EXPECT_EQ(readFile(fs::u8path(original)), "ORIGINAL-PNG");
}

TEST_F(ProjectInfoTests, RemoveDeletesTheStoredCopy)
{
    const std::string p = makeProject("Other", "Piece");
    ASSERT_TRUE(setCardImage(p, makeImage("photo.png", "X"), 50, 50).ok);
    ASSERT_TRUE(removeCardImage(p).ok);
    EXPECT_EQ(cardImageFiles(p), 0u);
    EXPECT_EQ(cardImagePath(p), "");
    EXPECT_FALSE(read(p).cardImage.has_value());
}

TEST_F(ProjectInfoTests, InvalidImageIsRefusedAndExistingImageKept)
{
    const std::string p = makeProject("Other", "Piece");
    ASSERT_TRUE(setCardImage(p, makeImage("photo.png", "KEEP"), 40, 60).ok);
    EXPECT_FALSE(setCardImage(p, makeImage("notes.txt", "text"), 50, 50).ok);
    EXPECT_FALSE(setCardImage(p, (m_root / "missing.png").u8string(), 50, 50).ok);
    EXPECT_EQ(readFile(fs::u8path(cardImagePath(p))), "KEEP");
    EXPECT_DOUBLE_EQ(read(p).cardImage->x, 40);
}

TEST_F(ProjectInfoTests, CropPositionIsClampedAndPersists)
{
    const std::string p = makeProject("Other", "Piece");
    ASSERT_TRUE(setCardImage(p, makeImage("photo.webp", "W"), 150, -20).ok);
    ProjectInfo info = read(p);
    EXPECT_DOUBLE_EQ(info.cardImage->x, 100);
    EXPECT_DOUBLE_EQ(info.cardImage->y, 0);
}

TEST_F(ProjectInfoTests, CardImageAndTagsSurviveRenameAndMove)
{
    std::string p = makeProject("Other", "Piece");
    ASSERT_TRUE(setCardImage(p, makeImage("photo.png", "IMG"), 20, 30).ok);
    ASSERT_TRUE(setTags(p, m_main, { "Swing" }).ok);

    Result renamed = projectlibrary::renameProject(p, "Renamed Piece");
    ASSERT_TRUE(renamed.ok) << renamed.error;
    Result moved = moveToCategory(renamed.path, m_main, "Orchestra");
    ASSERT_TRUE(moved.ok) << moved.error;

    EXPECT_EQ(readFile(fs::u8path(cardImagePath(moved.path))), "IMG");
    EXPECT_EQ(read(moved.path).tags, std::vector<std::string>({ "Swing" }));
    EXPECT_DOUBLE_EQ(read(moved.path).cardImage->y, 30);
}

TEST_F(ProjectInfoTests, ProjectCanBeListedInSeveralCategoriesWithoutCopies)
{
    const std::string p = makeProject("Big Band - Jazz Band", "Chart");
    Result r = setCategories(p, m_main, { "Big Band - Jazz Band", "Small Ensembles" });
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(r.path, p); // home kept, folder not moved

    EXPECT_EQ(names(projectsIn(m_main, "Big Band - Jazz Band")), std::vector<std::string>({ "Chart" }));
    EXPECT_EQ(names(projectsIn(m_main, "Small Ensembles")), std::vector<std::string>({ "Chart" }));
    EXPECT_TRUE(projectsIn(m_main, "Orchestra").empty());
    EXPECT_EQ(allProjects(m_main).size(), 1u); // one folder, one score file
    EXPECT_EQ(categoriesOf(p, m_main), std::vector<std::string>({ "Big Band - Jazz Band", "Small Ensembles" }));

    // Removing a category only unlists the score.
    ASSERT_TRUE(setCategories(p, m_main, { "Big Band - Jazz Band" }).ok);
    EXPECT_TRUE(projectsIn(m_main, "Small Ensembles").empty());
    EXPECT_TRUE(fs::exists(fs::u8path(projectlibrary::mainScorePath(p))));
}

TEST_F(ProjectInfoTests, UntickingHomeCategoryMovesFolderSafely)
{
    const std::string p = makeProject("Big Band - Jazz Band", "Chart", "bytes");
    ASSERT_TRUE(setCardImage(p, makeImage("photo.png", "IMG"), 50, 50).ok);

    Result r = setCategories(p, m_main, { "Small Ensembles", "Other" });
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_EQ(homeCategory(r.path), "Small Ensembles");
    EXPECT_FALSE(fs::exists(fs::u8path(p)));
    EXPECT_EQ(readFile(fs::u8path(projectlibrary::mainScorePath(r.path))), "bytes");
    EXPECT_EQ(read(r.path).extraCategories, std::vector<std::string>({ "Other" }));
    EXPECT_EQ(readFile(fs::u8path(cardImagePath(r.path))), "IMG");
}

TEST_F(ProjectInfoTests, AtLeastOneCategoryIsRequired)
{
    const std::string p = makeProject("Other", "Piece");
    EXPECT_FALSE(setCategories(p, m_main, {}).ok);
    EXPECT_FALSE(setCategories(p, m_main, { "No Such Category" }).ok);
    EXPECT_EQ(homeCategory(p), "Other");
}

TEST_F(ProjectInfoTests, CategoryRenameUpdatesListingsAndDeleteRefusesWhileListed)
{
    const std::string p = makeProject("Other", "Piece");
    ASSERT_TRUE(setCategories(p, m_main, { "Other", "Concert Band" }).ok);

    EXPECT_TRUE(isCategoryInUse(m_main, "Concert Band"));
    EXPECT_FALSE(deleteCategory(m_main, "Concert Band").ok);

    ASSERT_TRUE(renameCategory(m_main, "Concert Band", "Wind Band").ok);
    EXPECT_EQ(read(p).extraCategories, std::vector<std::string>({ "Wind Band" }));
    EXPECT_EQ(names(projectsIn(m_main, "Wind Band")), std::vector<std::string>({ "Piece" }));

    ASSERT_TRUE(setCategories(p, m_main, { "Other" }).ok);
    EXPECT_FALSE(isCategoryInUse(m_main, "Wind Band"));
    EXPECT_TRUE(deleteCategory(m_main, "Wind Band").ok);
}

TEST_F(ProjectInfoTests, TagsAreReusableAndCaseInsensitive)
{
    const std::string a = makeProject("Big Band - Jazz Band", "Chart A");
    const std::string b = makeProject("Concert Band", "Piece B");

    ASSERT_TRUE(addTagToLibrary(m_main, "Vocal").ok);
    ASSERT_TRUE(addTagToLibrary(m_main, "vocal").ok); // no duplicate
    ASSERT_TRUE(setTags(a, m_main, { "vocal", "Swing", " Swing " }).ok);
    ASSERT_TRUE(setTags(b, m_main, { "VOCAL" }).ok);

    EXPECT_EQ(read(a).tags, std::vector<std::string>({ "Vocal", "Swing" }));
    EXPECT_EQ(read(b).tags, std::vector<std::string>({ "Vocal" }));
    EXPECT_EQ(tagLibrary(m_main), std::vector<std::string>({ "Swing", "Vocal" }));

    // A tag created without any score keeps existing.
    ASSERT_TRUE(addTagToLibrary(m_main, "Latin").ok);
    EXPECT_EQ(tagLibrary(m_main), std::vector<std::string>({ "Latin", "Swing", "Vocal" }));
}

TEST_F(ProjectInfoTests, TagMatchingAllAndAny)
{
    const std::vector<std::string> tags { "Vocal", "Ballad" };
    EXPECT_TRUE(matchesTags(tags, {}, TagMatch::All));
    EXPECT_TRUE(matchesTags(tags, { "vocal" }, TagMatch::All));
    EXPECT_TRUE(matchesTags(tags, { "Vocal", "Ballad" }, TagMatch::All));
    EXPECT_FALSE(matchesTags(tags, { "Vocal", "Latin" }, TagMatch::All));
    EXPECT_TRUE(matchesTags(tags, { "Vocal", "Latin" }, TagMatch::Any));
    EXPECT_FALSE(matchesTags(tags, { "Swing", "Latin" }, TagMatch::Any));
}

TEST_F(ProjectInfoTests, RecentIsNewestSavedFirstLimitedAndWithoutDuplicates)
{
    for (int i = 0; i < 12; ++i) {
        makeProject("Other", "Piece " + std::to_string(i), "s", 100 - i); // Piece 11 newest
    }
    const std::string multi = makeProject("Big Band - Jazz Band", "Multi", "s", 200);
    ASSERT_TRUE(setCategories(multi, m_main, { "Big Band - Jazz Band", "Orchestra", "Other" }).ok);
    setAge(fs::u8path(projectlibrary::mainScorePath(multi)), 0); // just saved

    std::vector<RecentScore> recent = recentScores(m_main, 10);
    ASSERT_EQ(recent.size(), 10u);
    EXPECT_EQ(fs::u8path(recent[0].projectFolder).filename().u8string(), "Multi");
    EXPECT_EQ(fs::u8path(recent[1].projectFolder).filename().u8string(), "Piece 11");

    std::vector<std::string> seen;
    for (const RecentScore& r : recent) {
        EXPECT_EQ(std::count(seen.begin(), seen.end(), r.scorePath), 0) << r.scorePath;
        seen.push_back(r.scorePath);
    }
    for (size_t i = 1; i < recent.size(); ++i) {
        EXPECT_GE(recent[i - 1].savedTime, recent[i].savedTime);
    }
}

TEST_F(ProjectInfoTests, RecentReflectsSavesRenamesMovesAndRestart)
{
    const std::string a = makeProject("Other", "Alpha", "a", 50);
    const std::string b = makeProject("Other", "Beta", "b", 40);

    // A save (the score file changes) moves Alpha to the top.
    writeFile(fs::u8path(projectlibrary::mainScorePath(a)), "a2");
    std::vector<RecentScore> recent = recentScores(m_main);
    ASSERT_EQ(recent.size(), 2u);
    EXPECT_EQ(recent[0].scorePath, projectlibrary::mainScorePath(a));

    // Renaming and moving keep it listed once, under its new location.
    Result renamed = projectlibrary::renameProject(a, "Alpha 2");
    ASSERT_TRUE(renamed.ok);
    Result moved = moveToCategory(renamed.path, m_main, "Orchestra");
    ASSERT_TRUE(moved.ok);
    recent = recentScores(m_main);
    ASSERT_EQ(recent.size(), 2u);
    EXPECT_EQ(recent[0].scorePath, projectlibrary::mainScorePath(moved.path));
    EXPECT_EQ(recent[1].scorePath, projectlibrary::mainScorePath(b));

    // "Restart": nothing is cached, so a fresh call gives the same answer.
    std::vector<RecentScore> again = recentScores(m_main);
    ASSERT_EQ(again.size(), 2u);
    EXPECT_EQ(again[0].scorePath, recent[0].scorePath);
}

TEST_F(ProjectInfoTests, RecentAfterSaveAsCountsTheNewScoreAndKeepsTheOriginalTime)
{
    const std::string original = makeProject("Other", "Original", "o", 60);
    const auto originalTime = recentScores(m_main).at(0).savedTime;

    // Save As into a new project of its own.
    const std::string copy = makeProject("Concert Band", "Copy", "o", 0);
    std::vector<RecentScore> recent = recentScores(m_main);
    ASSERT_EQ(recent.size(), 2u);
    EXPECT_EQ(recent[0].scorePath, projectlibrary::mainScorePath(copy));
    EXPECT_EQ(recent[1].scorePath, projectlibrary::mainScorePath(original));
    EXPECT_EQ(recent[1].savedTime, originalTime);
}

TEST_F(ProjectInfoTests, ProjectsWithoutAScoreAreNotRecent)
{
    ASSERT_TRUE(projectlibrary::createProject(m_main, "Other", "Empty").ok);
    EXPECT_TRUE(recentScores(m_main).empty());
}

TEST_F(ProjectInfoTests, VideoExportsAreListedNewestFirstWithoutChangingAnything)
{
    const std::string p = makeProject("Other", "Film", "s", 5);
    EXPECT_TRUE(videoExports(p).empty()); // folder may be missing or empty: no crash, no list

    const fs::path videos = fs::u8path(projectlibrary::exportFolder(p, projectlibrary::ExportKind::Video));
    writeFile(videos / "old.mp4", "v1");
    setAge(videos / "old.mp4", 30);
    writeFile(videos / "new.MP4", "v2");
    setAge(videos / "new.MP4", 1);
    writeFile(videos / "notes.txt", "not a video");

    const Snapshot before = snapshot(p);
    std::vector<VideoFile> list = videoExports(p);
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(fs::u8path(list[0].path).filename().u8string(), "new.MP4");
    EXPECT_EQ(fs::u8path(list[1].path).filename().u8string(), "old.mp4");
    EXPECT_TRUE(snapshot(p) == before);
    EXPECT_EQ(readFile(videos / "old.mp4"), "v1");
}
