/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::io). Not part of MuseScore.
 *
 * A MuseScore 4.x score file (.mscz, or a plain .mscx) as Jubelle holds it: every archive
 * entry in order, with the main score and the parts (Excerpts/…) as lossless XML trees.
 * Saving writes the trees back (byte for byte when nothing changed) plus the engine store.
 *
 * The engine store (decision D-E10, D-E19) is versioned JSON in the score meta tag
 * "jubelleEngine": the only place official MuseScore 4.7.7 keeps when it saves a file (tested,
 * docs/13). It mirrors Jubelle's 11 style settings, so a score opened and saved in MuseScore
 * gets them back when Jubelle opens it again.
 */
#pragma once

#include <map>
#include <string>
#include <vector>

#include "xml.h"
#include "zip.h"

namespace je::io {
struct ScoreFile {
    std::vector<zip::Entry> entries;   // every entry, in order (the score XML entries are rewritten on save)
    std::string mainEntry;             // the main .mscx entry
    xml::Document main;
    std::map<std::string, xml::Document> excerpts;   // entry name → part score
    xml::Document style;               // score_style.mss: where a 4.x archive keeps the score's style
    bool hasStyleEntry = false;
    bool plainMscx = false;            // read from an .mscx file, not an archive
    bool dirContainer = false;         // an .mscx in a folder container (META-INF/container.xml): style in score_style.mss

    std::string formatVersion() const { return main.root.attr("version"); }   // "4.60"
    bool isVersion4() const { return formatVersion().rfind("4.", 0) == 0; }
    const xml::Node* score() const { return main.root.child("Score"); }
    xml::Node* score() { return main.root.child("Score"); }
};

bool load(const std::string& path, ScoreFile& file, std::string& error);
bool loadBytes(const std::string& bytes, bool plainMscx, ScoreFile& file, std::string& error);
//! The file's bytes (an archive, or the XML for a plain .mscx)
std::string saveBytes(const ScoreFile& file);
//! Saves; an .mscz path gets an archive even when the score was read from a plain .mscx
bool save(const std::string& path, const ScoreFile& file);
//! Makes a score read from a plain .mscx into an archive (container.xml and the score)
void makeArchive(ScoreFile& file, const std::string& scoreName = "score.mscx");

//! The score's <Style>: score_style.mss in an archive that has it (what MuseScore reads), else the main score's
xml::Node* styleNode(ScoreFile& file);
const xml::Node* styleNode(const ScoreFile& file);

//! Jubelle's style settings: the 11 Style values only Jubelle knows (MuseScore drops them on save)
const std::vector<std::string>& jubelleStyleNames();

//! Mirrors Jubelle's style values (those set in <Style>) into the engine store meta tag. Returns the store's JSON.
std::string updateEngineStore(ScoreFile& file);
//! Puts back into <Style> any Jubelle value the store has but <Style> lost. Returns the names restored.
std::vector<std::string> restoreFromEngineStore(ScoreFile& file);

//! The meta tag's value (decoded), or empty
std::string metaTag(const ScoreFile& file, const std::string& name);
void setMetaTag(ScoreFile& file, const std::string& name, const std::string& value);
}
