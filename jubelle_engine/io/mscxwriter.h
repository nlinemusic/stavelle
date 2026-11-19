/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::io). Not part of MuseScore.
 *
 * Writes a MuseScore 4.x score from the Jubelle model alone (S2): the model is the source of truth, nothing is
 * copied from the file the score came from. Everything the model represents is generated from its fields: parts and
 * instruments, staves, the measure grid (lengths, repeats, breaks), every voice's chords, rests, notes, written values,
 * dots, tuplets, grace notes, ties, articulations, ornaments, lyrics, fingering; clefs, keys, time signatures, tempo,
 * dynamics, chord symbols, texts, jumps, markers, instrument changes; spanners; style; score properties; Jubelle's
 * engine store. Details the model doesn't represent yet travel on their objects and are written only while those
 * objects' music is unchanged (D-E27).
 */
#pragma once

#include <string>

#include "scorefile.h"
#include "model/model.h"

namespace je::io {
struct WriteReport {
    int detailsWritten = 0;   // objects whose carried details were written
    int detailsDropped = 0;   // objects whose music changed since their details were read (details left out)
};

//! The score as a MuseScore 4.x file, generated from the model: an archive (main score, style, settings)
ScoreFile generateScoreFile(const model::Snapshot& snapshot, WriteReport* report = nullptr,
                            const std::string& scoreName = "score.mscx");

//! Opening and saving through the model (what Jubelle does): read, restoring Jubelle's settings from the engine
//! store when a MuseScore save dropped them; and generate, with the engine store updated
bool openScore(const std::string& path, model::Model& model, IdGenerator& ids, std::string& error);
bool saveScore(const model::Snapshot& snapshot, const std::string& path, std::string& error, WriteReport* report = nullptr);
}
