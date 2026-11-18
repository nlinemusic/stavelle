/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle G10: the YouTube connection's plain parts.
 */
#include <gtest/gtest.h>

#include <filesystem>

#include "project/internal/youtubeupload.h"

namespace jy = mu::project::jubelle;

TEST(Jubelle_YouTube, OnlyTheUploadPermissionIsAskedFor)
{
    EXPECT_STREQ(jy::YOUTUBE_UPLOAD_SCOPE, "https://www.googleapis.com/auth/youtube.upload");
}

TEST(Jubelle_YouTube, UploadDescriptionIsPrivateByDefaultAndSafe)
{
    const std::string json = jy::uploadMetadata("Blues \"for\" Basie <live>", "Line 1\nLine 2 \\ end", jy::Privacy::Private);
    EXPECT_NE(json.find("\"title\":\"Blues \\\"for\\\" Basie live\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"description\":\"Line 1\\nLine 2 \\\\ end\""), std::string::npos) << json;
    EXPECT_NE(json.find("\"privacyStatus\":\"private\""), std::string::npos);
    EXPECT_NE(json.find("\"categoryId\":\"10\""), std::string::npos) << "Music";
    EXPECT_NE(jy::uploadMetadata("x", "", jy::Privacy::Unlisted).find("\"unlisted\""), std::string::npos);
}

TEST(Jubelle_YouTube, TitlesFollowYouTubesLimits)
{
    EXPECT_EQ(jy::cleanTitle("   "), "Untitled score");
    EXPECT_EQ(jy::cleanTitle(std::string(150, 'a')).size(), 100u);
    // 100 characters, not 100 bytes: “é” is two bytes and must not be cut in half
    std::string accented;
    for (int i = 0; i < 120; ++i) {
        accented += "\xC3\xA9";
    }
    EXPECT_EQ(jy::cleanTitle(accented).size(), 200u);
    EXPECT_EQ(jy::videoLink("abc123"), "https://youtu.be/abc123");
}

TEST(Jubelle_YouTube, TheTokenIsKeptPrivatelyAndCanBeRemoved)
{
    const std::string dir = ::testing::TempDir() + "jubelle_youtube_secret";
    std::filesystem::create_directories(dir);
#ifdef __APPLE__
    // macOS: the Keychain. A build machine with no unlocked login keychain can't store anything;
    // that is reported as skipped, not passed.
    if (!jy::storeSecret("test", "refresh-token-value", dir)) {
        GTEST_SKIP() << "no unlocked Keychain on this machine";
    }
#else
    ASSERT_TRUE(jy::storeSecret("test", "refresh-token-value", dir));
#endif
    EXPECT_EQ(jy::readSecret("test", dir), "refresh-token-value");
    // storing again replaces the old value
    ASSERT_TRUE(jy::storeSecret("test", "second-value", dir));
    EXPECT_EQ(jy::readSecret("test", dir), "second-value");
#if !defined(_WIN32) && !defined(__APPLE__)
    const auto perms = std::filesystem::status(dir + "/jubelle_test.secret").permissions();
    EXPECT_EQ(perms & (std::filesystem::perms::group_all | std::filesystem::perms::others_all), std::filesystem::perms::none)
        << "nobody else can read it";
#endif
    jy::removeSecret("test", dir);
    EXPECT_EQ(jy::readSecret("test", dir), "");
}
