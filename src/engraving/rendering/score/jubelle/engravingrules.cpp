/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G7, step 2: engraving rules. See engravingrules.h.

#include "engravingrules.h"

#include <map>

#include "dom/chordrest.h"
#include "dom/engravingitem.h"
#include "dom/instrtemplate.h"
#include "dom/instrument.h"
#include "dom/lyrics.h"
#include "dom/measure.h"
#include "dom/part.h"
#include "dom/score.h"
#include "dom/segment.h"
#include "dom/spanner.h"
#include "dom/staff.h"
#include "style/style.h"

using namespace muse;

namespace mu::engraving::jubelle {
namespace {
struct KindName {
    RuleKind kind;
    const char16_t* name;
};

constexpr KindName KIND_NAMES[] = {
    { RuleKind::Dynamics, u"dynamics" },
    { RuleKind::Hairpins, u"hairpins" },
    { RuleKind::Expressions, u"expressions" },
    { RuleKind::StaffText, u"stafftext" },
    { RuleKind::ChordSymbols, u"chordsymbols" },
    { RuleKind::Lyrics, u"lyrics" },
};

std::optional<RuleKind> kindOf(const EngravingItem* item)
{
    switch (item->type()) {
    case ElementType::DYNAMIC: return RuleKind::Dynamics;
    case ElementType::HAIRPIN: return RuleKind::Hairpins;
    case ElementType::EXPRESSION: return RuleKind::Expressions;
    case ElementType::STAFF_TEXT: return RuleKind::StaffText;
    case ElementType::HARMONY: return RuleKind::ChordSymbols;
    case ElementType::LYRICS: return RuleKind::Lyrics;
    default: return std::nullopt;
    }
}

int specificity(const String& scope)
{
    if (scope.startsWith(u"instrument:")) {
        return 2;
    }
    if (scope.startsWith(u"group:")) {
        return 1;
    }
    return 0;
}

bool scopeMatches(const String& scope, const String& groupId, const String& instrumentId)
{
    if (scope == u"all") {
        return true;
    }
    if (scope.startsWith(u"group:")) {
        return !groupId.isEmpty() && scope.mid(6) == groupId;
    }
    if (scope.startsWith(u"instrument:")) {
        return !instrumentId.isEmpty() && scope.mid(11) == instrumentId;
    }
    return false;
}

// placement and distance from the style, for an item following the style
bool placedDuringLayout(const EngravingItem* item)
{
    // MuseScore 4 decides these from a direction while laying them out
    // (setPlacementBasedOnVoiceAssignment); see ruleDirection()
    return item->isDynamic() || item->isExpression() || item->isHairpin();
}

void applyToItem(EngravingItem* item, const EngravingRule* rule)
{
    bool placementChanged = false;
    if (!placedDuringLayout(item) && item->propertyFlags(Pid::PLACEMENT) == PropertyFlags::STYLED) {
        PlacementV want = item->style().styleV(item->getPropertyStyle(Pid::PLACEMENT)).value<PlacementV>();
        if (rule && rule->placement == RulePlacement::Above) {
            want = PlacementV::ABOVE;
        } else if (rule && rule->placement == RulePlacement::Below) {
            want = PlacementV::BELOW;
        }
        if (item->placement() != want) {
            item->setPlacement(want);
            placementChanged = true;
        }
    }
    if (item->propertyFlags(Pid::MIN_DISTANCE) == PropertyFlags::STYLED) {
        Spatium want = item->style().styleS(item->getPropertyStyle(Pid::MIN_DISTANCE));
        if (rule && rule->minDistanceSp) {
            want = Spatium(*rule->minDistanceSp);
        }
        if (item->minDistance() != want) {
            item->setMinDistance(want);
        }
    }
    // a styled offset depends on the placement (above and below have their own)
    if (placementChanged) {
        if (item->isStyled(Pid::OFFSET)) {
            item->setOffset(item->propertyDefault(Pid::OFFSET).value<PointF>());
        }
        if (item->isSpanner()) {
            for (SpannerSegment* seg : toSpanner(item)->spannerSegments()) {
                seg->setPlacement(item->placement());
                if (seg->isStyled(Pid::OFFSET)) {
                    seg->setOffset(seg->propertyDefault(Pid::OFFSET).value<PointF>());
                }
            }
        }
    }
}
}

String kindName(RuleKind kind)
{
    for (const KindName& k : KIND_NAMES) {
        if (k.kind == kind) {
            return String(k.name);
        }
    }
    return String();
}

std::optional<RuleKind> kindFromName(const String& name)
{
    for (const KindName& k : KIND_NAMES) {
        if (name == k.name) {
            return k.kind;
        }
    }
    return std::nullopt;
}

std::vector<EngravingRule> parseRules(const String& text)
{
    std::vector<EngravingRule> rules;
    for (String line : text.split(u'\n')) {
        line = line.trimmed();
        if (line.isEmpty() || line.startsWith(u"#")) {
            continue;
        }
        std::vector<String> words;
        for (const String& w : line.split(u' ')) {
            if (!w.isEmpty()) {
                words.push_back(w);
            }
        }
        if (words.size() < 3) {
            continue;
        }
        const std::optional<RuleKind> kind = kindFromName(words[0]);
        if (!kind) {
            continue;
        }
        EngravingRule r;
        r.kind = *kind;
        if (words[1] != u"all" && !words[1].startsWith(u"group:") && !words[1].startsWith(u"instrument:")) {
            continue;
        }
        r.scope = words[1];
        if (words[2] == u"above") {
            r.placement = RulePlacement::Above;
        } else if (words[2] == u"below") {
            r.placement = RulePlacement::Below;
        } else if (words[2] == u"default") {
            r.placement = RulePlacement::Default;
        } else {
            continue;
        }
        if (words.size() >= 4) {
            bool ok = false;
            const double d = words[3].toDouble(&ok);
            if (ok && d >= -10.0 && d <= 20.0) {
                r.minDistanceSp = d;
            }
        }
        rules.push_back(r);
    }
    return rules;
}

String formatRules(const std::vector<EngravingRule>& rules)
{
    String out;
    for (const EngravingRule& r : rules) {
        if (!out.isEmpty()) {
            out += u"\n";
        }
        out += kindName(r.kind) + u" " + r.scope + u" ";
        out += r.placement == RulePlacement::Above ? u"above" : r.placement == RulePlacement::Below ? u"below" : u"default";
        if (r.minDistanceSp) {
            out += u" " + String::number(*r.minDistanceSp, 2);
        }
    }
    return out;
}

std::vector<EngravingRule> presetRules(const String& presetId)
{
    auto rule = [](RuleKind k, const char16_t* scope, RulePlacement p, std::optional<double> d = std::nullopt) {
        EngravingRule r;
        r.kind = k;
        r.scope = String(scope);
        r.placement = p;
        r.minDistanceSp = d;
        return r;
    };
    // Gould, Behind Bars: instrumental dynamics and hairpins below the staff;
    // vocal dynamics and hairpins above, so the lyrics below stay clear.
    if (presetId == u"jazz") {
        return {
            rule(RuleKind::Dynamics, u"all", RulePlacement::Below),
            rule(RuleKind::Hairpins, u"all", RulePlacement::Below),
            rule(RuleKind::Dynamics, u"group:vocals", RulePlacement::Above),
            rule(RuleKind::Hairpins, u"group:vocals", RulePlacement::Above),
            // chord symbols a little clear of the staff, for reading at a glance
            rule(RuleKind::ChordSymbols, u"all", RulePlacement::Above, 1.0),
        };
    }
    if (presetId == u"choral") {
        return {
            rule(RuleKind::Dynamics, u"group:vocals", RulePlacement::Above),
            rule(RuleKind::Hairpins, u"group:vocals", RulePlacement::Above),
            rule(RuleKind::Expressions, u"group:vocals", RulePlacement::Above),
            rule(RuleKind::Lyrics, u"group:vocals", RulePlacement::Below),
        };
    }
    if (presetId == u"orchestral") {
        return {
            rule(RuleKind::Dynamics, u"all", RulePlacement::Below),
            rule(RuleKind::Hairpins, u"all", RulePlacement::Below),
            rule(RuleKind::Expressions, u"all", RulePlacement::Below),
            rule(RuleKind::StaffText, u"all", RulePlacement::Above),
            rule(RuleKind::Dynamics, u"group:vocals", RulePlacement::Above),
            rule(RuleKind::Hairpins, u"group:vocals", RulePlacement::Above),
        };
    }
    return {};   // "standard": no rules, MuseScore's own placement
}

const EngravingRule* ruleFor(const std::vector<EngravingRule>& rules, RuleKind kind, const String& groupId, const String& instrumentId)
{
    const EngravingRule* best = nullptr;
    int bestSpecificity = -1;
    for (const EngravingRule& r : rules) {
        if (r.kind != kind || !scopeMatches(r.scope, groupId, instrumentId)) {
            continue;
        }
        const int s = specificity(r.scope);
        if (s >= bestSpecificity) {   // the later one wins between equals
            best = &r;
            bestSpecificity = s;
        }
    }
    return best;
}

void applyEngravingRules(Score* score)
{
    if (!score) {
        return;
    }
    const String text = score->style().styleSt(Sid::jubelleEngravingRules);
    if (text.isEmpty()) {
        return;   // no rules: MuseScore's own placement (a style change already restored it)
    }
    const std::vector<EngravingRule> rules = parseRules(text);

    struct InstrumentInfo {
        String group;
        String id;
    };
    std::map<const Instrument*, InstrumentInfo> infoCache;
    auto infoFor = [&infoCache](const EngravingItem* item) -> const InstrumentInfo& {
        static const InstrumentInfo NONE;
        const Part* part = item->part();
        const Instrument* instrument = part ? part->instrument(item->tick()) : nullptr;
        if (!instrument) {
            return NONE;
        }
        auto it = infoCache.find(instrument);
        if (it == infoCache.end()) {
            InstrumentInfo info;
            info.id = instrument->id();
            if (const InstrumentTemplate* t = searchTemplate(info.id)) {
                info.group = t->groupId;
            }
            it = infoCache.emplace(instrument, info).first;
        }
        return it->second;
    };

    auto apply = [&](EngravingItem* item) {
        const std::optional<RuleKind> kind = kindOf(item);
        if (!kind) {
            return;
        }
        const InstrumentInfo& info = infoFor(item);
        applyToItem(item, ruleFor(rules, *kind, info.group, info.id));
    };

    for (Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        for (Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
            for (EngravingItem* a : seg->annotations()) {
                apply(a);
            }
            for (EngravingItem* e : seg->elist()) {
                if (e && e->isChordRest()) {
                    for (Lyrics* l : toChordRest(e)->lyrics()) {
                        apply(l);
                    }
                }
            }
        }
    }
    for (const auto& [tick, sp] : score->spanner()) {
        if (sp && sp->isHairpin()) {
            apply(sp);
        }
    }
}

DirectionV ruleDirection(const EngravingItem* item, DirectionV styled)
{
    if (!item || !item->score()) {
        return styled;
    }
    const String text = item->style().styleSt(Sid::jubelleEngravingRules);
    if (text.isEmpty()) {
        return styled;
    }
    // parsed once per text (layout asks for every item)
    static thread_local String cachedText;
    static thread_local std::vector<EngravingRule> cachedRules;
    if (text != cachedText) {
        cachedText = text;
        cachedRules = parseRules(text);
    }
    const std::optional<RuleKind> kind = kindOf(item);
    if (!kind) {
        return styled;
    }
    String group;
    String id;
    const Part* part = item->part();
    if (const Instrument* instrument = part ? part->instrument(item->tick()) : nullptr) {
        id = instrument->id();
        if (const InstrumentTemplate* t = searchTemplate(id)) {
            group = t->groupId;
        }
    }
    const EngravingRule* rule = ruleFor(cachedRules, *kind, group, id);
    if (!rule || rule->placement == RulePlacement::Default) {
        return styled;
    }
    return rule->placement == RulePlacement::Above ? DirectionV::UP : DirectionV::DOWN;
}
}
