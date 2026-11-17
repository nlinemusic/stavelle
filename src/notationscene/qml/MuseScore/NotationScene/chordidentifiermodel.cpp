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
#include "chordidentifiermodel.h"

#include <QColor>
#include <QStringList>
#include <QVariantMap>

#include "settings.h"
#include "translation.h"

#include "notation/inotationinteraction.h"
#include "notation/internal/jubelle/chordidentifierrunner.h"

using namespace mu::notation;
using namespace muse;

namespace ci = mu::notation::chordidentifier;

static Settings::Key key(const char* name)
{
    return Settings::Key("notation", std::string("jubelle/chordIdentifier/") + name);
}

static bool readBool(const char* name, bool def)
{
    const Val v = settings()->value(key(name));
    return v.isNull() ? def : v.toBool();
}

ChordIdentifierModel::ChordIdentifierModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void ChordIdentifierModel::load()
{
    m_chordSymbols = readBool("chordSymbols", true);
    m_romanNumerals = readBool("romanNumerals", false);
    m_slashBass = readBool("slashBass", true);
    m_figuredBass = readBool("figuredBass", true);
    m_replaceExisting = readBool("replaceExisting", false);
    m_onlyWhenChordChanges = readBool("onlyWhenChordChanges", true);
    m_highlight = readBool("highlight", false);
    emit optionsChanged();

    m_scopeTitle.clear();
    m_scopeDetail.clear();
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    if (notation) {
        const ci::Scope sc = notation->interaction()->chordIdentifierScope();
        const QString first = QString::fromStdString(sc.firstStaffName);
        const QString last = QString::fromStdString(sc.lastStaffName);
        const QString staves = sc.staffCount <= 1 ? first
                               : qtrc("notation", "%1 to %2 (%n staves)", nullptr, sc.staffCount).arg(first, last);
        const QString bars = sc.firstBar == sc.lastBar ? qtrc("notation", "bar %1").arg(sc.firstBar)
                             : qtrc("notation", "bars %1–%2").arg(sc.firstBar).arg(sc.lastBar);

        if (sc.listSelection) {
            m_scopeTitle = qtrc("notation", "Selected notes");
            m_scopeDetail = sc.noteCount == 0
                            ? qtrc("notation", "The selection has no notes to analyse. Select notes or a passage, or select nothing to analyse the whole score.")
                            : qtrc("notation", "Only the %n selected note(s) are used, in %1.", nullptr, sc.noteCount).arg(bars);
        } else if (sc.wholeScore) {
            m_scopeTitle = qtrc("notation", "Whole score (nothing is selected)");
            m_scopeDetail = qtrc("notation", "All of %1, %2. Drum staves are skipped.").arg(bars, staves);
        } else {
            m_scopeTitle = qtrc("notation", "Selected passage");
            m_scopeDetail = sc.staffCount == 0
                            ? qtrc("notation", "The selection contains only drum staves, which have no chords to name.")
                            : qtrc("notation", "%1, %2.").arg(bars, staves);
        }
    }
    emit scopeChanged();
    setResult(QString());
}

QString ChordIdentifierModel::scopeTitle() const { return m_scopeTitle; }
QString ChordIdentifierModel::scopeDetail() const { return m_scopeDetail; }

bool ChordIdentifierModel::canRun() const
{
    return m_chordSymbols || m_romanNumerals || m_highlight;
}

void ChordIdentifierModel::setOption(bool& field, bool value, const char* name)
{
    if (field == value) {
        return;
    }
    field = value;
    settings()->setSharedValue(key(name), Val(value));
    emit optionsChanged();
}

bool ChordIdentifierModel::chordSymbols() const { return m_chordSymbols; }
void ChordIdentifierModel::setChordSymbols(bool v) { setOption(m_chordSymbols, v, "chordSymbols"); }
bool ChordIdentifierModel::romanNumerals() const { return m_romanNumerals; }
void ChordIdentifierModel::setRomanNumerals(bool v) { setOption(m_romanNumerals, v, "romanNumerals"); }
bool ChordIdentifierModel::slashBass() const { return m_slashBass; }
void ChordIdentifierModel::setSlashBass(bool v) { setOption(m_slashBass, v, "slashBass"); }
bool ChordIdentifierModel::figuredBass() const { return m_figuredBass; }
void ChordIdentifierModel::setFiguredBass(bool v) { setOption(m_figuredBass, v, "figuredBass"); }
bool ChordIdentifierModel::replaceExisting() const { return m_replaceExisting; }
void ChordIdentifierModel::setReplaceExisting(bool v) { setOption(m_replaceExisting, v, "replaceExisting"); }
bool ChordIdentifierModel::onlyWhenChordChanges() const { return m_onlyWhenChordChanges; }
void ChordIdentifierModel::setOnlyWhenChordChanges(bool v) { setOption(m_onlyWhenChordChanges, v, "onlyWhenChordChanges"); }
bool ChordIdentifierModel::highlight() const { return m_highlight; }
void ChordIdentifierModel::setHighlight(bool v) { setOption(m_highlight, v, "highlight"); }

QVariantList ChordIdentifierModel::highlightLegend() const
{
    auto item = [](const QString& label, const muse::Color& c) {
        QVariantMap m;
        m["label"] = label;
        m["color"] = QColor(c.red(), c.green(), c.blue());
        return QVariant(m);
    };
    using H = ci::HighlightColors;
    return {
        item(qtrc("notation", "Root"), H::root()),
        item(qtrc("notation", "3rd"), H::third()),
        item(qtrc("notation", "5th"), H::fifth()),
        item(qtrc("notation", "7th"), H::seventh()),
        item(qtrc("notation", "9th, 11th, 13th, 6th"), H::extension()),
    };
}

QString ChordIdentifierModel::resultText() const { return m_resultText; }

void ChordIdentifierModel::setResult(const QString& text)
{
    if (m_resultText != text) {
        m_resultText = text;
        emit resultChanged();
    }
}

bool ChordIdentifierModel::run()
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    if (!notation || !canRun()) {
        return false;
    }

    ci::Options o;
    o.chordSymbols = m_chordSymbols;
    o.romanNumerals = m_romanNumerals;
    o.slashBass = m_slashBass;
    o.figuredBass = m_figuredBass;
    o.existing = m_replaceExisting ? ci::ExistingSymbols::Replace : ci::ExistingSymbols::Keep;
    o.onlyWhenChordChanges = m_onlyWhenChordChanges;
    o.highlightChordTones = m_highlight;

    const ci::Report r = notation->interaction()->identifyChords(o);

    QStringList parts;
    parts << qtrc("notation", "Added %n symbol(s)", nullptr, r.added);
    if (r.replaced > 0) {
        parts << qtrc("notation", "replaced %n", nullptr, r.replaced);
    }
    if (r.keptExisting > 0) {
        parts << qtrc("notation", "kept %n already there", nullptr, r.keptExisting);
    }
    if (r.unrecognised > 0) {
        parts << qtrc("notation", "%n place(s) not a recognised chord", nullptr, r.unrecognised);
    }
    if (m_highlight) {
        parts << qtrc("notation", "coloured %n note(s)", nullptr, r.notesColoured);
    }
    setResult(parts.join(" · ") + ". " + qtrc("notation", "Undo with Ctrl+Z (⌘Z on Mac)."));
    return true;
}

void ChordIdentifierModel::removeHighlight()
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    if (!notation) {
        return;
    }
    const ci::Report r = notation->interaction()->removeChordToneHighlight();
    setResult(qtrc("notation", "Removed highlighting from %n note(s). Colours you chose yourself are unchanged.", nullptr,
                   r.coloursRemoved));
}
