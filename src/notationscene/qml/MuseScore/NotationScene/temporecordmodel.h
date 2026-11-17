/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QVariantList>
#include <qqmlintegration.h>

#include "modularity/ioc.h"
#include "async/asyncable.h"
#include "actions/iactionsdispatcher.h"
#include "context/iglobalcontext.h"
#include "midi/imidiinport.h"
#include "playback/iplaybackcontroller.h"

#include "notation/internal/jubelle/tempotap.h"

namespace mu::notation {
//! Jubelle G6: Playback › Record tempo…  Tap the beat (the Tap button, any key,
//! or a MIDI key or pedal) while the score plays. The taps become hidden tempo
//! markings from where you started. "Follow me" makes playback keep up with
//! your taps while you tap. Tapping only: no camera or gesture tracking.
class TempoRecordModel : public QObject, public muse::Contextable, public muse::async::Asyncable
{
    Q_OBJECT

    Q_PROPERTY(bool recording READ recording NOTIFY changed)
    Q_PROPERTY(int tapCount READ tapCount NOTIFY changed)
    Q_PROPERTY(int liveBpm READ liveBpm NOTIFY changed)
    Q_PROPERTY(QVariantList beatOptions READ beatOptions CONSTANT)
    Q_PROPERTY(int beatIndex READ beatIndex NOTIFY changed)
    Q_PROPERTY(int smoothingPercent READ smoothingPercent NOTIFY changed)
    Q_PROPERTY(bool followMe READ followMe NOTIFY changed)
    Q_PROPERTY(bool midiTap READ midiTap NOTIFY changed)
    Q_PROPERTY(int recordedPoints READ recordedPoints NOTIFY changed)
    Q_PROPERTY(QVariantList points READ points NOTIFY changed)
    Q_PROPERTY(QString startText READ startText NOTIFY changed)
    Q_PROPERTY(QString resultText READ resultText NOTIFY changed)

    QML_ELEMENT

    muse::ContextInject<context::IGlobalContext> globalContext = { this };
    muse::ContextInject<playback::IPlaybackController> playbackController = { this };
    muse::ContextInject<muse::actions::IActionsDispatcher> dispatcher = { this };
    muse::ContextInject<muse::midi::IMidiInPort> midiInPort = { this };

public:
    explicit TempoRecordModel(QObject* parent = nullptr);
    ~TempoRecordModel() override;

    bool recording() const { return m_recording; }
    int tapCount() const { return int(m_taps.size()); }
    int liveBpm() const { return m_liveBpm; }
    QVariantList beatOptions() const;
    int beatIndex() const { return m_beatIndex; }
    int smoothingPercent() const { return int(m_settings.smoothing * 100.0 + 0.5); }
    bool followMe() const { return m_followMe; }
    bool midiTap() const { return m_midiTap; }
    int recordedPoints() const;
    //! Jubelle's recorded tempo points, to look at and correct: bar, beat and tempo
    QVariantList points() const;
    QString startText() const;
    QString resultText() const { return m_resultText; }

    Q_INVOKABLE void load();
    Q_INVOKABLE void setBeatIndex(int index);
    Q_INVOKABLE void setSmoothingPercent(int percent);
    Q_INVOKABLE void setFollowMe(bool on);
    Q_INVOKABLE void setMidiTap(bool on);

    //! Arms recording from the selection (or the start of the score). The first tap starts playback.
    Q_INVOKABLE void start();
    Q_INVOKABLE void tap();
    //! Stops and writes the recorded tempo into the score (one undo step)
    Q_INVOKABLE void finish();
    //! Stops without changing the score
    Q_INVOKABLE void cancel();
    //! Removes Jubelle's recorded tempo from the selection, or from the whole score
    Q_INVOKABLE void clearRecorded();
    //! Changes the tempo of one recorded point (one undo step)
    Q_INVOKABLE void setPointBpm(int index, double bpm);
    Q_INVOKABLE void goToPoint(int index);

signals:
    void changed();

private:
    engraving::Score* score() const;
    void stopPlayback();
    void findStart();

    jubelle::TapSettings m_settings;
    int m_beatIndex = 0;
    bool m_recording = false;
    bool m_followMe = true;
    bool m_midiTap = true;
    std::vector<double> m_taps;
    QElapsedTimer m_clock;
    engraving::Fraction m_startTick;
    engraving::Fraction m_limitTick;
    int m_playTick = 0;
    int m_liveBpm = 0;
    QString m_resultText;
};
}
