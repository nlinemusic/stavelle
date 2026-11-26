/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 *
 * The fingerprint of an object's musical fields (decision D-E27): details carried on an object (io/details.h) are
 * written back only while it still matches. In the model, so that an edit that changes an object's details (a slash
 * notehead) can keep them in step.
 */
#pragma once

#include <cstdint>
#include <utility>
#include <vector>

#include "object.h"

namespace je::model {
//! The fingerprint of an object's musical fields (structure, origin and details fields aside)
int64_t detailsKey(const std::vector<std::pair<Field, Value> >& fields);
int64_t detailsKey(const Object& object);
}
