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
#include "passagelinkplanner.h"

#include <algorithm>
#include <type_traits>

namespace mu::notation::passagelink {
namespace {
bool isRest(const Event& e)
{
    return e.pitches.empty();
}

const Event* eventAt(const Passage& p, int tick)
{
    for (const Event& e : p) {
        if (e.tick == tick) {
            return &e;
        }
    }
    return nullptr;
}

bool overlaps(const Event& a, const Event& b)
{
    return a.tick < b.tick + b.duration && b.tick < a.tick + a.duration;
}

//! The interval (in semitones) between target and source at the nearest point at
//! or before `tick` where both sounded in the original passages. Keeps each
//! player's own transposition/octave for notes that are new in the source.
int intervalNear(const Passage& sourceBefore, const Passage& target, int tick)
{
    int best = 0;
    int bestTick = -1;
    int fallbackTick = -1;
    int fallback = 0;
    for (const Event& t : target) {
        if (isRest(t)) {
            continue;
        }
        const Event* s = eventAt(sourceBefore, t.tick);
        if (!s || isRest(*s)) {
            continue;
        }
        const int interval = t.pitches.front() - s->pitches.front();
        if (t.tick <= tick && t.tick > bestTick) {
            bestTick = t.tick;
            best = interval;
        }
        if (fallbackTick < 0 || t.tick < fallbackTick) {
            fallbackTick = t.tick;
            fallback = interval;
        }
    }
    return bestTick >= 0 ? best : fallback;
}

//! Pitches for a target note at the position of source note `src`.
std::vector<int> targetPitches(const Event& src, const Passage& sourceBefore, const Passage& target)
{
    if (isRest(src)) {
        return {};
    }
    // A target note at the same position keeps its own pitches, unless the
    // source changed from a rest to a note there.
    const Event* t = eventAt(target, src.tick);
    const Event* before = eventAt(sourceBefore, src.tick);
    if (t && !isRest(*t) && before && !isRest(*before)) {
        return t->pitches;
    }
    const int interval = intervalNear(sourceBefore, target, src.tick);
    std::vector<int> out;
    for (int p : src.pitches) {
        out.push_back(p + interval);
    }
    return out;
}

void copyCategories(Event& dst, const Event* src, const Event* oldTarget, const SyncOptions& o)
{
    auto pick = [&](bool sync, auto member) {
        if (sync) {
            dst.*member = src ? src->*member : std::decay_t<decltype(dst.*member)>();
        } else {
            dst.*member = oldTarget ? oldTarget->*member : std::decay_t<decltype(dst.*member)>();
        }
    };
    pick(o.articulations, &Event::articulations);
    pick(o.dynamics, &Event::dynamic);
    pick(o.markings, &Event::markings);
}
}

Passage propagate(const Passage& sourceBefore, const Passage& sourceAfter, const Passage& target, const SyncOptions& options)
{
    Passage out;

    if (!options.rhythm) {
        // Keep the target's rhythm; copy the other chosen kinds from the source
        // note at the same position.
        for (const Event& t : target) {
            Event e = t;
            if (!t.detached && !isRest(t)) {
                const Event* s = eventAt(sourceAfter, t.tick);
                if (s && !isRest(*s)) {
                    copyCategories(e, s, &t, options);
                }
            }
            out.push_back(e);
        }
        return out;
    }

    // Rhythm follows the source, except where the target has detached events.
    std::vector<const Event*> detached;
    for (const Event& t : target) {
        if (t.detached) {
            detached.push_back(&t);
        }
    }

    for (const Event& s : sourceAfter) {
        const bool hidden = std::any_of(detached.begin(), detached.end(), [&](const Event* d) { return overlaps(*d, s); });
        if (hidden) {
            continue;
        }
        Event e;
        e.tick = s.tick;
        e.duration = s.duration;
        e.pitches = targetPitches(s, sourceBefore, target);
        const Event* oldTarget = eventAt(target, s.tick);
        if (oldTarget && oldTarget->detached) {
            oldTarget = nullptr;
        }
        if (!isRest(e)) {
            copyCategories(e, &s, oldTarget, options);
        }
        // Durations not synchronised: a note keeps its own length, as long as
        // it fits before the source's next note; the rest of the time is a rest
        if (!options.durations && !isRest(e) && oldTarget && !isRest(*oldTarget) && oldTarget->duration < s.duration) {
            e.duration = oldTarget->duration;
            out.push_back(e);
            Event filler;
            filler.tick = s.tick + oldTarget->duration;
            filler.duration = s.duration - oldTarget->duration;
            out.push_back(filler);
            continue;
        }
        out.push_back(e);
    }
    for (const Event* d : detached) {
        out.push_back(*d);
    }
    std::stable_sort(out.begin(), out.end(), [](const Event& a, const Event& b) { return a.tick < b.tick; });
    return out;
}
}
