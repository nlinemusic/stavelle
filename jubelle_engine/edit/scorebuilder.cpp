/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 */
#include "scorebuilder.h"

namespace F = je::model::fields;
using je::model::Kind;

namespace je::edit {
Id ScoreBuilder::newScore(Transaction& tx, const std::string& title)
{
    m_score = create(tx, m_ids, Id(), { Kind::Score, { { F::NAME, title } } });
    m_model.setScore(m_score);
    m_flow = create(tx, m_ids, m_score, { Kind::Flow, { { F::NAME, std::string("Flow 1") } } });
    return m_score;
}

void ScoreBuilder::addMeasures(Transaction& tx, int count, int num, int den)
{
    Fraction t;
    if (const model::Object* flow = m_model.find(m_flow)) {
        const auto& kids = flow->children();
        for (size_t i = kids.size(); i-- > 0;) {
            const model::Object* m = m_model.find(kids[i]);
            if (m && m->kind() == Kind::Measure) {
                t = m->fractionOr(F::TIME, Fraction()) + m->fractionOr(F::DURATION, Fraction());
                break;
            }
        }
    }
    const Fraction length(num, den);
    for (int i = 0; i < count; ++i) {
        create(tx, m_ids, m_flow, { Kind::Measure, { { F::TIME, t }, { F::DURATION, length },
                                        { F::TIMESIG_NUM, int64_t(num) }, { F::TIMESIG_DEN, int64_t(den) } } },
               Placement::ByTime);
        t += length;
    }
}

ScoreBuilder::PlayerIds ScoreBuilder::addPlayer(Transaction& tx, const std::string& name, const std::string& instrument,
                                                const std::string& family, int number, int transposeChromatic)
{
    PlayerIds out;
    std::vector<std::pair<Field, Value> > f { { F::NAME, name }, { F::INSTRUMENT, instrument }, { F::FAMILY, family } };
    if (number > 0) {
        f.push_back({ F::NUMBER, int64_t(number) });
    }
    if (transposeChromatic != 0) {
        f.push_back({ F::TRANSPOSE_CHROMATIC, int64_t(transposeChromatic) });
    }
    out.player = create(tx, m_ids, m_score, { Kind::Player, f });
    out.staff = create(tx, m_ids, out.player, { Kind::Staff, {} });
    out.voice = create(tx, m_ids, out.staff, { Kind::Voice, { { F::FLOW, m_flow }, { F::NUMBER, int64_t(0) } } });
    return out;
}

Id ScoreBuilder::addChord(Transaction& tx, Id voice, const Fraction& time, const Fraction& duration, const std::vector<int>& pitches)
{
    const Id chord = create(tx, m_ids, voice, { Kind::Chord, { { F::TIME, time }, { F::DURATION, duration } } }, Placement::ByTime);
    for (int p : pitches) {
        create(tx, m_ids, chord, { Kind::Note, { { F::PITCH, int64_t(p) } } });
    }
    return chord;
}

Id ScoreBuilder::addRest(Transaction& tx, Id voice, const Fraction& time, const Fraction& duration)
{
    return create(tx, m_ids, voice, { Kind::Rest, { { F::TIME, time }, { F::DURATION, duration } } }, Placement::ByTime);
}

std::vector<Id> ScoreBuilder::notesOf(Id chord) const
{
    std::vector<Id> out;
    if (const model::Object* c = m_model.find(chord)) {
        for (Id n : c->children()) {
            const model::Object* o = m_model.find(n);
            if (o && o->kind() == Kind::Note) {
                out.push_back(n);
            }
        }
    }
    return out;
}
}
