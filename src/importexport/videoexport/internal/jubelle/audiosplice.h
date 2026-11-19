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

// Jubelle: the WAV reader and splicer moved to the media module (shared by video export and AAC audio
// export); the video export keeps using them under its old names.
#include "media/jubelle/audiosplice.h"

namespace mu::iex::videoexport::jubelle {
using muse::media::jubelle::WavReader;
using muse::media::jubelle::Piece;
using muse::media::jubelle::SpliceOptions;
using muse::media::jubelle::splice;
using muse::media::jubelle::splicedFrames;
using muse::media::jubelle::writeFloatWav;
}
