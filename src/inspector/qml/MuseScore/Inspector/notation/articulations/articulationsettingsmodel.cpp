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
#include "../jubelleplaybacksection.h"

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
    const notation::INotationPtr notation = currentNotation();
    const Score* score = notation && notation->elements() ? notation->elements()->msScore() : nullptr;
    const std::vector<EngravingItem*> items(m_elementList.begin(), m_elementList.end());
    m_playback.update(score, items);
    emit playbackChanged();
}

QVariantList ArticulationSettingsModel::playbackRows() const
{
    return m_playback.rows;
}

int ArticulationSettingsModel::playbackLevel() const
{
    return m_playback.level;
}

void ArticulationSettingsModel::setPlaybackLevel(int level)
{
    if (level < 0 || level > 2 || level == m_playback.level) {
        return;
    }
    m_playback.level = level;
    updatePlayback();
}

QVariantList ArticulationSettingsModel::playbackLevels() const
{
    return m_playback.levels;
}

QString ArticulationSettingsModel::playbackNote() const
{
    return m_playback.note;
}

void ArticulationSettingsModel::changePlayback(const QString& key, std::optional<int> value)
{
    const notation::INotationPtr notation = currentNotation();
    Score* score = notation && notation->elements() ? notation->elements()->msScore() : nullptr;
    const std::vector<EngravingItem*> items(m_elementList.begin(), m_elementList.end());
    m_playback.change(score, items, key, value,
                      [this]() { beginCommand(muse::TranslatableString("undoableAction", "Change articulation playback")); },
                      [this]() { endCommand(); });
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
