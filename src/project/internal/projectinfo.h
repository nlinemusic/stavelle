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
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "projectlibrary.h"

//! Jubelle project information that is kept OUTSIDE the score file:
//! extra categories, tags and the Projects-page card image.
//!
//!   <project>/.jubelle/project.json      categories, tags, card image crop
//!   <project>/.jubelle/card-image.<ext>  the one card image (a copy)
//!   <main>/.jubelle-tags.json            every tag the user has created
//!
//! Nothing here ever opens, reads or writes the .mscz score. All writes go to a
//! temporary file first and are then swapped into place, so a crash leaves
//! either the old or the new information, never a half-written file.
namespace mu::project::projectinfo {
using projectlibrary::Result;

struct CardImage {
    std::string fileName;   //!< e.g. "card-image.jpg", inside <project>/.jubelle
    double x = 50.0;        //!< horizontal crop position, 0..100 (%), 50 = centred
    double y = 50.0;        //!< vertical crop position, 0..100 (%), 50 = centred
};

struct ProjectInfo {
    std::vector<std::string> extraCategories; //!< categories besides the folder's own ("home") category
    std::vector<std::string> tags;
    std::optional<CardImage> cardImage;
};

struct RecentScore {
    std::string projectFolder;
    std::string scorePath;
    int64_t savedTime = 0; //!< seconds since the epoch, from the score file itself
};

struct VideoFile {
    std::string path;
    int64_t modifiedTime = 0; //!< seconds since the epoch
};

//! Reads `<project>/.jubelle/project.json`. A missing or damaged file gives empty information.
ProjectInfo read(const std::string& projectFolder);
Result write(const std::string& projectFolder, const ProjectInfo& info);

//! The category the project folder lives in (its parent folder's name).
std::string homeCategory(const std::string& projectFolder);

//! Home category first, then the extra categories that still exist.
std::vector<std::string> categoriesOf(const std::string& projectFolder, const std::string& mainFolder);

//! Project folders listed in `category`: the ones stored there plus the ones
//! from other categories that list it as an extra category. Sorted by name.
std::vector<std::string> projectsIn(const std::string& mainFolder, const std::string& category);

//! Every project folder in the main folder.
std::vector<std::string> allProjects(const std::string& mainFolder);

//! Sets all categories of a project. `categories` must not be empty. If the
//! home category is not in the list, the project folder is moved (safely) into
//! the first listed category. On success `path` is the (possibly new) project folder.
Result setCategories(const std::string& projectFolder, const std::string& mainFolder,
                     const std::vector<std::string>& categories);

//! "Move to category": the project leaves its home category and goes to `category`.
Result moveToCategory(const std::string& projectFolder, const std::string& mainFolder, const std::string& category);

//! Keeps extra-category references correct when a category is renamed.
Result renameCategory(const std::string& mainFolder, const std::string& oldName, const std::string& newName);
//! Refuses while any project is stored in or lists the category.
Result deleteCategory(const std::string& mainFolder, const std::string& name);
bool isCategoryInUse(const std::string& mainFolder, const std::string& name);

//! Tags. Names are compared without regard to upper/lower case.
std::vector<std::string> tagLibrary(const std::string& mainFolder);
Result addTagToLibrary(const std::string& mainFolder, const std::string& tag);
Result setTags(const std::string& projectFolder, const std::string& mainFolder, const std::vector<std::string>& tags);

enum class TagMatch {
    All, //!< the project has every selected tag
    Any  //!< the project has at least one selected tag
};
bool matchesTags(const std::vector<std::string>& projectTags, const std::vector<std::string>& selected, TagMatch mode);

//! Card image. `preparedImagePath` is an image file already scaled by the caller;
//! it is copied in (the original is never touched), any previous card image is
//! deleted, so there is only ever one.
Result setCardImage(const std::string& projectFolder, const std::string& preparedImagePath, double x, double y);
Result setCardImagePosition(const std::string& projectFolder, double x, double y);
Result removeCardImage(const std::string& projectFolder);
//! Full path of the card image, or "" if the project has none.
std::string cardImagePath(const std::string& projectFolder);

//! The `limit` most recently saved project scores, newest first, judged by the
//! score file's own last-modified time. Each project appears at most once.
std::vector<RecentScore> recentScores(const std::string& mainFolder, size_t limit = 10);

//! Video files (.mp4, .mov, .m4v, .webm) in the project's "Video Exports" folder,
//! newest first. Nothing is created or changed; a missing folder gives an empty list.
std::vector<VideoFile> videoExports(const std::string& projectFolder);
}
