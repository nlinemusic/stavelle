/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */
#include "jubellecontextsettingsmodel.h"

#include <cmath>

#include "audio/engine/internal/synthesizers/fluidsynth/jubelledynamics.h"
#include "engraving/dom/dynamic.h"
#include "engraving/dom/instrtemplate.h"
#include "engraving/dom/instrument.h"
#include "engraving/dom/layoutbreak.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"
#include "engraving/dom/stafftext.h"
#include "engraving/dom/systemtext.h"
#include "engraving/dom/tempotext.h"
#include "engraving/playback/jubelle/swingfeel.h"
#include "notation/inotationelements.h"
#include "notation/inotationinteraction.h"
#include "notation/internal/jubelle/pageturns.h"
#include "notation/internal/jubelle/swingmarking.h"
#include "notation/internal/jubelle/tempotap.h"
#include "notation/internal/jubelle/timelinemarkers.h"

#include "translation.h"

using namespace mu::inspector;
using namespace mu::engraving;
namespace jctx = ::mu::notation::jubelle;

namespace {
bool isSwingMarking(const EngravingItem* e)
{
    if (!e || !e->isSystemText() || !e->score()) {
        return false;
    }
    const mu::engraving::jubelle::SwingSettings settings = mu::engraving::jubelle::SwingSettings::read(e->score());
    return jctx::findSwingMarking(e->score(), settings) == e;
}

QString kindOf(const EngravingItem* e)
{
    if (!e) {
        return QString();
    }
    if (e->isTempoText() && jctx::isRecordedTempoPoint(e->score(), toTempoText(e))) {
        return "tempo";
    }
    if (jctx::isTimelineMarker(e)) {
        return "marker";
    }
    if (jctx::isJubelleTurnBreak(e)) {
        return "pagebreak";
    }
    if (e->isDynamic()) {
        return "dynamic";
    }
    if (isSwingMarking(e)) {
        return "swing";
    }
    return QString();
}

//! 0 = ppp … 7 = fff, or -1 for other dynamics (sfz, fp …)
int dynamicStep(DynamicType t)
{
    switch (t) {
    case DynamicType::PPP: return 0;
    case DynamicType::PP: return 1;
    case DynamicType::P: return 2;
    case DynamicType::MP: return 3;
    case DynamicType::MF: return 4;
    case DynamicType::F: return 5;
    case DynamicType::FF: return 6;
    case DynamicType::FFF: return 7;
    default: break;
    }
    return -1;
}
}

JubelleContextSettingsModel::JubelleContextSettingsModel(QObject* parent, const muse::modularity::ContextPtr& iocCtx,
                                                         IElementRepositoryService* repository)
    : AbstractInspectorModel(parent, iocCtx, repository)
{
    setTitle(muse::qtrc("inspector", "Jubelle"));
    setSectionType(InspectorSectionType::SECTION_JUBELLE);
}

bool JubelleContextSettingsModel::appliesTo(const QList<EngravingItem*>& selection)
{
    for (const EngravingItem* e : selection) {
        if (!kindOf(e).isEmpty()) {
            return true;
        }
    }
    return false;
}

Score* JubelleContextSettingsModel::score() const
{
    const notation::INotationPtr notation = currentNotation();
    return notation && notation->elements() ? notation->elements()->msScore() : nullptr;
}

void JubelleContextSettingsModel::requestElements()
{
    m_elementList.clear();
    if (selection()) {
        for (EngravingItem* e : selection()->elements()) {
            m_elementList << e;
        }
    }
}

void JubelleContextSettingsModel::loadProperties()
{
    update();
}

void JubelleContextSettingsModel::onNotationChanged(const PropertyIdSet&, const StyleIdSet&)
{
    update();
}

void JubelleContextSettingsModel::update()
{
    m_item = nullptr;
    m_kind.clear();
    m_heading.clear();
    m_detail.clear();
    for (EngravingItem* e : m_elementList) {
        const QString k = kindOf(e);
        if (!k.isEmpty()) {
            m_item = e;
            m_kind = k;
            break;
        }
    }
    if (m_kind == "tempo") {
        m_tempoBpm = std::round(toTempoText(m_item)->tempo().toBPM().val * 10.0) / 10.0;
        m_heading = muse::qtrc("inspector", "Recorded tempo point");
        m_detail = muse::qtrc("inspector", "Recorded by tapping (Playback › Record tempo). MuseScore plays it the same way.");
    } else if (m_kind == "marker") {
        m_markerName = jctx::markerName(m_item).toQString();
        m_heading = muse::qtrc("inspector", "Timeline marker");
        m_detail = muse::qtrc("inspector", "A named place for rehearsal and navigation; it is never printed.");
    } else if (m_kind == "pagebreak") {
        m_heading = muse::qtrc("inspector", "Page break placed by Page turns");
        m_detail = muse::qtrc("inspector", "Page turns put this break here so the player has time to turn. "
                                           "It moves if the turns are worked out again, unless you keep it as your own.");
    } else if (m_kind == "dynamic") {
        const Dynamic* d = toDynamic(m_item);
        const int step = dynamicStep(d->dynamicType());
        m_heading = muse::qtrc("inspector", "How this dynamic plays");
        if (step < 0) {
            m_detail = muse::qtrc("inspector", "Accents and changes such as sfz or fp are played by MuseScore's own rules.");
        } else {
            const Part* part = d->part();
            const Instrument* inst = part ? part->instrument(d->tick()) : nullptr;
            const InstrumentTemplate* t = inst ? searchTemplate(inst->id()) : nullptr;
            const String group = t ? t->groupId : String();
            const int category = group == u"keyboards" ? 0 : (group == u"strings" || group == u"plucked-strings") ? 1
                                 : group == u"percussion" || group == u"pitched-percussion" ? 3 : group == u"vocals" ? 4 : 2;
            const auto family = muse::audio::jubelle::familyFor(category, inst ? inst->id().toLower().toStdString() : std::string());
            const double db = muse::audio::jubelle::dynamicDb(step, family);
            const QString signedDb = (db > 0 ? "+" : "") + QString::number(db, 'f', 1);
            m_detail = step == 4 ? muse::qtrc("inspector", "mf is the reference level (0 dB).")
                       : muse::qtrc("inspector", "%1 dB from mf for %2, in built-in sounds and VST3 instruments alike.")
                       .arg(signedDb, part ? part->partName().toQString() : muse::qtrc("inspector", "this instrument"));
        }
    } else if (m_kind == "swing") {
        const mu::engraving::jubelle::SwingSettings settings = mu::engraving::jubelle::SwingSettings::read(m_item->score());
        const auto* feel = settings.feel(settings.scoreFeel);
        m_heading = muse::qtrc("inspector", "Swing marking");
        m_detail = feel ? muse::qtrc("inspector", "Playback swings with the “%1” feel. Swing… changes it for the score or for single instruments.")
                   .arg(feel->name.toQString())
                   : muse::qtrc("inspector", "Swing… chooses the feel for the score or for single instruments.");
    }
    emit changed();
}

void JubelleContextSettingsModel::setTempoBpm(double bpm)
{
    if (m_kind != "tempo" || !score()) {
        return;
    }
    beginCommand(muse::TranslatableString("undoableAction", "Change recorded tempo"));
    jctx::setRecordedPointTempo(score(), toTempoText(m_item), bpm);
    endCommand();
    update();
}

void JubelleContextSettingsModel::setMarkerName(const QString& name)
{
    if (m_kind != "marker" || !score() || name.trimmed().isEmpty()) {
        return;
    }
    beginCommand(muse::TranslatableString("undoableAction", "Rename timeline marker"));
    jctx::renameTimelineMarker(score(), m_item, muse::String::fromQString(name.trimmed()));
    endCommand();
    update();
}

void JubelleContextSettingsModel::playFromHere()
{
    if (!m_item || !currentNotation()) {
        return;
    }
    currentNotation()->interaction()->select({ m_item }, SelectType::SINGLE);
    dispatcher()->dispatch("play");
}

void JubelleContextSettingsModel::removeItem()
{
    if (!m_item || !score()) {
        return;
    }
    beginCommand(muse::TranslatableString("undoableAction", "Remove"));
    if (m_kind == "marker") {
        jctx::removeTimelineMarker(score(), m_item);
    } else {
        score()->undoRemoveElement(m_item);
    }
    endCommand();
    m_item = nullptr;
    m_elementList.clear();
    update();
}

void JubelleContextSettingsModel::keepAsOwnBreak()
{
    if (m_kind != "pagebreak" || !score()) {
        return;
    }
    beginCommand(muse::TranslatableString("undoableAction", "Keep page break"));
    jctx::keepTurnBreakAsOwn(score(), m_item);
    endCommand();
    update();
}

void JubelleContextSettingsModel::openSwing()
{
    dispatcher()->dispatch("jubelle-swing");
}
