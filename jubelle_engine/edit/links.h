/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * Linked staves: a staff that shows the music of another (a guitar's tablature under its standard staff, MuseScore's
 * "linked staff"). The two always hold the same music: an edit on either one is made on the other in the same
 * transaction, so it is one undo step, as in MuseScore.
 *
 * In the model a linked staff keeps its own copy of the events (as MuseScore does, and as the file stores them):
 * LINKED_STAFF on the staff names the main staff, and an element Jubelle copies carries LINKED_TO, its original,
 * which the writer turns into MuseScore's link. After an edit, each measure whose music differs between the two is
 * written again on the other staff (the measures that didn't change keep their objects, and with them what the
 * file said about their appearance); lines (slurs, hammer-ons) anchored to the copy are moved to the new copies,
 * ties into and out of the measure are reconnected, and a tablature staff gets strings and frets.
 *
 * Lines and staff items (slurs, hairpins, dynamics, staff texts, chord symbols…) that an edit adds, changes or removes
 * on one staff of a pair are added, changed or removed on the others too, as MuseScore does; ones already different
 * in a file are left as they are.
 */
#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "changeset.h"
#include "model/model.h"

namespace je::edit {
struct LinkSyncReport {
    size_t voicesRebuilt = 0;    // a measure of a voice written again (its structure changed)
    size_t objectsPatched = 0;   // objects that took the music of their counterpart in place
    size_t itemsMirrored = 0;    // lines and staff items made, changed or removed on the other staves of a pair
    size_t conflicts = 0;      // notes that can't be played on their tablature staff
};

//! Brings each linked staff in line with its main staff. With `changes`, the staves they touched decide the
//! direction (an edit on the linked staff is made on the main staff, then on the others); without, the main staff
//! is the source
LinkSyncReport syncLinkedStaves(Transaction& tx, model::Model& m, IdGenerator& ids, const ChangeSet* changes = nullptr);

//! Measures in which a linked staff's music differs from its main staff's (a description of each, for checks)
std::vector<std::string> linkedStaffDifferences(model::Model& m);

//! The hook that keeps linked staves and tablature consistent: give it to every editing transaction
Transaction::Hook linkedStavesHook(model::Model& m, IdGenerator& ids);
}
