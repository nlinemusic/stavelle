/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle: page sizes for Jubelle's score templates (decided 15 Nov 2026, T4).
//
// Each Jubelle template is designed for a US page size and records its "page
// class" in a meta tag (jubellePageClass): letter, 9x12 or 11x17, each with an
// optional "-landscape". When a new score is made from the template:
//   - if the user's default page (default style or locale) is a US size, the
//     template's US size is used;
//   - otherwise the nearest international size is used with the same design:
//     Letter -> A4, 9x12 -> B4, 11x17 -> A3 (same orientation, same margins
//     and staff size).
// The tag is then removed, so the new score only stores an ordinary page size
// and opens normally in MuseScore Studio.
//
// The second tag, jubelleScoreOnlyStyle, lists style values (by their style
// file names, comma separated) that were designed for the full score only, such
// as the big-band minimum bar width. Parts made from the score start from the
// default values for those instead of the score's (see Score::Score for parts).
// It stays in the score, because parts can be made at any time.

#pragma once

#include "global/types/string.h"
#include "draw/types/geometry.h"

namespace mu::engraving {
class MasterScore;
class MStyle;
}

namespace mu::notation::jubelle {
inline constexpr const char16_t* PAGE_CLASS_TAG = u"jubellePageClass";
inline constexpr const char16_t* SCORE_ONLY_STYLE_TAG = u"jubelleScoreOnlyStyle";

struct PageSizeInch {
    double width = 0.0;   // portrait width
    double height = 0.0;  // portrait height
};

//! The US size of a page class ("letter", "9x12", "11x17"; "-landscape" ignored here).
//! Returns false for an unknown class.
bool usPageSize(const muse::String& pageClass, PageSizeInch& out);

//! The international equivalent: A4, B4 (ISO, 250x353 mm) or A3.
bool internationalPageSize(const muse::String& pageClass, PageSizeInch& out);

bool isLandscapeClass(const muse::String& pageClass);

//! True if a default page of this size (inches, any orientation) is a US size
//! (Letter, Legal, Tabloid/11x17, 9x12); false for A and B sizes and others.
bool isUsPaper(double widthInch, double heightInch);

//! Sets the page size of `style` for the class, keeping its margins: the printable
//! width becomes the page width minus both side margins.
void setPage(mu::engraving::MStyle& style, const PageSizeInch& portrait, bool landscape);

//! Applies the template's page class to a newly created score, choosing US or
//! international by the default page size, then removes the tag. Does nothing
//! (and returns false) if the score has no page class.
bool applyTemplatePageClass(mu::engraving::MasterScore* score, double defaultPageWidthInch, double defaultPageHeightInch);
}
