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

#include <string>
#include <vector>

//! Jubelle project library: the folder structure on disk.
//!
//!   Jubelle Projects/                 <- main project folder (Desktop by default)
//!     Concert Band/                   <- category
//!       Song Name/                    <- project folder
//!         Song Name.mscz              <- main score file
//!         Backups/
//!         PDF Exports/Full Score, Individual Parts, Combined Parts
//!         Audio Exports/, Video Exports/, Other Exports/
//!
//! The folders themselves are the source of truth: nothing is stored in a
//! separate database, so the structure stays correct even if the user also
//! looks at it in File Explorer or Finder. The only extra file is a small
//! hidden list that remembers the order the user arranged the categories in.
//!
//! Every operation is designed never to lose or overwrite a file: names that
//! already exist are refused (or numbered, for new projects), categories can
//! only be deleted when empty, and moves fall back to copy-verify-delete when
//! a plain rename is not possible.
//!
//! Paths and names are UTF-8. Only the C++ standard library and the native OS
//! API are used, so the behaviour is identical on Windows and macOS and can be
//! unit-tested in isolation.
namespace mu::project::projectlibrary {
struct Result {
    bool ok = false;
    std::string path;   //!< the resulting folder (or file) on success
    std::string error;  //!< a plain-language reason on failure
};

enum class ExportKind {
    FullScorePdf,
    IndividualPartsPdf,
    CombinedPartsPdf,
    Audio,
    Video,
    Other
};

//! "Jubelle Projects"
std::string mainFolderName();

//! The starting categories created together with a new main project folder.
std::vector<std::string> defaultCategories();

//! Returns an empty string if `name` can be used as a category or project
//! name on both Windows and macOS, otherwise a plain-language reason.
std::string validateName(const std::string& name);

//! Creates the main project folder if needed. When the folder is created
//! for the first time, the default categories are created inside it.
Result ensureMainFolder(const std::string& mainFolder);

//! Category names in the order the user arranged them (new ones last, alphabetically).
std::vector<std::string> categories(const std::string& mainFolder);

Result createCategory(const std::string& mainFolder, const std::string& name);
Result renameCategory(const std::string& mainFolder, const std::string& oldName, const std::string& newName);
//! Refuses to delete a category that still contains anything.
Result deleteCategory(const std::string& mainFolder, const std::string& name);
//! Stores the order of the categories; names not listed keep their place after the listed ones.
Result setCategoryOrder(const std::string& mainFolder, const std::vector<std::string>& orderedNames);

//! Project folder names inside a category, alphabetically.
std::vector<std::string> projects(const std::string& mainFolder, const std::string& category);

//! Creates a project folder with all its subfolders. If a project with this
//! name already exists in the category, " (2)", " (3)"... is added.
//! The score file itself is not created. On success `path` is the project folder.
Result createProject(const std::string& mainFolder, const std::string& category, const std::string& name);

//! True if `folder` has the Jubelle project structure.
bool isProjectFolder(const std::string& folder);

//! The project folder containing `scorePath` as its main score, or "" if the
//! score is not the main score of a Jubelle project.
std::string projectFolderForScore(const std::string& scorePath);

//! `<project>/<project name>.mscz`
std::string mainScorePath(const std::string& projectFolder);

//! Renames the project folder, its main score file and its backup file.
//! Refuses if a project with the new name already exists in the same category.
//! On success `path` is the new project folder.
Result renameProject(const std::string& projectFolder, const std::string& newName);

//! Moves a whole project (score, backups and exports) into another category.
//! Refuses if the destination already has a project with the same name.
//! On success `path` is the new project folder.
Result moveProject(const std::string& projectFolder, const std::string& mainFolder, const std::string& newCategory);

//! The folder that exports of the given kind go to (created if missing).
std::string exportFolder(const std::string& projectFolder, ExportKind kind);

//! `<project>/Backups` (created if missing).
std::string backupFolder(const std::string& projectFolder);
}
