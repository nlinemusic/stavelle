/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G6: Playback › Record tempo… See temporecordmodel.h.

#include "temporecordmodel.h"

#include <algorithm>
#include <cmath>

#include <QVariantMap>

#include "translation.h"

#include "engraving/dom/measure.h"
#include "engraving/dom/score.h"
#include "engraving/dom/tempotext.h"

#include "notation/inotation.h"
#include "notation/inotationelements.h"
#include "notation/inotationinteraction.h"
#include "notation/inotationselection.h"
#include "notation/internal/inotationselectionrange.h"
#include "notation/internal/inotationundostack.h"

using namespace mu::notation;
using namespace mu::engraving;
namespace jt = mu::notation::jubelle;

namespace {
struct BeatOption {
    Fraction value;
    const char* title;
};

const std::vector<BeatOption>& beats()
{
    static const std::vector<BeatOption> list {
        { Fraction(1, 4), QT_TRANSLATE_NOOP("notation/temporecord", "Quarter notes") },
        { Fraction(1, 2), QT_TRANSLATE_NOOP("notation/temporecord", "Half notes") },
        { Fraction(3, 8), QT_TRANSLATE_NOOP("notation/temporecord", "Dotted quarter notes (6/8, 9/8, 12/8)") },
        { Fraction(1, 8), QT_TRANSLATE_NOOP("notation/temporecord", "Eighth notes") },
        { Fraction(1, 1), QT_TRANSLATE_NOOP("notation/temporecord", "Whole notes (one tap per 4/4 bar)") },
    };
    return list;
}
}

TempoRecordModel::TempoRecordModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this))
{
}

TempoRecordModel::~TempoRecordModel()
{
    if (m_recording) {
        cancel();
    }
}

Score* TempoRecordModel::score() const
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    return notation && notation->elements() ? notation->elements()->msScore() : nullptr;
}

void TempoRecordModel::load()
{
    if (playbackController()) {
        playbackController()->currentPlaybackPositionChanged().onReceive(this, [this](muse::audio::secs_t, muse::midi::tick_t tick) {
            m_playTick = int(tick);
        });
    }
    if (midiInPort()) {
        // G10: a MIDI key or pedal as the tap
        midiInPort()->eventReceived().onReceive(this, [this](muse::midi::tick_t, const muse::midi::Event& event) {
            if (!m_recording || !m_midiTap) {
                return;
            }
            const bool noteOn = event.opcode() == muse::midi::Event::Opcode::NoteOn && event.velocity7() > 0;
            const bool pedalDown = event.opcode() == muse::midi::Event::Opcode::ControlChange && event.index() == 64
                                   && event.data7() >= 64;
            if (noteOn || pedalDown) {
                tap();
            }
        });
    }
    findStart();
    emit changed();
}

QVariantList TempoRecordModel::beatOptions() const
{
    QVariantList out;
    for (size_t i = 0; i < beats().size(); ++i) {
        QVariantMap m;
        m["value"] = int(i);
        m["text"] = muse::qtrc("notation/temporecord", beats()[i].title);
        out << m;
    }
    return out;
}

void TempoRecordModel::setBeatIndex(int index)
{
    m_beatIndex = std::clamp(index, 0, int(beats().size()) - 1);
    m_settings.beat = beats()[size_t(m_beatIndex)].value;
    emit changed();
}

void TempoRecordModel::setSmoothingPercent(int percent)
{
    m_settings.smoothing = std::clamp(percent, 0, 90) / 100.0;
    emit changed();
}

void TempoRecordModel::setFollowMe(bool on)
{
    m_followMe = on;
    emit changed();
}

void TempoRecordModel::setMidiTap(bool on)
{
    m_midiTap = on;
    emit changed();
}

int TempoRecordModel::recordedPoints() const
{
    return int(jt::recordedTempoPoints(score()).size());
}

void TempoRecordModel::findStart()
{
    m_startTick = Fraction(0, 1);
    m_limitTick = Fraction::max();
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* s = score();
    if (!notation || !s) {
        return;
    }
    m_limitTick = s->endTick();
    const INotationSelectionPtr selection = notation->interaction()->selection();
    if (selection->isRange()) {
        m_startTick = selection->range()->startTick();
        m_limitTick = selection->range()->endTick();
    } else if (!selection->elements().empty()) {
        Fraction first = Fraction::max();
        for (const EngravingItem* e : selection->elements()) {
            first = std::min(first, e->tick());
        }
        // from the start of that bar
        const Measure* m = s->tick2measure(first);
        m_startTick = m ? m->tick() : first;
    }
}

QString TempoRecordModel::startText() const
{
    Score* s = score();
    if (!s) {
        return QString();
    }
    int bar = 0;
    for (const Measure* m = s->firstMeasure(); m; m = m->nextMeasure()) {
        ++bar;
        if (m->tick() <= m_startTick && m_startTick < m->endTick()) {
            break;
        }
    }
    return m_limitTick < s->endTick()
           ? muse::qtrc("notation/temporecord", "Recording the selected passage, from bar %1. The rest of the score keeps its tempo.").arg(bar)
           : muse::qtrc("notation/temporecord", "Recording from bar %1. Select a passage first to record only that.").arg(bar);
}

void TempoRecordModel::start()
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* s = score();
    if (!notation || !s || m_recording) {
        return;
    }
    findStart();
    m_taps.clear();
    m_liveBpm = 0;
    m_resultText.clear();
    m_recording = true;
    stopPlayback();
    // the playhead waits at the start; the first tap starts the music
    if (Measure* m = s->tick2measure(m_startTick)) {
        if (Segment* seg = m->first(SegmentType::ChordRest)) {
            for (EngravingItem* e : seg->elist()) {
                if (e) {
                    playbackController()->seekElement(e);
                    break;
                }
            }
        }
    }
    emit changed();
}

void TempoRecordModel::tap()
{
    if (!m_recording) {
        return;
    }
    if (m_taps.empty()) {
        m_clock.start();
        m_taps.push_back(0.0);
        if (playbackController() && !playbackController()->isPlaying()) {
            dispatcher()->dispatch("play");
        }
    } else {
        m_taps.push_back(m_clock.nsecsElapsed() / 1e9);
    }
    const double live = jt::liveTempo(m_taps, m_settings);
    m_liveBpm = int(live + 0.5);

    // Follow me: playback keeps up with the taps (the written tempo is scaled, nothing is changed in the score yet)
    if (m_followMe && live > 0.0 && playbackController()) {
        if (Score* s = score()) {
            const double written = s->tempo(Fraction::fromTicks(m_playTick)).toBPM().val;
            if (written > 0.0) {
                playbackController()->setTempoMultiplier(std::clamp(live / written, 0.25, 4.0));
            }
        }
    }

    // the end of the selected passage, or of the score: finish by itself
    const Fraction nextTick = m_startTick + m_settings.beat * int(m_taps.size() - 1);
    if (nextTick >= m_limitTick) {
        finish();
        return;
    }
    emit changed();
}

void TempoRecordModel::stopPlayback()
{
    if (!playbackController()) {
        return;
    }
    if (playbackController()->isPlaying()) {
        dispatcher()->dispatch("stop");
    }
    playbackController()->setTempoMultiplier(1.0);
}

void TempoRecordModel::finish()
{
    if (!m_recording) {
        return;
    }
    m_recording = false;
    stopPlayback();

    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* s = score();
    const std::vector<jt::TempoPoint> points = jt::tapsToTempo(m_taps, m_startTick, m_settings);
    if (!notation || !s || points.empty()) {
        m_resultText = muse::qtrc("notation/temporecord", "Nothing recorded: tap at least twice.");
        emit changed();
        return;
    }
    const Fraction to = std::min(m_limitTick, m_startTick + m_settings.beat * int(m_taps.size() - 1));
    notation->undoStack()->prepareChanges(muse::TranslatableString("undoableAction", "Record tempo"));
    jt::applyTempoPoints(s, points, m_startTick, to);
    notation->undoStack()->commitChanges();

    double low = points.front().bpm, high = points.front().bpm;
    for (const jt::TempoPoint& p : points) {
        low = std::min(low, p.bpm);
        high = std::max(high, p.bpm);
    }
    m_resultText = muse::qtrc("notation/temporecord", "Recorded %n tap(s): ♩ = %1 to %2. Play to hear it; Undo takes it back.", nullptr,
                              int(m_taps.size())).arg(int(low + 0.5)).arg(int(high + 0.5));
    emit changed();
}

void TempoRecordModel::cancel()
{
    m_recording = false;
    m_taps.clear();
    stopPlayback();
    emit changed();
}

void TempoRecordModel::clearRecorded()
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* s = score();
    if (!notation || !s || m_recording) {
        return;
    }
    findStart();
    const bool passage = m_limitTick < s->endTick();
    notation->undoStack()->prepareChanges(muse::TranslatableString("undoableAction", "Remove recorded tempo"));
    jt::clearRecordedTempo(s, passage ? m_startTick : Fraction(0, 1), passage ? m_limitTick : s->endTick() + Fraction(1, 1));
    notation->undoStack()->commitChanges();
    m_resultText = muse::qtrc("notation/temporecord", "Recorded tempo removed. Your own tempo markings are untouched.");
    emit changed();
}

QVariantList TempoRecordModel::points() const
{
    QVariantList out;
    Score* s = score();
    if (!s) {
        return out;
    }
    int index = 0;
    for (TempoText* t : jt::recordedTempoPoints(s)) {
        QVariantMap m;
        m["index"] = index++;
        const Measure* measure = s->tick2measure(t->tick());
        const int bar = measure ? measure->no() + 1 : 0;
        // the beat inside the bar, counted in quarter notes from 1
        const double beat = measure ? 1.0 + (t->tick() - measure->tick()).toDouble() * 4.0 : 1.0;
        m["where"] = muse::qtrc("notation/temporecord", "Bar %1, beat %2").arg(bar).arg(beat, 0, 'g', 3);
        m["bpm"] = std::round(s->tempo(t->tick()).toBPM().val * 10.0) / 10.0;
        out << m;
    }
    return out;
}

void TempoRecordModel::setPointBpm(int index, double bpm)
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* s = score();
    if (!notation || !s || m_recording) {
        return;
    }
    const std::vector<TempoText*> all = jt::recordedTempoPoints(s);
    if (index < 0 || index >= int(all.size())) {
        return;
    }
    notation->undoStack()->prepareChanges(muse::TranslatableString("undoableAction", "Edit recorded tempo"));
    jt::setRecordedPointTempo(s, all[size_t(index)], std::clamp(bpm, 20.0, 400.0));
    notation->undoStack()->commitChanges();
    emit changed();
}

void TempoRecordModel::goToPoint(int index)
{
    INotationPtr notation = globalContext() ? globalContext()->currentNotation() : nullptr;
    Score* s = score();
    if (!notation || !s) {
        return;
    }
    const std::vector<TempoText*> all = jt::recordedTempoPoints(s);
    if (index < 0 || index >= int(all.size())) {
        return;
    }
    if (Measure* m = s->tick2measure(all[size_t(index)]->tick())) {
        notation->interaction()->select({ m }, SelectType::SINGLE, 0);
        notation->interaction()->showItem(m, 0);
    }
}
