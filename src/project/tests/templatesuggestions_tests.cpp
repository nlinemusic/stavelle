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

#include "project/internal/templatesuggestions.h"

using mu::project::templatesuggestions::isSuggested;

// Built-in MuseScore template categories: "General", "Choral", "Chamber Music", "Solo",
// "Jazz", "Popular", "Band and Percussion", "Orchestral"; custom templates are in
// "Your templates" with the file name as title.

TEST(TemplateSuggestionsTests, BigBandCategorySuggestsJazzTemplatesOnly)
{
    const std::string cat = "Big Band - Jazz Band";
    EXPECT_TRUE(isSuggested(cat, "Jazz", "Big Band"));
    EXPECT_TRUE(isSuggested(cat, "Jazz", "Jazz Combo"));
    EXPECT_TRUE(isSuggested(cat, "Your templates", "My Big Band Chart"));
    EXPECT_FALSE(isSuggested(cat, "Band and Percussion", "Concert Band"));
    EXPECT_FALSE(isSuggested(cat, "Orchestral", "Symphony Orchestra"));
    EXPECT_FALSE(isSuggested(cat, "General", "Treble Clef"));
}

TEST(TemplateSuggestionsTests, ConcertBandOrchestraAndSmallEnsembles)
{
    EXPECT_TRUE(isSuggested("Concert Band", "Band and Percussion", "Concert Band"));
    EXPECT_TRUE(isSuggested("Concert Band", "Band and Percussion", "Small Concert Band"));
    EXPECT_FALSE(isSuggested("Concert Band", "Jazz", "Big Band"));

    EXPECT_TRUE(isSuggested("Orchestra", "Orchestral", "Classical Orchestra"));
    EXPECT_TRUE(isSuggested("Orchestra", "Your templates", "Pit orchestra 2026"));
    EXPECT_FALSE(isSuggested("Orchestra", "Jazz", "Big Band"));

    EXPECT_TRUE(isSuggested("Small Ensembles", "Chamber Music", "String Quartet"));
    EXPECT_TRUE(isSuggested("Small Ensembles", "Chamber Music", "Wind Quintet"));
    EXPECT_FALSE(isSuggested("Small Ensembles", "Jazz", "Big Band"));
}

TEST(TemplateSuggestionsTests, OtherAndCustomCategories)
{
    // "Other" is too general to suggest anything.
    EXPECT_FALSE(isSuggested("Other", "General", "Grand Staff"));
    EXPECT_FALSE(isSuggested("Other", "Jazz", "Big Band"));

    // A distinctive word of a custom category is matched, case-insensitively.
    EXPECT_TRUE(isSuggested("Steel Pan Ensembles", "Your templates", "steel pan sextet"));
    EXPECT_TRUE(isSuggested("Choir", "Choral", "SATB + Piano"));
    EXPECT_FALSE(isSuggested("Film Scores", "General", "Treble Clef"));
}
