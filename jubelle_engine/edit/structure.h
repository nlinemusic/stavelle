/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * Structural edits: creating, removing and re-ordering objects inside a transaction, so
 * that the parent/children links stay consistent and undo restores everything exactly
 * (every field of a removed object is recorded before it goes).
 */
#pragma once

#include <utility>
#include <vector>

#include "changeset.h"
#include "model/fields.h"
#include "model/id.h"

namespace je::edit {
struct NewObject {
    model::Kind kind = model::Kind::None;
    std::vector<std::pair<Field, Value> > fields;
};

enum class Placement {
    Append,      //!< at the end of the parent's children
    ByTime,      //!< in time order (TIME), after any children with the same time
};

//! Creates a new object under `parent` (or with no parent if `parent` is invalid: the score). Returns its id.
Id create(Transaction& tx, IdGenerator& ids, Id parent, const NewObject& object, Placement where = Placement::Append);

//! Same, with an id chosen by the caller (ids read from a file)
void createWithId(Transaction& tx, Id id, Id parent, const NewObject& object, Placement where = Placement::Append);

//! Removes the object and everything under it, and takes it out of its parent's children
void removeTree(Transaction& tx, Id id);

//! The object's fields as stored now
std::vector<std::pair<Field, Value> > currentFields(const Transaction& tx, Id id);
}
