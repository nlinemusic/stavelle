/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2025 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <string>
#include <utility>
#include <vector>

#include <QImage>

#include "global/types/bytearray.h"
#include "io/path.h"

#include "modularity/imoduleinterface.h"

#include "iaacencoder.h"

namespace muse::media {
class IVideoEncoder : MODULE_CONTEXT_INTERFACE
{
    INTERFACE_ID(IVideoEncoder)

public:
    virtual ~IVideoEncoder() = default;

    struct Options {
        std::string format = "";
        unsigned width = static_cast<unsigned>(-1);
        unsigned height = static_cast<unsigned>(-1);
        unsigned bitrate = static_cast<unsigned>(-1);
        unsigned gop = static_cast<unsigned>(-1);
        unsigned fps = static_cast<unsigned>(-1);
    };

    virtual bool open(const muse::io::path_t& fileName, const Options& options) = 0;
    virtual void close() = 0;

    virtual bool encodeImage(const QImage& img) = 0;
    virtual void finishEncode() = 0;

    virtual bool encodeVideo(const muse::ByteArray& videoData, int maxFrames = -1) = 0;

    virtual bool addAudio(const muse::io::path_t& audioPath) = 0;

    //! Jubelle: keep only these pieces of the audio file (start and end in seconds), joined in this
    //! order, when addAudio() puts the sound into the video. Empty = the whole sound.
    void setAudioSegments(std::vector<std::pair<double, double> > segments) { m_jubelleAudioSegments = std::move(segments); }

    //! Jubelle: a new AAC encoder using the same loaded FFmpeg libraries (FFmpeg's own AAC encoder), or
    //! nullptr if they don't provide it. Used by AAC audio export and by the MP4 soundtrack.
    virtual IAacEncoderPtr createAacEncoder() const { return nullptr; }

protected:
    std::vector<std::pair<double, double> > m_jubelleAudioSegments;
};
using IVideoEncoderPtr = std::shared_ptr<IVideoEncoder>;
}
