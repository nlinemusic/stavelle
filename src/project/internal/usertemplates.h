/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#pragma once

#include <string>

//! Jubelle: your own templates and categories (15 Nov 2026), using MuseScore's
//! existing template system. A template saved directly in your Templates folder
//! is listed under "My templates"; one in a folder inside it (for example
//! "Templates/Big band charts/Shout chart.mscz") is listed under that folder's
//! name, so creating a folder creates a category. Pure text, no Qt, tested on its own.
namespace mu::project::usertemplates {
//! The category for a template file, from its path relative to the Templates
//! folder ('/' separators). Empty means "My templates".
//! Templates kept as folders ("Name/Name.mscx") count as one file.
std::string categoryFor(const std::string& relativePath);

//! A safe file name (without extension) for a template title: characters that
//! Windows or macOS don't allow are replaced, spaces kept, never empty.
std::string fileNameFor(const std::string& title);
}
