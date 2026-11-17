/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G9: File › Export drum stems… See drumstemsmodel.h.

#include "drumstemsmodel.h"

#include <algorithm>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVariantMap>

#include "translation.h"
#include "io/filestream.h"

#include "engraving/dom/masterscore.h"
#include "engraving/dom/part.h"
#include "engraving/dom/score.h"

#include "project/inotationproject.h"

#include "notation/imasternotation.h"
#include "notation/inotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationsolomutestate.h"
#include "notation/internal/inotationundostack.h"

using namespace mu::project;
using namespace mu::notation;
using namespace mu::engraving;
namespace jd = mu::notation::jubelle;

namespace {
const QStringList FORMATS { "wav", "flac", "mp3", "ogg" };
}

DrumStemsModel::DrumStemsModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

Score* DrumStemsModel::score() const
{
    IMasterNotationPtr master = globalContext() ? globalContext()->currentMasterNotation() : nullptr;
    INotationPtr notation = master ? master->notation() : nullptr;
    return notation && notation->elements() ? notation->elements()->msScore() : nullptr;
}

Part* DrumStemsModel::kit() const
{
    return m_kitIndex >= 0 && m_kitIndex < int(m_kits.size()) ? m_kits[size_t(m_kitIndex)] : nullptr;
}

void DrumStemsModel::load()
{
    m_kits = jd::drumParts(score());
    m_kitIndex = 0;
    if (m_folder.isEmpty()) {
        if (INotationProjectPtr project = globalContext() ? globalContext()->currentProject() : nullptr) {
            m_folder = QFileInfo(project->path().toQString()).absolutePath();
        }
    }
    readKit();
}

void DrumStemsModel::readKit()
{
    m_components = jd::drumComponents(score(), kit());
    m_groups = jd::suggestedGroups(m_components);
    m_status.clear();
    emit changed();
}

QVariantList DrumStemsModel::kits() const
{
    QVariantList out;
    for (size_t i = 0; i < m_kits.size(); ++i) {
        QVariantMap m;
        m["value"] = int(i);
        m["text"] = m_kits[i]->partName().toQString();
        out << m;
    }
    return out;
}

int DrumStemsModel::groupOf(int componentId) const
{
    for (size_t g = 0; g < m_groups.size(); ++g) {
        const std::vector<int>& ids = m_groups[g].componentIds;
        if (std::find(ids.begin(), ids.end(), componentId) != ids.end()) {
            return int(g);
        }
    }
    return -1;
}

QVariantList DrumStemsModel::components() const
{
    QVariantList out;
    for (const jd::DrumComponent& c : m_components) {
        QVariantMap m;
        m["id"] = c.id;
        m["name"] = c.name.toQString();
        m["notes"] = muse::qtrc("project/drumstems", "%n note(s)", nullptr, c.notes);
        m["group"] = groupOf(c.id);
        out << m;
    }
    return out;
}

QVariantList DrumStemsModel::groups() const
{
    QVariantList out;
    for (size_t g = 0; g < m_groups.size(); ++g) {
        QStringList names;
        for (const jd::DrumComponent& c : m_components) {
            if (groupOf(c.id) == int(g)) {
                names << c.name.toQString();
            }
        }
        QVariantMap m;
        m["index"] = int(g);
        m["name"] = m_groups[g].name.toQString();
        m["contents"] = names.isEmpty() ? muse::qtrc("project/drumstems", "empty: nothing will be exported for it") : names.join(", ");
        out << m;
    }
    return out;
}

QVariantList DrumStemsModel::groupChoices() const
{
    QVariantList out;
    QVariantMap none;
    none["value"] = -1;
    none["text"] = muse::qtrc("project/drumstems", "No group");
    out << none;
    for (size_t g = 0; g < m_groups.size(); ++g) {
        QVariantMap m;
        m["value"] = int(g);
        m["text"] = m_groups[g].name.toQString();
        out << m;
    }
    return out;
}

QVariantList DrumStemsModel::formats() const
{
    QVariantList out;
    for (int i = 0; i < FORMATS.size(); ++i) {
        QVariantMap m;
        m["value"] = i;
        m["text"] = FORMATS[i].toUpper();
        out << m;
    }
    return out;
}

void DrumStemsModel::setKitIndex(int index)
{
    m_kitIndex = std::clamp(index, 0, std::max(0, int(m_kits.size()) - 1));
    readKit();
}

void DrumStemsModel::setExportEach(bool on)
{
    m_exportEach = on;
    emit changed();
}

void DrumStemsModel::setExportGroups(bool on)
{
    m_exportGroups = on;
    emit changed();
}

void DrumStemsModel::setFormatIndex(int index)
{
    m_formatIndex = std::clamp(index, 0, int(FORMATS.size()) - 1);
    emit changed();
}

void DrumStemsModel::chooseFolder()
{
    const muse::io::path_t dir = interactive()->selectDirectory(muse::trc("project/drumstems", "Folder for the drum stems"),
                                                                muse::io::path_t(m_folder));
    if (!dir.empty()) {
        m_folder = dir.toQString();
        emit changed();
    }
}

void DrumStemsModel::addGroup(const QString& name)
{
    const QString n = name.trimmed();
    if (n.isEmpty()) {
        return;
    }
    m_groups.push_back({ muse::String::fromQString(n), {} });
    emit changed();
}

void DrumStemsModel::renameGroup(int index, const QString& name)
{
    const QString n = name.trimmed();
    if (index < 0 || index >= int(m_groups.size()) || n.isEmpty()) {
        return;
    }
    m_groups[size_t(index)].name = muse::String::fromQString(n);
    emit changed();
}

void DrumStemsModel::removeGroup(int index)
{
    if (index < 0 || index >= int(m_groups.size())) {
        return;
    }
    m_groups.erase(m_groups.begin() + index);
    emit changed();
}

void DrumStemsModel::moveToGroup(int componentId, int groupIndex)
{
    for (jd::StemGroup& g : m_groups) {
        g.componentIds.erase(std::remove(g.componentIds.begin(), g.componentIds.end(), componentId), g.componentIds.end());
    }
    if (groupIndex >= 0 && groupIndex < int(m_groups.size())) {
        m_groups[size_t(groupIndex)].componentIds.push_back(componentId);
    }
    emit changed();
}

void DrumStemsModel::resetGroups()
{
    m_groups = jd::suggestedGroups(m_components);
    emit changed();
}

// ---- presets: a small text file in Jubelle's data folder ----

QString DrumStemsModel::presetsPath() const
{
    return globalConfiguration()->userAppDataPath().toQString() + "/jubelle_drum_stem_presets.txt";
}

std::map<QString, QString> DrumStemsModel::readPresets() const
{
    std::map<QString, QString> out;
    QFile f(presetsPath());
    if (!f.open(QIODevice::ReadOnly)) {
        return out;
    }
    QString current;
    for (const QString& line : QString::fromUtf8(f.readAll()).split('\n')) {
        if (line.startsWith("[") && line.trimmed().endsWith("]")) {
            current = line.trimmed().mid(1, line.trimmed().size() - 2);
            out[current] = QString();
        } else if (!current.isEmpty() && !line.trimmed().isEmpty()) {
            out[current] += line + "\n";
        }
    }
    return out;
}

void DrumStemsModel::writePresets(const std::map<QString, QString>& presets) const
{
    QDir().mkpath(QFileInfo(presetsPath()).absolutePath());
    QFile f(presetsPath());
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return;
    }
    for (const auto& [name, text] : presets) {
        f.write(("[" + name + "]\n" + text).toUtf8());
    }
}

QStringList DrumStemsModel::presets() const
{
    QStringList out;
    for (const auto& [name, text] : readPresets()) {
        out << name;
    }
    return out;
}

void DrumStemsModel::savePreset(const QString& name)
{
    QString n = name.trimmed();
    n.remove('[').remove(']');
    if (n.isEmpty()) {
        return;
    }
    std::map<QString, QString> all = readPresets();
    all[n] = jd::groupsToText(m_groups).toQString();
    writePresets(all);
    m_status = muse::qtrc("project/drumstems", "Preset “%1” saved.").arg(n);
    emit changed();
}

void DrumStemsModel::loadPreset(const QString& name)
{
    const std::map<QString, QString> all = readPresets();
    auto it = all.find(name);
    if (it == all.end()) {
        return;
    }
    m_groups = jd::groupsFromText(muse::String::fromQString(it->second));
    m_status = muse::qtrc("project/drumstems", "Preset “%1” loaded. Drums this score doesn't use are left out.").arg(name);
    emit changed();
}

void DrumStemsModel::deletePreset(const QString& name)
{
    std::map<QString, QString> all = readPresets();
    all.erase(name);
    writePresets(all);
    emit changed();
}

// ---- export ----

std::vector<DrumStemsModel::Output> DrumStemsModel::outputs() const
{
    std::vector<Output> out;
    std::set<QString> used;
    auto add = [&](QString name, const std::set<int>& pitches) {
        if (pitches.empty()) {
            return;
        }
        name = jd::stemFileName(muse::String::fromQString(name)).toQString();
        QString unique = name;
        for (int n = 2; used.count(unique.toLower()); ++n) {
            unique = QString("%1 %2").arg(name).arg(n);
        }
        used.insert(unique.toLower());
        out.push_back({ unique, pitches });
    };
    if (m_exportGroups) {
        for (const jd::StemGroup& g : m_groups) {
            add(g.name.toQString(), jd::pitchesOf(g, m_components));
        }
    }
    if (m_exportEach) {
        for (const jd::DrumComponent& c : m_components) {
            // a group that holds just this drum under the same name is the same file
            bool same = false;
            for (const Output& o : out) {
                same = same || o.pitches == c.pitches;
            }
            if (!same) {
                add(c.name.toQString(), c.pitches);
            }
        }
    }
    return out;
}

int DrumStemsModel::fileCount() const
{
    return int(outputs().size());
}

bool DrumStemsModel::exportOne(const Output& output, const QString& path)
{
    IMasterNotationPtr master = globalContext()->currentMasterNotation();
    INotationPtr notation = master ? master->notation() : nullptr;
    Score* s = score();
    Part* drums = kit();
    INotationWriterPtr writer = writers()->writer(FORMATS[m_formatIndex].toStdString());
    if (!notation || !s || !drums || !writer) {
        return false;
    }

    // 1. only the kit sounds: solo it in the mixer for the length of the export
    INotationSoloMuteStatePtr mixer = notation->soloMuteState();
    std::vector<std::pair<InstrumentTrackId, INotationSoloMuteState::SoloMuteState> > before;
    if (mixer) {
        for (const InstrumentTrackId& id : drums->instrumentTrackIdList()) {
            before.push_back({ id, mixer->trackSoloMuteState(id) });
            mixer->setTrackSoloMuteState(id, { false, true });
        }
    }

    // 2. only this stem's drums play: the others are switched to "don't play", and switched back below
    notation->undoStack()->prepareChanges(muse::TranslatableString("undoableAction", "Export drum stem"));
    const int playing = jd::isolate(s, drums, output.pitches);
    notation->undoStack()->commitChanges();

    bool ok = false;
    if (playing > 0) {
        const muse::io::path_t filePath = muse::io::path_t(path);
        muse::io::FileStream file(filePath);
        file.setMeta("file_path", path.toStdString());
        if (file.open(muse::io::IODevice::WriteOnly)) {
            ok = bool(writer->write(notation, file));
            file.close();
        }
    }

    // 3. everything back as it was
    notation->undoStack()->undo(nullptr);
    if (mixer) {
        for (const auto& [id, state] : before) {
            mixer->setTrackSoloMuteState(id, state);
        }
    }
    return ok;
}

void DrumStemsModel::exportStems()
{
    if (m_exporting || !kit()) {
        return;
    }
    const std::vector<Output> list = outputs();
    if (list.empty() || m_folder.isEmpty()) {
        m_status = m_folder.isEmpty() ? muse::qtrc("project/drumstems", "Choose a folder first.")
                   : muse::qtrc("project/drumstems", "Nothing to export: tick at least one of the two choices.");
        emit changed();
        return;
    }
    QDir().mkpath(m_folder);
    m_exporting = true;
    int done = 0;
    QStringList failed;
    for (const Output& o : list) {
        m_status = muse::qtrc("project/drumstems", "Exporting %1 of %2: %3…").arg(done + failed.size() + 1).arg(list.size()).arg(o.name);
        emit changed();
        const QString path = QDir(m_folder).filePath(o.name + "." + FORMATS[m_formatIndex]);
        if (exportOne(o, path)) {
            ++done;
        } else {
            failed << o.name;
        }
    }
    m_exporting = false;
    m_status = muse::qtrc("project/drumstems", "%n file(s) written to %1.", nullptr, done).arg(QDir::toNativeSeparators(m_folder));
    if (!failed.isEmpty()) {
        m_status += QLatin1Char(' ') + muse::qtrc("project/drumstems", "Could not write: %1.").arg(failed.join(", "));
    }
    emit changed();
}
