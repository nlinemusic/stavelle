/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "jubellelayoutpagemodel.h"

#include <algorithm>

#include <QVariantMap>

#include "engraving/dom/instrument.h"
#include "engraving/dom/part.h"
#include "engraving/dom/score.h"
#include "engraving/rendering/score/jubelle/engravingrules.h"
#include "translation.h"

using namespace mu::notation;

JubelleLayoutPageModel::JubelleLayoutPageModel(QObject* parent)
    : AbstractStyleDialogModel(parent, {
    StyleId::jubelleMagneticLayout,
    StyleId::jubelleMagneticRows,
    StyleId::jubelleSidewaysNudge,
    StyleId::jubelleNudgeLimit,
    StyleId::jubelleCollisionAvoidance,
    StyleId::jubelleEngravingRules,
})
{
}

namespace jr = mu::engraving::jubelle;

QString JubelleLayoutPageModel::rulesText() const
{
    return styleItem(StyleId::jubelleEngravingRules)->value().toString();
}

void JubelleLayoutPageModel::setRulesText(const QString& text)
{
    styleItem(StyleId::jubelleEngravingRules)->modifyValue(text);
    emit rulesChanged();
}

QVariantList JubelleLayoutPageModel::rules() const
{
    QVariantList out;
    for (const jr::EngravingRule& r : jr::parseRules(muse::String::fromQString(rulesText()))) {
        QVariantMap m;
        m["kind"] = jr::kindName(r.kind).toQString();
        m["scope"] = r.scope.toQString();
        m["placement"] = r.placement == jr::RulePlacement::Above ? "above" : r.placement == jr::RulePlacement::Below ? "below" : "default";
        m["hasDistance"] = r.minDistanceSp.has_value();
        m["distance"] = r.minDistanceSp.value_or(0.0);
        out << m;
    }
    return out;
}

static QVariantMap option(const QString& value, const QString& text)
{
    QVariantMap m;
    m["value"] = value;
    m["text"] = text;
    return m;
}

QVariantList JubelleLayoutPageModel::kindOptions() const
{
    return {
        option("dynamics", muse::qtrc("notation/editstyle/jubellelayout", "Dynamics")),
        option("hairpins", muse::qtrc("notation/editstyle/jubellelayout", "Hairpins")),
        option("expressions", muse::qtrc("notation/editstyle/jubellelayout", "Expressions")),
        option("stafftext", muse::qtrc("notation/editstyle/jubellelayout", "Staff text")),
        option("chordsymbols", muse::qtrc("notation/editstyle/jubellelayout", "Chord symbols")),
        option("lyrics", muse::qtrc("notation/editstyle/jubellelayout", "Lyrics")),
    };
}

QVariantList JubelleLayoutPageModel::scopeOptions() const
{
    QVariantList out {
        option("all", muse::qtrc("notation/editstyle/jubellelayout", "All instruments")),
        option("group:woodwinds", muse::qtrc("notation/editstyle/jubellelayout", "Woodwinds")),
        option("group:brass", muse::qtrc("notation/editstyle/jubellelayout", "Brass")),
        option("group:strings", muse::qtrc("notation/editstyle/jubellelayout", "Strings")),
        option("group:vocals", muse::qtrc("notation/editstyle/jubellelayout", "Voices")),
        option("group:keyboards", muse::qtrc("notation/editstyle/jubellelayout", "Keyboards")),
        option("group:plucked-strings", muse::qtrc("notation/editstyle/jubellelayout", "Guitars and plucked strings")),
        option("group:pitched-percussion", muse::qtrc("notation/editstyle/jubellelayout", "Pitched percussion")),
        option("group:unpitched-percussion", muse::qtrc("notation/editstyle/jubellelayout", "Drums and percussion")),
    };
    // the score's own instruments
    QStringList seen;
    if (context() && context()->currentNotation()) {
        if (const mu::engraving::Score* score = context()->currentNotation()->elements()->msScore()) {
            for (const mu::engraving::Part* part : score->parts()) {
                const mu::engraving::Instrument* instrument = part->instrument();
                if (!instrument || seen.contains(instrument->id().toQString())) {
                    continue;
                }
                seen << instrument->id().toQString();
                out << option("instrument:" + instrument->id().toQString(), part->partName().toQString());
            }
        }
    }
    // a rule for an instrument no longer in the score stays visible
    for (const QVariant& v : rules()) {
        const QString scope = v.toMap().value("scope").toString();
        bool found = false;
        for (const QVariant& o : out) {
            found = found || o.toMap().value("value").toString() == scope;
        }
        if (!found) {
            out << option(scope, scope);
        }
    }
    return out;
}

QVariantList JubelleLayoutPageModel::placementOptions() const
{
    return {
        option("above", muse::qtrc("notation/editstyle/jubellelayout", "Above the staff")),
        option("below", muse::qtrc("notation/editstyle/jubellelayout", "Below the staff")),
        option("default", muse::qtrc("notation/editstyle/jubellelayout", "As in the style")),
    };
}

QVariantList JubelleLayoutPageModel::presetOptions() const
{
    return {
        option("standard", muse::qtrc("notation/editstyle/jubellelayout", "Standard (no rules)")),
        option("jazz", muse::qtrc("notation/editstyle/jubellelayout", "Jazz big band")),
        option("choral", muse::qtrc("notation/editstyle/jubellelayout", "Choral")),
        option("orchestral", muse::qtrc("notation/editstyle/jubellelayout", "Orchestral")),
    };
}

void JubelleLayoutPageModel::addRule()
{
    auto list = jr::parseRules(muse::String::fromQString(rulesText()));
    jr::EngravingRule r;
    r.kind = jr::RuleKind::Dynamics;
    r.scope = u"all";
    r.placement = jr::RulePlacement::Below;
    list.push_back(r);
    setRulesText(jr::formatRules(list).toQString());
}

void JubelleLayoutPageModel::removeRule(int index)
{
    auto list = jr::parseRules(muse::String::fromQString(rulesText()));
    if (index < 0 || index >= int(list.size())) {
        return;
    }
    list.erase(list.begin() + index);
    setRulesText(jr::formatRules(list).toQString());
}

void JubelleLayoutPageModel::setRuleField(int index, const QString& field, const QVariant& value)
{
    auto list = jr::parseRules(muse::String::fromQString(rulesText()));
    if (index < 0 || index >= int(list.size())) {
        return;
    }
    jr::EngravingRule& r = list[size_t(index)];
    const QString v = value.toString();
    if (field == "kind") {
        if (auto k = jr::kindFromName(muse::String::fromQString(v))) {
            r.kind = *k;
        }
    } else if (field == "scope") {
        if (v == "all" || v.startsWith("group:") || v.startsWith("instrument:")) {
            r.scope = muse::String::fromQString(v);
        }
    } else if (field == "placement") {
        r.placement = v == "above" ? jr::RulePlacement::Above : v == "below" ? jr::RulePlacement::Below : jr::RulePlacement::Default;
    } else if (field == "distance") {
        bool ok = false;
        const double d = value.toDouble(&ok);
        if (v.isEmpty() || !ok) {
            r.minDistanceSp.reset();
        } else {
            r.minDistanceSp = std::clamp(d, -10.0, 20.0);
        }
    }
    setRulesText(jr::formatRules(list).toQString());
}

void JubelleLayoutPageModel::applyPreset(const QString& presetId)
{
    setRulesText(jr::formatRules(jr::presetRules(muse::String::fromQString(presetId))).toQString());
}
