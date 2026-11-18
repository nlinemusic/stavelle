/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle: "Instrument settings" of a VST3 instrument's window: the level trim,
 * the library's pitch-bend range and its key switches. They are saved with the
 * score, with the plugin's own settings.
 */
#pragma once

#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "global/modularity/ioc.h"
#include "ivstinstancesregister.h"

namespace muse::vst {
class VstInstrumentSettingsModel : public QObject, public muse::Contextable
{
    Q_OBJECT

    Q_PROPERTY(int instanceId READ instanceId WRITE setInstanceId NOTIFY instanceIdChanged FINAL)
    Q_PROPERTY(double trimDb READ trimDb WRITE setTrimDb NOTIFY changed FINAL)
    Q_PROPERTY(double bendRange READ bendRange WRITE setBendRange NOTIFY changed FINAL)
    Q_PROPERTY(bool middleCIsC4 READ middleCIsC4 WRITE setMiddleCIsC4 NOTIFY changed FINAL)
    //! {group, title, note} for each articulation a key can select; note is "" when none is set
    Q_PROPERTY(QVariantList keySwitches READ keySwitches NOTIFY changed FINAL)

    QML_ELEMENT

    muse::ContextInject<IVstInstancesRegister> instancesRegister { this };

public:
    explicit VstInstrumentSettingsModel(QObject* parent = nullptr);

    int instanceId() const { return m_instanceId; }
    void setInstanceId(int id);

    double trimDb() const;
    void setTrimDb(double db);
    double bendRange() const;
    void setBendRange(double semitones);
    bool middleCIsC4() const;
    void setMiddleCIsC4(bool c4);
    QVariantList keySwitches() const;

    //! Sets the key for a group from a note name ("C1", "F#0") or a MIDI number; "" clears it.
    //! Returns false (and changes nothing) when the text isn't a note.
    Q_INVOKABLE bool setKeySwitch(const QString& group, const QString& note);
    Q_INVOKABLE void clearKeySwitches();

signals:
    void instanceIdChanged();
    void changed();

private:
    std::string extra(const char* key) const;
    void setExtra(const char* key, const std::string& value);

    int m_instanceId = -1;
};
}
