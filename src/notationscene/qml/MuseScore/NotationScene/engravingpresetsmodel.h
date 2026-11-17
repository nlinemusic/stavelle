/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 * Music Composition & Notation
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
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "context/iglobalcontext.h"
#include "notation/inotationconfiguration.h"
#include "iinteractive.h"

namespace mu::notation {
//! Jubelle: engraving presets. A preset is an ordinary MuseScore style file
//! (.mss) kept in the user's Styles folder (Preferences → Folders), so presets
//! also work with Format → Load style and in MuseScore itself.
class EngravingPresetsModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QVariantList presets READ presets NOTIFY presetsChanged)
    Q_PROPERTY(QString folder READ folder CONSTANT)

    QML_ELEMENT

    muse::GlobalInject<INotationConfiguration> configuration;
    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<muse::IInteractive> interactive = { this };

public:
    explicit EngravingPresetsModel(QObject* parent = nullptr);

    QVariantList presets() const;
    QString folder() const;

    Q_INVOKABLE void load();
    //! "" if `name` can be used for a preset file, otherwise the reason.
    Q_INVOKABLE QString validateName(const QString& name) const;
    //! Saves the current score's engraving style as a preset. Asks before replacing one.
    Q_INVOKABLE bool saveCurrentStyle(const QString& name);
    //! Applies the preset to the current score (one undoable step, like Load style).
    Q_INVOKABLE bool apply(const QString& path);
    Q_INVOKABLE void reveal(const QString& path);
    Q_INVOKABLE void openFolder();

signals:
    void presetsChanged();

private:
    QVariantList m_presets;
};
}
