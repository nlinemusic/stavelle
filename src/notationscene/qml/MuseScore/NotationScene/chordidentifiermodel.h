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

namespace mu::notation {
//! Jubelle: the Chord Identifier dialog (Tools → Chord Identifier…).
//! Shows what will be analysed, holds the options (remembered between uses)
//! and runs the built-in identifier as one undoable step.
class ChordIdentifierModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(QString scopeTitle READ scopeTitle NOTIFY scopeChanged)
    Q_PROPERTY(QString scopeDetail READ scopeDetail NOTIFY scopeChanged)
    Q_PROPERTY(bool canRun READ canRun NOTIFY optionsChanged)

    Q_PROPERTY(bool chordSymbols READ chordSymbols WRITE setChordSymbols NOTIFY optionsChanged)
    Q_PROPERTY(bool romanNumerals READ romanNumerals WRITE setRomanNumerals NOTIFY optionsChanged)
    Q_PROPERTY(bool slashBass READ slashBass WRITE setSlashBass NOTIFY optionsChanged)
    Q_PROPERTY(bool figuredBass READ figuredBass WRITE setFiguredBass NOTIFY optionsChanged)
    Q_PROPERTY(bool replaceExisting READ replaceExisting WRITE setReplaceExisting NOTIFY optionsChanged)
    Q_PROPERTY(bool onlyWhenChordChanges READ onlyWhenChordChanges WRITE setOnlyWhenChordChanges NOTIFY optionsChanged)
    Q_PROPERTY(bool highlight READ highlight WRITE setHighlight NOTIFY optionsChanged)

    Q_PROPERTY(QVariantList highlightLegend READ highlightLegend CONSTANT)
    Q_PROPERTY(QString resultText READ resultText NOTIFY resultChanged)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };

public:
    explicit ChordIdentifierModel(QObject* parent = nullptr);

    Q_INVOKABLE void load();
    Q_INVOKABLE bool run();
    Q_INVOKABLE void removeHighlight();

    QString scopeTitle() const;
    QString scopeDetail() const;
    bool canRun() const;

    bool chordSymbols() const;
    void setChordSymbols(bool v);
    bool romanNumerals() const;
    void setRomanNumerals(bool v);
    bool slashBass() const;
    void setSlashBass(bool v);
    bool figuredBass() const;
    void setFiguredBass(bool v);
    bool replaceExisting() const;
    void setReplaceExisting(bool v);
    bool onlyWhenChordChanges() const;
    void setOnlyWhenChordChanges(bool v);
    bool highlight() const;
    void setHighlight(bool v);

    QVariantList highlightLegend() const;
    QString resultText() const;

signals:
    void scopeChanged();
    void optionsChanged();
    void resultChanged();

private:
    void setOption(bool& field, bool value, const char* key);
    void setResult(const QString& text);

    QString m_scopeTitle;
    QString m_scopeDetail;
    QString m_resultText;

    bool m_chordSymbols = true;
    bool m_romanNumerals = false;
    bool m_slashBass = true;
    bool m_figuredBass = true;
    bool m_replaceExisting = false;
    bool m_onlyWhenChordChanges = true;
    bool m_highlight = false;
};
}
