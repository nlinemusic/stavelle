// SPDX-License-Identifier: GPL-3.0-only
#include <gtest/gtest.h>

#include "corpus.h"

using namespace je::test;

TEST(JE_Corpus, PublicScoresAreThere)
{
    const auto scores = publicCorpus();
    EXPECT_GE(scores.size(), 10u);
    for (const auto& s : scores) {
        EXPECT_FALSE(s.isPrivate);
        EXPECT_EQ(s.displayName, s.path.filename().u8string());
    }
}

TEST(JE_Corpus, PrivateScoresAreNeverNamed)
{
    const auto scores = privateCorpus();
    if (scores.empty()) {
        GTEST_SKIP() << "no private corpus on this machine (normal in CI)";
    }
    for (const auto& s : scores) {
        EXPECT_TRUE(s.isPrivate);
        EXPECT_EQ(s.displayName.rfind("private #", 0), 0u);
        EXPECT_EQ(s.displayName.find(s.path.stem().u8string()), std::string::npos);
    }
}
