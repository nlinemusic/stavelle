/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */
#pragma once

#include <functional>
#include <optional>
#include <vector>

#include <QString>
#include <QVariantList>

namespace mu::engraving {
class EngravingItem;
class Score;
}

namespace mu::inspector {
//! Jubelle: the "Playback" section of contextual Properties (articulations, jazz turn, glissandos):
//! the level to set ("Selected notes", instrument, whole score), one row per value with where it comes
//! from and its default, and the change itself. Shared by every Properties model that shows it, so
//! the same values behave the same way everywhere. Used with JubellePlaybackValues.qml.
class JubellePlaybackSection
{
public:
    QVariantList rows;
    QVariantList levels;
    QString note;
    int level = 0;   // notation::jubelle::EditLevel

    void update(const engraving::Score* score, const std::vector<engraving::EngravingItem*>& items);

    //! Sets (or with nullopt clears) one value at the current level, inside `begin`/`end` (the model's
    //! undoable command). Playback only: the written notation never changes.
    void change(engraving::Score* score, const std::vector<engraving::EngravingItem*>& items, const QString& key,
                std::optional<int> value, const std::function<void()>& begin, const std::function<void()>& end);
};
}
