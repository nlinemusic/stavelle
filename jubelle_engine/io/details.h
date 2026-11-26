/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::io). Not part of MuseScore.
 *
 * Details carried on model objects (decision D-E27): the file elements the model doesn't represent yet, as an XML
 * fragment, with a fingerprint of the object's musical fields. The writer emits them only while the fingerprint still
 * matches, so an edit that changes an object's music drops its old appearance details instead of writing them stale.
 */
#pragma once

#include "model/detailskey.h"
#include "model/packing.h"

#include <string>
#include <utility>
#include <vector>

#include "xml.h"
#include "model/object.h"

namespace je::io {
using FieldList = std::vector<std::pair<model::Field, model::Value> >;

//! The fingerprint of an object's musical fields (structure, origin and details fields aside): model/detailskey.h
using model::detailsKey;

//! Child elements as a fragment (each written in MuseScore's layout), and back
std::string fragment(const std::vector<const xml::Node*>& nodes);
std::vector<xml::Node> parseFragment(const std::string& fragment);

//! (name, value) pairs in one text (separated by control characters, which XML text never holds)
using model::packPairs;     // model/packing.h
using model::unpackPairs;

//! The file head Jubelle writes for a score that has none of its own (the program version), as a fragment
std::string defaultScoreHead();
}
