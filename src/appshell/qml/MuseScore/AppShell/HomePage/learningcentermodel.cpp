/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G10: Learning Center. See learningcentermodel.h.

#include "learningcentermodel.h"

#include <algorithm>
#include <map>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QVariantMap>

#include "settings.h"

#include "engraving/dom/instrtemplate.h"
#include "notation/internal/jubelle/instrumentreference.h"

using namespace mu::appshell;
namespace jr = mu::notation::jubelle;

LearningCenterModel::LearningCenterModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void LearningCenterModel::load()
{
    rebuild();
}

void LearningCenterModel::setSearch(const QString& text)
{
    m_search = text;
    rebuild();
}

void LearningCenterModel::setOnlyTransposing(bool on)
{
    m_onlyTransposing = on;
    rebuild();
}

void LearningCenterModel::openShortcuts()
{
    dispatcher()->dispatch("preference-dialog");
}

static const muse::Settings::Key RANGE_SELECTION_KEY("appshell", "jubelle/learningCenter/rangeSelection");

void LearningCenterModel::loadSelection()
{
    if (m_selectionLoaded) {
        return;
    }
    m_selectionLoaded = true;
    const QString text = QString::fromStdString(muse::settings()->value(RANGE_SELECTION_KEY).toString());
    for (const QString& id : text.split(',', Qt::SkipEmptyParts)) {
        m_selection.insert(id.trimmed());
    }
}

void LearningCenterModel::saveSelection()
{
    QStringList ids(m_selection.begin(), m_selection.end());
    ids.sort();
    muse::settings()->setSharedValue(RANGE_SELECTION_KEY, muse::Val(ids.join(",").toStdString()));
}

void LearningCenterModel::loadPresets()
{
    // Ensemble presets from the installed templates: each template's instruments
    if (m_presetsLoaded) {
        return;
    }
    m_presetsLoaded = true;
    if (!projectConfiguration()) {
        return;
    }
    static const QRegularExpression instrumentRx(QStringLiteral("<Instrument id=\"([^\"]+)\""));
    static const QRegularExpression prefixRx(QStringLiteral("^\\d+-"));
    for (const muse::io::path_t& dir : projectConfiguration()->availableTemplateDirs()) {
        QDirIterator it(dir.toQString(), { QStringLiteral("*.mscx") }, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString path = it.next();
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly)) {
                continue;
            }
            const QString text = QString::fromUtf8(f.readAll());
            Preset p;
            auto m = instrumentRx.globalMatch(text);
            while (m.hasNext()) {
                p.ids.insert(m.next().captured(1));
            }
            if (p.ids.size() < 2) {
                continue;   // a solo or blank template is no ensemble
            }
            p.name = QFileInfo(path).completeBaseName().remove(prefixRx).replace('_', ' ');
            if (std::none_of(m_presets.begin(), m_presets.end(), [&p](const Preset& q) { return q.name == p.name; })) {
                m_presets.push_back(p);
            }
        }
    }
    std::sort(m_presets.begin(), m_presets.end(), [](const Preset& a, const Preset& b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
    });
}

QVariantList LearningCenterModel::presets() const
{
    QVariantList out;
    for (const Preset& p : m_presets) {
        QVariantMap m;
        m["name"] = p.name;
        m["count"] = int(p.ids.size());
        out << m;
    }
    return out;
}

void LearningCenterModel::setPreset(int preset)
{
    if (preset == m_preset || preset < -2 || preset >= int(m_presets.size())) {
        return;
    }
    m_preset = preset;
    rebuild();
}

void LearningCenterModel::toggleFamily(const QString& family)
{
    if (m_collapsed.contains(family)) {
        m_collapsed.remove(family);
    } else {
        m_collapsed.insert(family);
    }
    rebuild();
}

void LearningCenterModel::setAllFamiliesCollapsed(bool collapsed)
{
    m_collapsed.clear();
    if (collapsed) {
        for (const QVariant& v : m_instruments) {
            const QVariantMap m = v.toMap();
            if (m.value("isHeader").toBool()) {
                m_collapsed.insert(m.value("family").toString());
            }
        }
    }
    rebuild();
}

void LearningCenterModel::toggleSelected(const QString& instrumentId)
{
    loadSelection();
    if (m_selection.contains(instrumentId)) {
        m_selection.remove(instrumentId);
    } else {
        m_selection.insert(instrumentId);
    }
    saveSelection();
    rebuild();
}

void LearningCenterModel::rebuild()
{
    loadPresets();
    loadSelection();
    m_instruments.clear();
    m_instrumentCount = 0;
    if (!instrumentsRepository()) {
        emit changed();
        return;
    }
    const QString needle = m_search.trimmed();
    QStringList seen;
    // families in the order the instrument data lists them, each with its instruments
    QStringList familyOrder;
    std::map<QString, QVariantList> byFamily;
    for (const mu::engraving::InstrumentTemplate* tmpl : instrumentsRepository()->instrumentTemplates()) {
        jr::InstrumentReference r;
        if (!tmpl || !jr::referenceFor(*tmpl, &r)) {
            continue;
        }
        if (m_onlyTransposing && !r.transposing) {
            continue;
        }
        const QString id = tmpl->id.toQString();
        if (m_preset == -2 && !m_selection.contains(id)) {
            continue;
        }
        if (m_preset >= 0 && !m_presets[size_t(m_preset)].ids.contains(id)) {
            continue;
        }
        const QString name = r.name.toQString();
        const QString family = r.family.toQString();
        if (!needle.isEmpty() && !name.contains(needle, Qt::CaseInsensitive) && !family.contains(needle, Qt::CaseInsensitive)) {
            continue;
        }
        // the same instrument is listed once (the data has variants that differ only in notation traits)
        const QString key = name + "|" + r.writtenRange.toQString() + "|" + r.transposition.toQString();
        if (seen.contains(key)) {
            continue;
        }
        seen << key;
        QVariantMap m;
        m["isHeader"] = false;
        m["id"] = id;
        m["name"] = name;
        m["family"] = family;
        m["written"] = r.writtenRange.toQString();
        m["comfortable"] = r.comfortableRange.toQString();
        m["sounding"] = r.soundingRange.toQString();
        m["transposition"] = r.transposition.toQString();
        m["transposing"] = r.transposing;
        m["selected"] = m_selection.contains(id);
        if (!familyOrder.contains(family)) {
            familyOrder << family;
        }
        byFamily[family] << m;
        ++m_instrumentCount;
    }
    for (const QString& family : familyOrder) {
        const QVariantList& list = byFamily[family];
        // while searching every match is shown, so nothing found is hidden in a folded family
        const bool collapsed = needle.isEmpty() && m_collapsed.contains(family);
        QVariantMap header;
        header["isHeader"] = true;
        header["family"] = family;
        header["title"] = family.isEmpty() ? QStringLiteral("—") : family;
        header["count"] = int(list.size());
        header["collapsed"] = collapsed;
        m_instruments << header;
        if (!collapsed) {
            m_instruments << list;
        }
    }
    emit changed();
}
