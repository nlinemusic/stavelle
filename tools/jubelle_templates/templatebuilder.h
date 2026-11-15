/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle: builds Jubelle's score templates with the real engine, so every
// template is an ordinary MuseScore file written by MuseScore's own writer.
//
// Existing templates are refined from MuseScore's untouched originals (taken
// from the baseline branch by build_templates.sh), so building is repeatable;
// new templates are made from MuseScore's instrument definitions.
//
//   jubelle_engine_tests --build-templates <originals dir> <output dir>

#pragma once

#include <string>
#include <vector>

#include "global/types/string.h"
#include "engraving/style/styledef.h"
#include "engraving/types/propertyvalue.h"

namespace mu::engraving {
class MasterScore;
class Part;
}

namespace jubelle_templates {
struct StyleValue {
    mu::engraving::Sid sid;
    mu::engraving::PropertyValue value;
    bool scoreOnly = false;     // reset to the default in parts
};

struct HouseStyle {
    muse::String name;
    muse::String pageClass;     // see src/notation/internal/jubelle/templatepage.h
    double marginTopInch = 0.5;
    double marginBottomInch = 0.5;
    double marginSideInch = 0.5;
    double spatiumMm = 1.75;
    std::vector<StyleValue> values;
};

//! Loads instruments.xml and orders.xml from the source tree (once).
bool loadInstrumentData();

mu::engraving::MasterScore* readTemplate(const std::string& path);
bool writeTemplate(mu::engraving::MasterScore* score, const std::string& path);
//! An ordinary .mscz file (for test scores)
bool writeMscz(mu::engraving::MasterScore* score, const std::string& path);

//! Page, margins, staff size, style values and the two Jubelle tags.
void applyHouseStyle(mu::engraving::MasterScore* score, const HouseStyle& style);

HouseStyle bigBandStyle();

//! Puts the parts in this order (all parts must be listed).
void reorderParts(mu::engraving::MasterScore* score, const std::vector<mu::engraving::Part*>& order);
void setNames(mu::engraving::Part* part, const muse::String& longName, const muse::String& shortName);
//! Replaces the part's instrument with a MuseScore instrument definition.
bool replaceInstrument(mu::engraving::Part* part, const muse::String& instrumentId);
//! Sets the score order by id (from orders.xml) and recreates brackets and barlines.
bool applyScoreOrder(mu::engraving::MasterScore* score, const muse::String& orderId);

struct NewPart {
    muse::String instrumentId;
    muse::String longName;      // empty: MuseScore's name for the instrument
    muse::String shortName;
};

//! A new template made from MuseScore's instrument definitions (not from
//! another template): the parts in this order, 4/4, C major, 32 bars, and a
//! title frame with the template's name (New Score lists templates by it).
mu::engraving::MasterScore* newTemplate(const muse::String& title, const std::vector<NewPart>& parts);

HouseStyle soloWithPianoStyle();

struct SoloWithPiano {
    const char* dir;            // folder and file name
    const char16_t* title;
    const char16_t* instrumentId;
};
const std::vector<SoloWithPiano>& soloWithPianoTemplates();

//! The Big Band redesign (T1): order, bass trombone, names, house style.
mu::engraving::MasterScore* buildBigBand(const std::string& originalsDir);

//! Builds every template that has been designed so far into outDir (same
//! relative paths as share/templates). Returns the number of failures.
int buildAll(const std::string& originalsDir, const std::string& outDir);
}
