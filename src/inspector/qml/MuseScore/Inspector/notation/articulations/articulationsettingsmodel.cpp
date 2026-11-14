/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */
#include "articulationsettingsmodel.h"

#include "engraving/dom/articulation.h"
#include "engraving/dom/tapping.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/playback/jubelle/articulationplayback.h"
#include "notation/inotationelements.h"
#include "notation/internal/jubelle/articulationpassage.h"

#include <QStringList>
#include <QVariantMap>

#include "log.h"
#include "translation.h"

using namespace mu::inspector;
using namespace mu::engraving;

ArticulationSettingsModel::ArticulationSettingsModel(QObject* parent, const muse::modularity::ContextPtr& iocCtx,
                                                     IElementRepositoryService* repository, InspectorModelType type)
    : AbstractInspectorModel(parent, iocCtx, repository)
{
    setModelType(type);
    setTitle(type == InspectorModelType::TYPE_ARTICULATION ? muse::qtrc("inspector", "Articulation") : muse::qtrc("inspector", "Tapping"));
    setIcon(muse::ui::IconCode::Code::ARTICULATION);
    createProperties();
}

void ArticulationSettingsModel::createProperties()
{
    m_placement = buildPropertyItem(mu::engraving::Pid::ARTICULATION_ANCHOR);
}

void ArticulationSettingsModel::requestElements()
{
    m_elementList = m_repository->findElementsByType(mu::engraving::ElementType::ARTICULATION);

    QList<mu::engraving::EngravingItem*> tappings = m_repository->findElementsByType(mu::engraving::ElementType::TAPPING);
    for (mu::engraving::EngravingItem* tapping : tappings) {
        m_elementList.push_back(tapping);
    }

    QList<mu::engraving::EngravingItem*> halfSlurSegs
        = m_repository->findElementsByType(mu::engraving::ElementType::TAPPING_HALF_SLUR_SEGMENT);
    for (mu::engraving::EngravingItem* halfSlurSeg : halfSlurSegs) {
        TappingHalfSlur* halfSlur = toTappingHalfSlur(toTappingHalfSlurSegment(halfSlurSeg)->spanner());
        Tapping* tapping = toTapping(halfSlur->parentItem());
        m_elementList.push_back(tapping);
    }
}

void ArticulationSettingsModel::loadProperties()
{
    loadPropertyItem(m_placement);
    updateIsPlacementAvailable();
    updatePlayback();
}

// ---------------------------------------------------------------------------
// Jubelle: articulation playback (global → instrument → passage)
// ---------------------------------------------------------------------------

void ArticulationSettingsModel::onNotationChanged(const PropertyIdSet&, const StyleIdSet&)
{
    updatePlayback();   // values may have changed by undo or elsewhere
}

void ArticulationSettingsModel::updatePlayback()
{
    namespace jap = mu::notation::jubelle;
    m_playbackRows.clear();
    m_playbackLevels.clear();
    m_playbackNote.clear();

    const notation::INotationPtr notation = currentNotation();
    const Score* score = notation && notation->elements() ? notation->elements()->msScore() : nullptr;
    const std::vector<EngravingItem*> items(m_elementList.begin(), m_elementList.end());
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
        auto level = [](const QString& text, int value) {
            QVariantMap m;
            m["text"] = text;
            m["value"] = value;
            return m;
        };
        m_playbackLevels << level(muse::qtrc("inspector", "Selected notes (%1)").arg(bars), int(jap::EditLevel::Passage))
                         << level(range.parts.size() == 1 ? names.front() : muse::qtrc("inspector", "Selected instruments (%1)").arg(names.join(", ")),
                                  int(jap::EditLevel::Instrument))
                         << level(muse::qtrc("inspector", "Whole score"), int(jap::EditLevel::WholeScore));
        if (range.parts.size() > 1) {
            m_playbackNote = muse::qtrc("inspector", "Changes apply to all %n selected instruments.", nullptr, int(range.parts.size()));
        }

        for (const jap::PassageRow& r : jap::levelRows(score, range, jap::EditLevel(m_playbackLevel), params)) {
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
            m_playbackRows << m;
        }
    }
    emit playbackChanged();
}

QVariantList ArticulationSettingsModel::playbackRows() const
{
    return m_playbackRows;
}

int ArticulationSettingsModel::playbackLevel() const
{
    return m_playbackLevel;
}

void ArticulationSettingsModel::setPlaybackLevel(int level)
{
    if (level < 0 || level > 2 || level == m_playbackLevel) {
        return;
    }
    m_playbackLevel = level;
    updatePlayback();
}

QVariantList ArticulationSettingsModel::playbackLevels() const
{
    return m_playbackLevels;
}

QString ArticulationSettingsModel::playbackNote() const
{
    return m_playbackNote;
}

void ArticulationSettingsModel::changePlayback(const QString& key, std::optional<int> value)
{
    namespace jap = mu::notation::jubelle;
    const notation::INotationPtr notation = currentNotation();
    Score* score = notation && notation->elements() ? notation->elements()->msScore() : nullptr;
    if (!score) {
        return;
    }
    const std::vector<EngravingItem*> items(m_elementList.begin(), m_elementList.end());
    const jap::PassageRange range = jap::passageForItems(score, items);
    for (const mu::engraving::jubelle::ArtParamInfo& info : mu::engraving::jubelle::artParams()) {
        if (key == QString::fromLatin1(info.key)) {
            // Playback only: the written articulations are never changed
            beginCommand(muse::TranslatableString("undoableAction", "Change articulation playback"));
            jap::setLevelValue(score, range, jap::EditLevel(m_playbackLevel), info.param, value);
            endCommand();
            break;
        }
    }
    updatePlayback();
}

void ArticulationSettingsModel::setPlaybackValue(const QString& key, int value)
{
    changePlayback(key, value);
}

void ArticulationSettingsModel::clearPlaybackValue(const QString& key)
{
    changePlayback(key, std::nullopt);
}

void ArticulationSettingsModel::resetProperties()
{
    m_placement->resetToDefault();
}

void ArticulationSettingsModel::updateIsPlacementAvailable()
{
    bool available = false;
    for (EngravingItem* item : m_elementList) {
        if (!item->isTapping()) {
            available = true;
            break;
        }

        Tapping* tapping = toTapping(item);
        bool hasSymbol = tapping->hand() == TappingHand::RIGHT || tapping->lhShowItems() != LHTappingShowItems::HALF_SLUR;
        if (hasSymbol) {
            available = true;
            break;
        }
    }

    if (available != m_isPlacementAvailable) {
        m_isPlacementAvailable = available;
        emit isPlacementAvailableChanged(m_isPlacementAvailable);
    }
}

PropertyItem* ArticulationSettingsModel::placement() const
{
    return m_placement;
}

bool ArticulationSettingsModel::isPlacementAvailable() const
{
    return m_isPlacementAvailable;
}
