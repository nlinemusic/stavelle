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

//! Jubelle: which templates to suggest for a project category in the New Score
//! dialog ("Suggested for Big Band - Jazz Band"). Pure text matching, no Qt, so
//! it can be unit-tested on its own. Nothing is hidden: the full template list
//! stays available; suggestions are only an extra, first shortcut.
namespace mu::project::templatesuggestions {
//! True if a template with this category title (e.g. "Jazz", "Your templates")
//! and title (e.g. "Big Band", or a custom template's file name) suits the
//! project category (e.g. "Big Band - Jazz Band", "Concert Band", "Orchestra").
bool isSuggested(const std::string& projectCategory, const std::string& templateCategory, const std::string& templateTitle);
}
