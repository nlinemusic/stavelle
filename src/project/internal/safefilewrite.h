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

#include <cstddef>
#include <cstdint>
#include <string>

//! Low-level primitives for crash-safe saving.
//!
//! These functions guarantee that a score file on disk is, at every instant,
//! either the complete previous version or the complete new version, never a
//! truncated or partially written mix, even if the application crashes,
//! the computer loses power, or the disk fills up during a save.
//!
//! Paths are UTF-8. The functions deliberately depend only on the C++
//! standard library and the native OS API (Win32 / POSIX), so they behave
//! identically on Windows and macOS and can be unit-tested in isolation.
namespace mu::project::safefilewrite {
//! Writes `size` bytes to `path` (creating or truncating it) and forces the
//! data to physical storage before returning. Fails if any byte could not be
//! written. On failure `errorText` (if provided) describes the reason.
bool writeFileDurably(const std::string& path, const uint8_t* data, size_t size, std::string* errorText = nullptr);

//! Atomically replaces `targetPath` with `sourcePath` (which is consumed).
//! Both must be on the same volume (e.g. in the same folder). If `targetPath`
//! does not exist it is created. If this function fails, `targetPath` is left
//! exactly as it was. On Windows, transient locks held by antivirus or cloud
//! sync software are retried for a short while.
bool replaceFileAtomically(const std::string& sourcePath, const std::string& targetPath, std::string* errorText = nullptr);
}
