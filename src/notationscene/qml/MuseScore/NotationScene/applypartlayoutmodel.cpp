/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */
#include "applypartlayoutmodel.h"

#include <algorithm>

#include <QStringList>
#include <QVariantMap>

#include "translation.h"

#include "engraving/dom/excerpt.h"
#include "engraving/dom/masterscore.h"

#include "notation/inotation.h"
#include "notation/inotationelements.h"
#include "notation/internal/inotationundostack.h"

using namespace mu::notation;
using namespace mu::engraving;
namespace jpl = mu::notation::jubelle;

ApplyPartLayoutModel::ApplyPartLayoutModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

void ApplyPartLayoutModel::load()
{
    m_parts.clear();
    m_sourceIndex = 0;
    m_sourceFixed = false;
    m_resultText.clear();

    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* current = notation && notation->elements() ? notation->elements()->msScore() : nullptr;
    if (current && current->masterScore()) {
        for (Excerpt* ex : current->masterScore()->excerpts()) {
            if (ex->excerptScore()) {
                m_parts.push_back({ ex->name().toQString(), ex->excerptScore(), false });
                // Opened from a part: that part is the source
                if (ex->excerptScore() == current) {
                    m_sourceIndex = int(m_parts.size()) - 1;
                    m_sourceFixed = true;
                }
            }
        }
    }
    emit changed();
}

void ApplyPartLayoutModel::touch()
{
    m_resultText.clear();
    emit changed();
}

Score* ApplyPartLayoutModel::source() const
{
    return m_sourceIndex >= 0 && m_sourceIndex < int(m_parts.size()) ? m_parts[m_sourceIndex].score : nullptr;
}

QVariantList ApplyPartLayoutModel::sources() const
{
    QVariantList list;
    for (const PartEntry& p : m_parts) {
        QVariantMap m;
        m["text"] = p.name;
        list << m;
    }
    return list;
}

int ApplyPartLayoutModel::sourceIndex() const
{
    return m_sourceIndex;
}

void ApplyPartLayoutModel::setSourceIndex(int index)
{
    if (index < 0 || index >= int(m_parts.size()) || index == m_sourceIndex) {
        return;
    }
    m_sourceIndex = index;
    m_parts[index].checked = false;   // a part is never its own destination
    touch();
}

bool ApplyPartLayoutModel::sourceFixed() const
{
    return m_sourceFixed;
}

QVariantList ApplyPartLayoutModel::destinations() const
{
    QVariantList list;
    const Score* src = source();
    for (int i = 0; i < int(m_parts.size()); ++i) {
        if (i == m_sourceIndex) {
            continue;
        }
        const PartEntry& p = m_parts[i];
        QVariantMap m;
        m["row"] = i;
        m["name"] = p.name;
        m["checked"] = p.checked;
        if (p.checked && src) {
            // Preview: what will and won't match, before anything changes
            const jpl::PartLayoutCheck check = jpl::checkPartLayout(src, p.score, m_options);
            QStringList issues;
            for (const muse::String& s : check.issues) {
                issues << s.toQString();
            }
            m["status"] = check.compatible ? muse::qtrc("notation", "✓ Matches") : muse::qtrc("notation", "⚠ Partly");
            m["issues"] = issues.join("\n");
        } else {
            m["status"] = QString();
            m["issues"] = QString();
        }
        list << m;
    }
    return list;
}

void ApplyPartLayoutModel::setDestinationChecked(int row, bool checked)
{
    if (row < 0 || row >= int(m_parts.size()) || row == m_sourceIndex) {
        return;
    }
    m_parts[row].checked = checked;
    touch();
}

void ApplyPartLayoutModel::selectAll()
{
    for (int i = 0; i < int(m_parts.size()); ++i) {
        m_parts[i].checked = i != m_sourceIndex;
    }
    touch();
}

void ApplyPartLayoutModel::clearSelection()
{
    for (PartEntry& p : m_parts) {
        p.checked = false;
    }
    touch();
}

bool ApplyPartLayoutModel::canApply() const
{
    return source() && std::any_of(m_parts.begin(), m_parts.end(), [](const PartEntry& p) { return p.checked; });
}

QString ApplyPartLayoutModel::resultText() const
{
    return m_resultText;
}

bool ApplyPartLayoutModel::apply()
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    if (!notation || !canApply()) {
        return false;
    }
    std::vector<Score*> dests;
    std::vector<QString> names;
    for (int i = 0; i < int(m_parts.size()); ++i) {
        if (m_parts[i].checked && i != m_sourceIndex) {
            dests.push_back(m_parts[i].score);
            names.push_back(m_parts[i].name);
        }
    }

    // All parts share the main score's undo history: one undo for everything
    notation->undoStack()->prepareChanges(muse::TranslatableString("undoableAction", "Apply part layout"));
    const std::vector<jpl::PartLayoutReport> reports = jpl::applyPartLayout(source(), dests, m_options);
    notation->undoStack()->commitChanges();

    QStringList lines;
    for (size_t i = 0; i < reports.size(); ++i) {
        const jpl::PartLayoutReport& r = reports[i];
        QStringList parts;
        parts << muse::qtrc("notation", "%n system break(s)", nullptr, r.systemBreaks)
              << muse::qtrc("notation", "%n page break(s)", nullptr, r.pageBreaks);
        if (r.keepTogether) {
            parts << muse::qtrc("notation", "%n keep-together mark(s)", nullptr, r.keepTogether);
        }
        if (m_options.systemLocks) {
            parts << muse::qtrc("notation", "%n system lock(s)", nullptr, r.systemLocks);
        }
        if (m_options.measureWidths) {
            parts << muse::qtrc("notation", "%n measure width(s)", nullptr, r.measureWidths);
        }
        if (m_options.spacers) {
            parts << (r.spacersSkipped ? muse::qtrc("notation", "spacers skipped (different staves)")
                      : muse::qtrc("notation", "%n spacer(s)", nullptr, r.spacers));
        }
        if (r.pageAndSpacing) {
            parts << muse::qtrc("notation", "page settings ✓");
        }
        if (r.skippedInMMRests) {
            parts << muse::qtrc("notation", "%n skipped inside multi-measure rests", nullptr, r.skippedInMMRests);
        }
        if (r.skippedNoFrame) {
            parts << muse::qtrc("notation", "%n skipped (no matching frame)", nullptr, r.skippedNoFrame);
        }
        lines << names[i] + ": " + parts.join(", ");
    }
    for (PartEntry& p : m_parts) {
        p.checked = false;
    }
    m_resultText = lines.join("\n") + "\n" + muse::qtrc("notation", "No music was changed. Undo with Ctrl+Z (⌘Z on Mac).");
    emit changed();
    return true;
}
