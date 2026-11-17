/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle G9: which pages a video shows, and in what order.
 */
#include <gtest/gtest.h>

#include "importexport/videoexport/internal/jubelle/videopages.h"

namespace jv = mu::iex::videoexport::jubelle;

namespace {
// 4 pages of 30 s; page 2 is repeated (played twice): 1, 2, 2, 3, 4
std::vector<jv::PageVisit> visits()
{
    return jv::pageVisits(150.0, 0.5, [](double t) {
        const int slot = int(t / 30.0);
        static const int PAGES[] = { 1, 2, 2, 3, 4 };
        return PAGES[std::min(slot, 4)];
    });
}
}

TEST(Jubelle_VideoPages, ReadingPageLists)
{
    EXPECT_TRUE(jv::parsePageList("", 6).pages.empty());
    EXPECT_EQ(jv::parsePageList("1-3, 5", 6).pages, (std::vector<int> { 1, 2, 3, 5 }));
    EXPECT_EQ(jv::parsePageList(" 2 ; 1 ", 6).pages, (std::vector<int> { 2, 1 }));
    EXPECT_EQ(jv::parsePageList("4\xE2\x80\x93" "6", 6).pages, (std::vector<int> { 4, 5, 6 })) << "an en dash works too";
    EXPECT_EQ(jv::parsePageList("3-1", 6).pages, (std::vector<int> { 3, 2, 1 }));

    const jv::PageList bad = jv::parsePageList("1-x", 6);
    EXPECT_FALSE(bad.ok);
    EXPECT_FALSE(bad.error.empty());
    const jv::PageList outside = jv::parsePageList("2-9", 6);
    EXPECT_FALSE(outside.ok);
    EXPECT_NE(outside.error.find("6 pages"), std::string::npos);
}

TEST(Jubelle_VideoPages, VisitsFollowTheMusicIncludingRepeats)
{
    const auto v = visits();
    ASSERT_EQ(v.size(), 4u) << "page 2 is one visit of 60 s here (its repeat follows straight on)";
    EXPECT_EQ(v[1].page, 2);
    EXPECT_NEAR(v[1].start, 30.0, 0.01);
    EXPECT_NEAR(v[1].end, 90.0, 0.01);
}

TEST(Jubelle_VideoPages, EveryPageIsTheWholePiece)
{
    const auto segs = jv::planSegments(visits(), jv::parsePageList("", 4));
    EXPECT_NEAR(jv::totalLength(segs), 150.0, 0.01);
}

TEST(Jubelle_VideoPages, ARangeKeepsOnlyThosePages)
{
    const auto segs = jv::planSegments(visits(), jv::parsePageList("2-3", 4));
    ASSERT_EQ(segs.size(), 2u);
    EXPECT_EQ(segs[0].page, 2);
    EXPECT_EQ(segs[1].page, 3);
    EXPECT_NEAR(jv::totalLength(segs), 90.0, 0.01);

    // the video starts with the music of page 2
    double music = 0.0;
    EXPECT_EQ(jv::segmentAt(segs, 0.0, &music), 0);
    EXPECT_NEAR(music, 30.0, 0.01);
    EXPECT_EQ(jv::segmentAt(segs, 61.0, &music), 1);
    EXPECT_NEAR(music, 91.0, 0.01);
    EXPECT_EQ(jv::segmentAt(segs, 95.0, &music), -1);
}

TEST(Jubelle_VideoPages, AChosenOrderPlaysPagesAgain)
{
    const auto segs = jv::planSegments(visits(), jv::parsePageList("1, 4, 1", 4));
    ASSERT_EQ(segs.size(), 3u);
    EXPECT_EQ(segs[0].page, 1);
    EXPECT_EQ(segs[1].page, 4);
    EXPECT_EQ(segs[2].page, 1);
    EXPECT_NEAR(segs[2].start, 0.0, 0.01) << "page 1 has one visit, so it is heard again from its start";
    EXPECT_NEAR(jv::totalLength(segs), 90.0, 0.01);
}

TEST(Jubelle_VideoPages, PagesWithoutMusicAreSkipped)
{
    // a title page (page 1) has no music: the music starts on page 2
    const auto v = jv::pageVisits(60.0, 0.5, [](double t) { return t < 30.0 ? 2 : 3; });
    const auto segs = jv::planSegments(v, jv::parsePageList("1-3", 3));
    ASSERT_EQ(segs.size(), 2u);
    EXPECT_EQ(segs[0].page, 2);
}
