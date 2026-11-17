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

//! Jubelle: "Convert file to score" was removed. Only the types that describe
//! MuseScore.com scores which originated from a conversion remain.
namespace muse::cloud {
enum class ConvertType {
    Unknown = -1,
    Omr = 0,
    Audio2Score,

    Last = Audio2Score
};

enum class ConvertStatus {
    Processing,
    AwaitingReview,
    Done,
    Failed,
    Unknown
};

inline const char* convertTypeToString(ConvertType type)
{
    switch (type) {
    case ConvertType::Unknown: return "Unknown";
    case ConvertType::Omr: return "Omr";
    case ConvertType::Audio2Score: return "Audio2Score";
    }
    return "Unknown";
}

inline const char* convertStatusToString(ConvertStatus status)
{
    switch (status) {
    case ConvertStatus::Processing: return "Processing";
    case ConvertStatus::AwaitingReview: return "AwaitingReview";
    case ConvertStatus::Done: return "Done";
    case ConvertStatus::Failed: return "Failed";
    case ConvertStatus::Unknown: break;
    }
    return "Unknown";
}
}
