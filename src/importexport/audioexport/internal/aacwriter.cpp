/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
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

#include "aacwriter.h"

#include <algorithm>
#include <vector>

#include "io/buffer.h"
#include "io/file.h"
#include "media/jubelle/audiosplice.h"

#include "log.h"

using namespace muse;
using namespace muse::audio;
using namespace mu::iex::audioexport;

Ret AacWriter::write(notation::INotationPtr notation, io::IODevice& destinationDevice, const Options& options)
{
    const std::string aacPath = destinationDevice.meta("file_path");
    IF_ASSERT_FAILED(!aacPath.empty()) {
        return make_ret(Ret::Code::InternalError);
    }

    // The encoder comes from the FFmpeg libraries; without them AAC can't be written (they ship with Jubelle)
    media::IAacEncoderPtr aac;
    if (ffmpegResolver()) {
        if (media::IVideoEncoderPtr ffmpeg = ffmpegResolver()->currentVideoEncoder()) {
            aac = ffmpeg->createAacEncoder();
        }
    }
    if (!aac) {
        LOGE() << "AAC export: the FFmpeg libraries (bundled with Jubelle) weren't found";
        return make_ret(Ret::Code::NotSupported,
                        std::string("AAC export needs the FFmpeg libraries that come with Jubelle, and they weren't found."));
    }

    // 1. Render the sound, as the WAV export does, at full precision
    const std::string wavPath = aacPath + ".jubelle_tmp.wav";
    const SoundTrackFormat wavFormat {
        SoundTrackType::WAV,
        {
            static_cast<sample_rate_t>(configuration()->exportSampleRate()),
            configuration()->exportBufferSize(),
            2 /* audioChannelsNumber */
        },
        AudioSampleFormat::Float32,
        0 /* bitRate */
    };
    io::Buffer wavDevice;
    wavDevice.setMeta("file_path", wavPath);
    wavDevice.open(io::IODevice::WriteOnly);
    Options renderOptions = options;
    renderOptions[OptionKey::WAIT_FOR_COMPLETION] = Val(true);
    Ret ret = doWriteAndWait(notation, wavDevice, wavFormat, renderOptions);
    wavDevice.close();
    if (!ret) {
        io::File::remove(wavPath);
        return ret;
    }

    // 2. Encode it (AAC-LC, ADTS: the same kind of .aac file as before)
    media::jubelle::WavReader wav;
    if (!wav.open(wavPath)) {
        LOGE() << "AAC export: " << wav.error();
        io::File::remove(wavPath);
        return make_ret(Ret::Code::UnknownError, wav.error());
    }
    bool ok = aac->open(aacPath, wav.sampleRate(), wav.channels(), configuration()->exportMp3Bitrate());
    const int64_t block = 4096;
    std::vector<float> samples;
    for (int64_t frame = 0; ok && frame < wav.frames(); frame += block) {
        const int64_t count = std::min(block, wav.frames() - frame);
        wav.read(frame, count, samples);
        ok = aac->encode(samples.data(), size_t(count));
    }
    ok = ok && aac->finish();
    io::File::remove(wavPath);

    if (!ok) {
        LOGE() << "AAC export: " << aac->lastError();
        io::File::remove(aacPath);
        return make_ret(Ret::Code::UnknownError, aac->lastError());
    }
    LOGI() << "AAC export: encoded with " << aac->encoderName();
    return make_ok();
}
