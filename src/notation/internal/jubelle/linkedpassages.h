/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

// Jubelle Phase 4: defined linked musical passages.
//
// A link group has one source passage and one or more target passages, each a
// stretch of bars on one staff (voice 1). Edits in the source are copied to
// the targets in the same undo step, using the tested propagation logic
// (passagelinkplanner): targets keep their own pitches and octave, only the
// chosen kinds of change are copied, and notes you changed by hand in a
// target are marked as your own and never overwritten (Re-sync brings them
// back in line).
//
// Storage: each member is a hidden (never printed) MuseScore text line over
// its passage, whose text holds the link ("Jubelle link: group=1 role=source
// sync=…"). Lines move with inserted and deleted bars, save and reopen with
// the score, and open in MuseScore 4.7.7 as ordinary hidden lines. Hand-edited
// notes are remembered by element id, which also survives bar edits.

#pragma once

#include <optional>
#include <set>
#include <vector>

#include "global/types/string.h"

#include "passagelinkplanner.h"

namespace mu::engraving {
class EngravingItem;
class Score;
class TextLine;
}

namespace mu::notation::jubelle {
constexpr const char16_t* LINK_PREFIX = u"Jubelle link:";

struct LinkMember {
    engraving::TextLine* line = nullptr;
    size_t staffIdx = 0;
    int startTick = 0;
    int endTick = 0;
    bool source = false;
    std::set<std::string> handEdited;   // element ids of chords/rests changed by hand (targets only)
};

struct LinkGroup {
    int id = 0;
    passagelink::SyncOptions sync;
    LinkMember source;
    std::vector<LinkMember> targets;
};

//! All link groups in the score (its main score)
std::vector<LinkGroup> linkGroups(const engraving::Score* score);
//! The link member a line (or line segment) belongs to, if it is one
std::optional<LinkMember> linkMember(const engraving::EngravingItem* item);

//! Links the passage [startTick, endTick) on sourceStaff to the same bars on
//! each target staff. Nothing in the music changes. Undoable. Returns the
//! group id, or 0 if nothing could be linked.
int linkPassages(engraving::Score* score, size_t sourceStaff, int startTick, int endTick, const std::vector<size_t>& targetStaves,
                 const passagelink::SyncOptions& sync);

//! Removes one member from its group; the others stay linked. If the source
//! is unlinked, the first target becomes the source. Undoable.
void unlinkMember(engraving::Score* score, engraving::TextLine* memberLine);
//! Changes which kinds of edit a group synchronises. Undoable.
void setSyncOptions(engraving::Score* score, int groupId, const passagelink::SyncOptions& sync);
//! Brings a target back in line with its source, including hand-edited
//! notes (their pitches are kept). Undoable.
void resyncTarget(engraving::Score* score, engraving::TextLine* targetLine);

//! Messages from the last propagation (e.g. why a passage couldn't be updated)
const std::vector<muse::String>& lastLinkMessages();

//! The passage's music as the planner sees it (voice 1)
passagelink::Passage readPassage(const engraving::Score* score, size_t staffIdx, int startTick, int endTick,
                                 const std::set<std::string>& handEdited = {});

//! Installs the hook that makes edits follow links (once, at start-up)
void installLinkedPassagesHook();
}
