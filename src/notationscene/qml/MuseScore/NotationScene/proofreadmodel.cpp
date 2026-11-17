/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G5: the Proofread panel. See proofreadmodel.h.

#include "proofreadmodel.h"

#include <QVariantMap>

#include "translation.h"

#include "engraving/dom/measure.h"
#include "engraving/dom/score.h"

#include "notation/inotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationinteraction.h"
#include "notation/internal/inotationundostack.h"

using namespace mu::notation;
using namespace mu::engraving;
namespace jp = mu::notation::jubelle;

ProofreadModel::ProofreadModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

Score* ProofreadModel::score() const
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    return notation && notation->elements() ? notation->elements()->msScore() : nullptr;
}

bool ProofreadModel::hasScore() const
{
    return score() != nullptr;
}

void ProofreadModel::load()
{
    if (globalContext()) {
        globalContext()->currentNotationChanged().onNotify(this, [this]() {
            m_findings.clear();
            m_dismissed.clear();
            check();
        });
    }
    check();
}

void ProofreadModel::check()
{
    m_findings = jp::proofread(score(), m_options);
    m_intentional = jp::intentionalKeys(score());
    m_counts[0] = m_counts[1] = m_counts[2] = 0;
    for (const jp::Finding& f : m_findings) {
        if (!m_dismissed.count(f.key)) {
            m_counts[int(f.severity)]++;
        }
    }
    emit changed();
}

QVariantList ProofreadModel::findings() const
{
    QVariantList out;
    for (size_t i = 0; i < m_findings.size(); ++i) {
        const jp::Finding& f = m_findings[i];
        if (m_dismissed.count(f.key)) {
            continue;
        }
        QVariantMap m;
        m["index"] = int(i);
        m["severity"] = int(f.severity);
        m["category"] = int(f.category);
        m["categoryTitle"] = jp::categoryTitle(f.category).toQString();
        m["message"] = f.message.toQString();
        m["bar"] = f.bar;
        m["canGo"] = f.item != nullptr || f.bar > 0;
        m["intentional"] = m_intentional.count(f.key) > 0;
        out << m;
    }
    return out;
}

QVariantList ProofreadModel::categories() const
{
    QVariantList out;
    for (int c = int(jp::CheckCategory::Range); c <= int(jp::CheckCategory::Preparation); ++c) {
        QVariantMap m;
        m["category"] = c;
        m["title"] = jp::categoryTitle(jp::CheckCategory(c)).toQString();
        m["on"] = m_options.categories.count(jp::CheckCategory(c)) > 0;
        out << m;
    }
    return out;
}

void ProofreadModel::setCategory(int category, bool on)
{
    const jp::CheckCategory c = jp::CheckCategory(category);
    on ? (void)m_options.categories.insert(c) : (void)m_options.categories.erase(c);
    check();
}

void ProofreadModel::setShowIntentional(bool on)
{
    m_options.includeIntentional = on;
    check();
}

void ProofreadModel::goTo(int index)
{
    if (index < 0 || index >= int(m_findings.size())) {
        return;
    }
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    if (!notation) {
        return;
    }
    const jp::Finding& f = m_findings[size_t(index)];
    EngravingItem* item = f.item;
    if (!item) {
        // go to the bar: select its first item on that staff
        if (Score* s = score()) {
            if (Measure* m = s->tick2measure(f.tick)) {
                item = m;
            }
        }
    }
    if (!item) {
        return;
    }
    notation->interaction()->select({ item }, SelectType::SINGLE, f.staffIdx);
    notation->interaction()->showItem(item, int(f.staffIdx));
}

void ProofreadModel::dismiss(int index)
{
    if (index >= 0 && index < int(m_findings.size())) {
        m_dismissed.insert(m_findings[size_t(index)].key);
        check();
    }
}

void ProofreadModel::setIntentional(int index, bool intentional)
{
    if (index < 0 || index >= int(m_findings.size())) {
        return;
    }
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    if (!notation) {
        return;
    }
    notation->undoStack()->prepareChanges(muse::TranslatableString("undoableAction", "Mark proofreading finding as intentional"));
    jp::setIntentional(score(), m_findings[size_t(index)].key, intentional);
    notation->undoStack()->commitChanges();
    check();
}
