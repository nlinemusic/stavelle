/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

#include "mp3writer.h"

#include <algorithm>

#include "log.h"

using namespace muse;
using namespace muse::audio;
using namespace mu::iex::audioexport;

Ret Mp3Writer::write(notation::INotationPtr notation, io::IODevice& destinationDevice, const Options& options)
{
    const SoundTrackFormat format {
        SoundTrackType::MP3,
        {
            // MP3 goes up to 48 kHz: a 96 kHz choice is written at 48 kHz
            static_cast<sample_rate_t>(std::min(configuration()->exportSampleRate(), 48000)),
            configuration()->exportBufferSize(),
            2 /* audioChannelsNumber */
        },
        AudioSampleFormat::Undefined,
        configuration()->exportMp3Bitrate()
    };

    return doWriteAndWait(notation, destinationDevice, format, options);
}
