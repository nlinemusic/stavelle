/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::io). Not part of MuseScore.
 *
 * ZIP archives (.mscz), read and written with zlib (S2). Entries keep their order, names and
 * timestamps; stored and deflated entries are read; entries are written deflated (or stored
 * when that's smaller, as for PNG thumbnails). CRCs are checked on reading. No ZIP64 (a score
 * archive over 4 GB isn't supported) and no encryption.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace je::io::zip {
struct Entry {
    std::string name;
    std::string data;        // uncompressed
    uint16_t dosTime = 0;    // modification time, as stored
    uint16_t dosDate = 0x21; // 1980-01-01
    bool stored = false;     // written without compression
};

//! Reads every entry; false with `error` on a damaged or unsupported archive
bool read(const std::string& bytes, std::vector<Entry>& entries, std::string& error);
//! Writes the entries in order
std::string write(const std::vector<Entry>& entries);

bool readFile(const std::string& path, std::string& bytes);
//! Writes via a temporary file and a rename, so a failed write never damages an existing file
bool writeFile(const std::string& path, const std::string& bytes);
}
