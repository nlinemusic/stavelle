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

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

#include "project/internal/projectlibrary.h"

//! Jubelle Phase 3: the library parts of "New score creates its project folder".
namespace fs = std::filesystem;
using namespace mu::project::projectlibrary;

namespace {
class NewProjectTests : public ::testing::Test
{
protected:
    void SetUp() override
    {
        std::random_device rd;
        m_root = fs::temp_directory_path() / ("jubelle_newproject_" + std::to_string(rd()));
        fs::create_directories(m_root);
        m_main = (m_root / fs::u8path(mainFolderName())).u8string();
        ASSERT_TRUE(ensureMainFolder(m_main).ok);
    }

    void TearDown() override
    {
        std::error_code ec;
        fs::remove_all(m_root, ec);
    }

    fs::path m_root;
    std::string m_main;
};
}

TEST_F(NewProjectTests, AvailableNameMatchesWhatCreateProjectMakes)
{
    EXPECT_EQ(availableProjectName(m_main, "Concert Band", "Fanfare"), "Fanfare");

    Result first = createProject(m_main, "Concert Band", "Fanfare");
    ASSERT_TRUE(first.ok) << first.error;
    EXPECT_EQ(availableProjectName(m_main, "Concert Band", "Fanfare"), "Fanfare (2)");

    Result second = createProject(m_main, "Concert Band", "Fanfare");
    ASSERT_TRUE(second.ok) << second.error;
    EXPECT_EQ(fs::u8path(second.path).filename().u8string(), "Fanfare (2)");
    EXPECT_EQ(availableProjectName(m_main, "Concert Band", "Fanfare"), "Fanfare (3)");

    // Other categories are independent.
    EXPECT_EQ(availableProjectName(m_main, "Orchestra", "Fanfare"), "Fanfare");
}

TEST_F(NewProjectTests, NewProjectFolderHasFullStructureAndScorePath)
{
    Result r = createProject(m_main, "Big Band - Jazz Band", "Night Train");
    ASSERT_TRUE(r.ok) << r.error;
    EXPECT_TRUE(isProjectFolder(r.path));
    EXPECT_EQ(fs::u8path(mainScorePath(r.path)).filename().u8string(), "Night Train.mscz");
    EXPECT_EQ(fs::u8path(r.path).parent_path().filename().u8string(), "Big Band - Jazz Band");
    // No score file is created by the library; the app saves it.
    EXPECT_FALSE(fs::exists(fs::u8path(mainScorePath(r.path))));
}

TEST_F(NewProjectTests, UnusedProjectIsRemovedButOneWithFilesIsKept)
{
    Result empty = createProject(m_main, "Other", "Abandoned");
    ASSERT_TRUE(empty.ok);
    EXPECT_TRUE(removeUnusedProject(empty.path).ok);
    EXPECT_FALSE(fs::exists(fs::u8path(empty.path)));

    Result used = createProject(m_main, "Other", "Kept");
    ASSERT_TRUE(used.ok);
    {
        std::ofstream out(fs::u8path(mainScorePath(used.path)), std::ios::binary);
        out << "score";
    }
    Result refused = removeUnusedProject(used.path);
    EXPECT_FALSE(refused.ok);
    EXPECT_TRUE(fs::exists(fs::u8path(mainScorePath(used.path))));
    EXPECT_TRUE(isProjectFolder(used.path));

    // A file deep inside (e.g. an export) also keeps the folder.
    Result deep = createProject(m_main, "Other", "Deep");
    ASSERT_TRUE(deep.ok);
    {
        std::ofstream out(fs::u8path(exportFolder(deep.path, ExportKind::Audio)) / "take.wav", std::ios::binary);
        out << "x";
    }
    EXPECT_FALSE(removeUnusedProject(deep.path).ok);
    EXPECT_TRUE(fs::exists(fs::u8path(deep.path)));

    // Refuses a folder that is not a project at all.
    EXPECT_FALSE(removeUnusedProject((fs::u8path(m_main) / "Other").u8string()).ok);
    EXPECT_TRUE(fs::exists(fs::u8path(m_main) / "Other"));
}
