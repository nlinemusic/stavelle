/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */
#pragma once

#include <QVariantList>
#include <qqmlintegration.h>

#include "abstractinspectormodel.h"

namespace mu::inspector {
//! Jubelle: contextual Properties for Jubelle's own items (menu and Properties reorganisation):
//!  - a recorded tempo point: its tempo, editable; Remove;
//!  - a timeline marker: its name; Play from here; Delete;
//!  - a page break placed by Page turns: Keep as my own break; Remove;
//!  - a dynamic: how loud it plays, in decibels from mf for this instrument family (read-only);
//!  - the swing marking: the feel it plays; Swing…
//! Shown only when the selection holds one of these.
class JubelleContextSettingsModel : public AbstractInspectorModel
{
    Q_OBJECT
    QML_ELEMENT;
    QML_UNCREATABLE("Not creatable from QML")

    //! "tempo", "marker", "pagebreak", "dynamic", "swing" or ""
    Q_PROPERTY(QString kind READ kind NOTIFY changed FINAL)
    Q_PROPERTY(QString heading READ heading NOTIFY changed FINAL)
    Q_PROPERTY(QString detail READ detail NOTIFY changed FINAL)
    Q_PROPERTY(double tempoBpm READ tempoBpm NOTIFY changed FINAL)
    Q_PROPERTY(QString markerName READ markerName NOTIFY changed FINAL)

public:
    explicit JubelleContextSettingsModel(QObject* parent, const muse::modularity::ContextPtr& iocCtx, IElementRepositoryService* repository);

    QString kind() const { return m_kind; }
    QString heading() const { return m_heading; }
    QString detail() const { return m_detail; }
    double tempoBpm() const { return m_tempoBpm; }
    QString markerName() const { return m_markerName; }

    Q_INVOKABLE void setTempoBpm(double bpm);
    Q_INVOKABLE void setMarkerName(const QString& name);
    Q_INVOKABLE void playFromHere();
    Q_INVOKABLE void removeItem();
    Q_INVOKABLE void keepAsOwnBreak();
    Q_INVOKABLE void openSwing();

    //! Whether any of these items is selected (decides whether the section is shown)
    static bool appliesTo(const QList<mu::engraving::EngravingItem*>& selection);

signals:
    void changed();

private:
    void createProperties() override {}
    void requestElements() override;
    void loadProperties() override;
    void resetProperties() override {}
    void onNotationChanged(const mu::engraving::PropertyIdSet&, const mu::engraving::StyleIdSet&) override;

    void update();
    mu::engraving::Score* score() const;

    mu::engraving::EngravingItem* m_item = nullptr;
    QString m_kind;
    QString m_heading;
    QString m_detail;
    double m_tempoBpm = 0.0;
    QString m_markerName;
};
}
