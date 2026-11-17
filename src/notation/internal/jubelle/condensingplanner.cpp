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
#include "condensingplanner.h"

#include <algorithm>

namespace mu::notation::condensing {
namespace {
bool isRest(const Event& e)
{
    return e.pitches.empty();
}

//! The sounding events only, with consecutive rests merged away.
PlayerPassage notesOnly(const PlayerPassage& p)
{
    PlayerPassage out;
    for (const Event& e : p) {
        if (!isRest(e)) {
            Event s = e;
            std::sort(s.pitches.begin(), s.pitches.end());
            out.push_back(s);
        }
    }
    return out;
}

bool sameRhythm(const PlayerPassage& a, const PlayerPassage& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].tick != b[i].tick || a[i].duration != b[i].duration) {
            return false;
        }
    }
    return true;
}

bool samePitches(const PlayerPassage& a, const PlayerPassage& b)
{
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].pitches != b[i].pitches) {
            return false;
        }
    }
    return true;
}

std::string playerLabel(int index)
{
    return std::to_string(index + 1) + ".";
}

//! Lowest pitch of player `upper` must never be below the highest pitch of
//! player `lower` at any moment both sound (no crossing voices).
bool voicesCross(const PlayerPassage& upper, const PlayerPassage& lower)
{
    for (const Event& u : upper) {
        for (const Event& l : lower) {
            const bool overlap = u.tick < l.tick + l.duration && l.tick < u.tick + u.duration;
            if (overlap && u.pitches.front() < l.pitches.back()) {
                return true;
            }
        }
    }
    return false;
}
}

Result decide(const std::vector<PlayerPassage>& players)
{
    Result r;
    if (players.size() < 2) {
        r.reason = "A condensing group needs at least two players.";
        return r;
    }

    std::vector<PlayerPassage> sounding;
    std::vector<int> playing;
    for (size_t i = 0; i < players.size(); ++i) {
        sounding.push_back(notesOnly(players[i]));
        if (!sounding.back().empty()) {
            playing.push_back(static_cast<int>(i));
        }
    }

    if (playing.empty()) {
        r.decision = Decision::AllRest;
        return r;
    }

    if (playing.size() == 1) {
        r.decision = Decision::OnlyOne;
        r.soloPlayer = playing.front();
        r.label = playerLabel(r.soloPlayer);
        return r;
    }

    // Everyone who plays has the same rhythm?
    bool allSameRhythm = true;
    bool allSamePitches = true;
    for (size_t k = 1; k < playing.size(); ++k) {
        const PlayerPassage& a = sounding[playing.front()];
        const PlayerPassage& b = sounding[playing[k]];
        if (!sameRhythm(a, b)) {
            allSameRhythm = false;
            allSamePitches = false;
            break;
        }
        if (!samePitches(a, b)) {
            allSamePitches = false;
        }
    }

    // Some players resting for the whole passage while the others play together:
    // label which ones play ("1.2." of three), otherwise "a2"/"a3" for everyone.
    const bool everyonePlays = playing.size() == players.size();

    if (allSameRhythm && allSamePitches) {
        r.decision = Decision::Unison;
        if (everyonePlays) {
            r.label = "a" + std::to_string(players.size());
        } else {
            for (int p : playing) {
                r.label += playerLabel(p);
            }
        }
        return r;
    }

    if (allSameRhythm) {
        // Chords: each player's notes must stay in order (player 1 on top), and
        // every player plays single notes, so each note can be told apart.
        for (int p : playing) {
            for (const Event& e : sounding[p]) {
                if (e.pitches.size() != 1) {
                    r.reason = "A player already plays chords here.";
                    return r;
                }
            }
        }
        for (size_t k = 1; k < playing.size(); ++k) {
            if (voicesCross(sounding[playing[k - 1]], sounding[playing[k]])) {
                r.reason = "The parts cross each other.";
                return r;
            }
        }
        r.decision = Decision::Chords;
        if (!everyonePlays) {
            for (int p : playing) {
                r.label += playerLabel(p);
            }
        }
        return r;
    }

    // Different rhythms: only two players fit, as two voices.
    if (playing.size() != 2) {
        r.reason = "More than two players with different rhythms.";
        return r;
    }
    const PlayerPassage& upper = sounding[playing[0]];
    const PlayerPassage& lower = sounding[playing[1]];
    for (const PlayerPassage* p : { &upper, &lower }) {
        for (const Event& e : *p) {
            if (e.pitches.size() != 1) {
                r.reason = "A player plays chords against a different rhythm.";
                return r;
            }
        }
    }
    if (voicesCross(upper, lower)) {
        r.reason = "The parts cross each other.";
        return r;
    }
    r.decision = Decision::TwoVoices;
    return r;
}
}
