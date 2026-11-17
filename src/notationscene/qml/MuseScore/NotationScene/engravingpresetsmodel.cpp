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
#include "engravingpresetsmodel.h"

#include <QDir>
#include <QFileInfo>
#include <QUrl>

#include "io/file.h"
#include "engraving/style/style.h"
#include "translation.h"

#include "log.h"

using namespace mu::notation;
using namespace muse;

EngravingPresetsModel::EngravingPresetsModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

QVariantList EngravingPresetsModel::presets() const
{
    return m_presets;
}

QString EngravingPresetsModel::folder() const
{
    return configuration()->userStylesPath().toQString();
}

void EngravingPresetsModel::load()
{
    m_presets.clear();

    QDir dir(folder());
    const QFileInfoList files = dir.entryInfoList({ "*.mss" }, QDir::Files | QDir::Readable,
                                                  QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& fi : files) {
        QVariantMap item;
        item["name"] = fi.completeBaseName();
        item["path"] = fi.absoluteFilePath();
        m_presets << item;
    }

    emit presetsChanged();
}

QString EngravingPresetsModel::validateName(const QString& name) const
{
    const QString n = name.trimmed();
    if (n.isEmpty()) {
        return muse::qtrc("notation", "Enter a name for the preset.");
    }
    static const QString forbidden = QStringLiteral("\\/:*?\"<>|");
    for (const QChar c : n) {
        if (forbidden.contains(c) || c.unicode() < 32) {
            return muse::qtrc("notation", "A preset name can’t contain \\ / : * ? < > | or double quote marks");
        }
    }
    if (n.startsWith('.') || n.endsWith('.')) {
        return muse::qtrc("notation", "A preset name can’t start or end with a full stop.");
    }
    return QString();
}

bool EngravingPresetsModel::saveCurrentStyle(const QString& name)
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    if (!notation || !validateName(name).isEmpty()) {
        return false;
    }

    QDir().mkpath(folder());
    const QString path = QDir(folder()).filePath(name.trimmed() + ".mss");

    if (QFileInfo::exists(path)) {
        IInteractive::Result result = interactive()->questionSync(
            muse::qtrc("notation", "Replace the preset “%1”?").arg(name.trimmed()).toStdString(),
            muse::trc("notation", "A preset with this name already exists. It will be replaced by the current score’s style."),
            { IInteractive::Button::Cancel, IInteractive::Button::Yes }, IInteractive::Button::Cancel);
        if (result.standardButton() != IInteractive::Button::Yes) {
            return false;
        }
    }

    if (!notation->style()->saveStyle(muse::io::path_t(path))) {
        interactive()->error(muse::trc("notation", "The preset could not be saved."),
                             muse::trc("notation", "An error occurred."));
        return false;
    }

    load();
    return true;
}

bool EngravingPresetsModel::apply(const QString& path)
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    if (!notation) {
        return false;
    }

    muse::io::File f(path);
    if (!f.open(muse::io::IODevice::ReadOnly) || !mu::engraving::MStyle::isValid(&f)) {
        interactive()->error(muse::trc("notation", "The preset could not be loaded."), f.errorString());
        return false;
    }
    f.close();

    if (notation->style()->loadStyle(muse::io::path_t(path), false)) {
        return true;
    }

    IInteractive::Result result = interactive()->warningSync(
        muse::trc("notation", "Since this style file is from a different version of MuseScore Studio, your score is not guaranteed to display correctly."),
        muse::trc("notation", "Click OK to load anyway."),
        { IInteractive::Button::Ok, IInteractive::Button::Cancel }, IInteractive::Button::Ok);
    if (result.standardButton() == IInteractive::Button::Ok) {
        return notation->style()->loadStyle(muse::io::path_t(path), true);
    }
    return false;
}

void EngravingPresetsModel::reveal(const QString& path)
{
    interactive()->revealInFileBrowser(muse::io::path_t(path));
}

void EngravingPresetsModel::openFolder()
{
    QDir().mkpath(folder());
    interactive()->openUrl(QUrl::fromLocalFile(folder()));
}
