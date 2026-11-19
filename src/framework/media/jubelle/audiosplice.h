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

// Jubelle: the video's sound, made from an uncompressed WAV rendering.
//
// The MP4 used to take MuseScore's AAC file and keep or drop whole compressed
// packets to follow a page sequence, so every join was a hard cut (and could
// click). Now the sound is rendered once as WAV, the chosen pieces are read
// from it, and each join that skips or repeats music gets a short equal-power
// crossfade. The total length never changes, so the sound stays in step with
// the pictures. No Qt is used, so this is tested on every platform.

#include <cstdint>
#include <fstream>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace muse::media::jubelle {
//! Reads PCM WAV files (16, 24 or 32-bit integer, or 32-bit float; plain or
//! WAVE_FORMAT_EXTENSIBLE) as interleaved float frames.
class WavReader
{
public:
    bool open(const std::string& path);
    int sampleRate() const { return m_sampleRate; }
    int channels() const { return m_channels; }
    int bitsPerSample() const { return m_bits; }
    bool isFloat() const { return m_float; }
    int64_t frames() const { return m_frames; }
    const std::string& error() const { return m_error; }

    //! Reads up to `count` frames starting at `frame` into `out` (interleaved,
    //! `channels()` values per frame). Frames outside the file read as silence,
    //! so the result always has exactly `count` frames.
    void read(int64_t frame, int64_t count, std::vector<float>& out);

private:
    std::ifstream m_file;
    std::streamoff m_dataStart = 0;
    int m_sampleRate = 0;
    int m_channels = 0;
    int m_bits = 0;
    bool m_float = false;
    int64_t m_frames = 0;
    std::string m_error;
    std::vector<char> m_raw;
};

//! One piece of the sound, in seconds of the WAV file.
using Piece = std::pair<double, double>;

struct SpliceOptions {
    double crossfadeSec = 0.020;   // about 20 ms at each join
    int64_t blockFrames = 4096;    // frames handed to the sink at a time
};

//! Calls `sink(frames, interleavedSamples)` with the joined sound, in order.
//! With no pieces, the whole file is passed through unchanged.
//! Returns the number of frames produced.
int64_t splice(WavReader& wav, const std::vector<Piece>& pieces, const SpliceOptions& options,
               const std::function<void(int64_t frames, const float* samples)>& sink);

//! Frames that `splice` will produce for these pieces (for progress and checks).
int64_t splicedFrames(const WavReader& wav, const std::vector<Piece>& pieces);

//! Writes a 32-bit float WAV (used by the tests and by the audio checks).
bool writeFloatWav(const std::string& path, int sampleRate, int channels, const std::vector<float>& samples);
}
