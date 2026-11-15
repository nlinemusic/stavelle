/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 * Music Composition & Notation
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "global/io/path.h"
#include "global/types/string.h"
#include "modularity/ioc.h"

namespace mu::engraving {
class EngravingProject;
class MasterScore;
class Score;
}

//! Jubelle G2: the condensed full-score copy.
//!
//! makeCondensedCopy() builds a separate score from the original: pairs of
//! players of the same instrument (Flute 1 and 2, Trumpet 1 and 2…) share one
//! staff wherever the music allows it, with a2 / 1. / 2. labels, same-rhythm
//! chords on one stem and two voices for different rhythms. On any system where
//! the music doesn't fit on one staff (divisi, crossing parts, a player using
//! several voices…), both players keep their own staves for that system only.
//!
//! The original score is only read: nothing in it changes. The copy is made
//! only when the user runs the command, and running it again replaces the
//! copy's contents (including any edits made in the copy). Manual overrides
//! ("keep separate here" / "condense here") are kept in the copy and read back
//! when it is updated.
namespace mu::notation::jubelle {
//! Meta tags of a condensed copy (MuseScore keeps and ignores them)
constexpr const char16_t* CONDENSED_COPY_TAG = u"jubelleCondensedCopy";        // "1" in a condensed copy
constexpr const char16_t* CONDENSED_FROM_TAG = u"jubelleCondensedFrom";        // the original's file name
constexpr const char16_t* CONDENSE_OVERRIDES_TAG = u"jubelleCondenseOverrides";

//! Two players who can share a staff: staff indices in the original (and the copy)
struct CondenseGroup {
    size_t first = 0;
    size_t second = 0;
};

//! A manual override for bars [fromBar, toBar] (0-based measure indices, both
//! included) of the group whose first player is on staff `staffIdx`
struct CondenseOverride {
    enum class Mode {
        Separate,   //!< keep the players on their own staves here
        Condense,   //!< put them on one staff here even if the automatic choice wouldn't
    };
    size_t staffIdx = 0;
    int fromBar = 0;
    int toBar = 0;
    Mode mode = Mode::Separate;

    bool operator==(const CondenseOverride& o) const
    {
        return staffIdx == o.staffIdx && fromBar == o.fromBar && toBar == o.toBar && mode == o.mode;
    }
};

struct CondenseReport {
    int groups = 0;
    int systems = 0;
    int systemsCondensed = 0;    //!< (group, system) pairs on one staff
    int systemsSeparate = 0;     //!< (group, system) pairs kept apart
    std::vector<muse::String> notes;   //!< plain-language notes (what couldn't be shown and why)
};

//! The pairs of players that can be condensed, in score order: consecutive
//! single-staff woodwind or brass parts of the same instrument and
//! transposition (strings, keyboards, voices and percussion are never condensed)
std::vector<CondenseGroup> condenseGroups(const engraving::Score* score);

bool isCondensedCopy(const engraving::Score* score);

muse::String overridesText(const std::vector<CondenseOverride>& overrides);
std::vector<CondenseOverride> parseOverrides(const muse::String& text);
std::vector<CondenseOverride> readOverrides(const engraving::Score* copy);

//! Sets (or, with mode nullopt, removes) the override for bars [fromBar, toBar]
//! of the group on `staffIdx` in a condensed copy. Undoable (call inside a
//! command). It takes effect the next time the copy is updated.
void setOverride(engraving::Score* copy, size_t staffIdx, int fromBar, int toBar, std::optional<CondenseOverride::Mode> mode);

//! Builds the condensed copy. The original is only read. Returns the new
//! score's project (it owns the score), or nullptr if there is nothing to
//! condense (the report's notes say why).
std::shared_ptr<engraving::EngravingProject> makeCondensedCopy(engraving::MasterScore* original,
                                                               const std::vector<CondenseOverride>& overrides, CondenseReport* report);

//! Where the condensed copy of a score lives: next to it, named
//! "<name> (condensed).mscz"
muse::io::path_t condensedPathFor(const muse::io::path_t& originalPath);

//! What is at that path now
struct ExistingCopy {
    bool exists = false;
    bool isCondensedCopy = false;    //!< false: another file has that name, and it is never replaced
    std::vector<CondenseOverride> overrides;
};
ExistingCopy readExistingCopy(const muse::io::path_t& path, const muse::modularity::ContextPtr& iocCtx);

//! Writes the copy as an .mscz file: first to a temporary file next to it,
//! then moved into place, so a failure never leaves a half-written copy.
//! Refuses to replace a file that isn't a condensed copy.
bool writeCondensedFile(engraving::EngravingProject& project, const muse::io::path_t& path, muse::String* error);

//! "Trumpet 1" + "Trumpet 2" → "Trumpet 1.2"; "Flute" + "Flute" → "Flute 1.2"
muse::String combinedName(const muse::String& a, const muse::String& b);
}
