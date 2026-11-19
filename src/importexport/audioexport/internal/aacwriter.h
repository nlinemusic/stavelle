/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2026 MuseScore Limited and others
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

#include "abstractaudiowriter.h"

#include "media/ivideoencoderresolver.h"

namespace mu::iex::audioexport {
//! Jubelle (D-E15): AAC export renders the sound as a 32-bit float WAV, then encodes it with FFmpeg's own AAC
//! encoder from the LGPL FFmpeg libraries Jubelle bundles. The Fraunhofer FDK AAC library is no longer used.
class AacWriter : public AbstractAudioWriter
{
    muse::ContextInject<muse::media::IVideoEncoderResolver> ffmpegResolver = { this };

public:
    AacWriter(const muse::modularity::ContextPtr& iocCtx)
        : AbstractAudioWriter(iocCtx) {}

    muse::Ret write(notation::INotationPtr notation, muse::io::IODevice& destinationDevice, const Options& options = Options()) override;
};
}
