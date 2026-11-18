/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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

#include "glissandosrenderer.h"

#include "dom/glissando.h"

#include "playback/renderingcontext.h"
#include "playback/jubelle/articulationplayback.h"

#include <algorithm>
#include "playback/utils/expressionutils.h"

using namespace mu::engraving;
using namespace muse;
using namespace muse::mpe;

static std::vector<int> pitchSteps(const Note* note)
{
    const Glissando* glissando = nullptr;
    for (const Spanner* spanner : note->spannerFor()) {
        if (spanner->isGlissando()) {
            glissando = toGlissando(spanner);
            break;
        }
    }

    if (!glissando) {
        return {};
    }

    std::vector<int> pitchSteps;
    Glissando::pitchSteps(glissando, pitchSteps);

    return pitchSteps;
}

muse::mpe::duration_t GlissandosRenderer::discreteGlissandoStepDuration(const Note* note, const duration_t noteDuration)
{
    const std::vector<int> steps = pitchSteps(note);
    if (steps.empty()) {
        return 0;
    }

    return noteDuration / static_cast<float>(steps.size());
}

void GlissandosRenderer::renderDiscreteGlissando(const Note* note, NominalNoteCtx& ctx, mpe::PlaybackEventList& result)
{
    const std::vector<int> steps = pitchSteps(note);
    if (steps.empty()) {
        return;
    }

    const size_t stepsCount = steps.size();

    // Jubelle: chromatic and white-key glissandos honour "Slide starts at" too: the first note is held
    // for that part of the note and the steps fill the rest (0% = MuseScore's even steps)
    const Chord* chord = note->chord();
    const int startPercent = chord && stepsCount > 1 ? std::clamp(jubelle::effectiveValue(chord->score(), chord->part(), chord->tick().ticks(),
                                                                        jubelle::ArtParam::GlissandoStart), 0, 90) : 0;
    const float held = ctx.duration * (static_cast<float>(startPercent) / 100.f);
    const float durationStep = stepsCount > 1 ? (ctx.duration - held) / static_cast<float>(stepsCount - 1 + (startPercent > 0 ? 0 : 1))
                               : ctx.duration;
    size_t firstStep = 0;

    if (note->tieBack()) {
        mpe::ArticulationMeta& meta = ctx.articulations.at(ArticulationType::DiscreteGlissando).meta;
        const float firstLength = startPercent > 0 ? held : durationStep;
        meta.timestamp += firstLength;
        meta.overallDuration -= firstLength;
        firstStep = 1;
    }

    for (size_t i = firstStep; i < stepsCount; ++i) {
        NominalNoteCtx noteCtx(ctx);
        if (startPercent > 0) {
            // step 0 is held; the others share what is left of the note
            noteCtx.duration = i == 0 ? held : durationStep;
            noteCtx.timestamp += i == 0 ? 0 : held + (i - 1) * durationStep;
        } else {
            noteCtx.duration = durationStep;
            noteCtx.timestamp += i * durationStep;
        }
        noteCtx.pitchLevel += steps.at(i) * mpe::PITCH_LEVEL_STEP;

        const int utick = timestampToTick(noteCtx.chordCtx.score, noteCtx.timestamp);
        noteCtx.dynamicLevel = ctx.chordCtx.playbackCtx->appliableDynamicLevel(note->track(), utick);

        updateArticulationBoundaries(ArticulationType::DiscreteGlissando,
                                     noteCtx.timestamp,
                                     noteCtx.duration,
                                     noteCtx.articulations);

        result.emplace_back(buildNoteEvent(noteCtx));
    }
}

void GlissandosRenderer::renderContinuousGlissando(const Note*, NominalNoteCtx& ctx, muse::mpe::PlaybackEventList& result)
{
    result.emplace_back(buildNoteEvent(ctx));
}
