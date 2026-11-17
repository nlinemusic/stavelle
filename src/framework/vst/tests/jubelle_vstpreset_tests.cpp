/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle: tests for the VST3 preset (.vstpreset) reader.
 */
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include "framework/vst/internal/jubelle/vstpresetfile.h"

using namespace muse::vst::jubelle;
namespace fs = std::filesystem;

namespace {
const std::string CID = "565354416D73356A7562656C6C650000"; // 32 hex characters

void writeFile(const fs::path& p, const std::string& bytes)
{
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out.write(bytes.data(), std::streamsize(bytes.size()));
}

struct TempDir {
    fs::path path;
    TempDir()
    {
        path = fs::temp_directory_path() / ("jubelle_vstpreset_" + std::to_string(::testing::UnitTest::GetInstance()->random_seed())
                                            + "_" + ::testing::UnitTest::GetInstance()->current_test_info()->name());
        fs::remove_all(path);
        fs::create_directories(path);
    }
    ~TempDir() { std::error_code ec; fs::remove_all(path, ec); }
};
}

TEST(JubelleVstPreset, RoundTripKeepsBothStatesAndClassId)
{
    const std::string comp("component\0state\xff", 16);
    const std::string cont = "controller state";
    const auto parsed = parseVstPreset(makeVstPreset(CID, comp, cont));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->classId, CID);
    EXPECT_EQ(parsed->componentState, comp);
    EXPECT_EQ(parsed->controllerState, cont);
}

TEST(JubelleVstPreset, ComponentOnlyPresetIsValid)
{
    const auto parsed = parseVstPreset(makeVstPreset(CID, "abc", ""));
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->componentState, "abc");
    EXPECT_TRUE(parsed->controllerState.empty());
}

TEST(JubelleVstPreset, RejectsWrongMagicTruncationAndBadChunks)
{
    EXPECT_FALSE(parseVstPreset("").has_value());
    EXPECT_FALSE(parseVstPreset("RIFF0000").has_value());

    std::string good = makeVstPreset(CID, "abcdef", "xyz");
    std::string notVst = good;
    notVst[0] = 'X';
    EXPECT_FALSE(parseVstPreset(notVst).has_value());

    // cut off inside the chunk list
    EXPECT_FALSE(parseVstPreset(good.substr(0, good.size() - 5)).has_value());

    // a chunk that claims to run past the end of the file
    std::string bad = good;
    const size_t list = bad.rfind("List");
    ASSERT_NE(list, std::string::npos);
    bad[list + 8 + 4 + 8] = char(0x7f); // first chunk's size, low byte
    bad[list + 8 + 4 + 8 + 3] = char(0x7f);
    EXPECT_FALSE(parseVstPreset(bad).has_value());
}

TEST(JubelleVstPreset, ClassIdsCompareWithoutCaseOrSeparators)
{
    EXPECT_TRUE(sameClassId("565354416d73356a7562656c6c650000", CID));
    EXPECT_TRUE(sameClassId("{56535441-6D73-356A-7562-656C6C650000}", CID));
    EXPECT_FALSE(sameClassId("565354416D73356A7562656C6C650001", CID));
    EXPECT_FALSE(sameClassId("", ""));
}

TEST(JubelleVstPreset, FindsOnlyThisPluginsPresetsSortedByFolder)
{
    TempDir tmp;
    const fs::path root = tmp.path / "VST3 Presets";
    const std::string mine = makeVstPreset(CID, "s", "");
    const std::string other = makeVstPreset("00000000000000000000000000000000", "s", "");

    writeFile(root / "Acme" / "Big Horns" / "Trumpet Lead.vstpreset", mine);
    writeFile(root / "Acme" / "Big Horns" / "Sections" / "Saxes.vstpreset", mine);
    writeFile(root / "Acme" / "Big Horns" / "Alto Solo.VSTPRESET", mine);
    writeFile(root / "Acme" / "Big Horns" / "readme.txt", "not a preset");
    writeFile(root / "Acme" / "Big Horns" / "Wrong Plugin.vstpreset", other);  // another plugin's class ID
    writeFile(root / "Acme" / "Strings" / "Violins.vstpreset", mine);          // another plugin's folder
    writeFile(root / "OtherVendor" / "big horns" / "Shared.vstpreset", mine);  // same plugin name, other vendor folder

    const auto presets = findPresets({ root }, "Acme", "Big Horns", CID);
    std::vector<std::string> names;
    for (const auto& p : presets) {
        names.push_back(p.group + "|" + p.title);
    }
    const std::vector<std::string> expected { "|Alto Solo", "|Shared", "|Trumpet Lead", "Sections|Saxes" };
    EXPECT_EQ(names, expected);
}

TEST(JubelleVstPreset, WithoutAClassIdEveryPresetInThePluginFolderCounts)
{
    TempDir tmp;
    const fs::path root = tmp.path / "presets";
    writeFile(root / "Acme" / "Big Horns" / "A.vstpreset", makeVstPreset(CID, "s", ""));
    writeFile(root / "Acme" / "Big Horns" / "B.vstpreset", makeVstPreset("11111111111111111111111111111111", "s", ""));
    EXPECT_EQ(findPresets({ root }, "Acme", "Big Horns", "").size(), 2u);
    EXPECT_TRUE(findPresets({ root }, "Acme", "", CID).empty());
}

TEST(JubelleVstPreset, ReadsAPresetFileFromDisk)
{
    TempDir tmp;
    const fs::path file = tmp.path / "x.vstpreset";
    writeFile(file, makeVstPreset(CID, "component", "controller"));
    const auto parsed = readVstPresetFile(file);
    ASSERT_TRUE(parsed.has_value());
    EXPECT_EQ(parsed->componentState, "component");
    EXPECT_FALSE(readVstPresetFile(tmp.path / "missing.vstpreset").has_value());
}
