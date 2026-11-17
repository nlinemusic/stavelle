/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "passageplaybackmodel.h"

#include <algorithm>

#include <QStringList>
#include <QVariantMap>

#include "translation.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"

#include "notation/inotation.h"
#include "notation/inotationelements.h"
#include "notation/internal/inotationundostack.h"

#include "articulationplaybackmodel.h"

using namespace mu::notation;
using namespace mu::engraving;
using namespace mu::engraving::jubelle;
namespace jap = mu::notation::jubelle;

PassagePlaybackModel::PassagePlaybackModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void PassagePlaybackModel::load()
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* score = notation && notation->elements() ? notation->elements()->msScore() : nullptr;
    m_range = score ? jap::passageFromSelection(score) : jap::PassageRange();
    m_rows = m_range.isValid() ? jap::passageRows(score, m_range) : std::vector<jap::PassageRow>();

    m_title.clear();
    if (m_range.isValid()) {
        const MasterScore* master = score->masterScore();
        const Measure* a = master->tick2measure(Fraction::fromTicks(m_range.startTick));
        const Measure* b = master->tick2measure(Fraction::fromTicks(m_range.endTick - 1));
        QStringList names;
        for (const Part* p : m_range.parts) {
            names << p->partName().toQString();
        }
        const QString barsText = (a && b && a != b) ? muse::qtrc("notation", "bars %1–%2").arg(a->no() + 1).arg(b->no() + 1)
                                 : muse::qtrc("notation", "bar %1").arg(a ? a->no() + 1 : 1);
        m_title = names.join(", ") + " · " + barsText;
    }
    emit changed();
}

QString PassagePlaybackModel::passageTitle() const
{
    return m_title;
}

QVariantList PassagePlaybackModel::rows() const
{
    QVariantList list;
    for (const jap::PassageRow& r : m_rows) {
        if (!m_showAll && !r.inSelection && r.source != jap::ValueSource::Here) {
            continue;
        }
        QVariantMap m = ArticulationPlaybackModel::describeParam(r.param);
        m["value"] = r.value;
        m["isSet"] = r.source == jap::ValueSource::Here;
        switch (r.source) {
        case jap::ValueSource::Here: m["source"] = muse::qtrc("notation", "set here");
            break;
        case jap::ValueSource::OtherPassage: m["source"] = muse::qtrc("notation", "from passage, %1").arg(r.sourceName.toQString());
            break;
        case jap::ValueSource::Instrument: m["source"] = muse::qtrc("notation", "from %1").arg(r.sourceName.toQString());
            break;
        case jap::ValueSource::WholeScore: m["source"] = muse::qtrc("notation", "from whole score");
            break;
        case jap::ValueSource::MuseScore: m["source"] = muse::qtrc("notation", "default");
            break;
        }
        list << m;
    }
    return list;
}

bool PassagePlaybackModel::showAll() const
{
    return m_showAll;
}

void PassagePlaybackModel::setShowAll(bool v)
{
    m_showAll = v;
    emit changed();
}

bool PassagePlaybackModel::isValid() const
{
    return m_range.isValid();
}

bool PassagePlaybackModel::hasOverrides() const
{
    return std::any_of(m_rows.begin(), m_rows.end(), [](const jap::PassageRow& r) { return r.source == jap::ValueSource::Here; });
}

void PassagePlaybackModel::change(const QString& key, std::optional<int> value)
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* score = notation && notation->elements() ? notation->elements()->msScore() : nullptr;
    if (!score || !m_range.isValid()) {
        return;
    }
    for (const ArtParamInfo& info : artParams()) {
        if (key == QString::fromLatin1(info.key)) {
            notation->undoStack()->prepareChanges(muse::TranslatableString("undoableAction", "Change passage articulation playback"));
            jap::setPassageValue(score, m_range, info.param, value);
            notation->undoStack()->commitChanges();
            break;
        }
    }
    m_rows = jap::passageRows(score, m_range);
    emit changed();
}

void PassagePlaybackModel::setValue(const QString& key, int value)
{
    change(key, value);
}

void PassagePlaybackModel::clearValue(const QString& key)
{
    change(key, std::nullopt);
}

void PassagePlaybackModel::clearAll()
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* score = notation && notation->elements() ? notation->elements()->msScore() : nullptr;
    if (!score || !m_range.isValid()) {
        return;
    }
    notation->undoStack()->prepareChanges(muse::TranslatableString("undoableAction", "Clear passage articulation playback"));
    jap::clearPassage(score, m_range);
    notation->undoStack()->commitChanges();
    m_rows = jap::passageRows(score, m_range);
    emit changed();
}
