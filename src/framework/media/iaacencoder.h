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

#include <memory>
#include <string>

#include "io/path.h"

namespace muse::media {
//! Jubelle: AAC-LC audio encoder (decision D-E15, `jubelle_engine/docs/08_aac_encoder_licensing.md`).
//!
//! Implemented with FFmpeg's own ("native") AAC encoder in the LGPL FFmpeg libraries Jubelle bundles and
//! loads at run time, replacing the Fraunhofer FDK AAC library, whose licence is incompatible with the GPL.
//! Writes an ADTS stream (`.aac`), the same kind of file as before.
class IAacEncoder
{
public:
    virtual ~IAacEncoder() = default;

    //! Opens `path` for an AAC-LC stream. Only 1 or 2 channels.
    virtual bool open(const io::path_t& path, int sampleRate, int channels, int bitRateKbps) = 0;

    //! The encoder's delay (priming), in samples per channel, known after open(). A raw AAC stream can't
    //! tell a player to skip it, so the caller may shift its input by this much.
    virtual int encoderDelay() const = 0;

    //! Encodes `frames` frames of interleaved float samples (-1..1).
    virtual bool encode(const float* interleaved, size_t frames) = 0;

    //! Encodes what is buffered, writes the end of the stream and closes the file.
    virtual bool finish() = 0;

    //! The FFmpeg encoder in use (always "aac", FFmpeg's own encoder) and its library version.
    virtual std::string encoderName() const = 0;
    virtual std::string lastError() const = 0;
};
using IAacEncoderPtr = std::shared_ptr<IAacEncoder>;
}
