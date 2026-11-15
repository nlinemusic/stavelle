/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */
#include <gtest/gtest.h>

#include "project/internal/usertemplates.h"

using namespace mu::project::usertemplates;

TEST(Jubelle_UserTemplates, TopLevelTemplatesAreMyTemplates)
{
    EXPECT_EQ(categoryFor("Shout chart.mscz"), "");
    EXPECT_EQ(categoryFor("Shout chart/Shout chart.mscx"), "") << "a template kept as a folder";
}

TEST(Jubelle_UserTemplates, FoldersAreCategories)
{
    EXPECT_EQ(categoryFor("Big band charts/Shout chart.mscz"), "Big band charts");
    EXPECT_EQ(categoryFor("Big band charts/Shout chart/Shout chart.mscx"), "Big band charts");
    EXPECT_EQ(categoryFor("Big band charts\\Ballad.mscz"), "Big band charts") << "Windows separators";
    EXPECT_EQ(categoryFor("Studio/Strings/Pad.mscz"), "Studio") << "deeper folders belong to the top category";
}

TEST(Jubelle_UserTemplates, SafeFileNames)
{
    EXPECT_EQ(fileNameFor("Big Band: Shout / Ballad?"), "Big Band_ Shout _ Ballad_");
    EXPECT_EQ(fileNameFor("  Sax Soli.  "), "Sax Soli");
    EXPECT_EQ(fileNameFor(""), "Template");
    EXPECT_EQ(fileNameFor("Café ♭9"), "Café ♭9") << "non-ASCII kept";
}
