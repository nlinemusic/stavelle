/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G7, step 2 (G3 design approved 15 Nov 2026): engraving rules.
//
// A rule says where a kind of marking goes (above or below the staff) and/or
// how far from the staff it stays at least, for the whole score, one
// instrument group (woodwinds, brass, strings, vocals, keyboards…) or one
// instrument. The most specific rule wins (instrument, then group, then the
// whole score); between equally specific rules, the later one wins.
//
// Rules are stored in the score's style (Sid::jubelleEngravingRules), one per
// line:   <kind> <scope> <placement> [<minimum distance in sp>]
//   kind:       dynamics | hairpins | expressions | stafftext | chordsymbols | lyrics
//   scope:      all | group:<group id> | instrument:<instrument id>
//   placement:  above | below | default
//   e.g.        dynamics group:vocals above
//               chordsymbols all default 1.5
//
// At each layout they decide the placement and minimum distance of markings
// that still follow the style (anything you set by hand in Properties wins).
// Nothing is written into the music itself, so MuseScore opens the file with
// its own layout. With no rules, nothing changes.

#pragma once

#include <optional>
#include <vector>

#include "global/types/string.h"
#include "../../../types/types.h"

namespace mu::engraving {
class EngravingItem;
class Score;
}

namespace mu::engraving::jubelle {
enum class RuleKind {
    Dynamics,
    Hairpins,
    Expressions,
    StaffText,
    ChordSymbols,
    Lyrics,
};

enum class RulePlacement {
    Default,    // keep the style's placement
    Above,
    Below,
};

struct EngravingRule {
    RuleKind kind = RuleKind::Dynamics;
    muse::String scope = u"all";             // "all", "group:<id>" or "instrument:<id>"
    RulePlacement placement = RulePlacement::Default;
    std::optional<double> minDistanceSp;     // none: keep the style's distance

    bool operator==(const EngravingRule& o) const
    {
        return kind == o.kind && scope == o.scope && placement == o.placement && minDistanceSp == o.minDistanceSp;
    }
};

//! Parses the stored text; lines that aren't rules are skipped
std::vector<EngravingRule> parseRules(const muse::String& text);
//! The stored text for a list of rules
muse::String formatRules(const std::vector<EngravingRule>& rules);

//! The kind names used in the stored text ("dynamics", ...)
muse::String kindName(RuleKind kind);
std::optional<RuleKind> kindFromName(const muse::String& name);

//! The presets (Gould, Behind Bars): "standard" (no rules), "jazz", "choral", "orchestral"
std::vector<EngravingRule> presetRules(const muse::String& presetId);

//! The rule that applies to an item of `kind` on an instrument, if any.
//! `groupId` and `instrumentId` describe the instrument.
const EngravingRule* ruleFor(const std::vector<EngravingRule>& rules, RuleKind kind, const muse::String& groupId,
                             const muse::String& instrumentId);

//! Applies the score's rules before layout (does nothing without rules)
void applyEngravingRules(Score* score);

//! For dynamics, hairpins and expressions, which MuseScore places during
//! layout from a styled direction: the rule's direction for this item, or
//! `styled` if no rule applies. (Your own above/below choice in Properties is
//! checked before this, so it still wins.)
DirectionV ruleDirection(const EngravingItem* item, DirectionV styled);
}
