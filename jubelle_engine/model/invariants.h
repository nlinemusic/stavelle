/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 *
 * The rules a committed score must satisfy. Tests check them after every edit sequence;
 * the converter and the file reader check them after loading.
 */
#pragma once

#include <string>
#include <vector>

#include "model.h"

namespace je::model {
//! Empty if the snapshot is consistent; otherwise one plain-language line per problem (at most `limit`)
std::vector<std::string> checkInvariants(const Snapshot& snapshot, size_t limit = 50);

//! Which kinds may hold which (structure of model/fields.h)
bool allowedChild(Kind parent, Kind child);
}
