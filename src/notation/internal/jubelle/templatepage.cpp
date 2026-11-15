/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "templatepage.h"

#include <cmath>

#include "engraving/dom/masterscore.h"
#include "engraving/style/style.h"

using namespace mu::engraving;

namespace mu::notation::jubelle {
namespace {
constexpr double MM = 1.0 / 25.4;

muse::String baseClass(const muse::String& pageClass)
{
    muse::String c = pageClass.trimmed().toLower();
    if (c.endsWith(u"-landscape")) {
        c = c.left(c.size() - 10);
    }
    return c;
}

bool near(double a, double b)
{
    return std::abs(a - b) < 0.06; // inches
}

bool matches(double w, double h, double pw, double ph)
{
    return (near(w, pw) && near(h, ph)) || (near(w, ph) && near(h, pw));
}
}

bool usPageSize(const muse::String& pageClass, PageSizeInch& out)
{
    const muse::String c = baseClass(pageClass);
    if (c == u"letter") {
        out = { 8.5, 11.0 };
    } else if (c == u"9x12") {
        out = { 9.0, 12.0 };
    } else if (c == u"11x17") {
        out = { 11.0, 17.0 };
    } else {
        return false;
    }
    return true;
}

bool internationalPageSize(const muse::String& pageClass, PageSizeInch& out)
{
    const muse::String c = baseClass(pageClass);
    if (c == u"letter") {
        out = { 210 * MM, 297 * MM };       // A4
    } else if (c == u"9x12") {
        out = { 250 * MM, 353 * MM };       // B4 (ISO)
    } else if (c == u"11x17") {
        out = { 297 * MM, 420 * MM };       // A3
    } else {
        return false;
    }
    return true;
}

bool isLandscapeClass(const muse::String& pageClass)
{
    return pageClass.trimmed().toLower().endsWith(u"-landscape");
}

bool isUsPaper(double w, double h)
{
    return matches(w, h, 8.5, 11.0)      // Letter
           || matches(w, h, 8.5, 14.0)   // Legal
           || matches(w, h, 11.0, 17.0)  // Tabloid / 11x17
           || matches(w, h, 9.0, 12.0)   // 9x12
           || matches(w, h, 5.5, 8.5);   // Statement
}

void setPage(MStyle& style, const PageSizeInch& portrait, bool landscape)
{
    const double w = landscape ? portrait.height : portrait.width;
    const double h = landscape ? portrait.width : portrait.height;
    const double left = style.styleD(Sid::pageOddLeftMargin);
    const double printable = style.styleD(Sid::pagePrintableWidth);
    const double oldWidth = style.styleD(Sid::pageWidth);
    // keep the right margin as it was (printable width = page - left - right)
    double right = oldWidth - left - printable;
    if (right < 0) {
        right = left;
    }
    style.set(Sid::pageWidth, w);
    style.set(Sid::pageHeight, h);
    style.set(Sid::pagePrintableWidth, w - left - right);
}

void applyGeneralStyle(MStyle& style)
{
    const double side = 0.6;   // inches
    const double width = style.styleD(Sid::pageWidth);
    style.set(Sid::pageOddLeftMargin, side);
    style.set(Sid::pageEvenLeftMargin, side);
    style.set(Sid::pagePrintableWidth, width - 2 * side);
    style.set(Sid::pageOddTopMargin, 0.5);
    style.set(Sid::pageEvenTopMargin, 0.5);
    style.set(Sid::pageOddBottomMargin, 0.5);
    style.set(Sid::pageEvenBottomMargin, 0.5);
    style.set(Sid::minSystemDistance, Spatium(9.5));
    style.set(Sid::maxSystemDistance, Spatium(16.0));
    style.set(Sid::enableIndentationOnFirstSystem, true);
    style.precomputeValues();
}

bool applyTemplatePageClass(MasterScore* score, double defaultW, double defaultH)
{
    if (!score) {
        return false;
    }
    const muse::String cls = score->metaTag(PAGE_CLASS_TAG);
    if (cls.isEmpty()) {
        return false;
    }

    PageSizeInch size;
    const bool us = isUsPaper(defaultW, defaultH);
    const bool known = us ? usPageSize(cls, size) : internationalPageSize(cls, size);
    if (known) {
        setPage(score->style(), size, isLandscapeClass(cls));
    }

    // The tag is only an instruction for this moment; don't leave it in the score
    score->metaTags().erase(PAGE_CLASS_TAG);
    return known;
}
}
