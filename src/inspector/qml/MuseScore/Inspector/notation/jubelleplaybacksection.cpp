/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */
#include "jubelleplaybacksection.h"

#include <QStringList>
#include <QVariantMap>

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/playback/jubelle/articulationplayback.h"
#include "notation/internal/jubelle/articulationpassage.h"

#include "translation.h"

using namespace mu::inspector;
using namespace mu::engraving;
namespace jap = mu::notation::jubelle;

void JubellePlaybackSection::update(const Score* score, const std::vector<EngravingItem*>& items)
{

    rows.clear();
    levels.clear();
    note.clear();

    const std::vector<mu::engraving::jubelle::ArtParam> params = jap::paramsForItems(items);
    const jap::PassageRange range = score ? jap::passageForItems(score, items) : jap::PassageRange();

    if (score && range.isValid() && !params.empty()) {
        const MasterScore* master = score->masterScore();
        const Measure* a = master->tick2measure(Fraction::fromTicks(range.startTick));
        const Measure* b = master->tick2measure(Fraction::fromTicks(range.endTick - 1));
        const QString bars = (a && b && a != b) ? muse::qtrc("inspector", "bars %1–%2").arg(a->no() + 1).arg(b->no() + 1)
                             : muse::qtrc("inspector", "bar %1").arg(a ? a->no() + 1 : 1);
        QStringList names;
        for (const Part* p : range.parts) {
            names << p->partName().toQString();
        }
        auto levelEntry = [](const QString& text, int value) {
            QVariantMap m;
            m["text"] = text;
            m["value"] = value;
            return m;
        };
        levels << levelEntry(muse::qtrc("inspector", "Selected notes (%1)").arg(bars), int(jap::EditLevel::Passage))
                         << levelEntry(range.parts.size() == 1 ? names.front() : muse::qtrc("inspector", "Selected instruments (%1)").arg(names.join(", ")),
                                  int(jap::EditLevel::Instrument))
                         << levelEntry(muse::qtrc("inspector", "Whole score"), int(jap::EditLevel::WholeScore));
        if (range.parts.size() > 1) {
            note = muse::qtrc("inspector", "Changes apply to all %n selected instruments.", nullptr, int(range.parts.size()));
        }

        for (const jap::PassageRow& r : jap::levelRows(score, range, jap::EditLevel(this->level), params)) {
            const mu::engraving::jubelle::ArtParamInfo& info = mu::engraving::jubelle::artParamInfo(r.param);
            QVariantMap m;
            m["key"] = QString::fromLatin1(info.key);
            m["title"] = muse::qtrc("notation", jap::paramGroup(r.param)) + ": " + muse::qtrc("notation", jap::paramTitle(r.param));
            m["unit"] = QString::fromLatin1(jap::paramUnit(r.param));
            m["min"] = info.minValue;
            m["max"] = info.maxValue;
            m["value"] = r.value;
            m["isSet"] = r.source == jap::ValueSource::Here;
            m["varies"] = r.varies;
            QString source;
            switch (r.source) {
            case jap::ValueSource::Here: source = muse::qtrc("inspector", "set here");
                break;
            case jap::ValueSource::OtherPassage: source = muse::qtrc("inspector", "from passage, %1").arg(r.sourceName.toQString());
                break;
            case jap::ValueSource::Instrument: source = muse::qtrc("inspector", "from %1").arg(r.sourceName.toQString());
                break;
            case jap::ValueSource::WholeScore: source = muse::qtrc("inspector", "from whole score");
                break;
            case jap::ValueSource::MuseScore: source = muse::qtrc("inspector", "default");
                break;
            }
            if (r.varies) {
                source = muse::qtrc("inspector", "varies between instruments");
            }
            m["source"] = source;

            // Jubelle: what "Use default" goes back to, shown beside a value set here
            const QString unit = QString::fromLatin1(jap::paramUnit(r.param));
            auto valueText = [&unit](int v) {
                if (unit == "curve") {
                    return v < 0 ? muse::qtrc("inspector", "slows down")
                           : v > 0 ? muse::qtrc("inspector", "speeds up") : muse::qtrc("inspector", "even");
                }
                if (unit == "semitones") {
                    return muse::qtrc("inspector", "%n semitone(s)", nullptr, v);
                }
                if (unit.startsWith("%")) {
                    return QString::number(v) + "%";
                }
                return QString::number(v);
            };
            QString inheritedFrom;
            switch (r.inheritedSource) {
            case jap::ValueSource::OtherPassage: inheritedFrom = muse::qtrc("inspector", "passage, %1").arg(r.inheritedSourceName.toQString());
                break;
            case jap::ValueSource::Instrument: inheritedFrom = r.inheritedSourceName.toQString();
                break;
            case jap::ValueSource::WholeScore: inheritedFrom = muse::qtrc("inspector", "whole score");
                break;
            case jap::ValueSource::MuseScore:
            case jap::ValueSource::Here: inheritedFrom = muse::qtrc("inspector", "standard");
                break;
            }
            m["inheritedValue"] = r.inheritedValue;
            m["valueText"] = valueText(r.value);
            m["defaultText"] = muse::qtrc("inspector", "Default: %1 (%2)").arg(valueText(r.inheritedValue), inheritedFrom);
            rows << m;
        }
    }
}

void JubellePlaybackSection::change(Score* score, const std::vector<EngravingItem*>& items, const QString& key,
                                    std::optional<int> value, const std::function<void()>& begin, const std::function<void()>& end)
{
    if (!score) {
        return;
    }
    const jap::PassageRange range = jap::passageForItems(score, items);
    for (const mu::engraving::jubelle::ArtParamInfo& info : mu::engraving::jubelle::artParams()) {
        if (key == QString::fromLatin1(info.key)) {
            begin();
            jap::setLevelValue(score, range, jap::EditLevel(level), info.param, value);
            end();
            break;
        }
    }
}
