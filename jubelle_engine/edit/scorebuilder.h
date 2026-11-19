/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * ScoreBuilder: builds scores through transactions (tests, benchmarks, and later the file
 * reader). Every call is one undoable transaction unless a transaction is passed in.
 */
#pragma once

#include <string>
#include <vector>

#include "changeset.h"
#include "structure.h"
#include "model/model.h"

namespace je::edit {
class ScoreBuilder
{
public:
    ScoreBuilder(model::Model& model, IdGenerator& ids)
        : m_model(model), m_ids(ids) {}

    //! A new empty score with one flow; returns the score id
    Id newScore(Transaction& tx, const std::string& title = "Untitled");
    Id flow() const { return m_flow; }

    //! `count` measures of num/den at the end of the flow
    void addMeasures(Transaction& tx, int count, int num = 4, int den = 4);

    struct PlayerIds {
        Id player;
        Id staff;
        Id voice;   // voice 1 in the flow
    };
    PlayerIds addPlayer(Transaction& tx, const std::string& name, const std::string& instrument,
                        const std::string& family, int number = 0, int transposeChromatic = 0);

    //! A chord with these sounding pitches (one note each) in a voice
    Id addChord(Transaction& tx, Id voice, const Fraction& time, const Fraction& duration, const std::vector<int>& pitches);
    Id addRest(Transaction& tx, Id voice, const Fraction& time, const Fraction& duration);

    //! The notes of a chord, in order
    std::vector<Id> notesOf(Id chord) const;

private:
    model::Model& m_model;
    IdGenerator& m_ids;
    Id m_score;
    Id m_flow;
};
}
