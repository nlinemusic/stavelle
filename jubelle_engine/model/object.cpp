/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 */
#include "object.h"

#include <algorithm>

namespace je::model {
namespace {
const Value& absent()
{
    static const Value none;
    return none;
}

const IdList& noChildren()
{
    static const IdList none;
    return none;
}

template<typename Props>
auto findField(Props& props, Field field)
{
    return std::lower_bound(props.begin(), props.end(), field,
                            [](const std::pair<Field, Value>& p, Field f) { return p.first < f; });
}
}

Object::Object(Id id, std::vector<std::pair<Field, Value> > properties)
    : m_id(id)
{
    for (auto it = properties.begin(); it != properties.end();) {
        if (it->first == fields::KIND || it->first == fields::PARENT) {
            if (it->first == fields::KIND) {
                m_kind = std::holds_alternative<int64_t>(it->second) ? Kind(std::get<int64_t>(it->second)) : Kind::None;
            } else {
                m_parent = std::holds_alternative<Id>(it->second) ? std::get<Id>(it->second) : Id();
            }
            it = properties.erase(it);
        } else {
            ++it;
        }
    }
    std::stable_sort(properties.begin(), properties.end(),
                     [](const std::pair<Field, Value>& a, const std::pair<Field, Value>& b) { return a.first < b.first; });
    for (auto& p : properties) {
        if (isAbsent(p.second)) {
            continue;
        }
        if (!m_props.empty() && m_props.back().first == p.first) {
            m_props.back().second = std::move(p.second);
        } else {
            m_props.push_back(std::move(p));
        }
    }
    m_props.shrink_to_fit();
}

Value Object::get(Field field) const
{
    if (field == fields::KIND) {
        return m_kind == Kind::None ? Value() : Value(int64_t(m_kind));
    }
    if (field == fields::PARENT) {
        return m_parent.isValid() ? Value(m_parent) : Value();
    }
    auto it = findField(m_props, field);
    return (it != m_props.end() && it->first == field) ? it->second : absent();
}

bool Object::has(Field field) const
{
    if (field == fields::KIND) {
        return m_kind != Kind::None;
    }
    if (field == fields::PARENT) {
        return m_parent.isValid();
    }
    auto it = findField(m_props, field);
    return it != m_props.end() && it->first == field;
}

std::string Object::text(Field field) const
{
    const Value v = get(field);
    return std::holds_alternative<Text>(v) ? std::get<Text>(v).str() : std::string();
}

Object Object::with(Field field, const Value& value) const
{
    Object copy = *this;
    if (field == fields::KIND) {
        copy.m_kind = std::holds_alternative<int64_t>(value) ? Kind(std::get<int64_t>(value)) : Kind::None;
        return copy;
    }
    if (field == fields::PARENT) {
        copy.m_parent = std::holds_alternative<Id>(value) ? std::get<Id>(value) : Id();
        return copy;
    }
    auto it = findField(copy.m_props, field);
    const bool present = it != copy.m_props.end() && it->first == field;
    if (isAbsent(value)) {
        if (present) {
            copy.m_props.erase(it);
        }
    } else if (present) {
        it->second = value;
    } else {
        copy.m_props.insert(it, { field, value });
    }
    copy.m_props.shrink_to_fit();
    return copy;
}

int64_t Object::intOr(Field field, int64_t fallback) const
{
    const Value v = get(field);
    return std::holds_alternative<int64_t>(v) ? std::get<int64_t>(v) : fallback;
}

Fraction Object::fractionOr(Field field, const Fraction& fallback) const
{
    auto it = findField(m_props, field);
    if (it == m_props.end() || it->first != field) {
        return fallback;
    }
    const Value& v = it->second;
    return std::holds_alternative<Fraction>(v) ? std::get<Fraction>(v) : fallback;
}

Id Object::idOr(Field field, Id fallback) const
{
    const Value v = get(field);
    return std::holds_alternative<Id>(v) ? std::get<Id>(v) : fallback;
}

const IdList& Object::children() const
{
    auto it = findField(m_props, fields::CHILDREN);
    if (it == m_props.end() || it->first != fields::CHILDREN) {
        return noChildren();
    }
    return std::holds_alternative<IdList>(it->second) ? std::get<IdList>(it->second) : noChildren();
}

std::string_view kindName(Kind kind)
{
    switch (kind) {
    case Kind::None: return "none";
    case Kind::Score: return "score";
    case Kind::Flow: return "flow";
    case Kind::Measure: return "measure";
    case Kind::FlowItem: return "flowItem";
    case Kind::Player: return "player";
    case Kind::Staff: return "staff";
    case Kind::Voice: return "voice";
    case Kind::Chord: return "chord";
    case Kind::Rest: return "rest";
    case Kind::Note: return "note";
    case Kind::Tuplet: return "tuplet";
    case Kind::Attachment: return "attachment";
    case Kind::Spanner: return "spanner";
    case Kind::Layout: return "layout";
    }
    return "unknown";
}

std::string_view fieldName(Field field)
{
    using namespace fields;
    switch (field) {
    case EXISTS: return "exists";
    case KIND: return "kind";
    case PARENT: return "parent";
    case CHILDREN: return "children";
    case TIME: return "time";
    case DURATION: return "duration";
    case FLOW: return "flow";
    case NAME: return "name";
    case SHORT_NAME: return "shortName";
    case NUMBER: return "number";
    case SUBKIND: return "subkind";
    case TIMESIG_NUM: return "timesigNum";
    case TIMESIG_DEN: return "timesigDen";
    case INSTRUMENT: return "instrument";
    case FAMILY: return "family";
    case TRANSPOSE_CHROMATIC: return "transposeChromatic";
    case TRANSPOSE_DIATONIC: return "transposeDiatonic";
    case PITCH: return "pitch";
    case TPC: return "tpc";
    case TPC2: return "tpc2";
    case TIE_TO: return "tieTo";
    case DOTS: return "dots";
    case TUPLET: return "tuplet";
    case GRACE: return "grace";
    case RATIO_NUM: return "ratioNum";
    case RATIO_DEN: return "ratioDen";
    case ANCHOR: return "anchor";
    case END_ANCHOR: return "endAnchor";
    case TEXT: return "text";
    case INT_VALUE: return "intValue";
    case REAL_VALUE: return "realValue";
    case VERSE: return "verse";
    case SYLLABIC: return "syllabic";
    case BASE_DURATION: return "baseDuration";
    case GRACE_TYPE: return "graceType";
    case MEASURE_REST: return "measureRest";
    case EXPLICIT_DURATION: return "explicitDuration";
    case STRETCH: return "stretch";
    case REPEAT_START: return "repeatStart";
    case REPEAT_END: return "repeatEnd";
    case IRREGULAR: return "irregular";
    case BREAK: return "break";
    case PLAYERS: return "players";
    case ORIGIN_EID: return "originEid";
    case TAG: return "tag";
    case VOICE_INDEX: return "voiceIndex";
    case SEQ: return "seq";
    case AT_END: return "atEnd";
    case HOME_STAFF: return "homeStaff";
    case STYLE: return "style";
    case META: return "meta";
    case ARCHIVE_EXTRAS: return "archiveExtras";
    case MUSESCORE_INSTRUMENT: return "museScoreInstrument";
    case DETAILS: return "details";
    case DETAILS_KEY: return "detailsKey";
    case DETAILS_2: return "details2";
    case SCORE_HEAD: return "scoreHead";
    case SCORE_PRE: return "scorePre";
    case SCORE_POST: return "scorePost";
    case LAYOUT_BREAKS: return "layoutBreaks";
    default: break;
    }
    return field >= NAMED_BASE ? "named" : "unknown";
}
}
