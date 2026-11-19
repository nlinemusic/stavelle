/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2022 MuseScore Limited and others
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

#include "fluidsequencer.h"

#include "global/interpolation.h"

#include "log.h"

using namespace muse;
using namespace muse::audio;
using namespace muse::audio::synth;
using namespace muse::midi;
using namespace muse::mpe;

static constexpr uint32_t CTRL_ON = 127;
static constexpr uint32_t CTRL_OFF = 0;

void FluidSequencer::init(const PlaybackSetupData& setupData, const std::optional<midi::Program>& programOverride,
                          bool useDynamicEvents)
{
    TRACEFUNC;

    // (set before the channels: adding a channel sets it up with these)
    m_useDynamicEvents = useDynamicEvents;
    m_jubelleDynamicsFamily = jubelle::familyFor(int(setupData.category), setupData.id.toLower().toStdString());
    m_channels.init(setupData, programOverride);
}

int FluidSequencer::currentExpressionLevel() const
{
    if (m_useDynamicEvents) {
        return expressionLevel(dynamicLevel(m_playbackPosition));
    }

    return naturalExpressionLevel();
}

int FluidSequencer::naturalExpressionLevel() const
{
    // (per instrument: velocity instruments use MuseScore's scale, see expressionLevel)
    return expressionLevel(dynamicLevelFromType(DynamicType::Natural));
}

void FluidSequencer::updateOffStreamEvents(const mpe::PlaybackEventsMap& events, const mpe::DynamicLevelLayers& dynamics)
{
    addPlaybackEvents(m_offStreamEvents, events);

    if (m_useDynamicEvents) {
        addDynamicEvents(m_offStreamEvents, dynamics);
    }

    updateOffSequenceIterator();
}

void FluidSequencer::updateMainStreamEvents(const mpe::PlaybackEventsMap& events, const mpe::DynamicLevelLayers& dynamics)
{
    m_mainStreamEvents.clear();

    if (m_onMainStreamFlushed) {
        m_onMainStreamFlushed();
    }

    addPlaybackEvents(m_mainStreamEvents, events);

    if (m_useDynamicEvents) {
        addDynamicEvents(m_mainStreamEvents, dynamics);
    }

    updateMainSequenceIterator();
}

muse::async::Channel<channel_t, Program> FluidSequencer::channelAdded() const
{
    return m_channels.channelAdded;
}

const ChannelMap& FluidSequencer::channels() const
{
    return m_channels;
}

int FluidSequencer::lastStaff() const
{
    return m_lastStaff;
}

void FluidSequencer::addPlaybackEvents(EventSequenceMap& destination, const mpe::PlaybackEventsMap& events)
{
    SostenutoTimeAndDurations sostenutoTimeAndDurations;

    for (const auto& pair : events) {
        for (const mpe::PlaybackEvent& event : pair.second) {
            if (std::holds_alternative<mpe::NoteEvent>(event)) {
                addNoteEvent(destination, std::get<mpe::NoteEvent>(event), sostenutoTimeAndDurations);
            } else if (std::holds_alternative<mpe::ControllerChangeEvent>(event)) {
                addControlChangeEvent(destination, pair.first, std::get<mpe::ControllerChangeEvent>(event));
            }
        }
    }

    addSostenutoEvents(destination, sostenutoTimeAndDurations);

    // Jubelle: bends end after the release, not at note-off (no "snap back")
    jubelle::placeResets(destination, m_jubellePendingResets);
    m_jubellePendingResets.clear();
}

void FluidSequencer::addDynamicEvents(EventSequenceMap& destination, const mpe::DynamicLevelLayers& dynamics)
{
    for (const auto& layer : dynamics) {
        for (const auto& dynamic : layer.second) {
            midi::Event event(muse::midi::Event::Opcode::ControlChange, Event::MessageType::ChannelVoice10);
            event.setIndex(midi::EXPRESSION_CONTROLLER);
            event.setData(expressionLevel(dynamic.second));

            destination[dynamic.first].emplace_back(std::move(event));
        }
    }
}

void FluidSequencer::addNoteEvent(EventSequenceMap& destination, const mpe::NoteEvent& noteEvent,
                                  SostenutoTimeAndDurations& sostenutoTimeAndDurations)
{
    const ArrangementContext& arrangementCtx = noteEvent.arrangementCtx();
    const channel_t channelIdx = channel(noteEvent);
    const note_idx_t noteIdx = noteIndex(noteEvent.pitchCtx().nominalPitchLevel);
    const velocity_t velocity = noteVelocity(noteEvent);
    const tuning_t tuning = noteTuning(noteEvent, noteIdx);

    // Assumption: 1:1 mapping between staff and instrument and FluidSynth and FluidSequencer instances.
    // So staffLayerIndex is constant. It changes only when moving staffs.
    m_lastStaff = noteEvent.arrangementCtx().staffLayerIndex;

    if (arrangementCtx.hasStart()) {
        midi::Event noteOn(Event::Opcode::NoteOn, Event::MessageType::ChannelVoice20);
        noteOn.setChannel(channelIdx);
        noteOn.setNote(noteIdx);
        noteOn.setVelocity16(velocity);
        noteOn.setPitchNote(noteIdx, tuning);

        destination[arrangementCtx.actualTimestamp].emplace_back(std::move(noteOn));
    }

    if (arrangementCtx.hasEnd()) {
        midi::Event noteOff(Event::Opcode::NoteOff, Event::MessageType::ChannelVoice20);
        noteOff.setChannel(channelIdx);
        noteOff.setNote(noteIdx);
        noteOff.setPitchNote(noteIdx, tuning);

        const timestamp_t timestampTo = arrangementCtx.actualTimestamp + noteEvent.arrangementCtx().actualDuration;
        destination[timestampTo].emplace_back(std::move(noteOff));
    }

    for (const auto& artPair : noteEvent.expressionCtx().articulations) {
        if (artPair.first == ArticulationType::Standard) {
            continue;
        }

        const mpe::ArticulationMeta& meta = artPair.second.meta;

        if (!noteEvent.pitchCtx().pitchCurve.empty() && muse::contains(BEND_SUPPORTED_TYPES, meta.type)) {
            addPitchCurve(destination, noteEvent, meta, channelIdx);
            continue;
        }

        if (muse::contains(SUSTAIN_PEDAL_CC_SUPPORTED_TYPES, meta.type)) {
            addPedalEvent(destination, meta, channelIdx);
            continue;
        }

        if (muse::contains(SOSTENUTO_PEDAL_CC_SUPPORTED_TYPES, meta.type)) {
            const mpe::timestamp_t timestamp = arrangementCtx.actualTimestamp + arrangementCtx.actualDuration * 0.1; // add offset for Sostenuto to take effect
            sostenutoTimeAndDurations[channelIdx].push_back(TimestampAndDuration { timestamp, meta.overallDuration });
            continue;
        }
    }
}

void FluidSequencer::addPedalEvent(EventSequenceMap& destination, const mpe::ArticulationMeta& meta,
                                   const channel_t channelIdx)
{
    //! NOTE: Endless pedals are not currently supported by SND instrument
    //! Otherwise, we will get infinite sustain
    if (m_useDynamicEvents && meta.hasStart() && !meta.hasEnd()) {
        return;
    }

    if (meta.hasStart()) {
        addControlChange(destination, meta.timestamp, midi::SUSTAIN_PEDAL_CONTROLLER, channelIdx, CTRL_ON);
    }

    if (meta.hasEnd()) {
        addControlChange(destination, meta.timestamp + meta.overallDuration,
                         midi::SUSTAIN_PEDAL_CONTROLLER, channelIdx, CTRL_OFF);
    }
}

void FluidSequencer::addControlChangeEvent(EventSequenceMap& destination, const mpe::timestamp_t timestamp,
                                           const mpe::ControllerChangeEvent& event)
{
    const channel_t lastChannelIdx = m_channels.lastIndex();

    for (channel_t channelIdx = 0; channelIdx < lastChannelIdx; ++channelIdx) {
        switch (event.type) {
        case mpe::ControllerChangeEvent::Modulation:
            addControlChange(destination, timestamp, midi::MODWHEEL_CONTROLLER, channelIdx,
                             static_cast<uint32_t>(event.val * 127.f));
            break;
        case mpe::ControllerChangeEvent::SustainPedalOnOff:
            addControlChange(destination, timestamp, midi::SUSTAIN_PEDAL_CONTROLLER, channelIdx,
                             static_cast<uint32_t>(event.val * 127.f));
            break;
        case mpe::ControllerChangeEvent::PitchBend:
            addPitchBend(destination, timestamp, channelIdx,
                         static_cast<uint32_t>(event.val * 16383.f));
            break;
        case mpe::ControllerChangeEvent::Undefined:
            break;
        }
    }
}

void FluidSequencer::addControlChange(EventSequenceMap& destination, const mpe::timestamp_t timestamp,
                                      const int midiControlIdx, const channel_t channelIdx, const uint32_t value)
{
    EventSequence& events = destination[timestamp];
    for (const EventType& e : events) {
        const midi::Event& midiEvent = std::get<midi::Event>(e);
        if (midiEvent.opcode() == Event::Opcode::ControlChange
            && midiEvent.channel() == channelIdx
            && midiEvent.index() == static_cast<uint8_t>(midiControlIdx)
            && midiEvent.data() == value) {
            return;
        }
    }

    midi::Event cc(Event::Opcode::ControlChange, Event::MessageType::ChannelVoice10);
    cc.setIndex(midiControlIdx);
    cc.setChannel(channelIdx);
    cc.setData(value);

    events.emplace_back(cc);
}

void FluidSequencer::addPitchCurve(EventSequenceMap& destination, const mpe::NoteEvent& noteEvent,
                                   const mpe::ArticulationMeta& artMeta, const channel_t channelIdx)
{
    const timestamp_t noteTimestampTo = noteEvent.arrangementCtx().actualTimestamp + noteEvent.arrangementCtx().actualDuration;
    const timestamp_t pitchBendTimestampTo = std::min(artMeta.timestamp + artMeta.overallDuration, noteTimestampTo);

    // Jubelle: the reset to the written pitch is placed after the note's release
    // (addPlaybackEvents); gestures that leave the note also fade out
    const bool fades = artMeta.type == ArticulationType::Fall || artMeta.type == ArticulationType::QuickFall
                       || artMeta.type == ArticulationType::Doit || artMeta.type == ArticulationType::SlideOutDown
                       || artMeta.type == ArticulationType::SlideOutUp;
    m_jubellePendingResets.push_back({ static_cast<int64_t>(pitchBendTimestampTo), channelIdx, fades });

    int maxBendDistance = 0;
    for (const auto& point : noteEvent.pitchCtx().pitchCurve) {
        maxBendDistance = std::max(maxBendDistance, std::abs(pitchBendLevel(point.second) - 8192));
    }
    uint32_t prevVolume = jubelle::FULL_VOLUME;

    // Jubelle: a smooth monotone curve through the pattern's points instead of
    // straight lines between them (no abrupt changes of speed at each point)
    std::vector<jubelle::CurvePoint> points;
    for (const auto& point : noteEvent.pitchCtx().pitchCurve) {
        const timestamp_t time = artMeta.timestamp + artMeta.overallDuration * percentageToFactor(point.first);
        points.push_back({ static_cast<double>(time), static_cast<double>(pitchBendLevel(point.second)) });
    }
    //! NOTE: MuseScore's interpolation density: about one step per 1/25 semitone
    const double bendPerStep = std::max(1.0, std::abs(pitchBendLevel(mpe::PITCH_LEVEL_STEP / 25) - 8192.0));
    int prevBendValue = -1;
    for (const jubelle::CurvePoint& point : jubelle::smoothCurve(points, 8, bendPerStep)) {
        const timestamp_t time = static_cast<timestamp_t>(std::round(point.time));
        const int bendValue = std::clamp(static_cast<int>(std::round(point.value)), 0, 16383);

        if (time < pitchBendTimestampTo && bendValue != prevBendValue) {
            addPitchBend(destination, time, channelIdx, bendValue);
            if (fades && maxBendDistance > 0) {
                const uint32_t volume = jubelle::fadedVolume(std::abs(bendValue - 8192) / double(maxBendDistance));
                if (volume != prevVolume) {
                    destination[time].push_back(jubelle::volumeEvent(channelIdx, volume));
                    prevVolume = volume;
                }
            }
        }

        prevBendValue = bendValue;
    }
}

void FluidSequencer::addPitchBend(EventSequenceMap& destination, const mpe::timestamp_t timestamp,
                                  const midi::channel_t channelIdx, const uint32_t value)
{
    midi::Event event(Event::Opcode::PitchBend, Event::MessageType::ChannelVoice10);
    event.setChannel(channelIdx);
    event.setData(value);
    destination[timestamp].push_back(event);
}

void FluidSequencer::addSostenutoEvents(EventSequenceMap& destination, const SostenutoTimeAndDurations& sostenutoTimeAndDurations)
{
    for (const auto& channelPair : sostenutoTimeAndDurations) {
        for (size_t i = 0; i < channelPair.second.size(); ++i) {
            const TimestampAndDuration& currentTnD = channelPair.second.at(i);
            const timestamp_t timestampTo = currentTnD.timestamp + currentTnD.duration;

            addControlChange(destination, currentTnD.timestamp, midi::SOSTENUTO_PEDAL_CONTROLLER, channelPair.first, CTRL_ON);

            if (i == channelPair.second.size() - 1) {
                addControlChange(destination, timestampTo, midi::SOSTENUTO_PEDAL_CONTROLLER, channelPair.first, CTRL_OFF);
                continue;
            }

            const TimestampAndDuration& nextTnD = channelPair.second.at(i + 1);
            if (timestampTo <= nextTnD.timestamp) { // handle potential overlap
                addControlChange(destination, timestampTo, midi::SOSTENUTO_PEDAL_CONTROLLER, channelPair.first, CTRL_OFF);
            }
        }
    }
}

channel_t FluidSequencer::channel(const mpe::NoteEvent& noteEvent) const
{
    return m_channels.resolveChannelForEvent(noteEvent);
}

note_idx_t FluidSequencer::noteIndex(const mpe::pitch_level_t pitchLevel) const
{
    float stepCount = mpe::ZERO_PITCH_LEVEL_MIDI_EQUIVALENT + pitchLevel / static_cast<float>(mpe::PITCH_LEVEL_STEP);

    return std::clamp(stepCount, 0.f, 127.f);
}

tuning_t FluidSequencer::noteTuning(const mpe::NoteEvent& noteEvent, const int noteIdx) const
{
    int semitonesCount = noteIdx - mpe::ZERO_PITCH_LEVEL_MIDI_EQUIVALENT;

    mpe::pitch_level_t tuningPitchLevel = noteEvent.pitchCtx().nominalPitchLevel - semitonesCount * mpe::PITCH_LEVEL_STEP;

    return tuningPitchLevel / static_cast<float>(mpe::PITCH_LEVEL_STEP);
}

velocity_t FluidSequencer::noteVelocity(const mpe::NoteEvent& noteEvent) const
{
    static constexpr midi::velocity_t MAX_SUPPORTED_VELOCITY = std::numeric_limits<midi::velocity_t>::max();

    const mpe::ExpressionContext& expressionCtx = noteEvent.expressionCtx();

    if (expressionCtx.velocityOverride.has_value()) {
        velocity_t velocity = RealRound(expressionCtx.velocityOverride.value() * MAX_SUPPORTED_VELOCITY, 0);
        return std::clamp<velocity_t>(velocity, 0, MAX_SUPPORTED_VELOCITY);
    }

    if (m_useDynamicEvents) {
        velocity_t result = RealRound(expressionCtx.expressionCurve.velocityFraction() * MAX_SUPPORTED_VELOCITY, 0);
        return std::clamp<velocity_t>(result, 0, MAX_SUPPORTED_VELOCITY);
    }

    dynamic_level_t dynamicLevel = expressionCtx.expressionCurve.maxAmplitudeLevel();
    return expressionLevel(dynamicLevel) << 9; // midi::Event::scaleUp(7,16)
}

int FluidSequencer::expressionLevel(const mpe::dynamic_level_t dynamicLevel) const
{
    static constexpr mpe::dynamic_level_t MIN_SUPPORTED_DYNAMICS_LEVEL = mpe::dynamicLevelFromType(DynamicType::ppp);

    const bool natural = dynamicLevel == mpe::dynamicLevelFromType(DynamicType::Natural);
    const double steps = (dynamicLevel - MIN_SUPPORTED_DYNAMICS_LEVEL) / static_cast<double>(mpe::DYNAMIC_LEVEL_STEP);

    // Jubelle: instruments played by velocity keep MuseScore's own scale: the
    // velocity also chooses their sample layer and brightness (jubelledynamics.h)
    if (!m_useDynamicEvents) {
        return jubelle::museScoreLevel(natural ? steps - 0.5 : steps);
    }

    // Jubelle: sustained instruments (dynamics through the expression controller,
    // which changes only the level): each marking's loudness is chosen in
    // decibels and by instrument family, then turned into the MIDI level that
    // gives it. MuseScore's own mapping was 16 + 16 per marking for every instrument.
    if (natural) {
        return jubelle::naturalMidiLevel();
    }
    return jubelle::midiLevel(steps, m_jubelleDynamicsFamily);
}

int FluidSequencer::pitchBendLevel(const mpe::pitch_level_t pitchLevel) const
{
    static constexpr int PITCH_BEND_SEMITONE_STEP = 4096 / 12;

    float pitchLevelSteps = pitchLevel / static_cast<float>(mpe::PITCH_LEVEL_STEP);

    int offset = pitchLevelSteps * PITCH_BEND_SEMITONE_STEP;

    return std::clamp(8192 + offset, 0, 16383);
}
