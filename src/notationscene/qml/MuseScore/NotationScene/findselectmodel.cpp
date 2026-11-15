/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G5: Select › Find and Select… See findselectmodel.h.

#include "findselectmodel.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QVariantMap>

#include "translation.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/part.h"
#include "engraving/dom/staff.h"

#include "notation/inotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationinteraction.h"

using namespace mu::notation;
using namespace mu::engraving;
namespace js = mu::notation::jubelle;

namespace {
struct KindOption {
    js::FindKind kind;
    const char* id;
    const char* title;
};

const KindOption KINDS[] = {
    { js::FindKind::Notes, "notes", QT_TRANSLATE_NOOP("notation/findselect", "Notes") },
    { js::FindKind::Rests, "rests", QT_TRANSLATE_NOOP("notation/findselect", "Rests") },
    { js::FindKind::Articulations, "articulations", QT_TRANSLATE_NOOP("notation/findselect", "Articulations") },
    { js::FindKind::Dynamics, "dynamics", QT_TRANSLATE_NOOP("notation/findselect", "Dynamics") },
    { js::FindKind::Hairpins, "hairpins", QT_TRANSLATE_NOOP("notation/findselect", "Hairpins") },
    { js::FindKind::Slurs, "slurs", QT_TRANSLATE_NOOP("notation/findselect", "Slurs") },
    { js::FindKind::StaffText, "stafftext", QT_TRANSLATE_NOOP("notation/findselect", "Staff text and expressions") },
    { js::FindKind::ChordSymbols, "chordsymbols", QT_TRANSLATE_NOOP("notation/findselect", "Chord symbols") },
    { js::FindKind::Lyrics, "lyrics", QT_TRANSLATE_NOOP("notation/findselect", "Lyrics") },
};

struct ArticulationOption {
    const char* id;
    const char* title;
    std::set<SymId> syms;
};

const std::vector<ArticulationOption>& articulations()
{
    static const std::vector<ArticulationOption> list {
        { "", QT_TRANSLATE_NOOP("notation/findselect", "Any or none"), {} },
        { "staccato", QT_TRANSLATE_NOOP("notation/findselect", "Staccato"),
          { SymId::articStaccatoAbove, SymId::articStaccatoBelow, SymId::articAccentStaccatoAbove, SymId::articAccentStaccatoBelow,
            SymId::articMarcatoStaccatoAbove, SymId::articMarcatoStaccatoBelow, SymId::articTenutoStaccatoAbove,
            SymId::articTenutoStaccatoBelow } },
        { "staccatissimo", QT_TRANSLATE_NOOP("notation/findselect", "Staccatissimo"),
          { SymId::articStaccatissimoAbove, SymId::articStaccatissimoBelow } },
        { "accent", QT_TRANSLATE_NOOP("notation/findselect", "Accent"),
          { SymId::articAccentAbove, SymId::articAccentBelow, SymId::articAccentStaccatoAbove, SymId::articAccentStaccatoBelow } },
        { "marcato", QT_TRANSLATE_NOOP("notation/findselect", "Marcato"),
          { SymId::articMarcatoAbove, SymId::articMarcatoBelow, SymId::articMarcatoStaccatoAbove, SymId::articMarcatoStaccatoBelow } },
        { "tenuto", QT_TRANSLATE_NOOP("notation/findselect", "Tenuto"),
          { SymId::articTenutoAbove, SymId::articTenutoBelow, SymId::articTenutoStaccatoAbove, SymId::articTenutoStaccatoBelow } },
    };
    return list;
}

const ArticulationOption* articulationById(const QString& id)
{
    for (const ArticulationOption& a : articulations()) {
        if (id == a.id) {
            return &a;
        }
    }
    return nullptr;
}

struct LengthOption {
    Fraction value;
    const char* title;
};

const std::vector<LengthOption>& lengths()
{
    static const std::vector<LengthOption> list {
        { Fraction(1, 64), QT_TRANSLATE_NOOP("notation/findselect", "64th") },
        { Fraction(1, 32), QT_TRANSLATE_NOOP("notation/findselect", "32nd") },
        { Fraction(1, 16), QT_TRANSLATE_NOOP("notation/findselect", "16th") },
        { Fraction(1, 8), QT_TRANSLATE_NOOP("notation/findselect", "Eighth") },
        { Fraction(1, 4), QT_TRANSLATE_NOOP("notation/findselect", "Quarter") },
        { Fraction(1, 2), QT_TRANSLATE_NOOP("notation/findselect", "Half") },
        { Fraction(1, 1), QT_TRANSLATE_NOOP("notation/findselect", "Whole") },
        { Fraction(2, 1), QT_TRANSLATE_NOOP("notation/findselect", "Breve") },
    };
    return list;
}

QString pitchName(int p)
{
    static const char* NAMES[] = { "C", "C♯", "D", "E♭", "E", "F", "F♯", "G", "A♭", "A", "B♭", "B" };
    return QString::fromUtf8(NAMES[((p % 12) + 12) % 12]) + QString::number(p / 12 - 1);
}
}

FindSelectModel::FindSelectModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

Score* FindSelectModel::score() const
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    return notation && notation->elements() ? notation->elements()->msScore() : nullptr;
}

void FindSelectModel::load()
{
    m_toBar = std::max(1, barCount());
    update();
}

int FindSelectModel::barCount() const
{
    const Score* s = score();
    int n = 0;
    for (const Measure* m = s ? s->firstMeasure() : nullptr; m; m = m->nextMeasure()) {
        ++n;
    }
    return n;
}

QVariantList FindSelectModel::kindOptions() const
{
    QVariantList out;
    for (const KindOption& k : KINDS) {
        QVariantMap m;
        m["id"] = k.id;
        m["title"] = muse::qtrc("notation/findselect", k.title);
        out << m;
    }
    return out;
}

QVariantList FindSelectModel::selectedKinds() const
{
    QVariantList out;
    for (const KindOption& k : KINDS) {
        if (m_query.kinds.count(k.kind)) {
            out << QString(k.id);
        }
    }
    return out;
}

QVariantList FindSelectModel::staffOptions() const
{
    QVariantList out;
    const Score* s = score();
    if (!s) {
        return out;
    }
    for (const Staff* st : s->staves()) {
        const Part* part = st->part();
        QString name = part ? part->partName().toQString() : QString();
        if (part && part->nstaves() > 1) {
            name += " " + QString::number(int(st->idx() - part->staff(0)->idx()) + 1);
        }
        QVariantMap m;
        m["staff"] = int(st->idx());
        m["title"] = name;
        out << m;
    }
    return out;
}

QVariantList FindSelectModel::selectedStaves() const
{
    QVariantList out;
    for (size_t s : m_query.staves) {
        out << int(s);
    }
    return out;
}

QVariantList FindSelectModel::articulationOptions() const
{
    QVariantList out;
    for (const ArticulationOption& a : articulations()) {
        QVariantMap m;
        m["value"] = QString(a.id);
        m["text"] = muse::qtrc("notation/findselect", a.title);
        out << m;
    }
    return out;
}

QVariantList FindSelectModel::lengthOptions() const
{
    QVariantList out;
    for (size_t i = 0; i < lengths().size(); ++i) {
        QVariantMap m;
        m["value"] = int(i);
        m["text"] = muse::qtrc("notation/findselect", lengths()[i].title);
        out << m;
    }
    return out;
}

QString FindSelectModel::lowestPitchName() const
{
    return pitchName(m_lowest);
}

QString FindSelectModel::highestPitchName() const
{
    return pitchName(m_highest);
}

void FindSelectModel::setKind(const QString& kind, bool on)
{
    for (const KindOption& k : KINDS) {
        if (kind == k.id) {
            on ? (void)m_query.kinds.insert(k.kind) : (void)m_query.kinds.erase(k.kind);
        }
    }
    update();
}

void FindSelectModel::setStaff(int staffIdx, bool on)
{
    if (staffIdx < 0) {
        return;
    }
    on ? (void)m_query.staves.insert(size_t(staffIdx)) : (void)m_query.staves.erase(size_t(staffIdx));
    update();
}

void FindSelectModel::setAllStaves()
{
    m_query.staves.clear();
    update();
}

void FindSelectModel::setVoice(int voice, bool on)
{
    if (voice < 0 || voice > 3) {
        return;
    }
    m_query.voices = on ? (m_query.voices | (1u << voice)) : (m_query.voices & ~(1u << voice));
    update();
}

void FindSelectModel::setArticulation(const QString& name)
{
    m_articulation = articulationById(name) ? name : QString();
    update();
}

void FindSelectModel::setUsePitch(bool on) { m_usePitch = on; update(); }
void FindSelectModel::setLowestPitch(int p) { m_lowest = std::clamp(p, 0, 127); update(); }
void FindSelectModel::setHighestPitch(int p) { m_highest = std::clamp(p, 0, 127); update(); }
void FindSelectModel::setWrittenPitch(bool on) { m_query.writtenPitch = on; update(); }
void FindSelectModel::setUseLength(bool on) { m_useLength = on; update(); }
void FindSelectModel::setShortestIndex(int i) { m_shortestIndex = std::clamp(i, 0, int(lengths().size()) - 1); update(); }
void FindSelectModel::setLongestIndex(int i) { m_longestIndex = std::clamp(i, 0, int(lengths().size()) - 1); update(); }
void FindSelectModel::setSkipTied(bool on) { m_query.skipTiedContinuations = on; update(); }
void FindSelectModel::setRangeMode(int mode) { m_rangeMode = std::clamp(mode, 0, 2); update(); }
void FindSelectModel::setFromBar(int bar) { m_fromBar = std::max(1, bar); update(); }
void FindSelectModel::setToBar(int bar) { m_toBar = std::max(1, bar); update(); }

js::FindQuery FindSelectModel::effectiveQuery() const
{
    js::FindQuery q = m_query;
    q.lowestPitch.reset();
    q.highestPitch.reset();
    if (m_usePitch) {
        q.lowestPitch = std::min(m_lowest, m_highest);
        q.highestPitch = std::max(m_lowest, m_highest);
    }
    q.shortest.reset();
    q.longest.reset();
    if (m_useLength) {
        q.shortest = lengths()[size_t(std::min(m_shortestIndex, m_longestIndex))].value;
        q.longest = lengths()[size_t(std::max(m_shortestIndex, m_longestIndex))].value;
    }
    q.withArticulation.clear();
    if (const ArticulationOption* a = articulationById(m_articulation)) {
        q.withArticulation = a->syms;
    }
    q.fromBar.reset();
    q.toBar.reset();
    q.inSelection = m_rangeMode == 2;
    if (m_rangeMode == 1) {
        q.fromBar = std::min(m_fromBar, m_toBar);
        q.toBar = std::max(m_fromBar, m_toBar);
    }
    return q;
}

void FindSelectModel::update()
{
    m_matchCount = int(js::findItems(score(), effectiveQuery()).size());
    emit changed();
}

int FindSelectModel::selectMatches()
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    if (!notation) {
        return 0;
    }
    const std::vector<EngravingItem*> items = js::findItems(score(), effectiveQuery());
    notation->interaction()->clearSelection();
    if (!items.empty()) {
        notation->interaction()->select(items, SelectType::ADD);
    }
    return int(items.size());
}

// --- saved filters: Jubelle's settings folder, never the score ---

QString FindSelectModel::filtersPath() const
{
    return globalConfiguration() ? globalConfiguration()->userAppDataPath().toQString() + "/jubelle_find_filters.json" : QString();
}

QVariantMap FindSelectModel::readFilters() const
{
    QFile f(filtersPath());
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(f.readAll()).object().toVariantMap();
}

void FindSelectModel::writeFilters(const QVariantMap& filters)
{
    QFile f(filtersPath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(QJsonObject::fromVariantMap(filters)).toJson());
    }
    emit savedFiltersChanged();
}

QStringList FindSelectModel::savedFilters() const
{
    return readFilters().keys();
}

void FindSelectModel::saveFilter(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) {
        return;
    }
    QVariantMap filters = readFilters();
    QString text = js::toText(effectiveQuery()).toQString();
    if (!m_articulation.isEmpty()) {
        text += "articulationchoice=" + m_articulation + "\n";
    }
    filters[n] = text;
    writeFilters(filters);
}

void FindSelectModel::fromQuery(const js::FindQuery& q)
{
    m_query = q;
    m_usePitch = q.lowestPitch.has_value() || q.highestPitch.has_value();
    if (q.lowestPitch) {
        m_lowest = *q.lowestPitch;
    }
    if (q.highestPitch) {
        m_highest = *q.highestPitch;
    }
    m_useLength = q.shortest.has_value() || q.longest.has_value();
    for (size_t i = 0; i < lengths().size(); ++i) {
        if (q.shortest && lengths()[i].value == *q.shortest) {
            m_shortestIndex = int(i);
        }
        if (q.longest && lengths()[i].value == *q.longest) {
            m_longestIndex = int(i);
        }
    }
    m_rangeMode = q.inSelection ? 2 : (q.fromBar || q.toBar) ? 1 : 0;
    if (q.fromBar) {
        m_fromBar = *q.fromBar;
    }
    if (q.toBar) {
        m_toBar = *q.toBar;
    }
}

void FindSelectModel::loadFilter(const QString& name)
{
    const QVariantMap filters = readFilters();
    if (!filters.contains(name)) {
        return;
    }
    const QString text = filters.value(name).toString();
    fromQuery(js::fromText(muse::String::fromQString(text)));
    m_articulation.clear();
    for (const QString& line : text.split('\n')) {
        if (line.startsWith("articulationchoice=")) {
            m_articulation = line.mid(19);
        }
    }
    update();
}

void FindSelectModel::deleteFilter(const QString& name)
{
    QVariantMap filters = readFilters();
    filters.remove(name);
    writeFilters(filters);
}
