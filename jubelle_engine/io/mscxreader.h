/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::io). Not part of MuseScore.
 *
 * Reads a MuseScore 4.x score's XML into the Jubelle model (S2), without MuseScore's engine:
 * parts → players and staves; measures with their actual lengths, time signatures, repeats and
 * breaks; every voice's chords, rests, notes, written values, dots, tuplets (nested), grace notes,
 * ties (resolved by location), articulations, lyrics; clefs, keys, time signatures, tempo,
 * dynamics, chord symbols, texts, rehearsal marks, jumps, markers, instrument changes; spanners
 * (slurs, hairpins, voltas, ottavas, pedals, trills, lines) with their extents; part layouts.
 *
 * Everything the model doesn't hold yet stays in the lossless XML (ScoreFile), so nothing is lost
 * on saving. The report lists, by element name, what was read into the model and what is kept only
 * in the XML, plus any problem (an unresolved tie, an unknown duration).
 */
#pragma once

#include <map>
#include <string>
#include <vector>

#include "scorefile.h"
#include "model/model.h"

namespace je::io {
struct ReadReport {
    std::map<std::string, int> modelled;   // element name → count read into the model
    std::map<std::string, int> keptOnly;   // element name → count kept only in the XML
    std::vector<std::string> warnings;
    size_t objects = 0;
    int measures = 0;
    int staves = 0;
    int chords = 0;
    int notes = 0;
    int rests = 0;
    int tuplets = 0;
    int graces = 0;
    int ties = 0;
    int spanners = 0;
    int lyrics = 0;
};

//! Reads the main score into `model` (replacing its content). False if the file isn't a 4.x score.
bool readScore(const ScoreFile& file, model::Model& model, IdGenerator& ids, ReadReport& report, std::string& error);

//! A plain listing of the music (staff, voice, time, duration, pitches, grace) for comparing two readings
std::string musicListing(const model::Snapshot& snapshot);

//! Every object below the score in tree order with its fields, ids written as tree paths, so that two readings of
//! the same music compare equal whatever ids they were given (read-order counters and fingerprints left out)
std::string modelDump(const model::Snapshot& snapshot);
}
