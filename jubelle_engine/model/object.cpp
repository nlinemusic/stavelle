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

Kind Object::kind() const
{
    return Kind(intOr(fields::KIND, int64_t(Kind::None)));
}

const Value& Object::get(Field field) const
{
    auto it = findField(m_props, field);
    return (it != m_props.end() && it->first == field) ? it->second : absent();
}

bool Object::has(Field field) const
{
    return !isAbsent(get(field));
}

Object Object::with(Field field, const Value& value) const
{
    Object copy = *this;
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
    return copy;
}

int64_t Object::intOr(Field field, int64_t fallback) const
{
    const Value& v = get(field);
    return std::holds_alternative<int64_t>(v) ? std::get<int64_t>(v) : fallback;
}

Fraction Object::fractionOr(Field field, const Fraction& fallback) const
{
    const Value& v = get(field);
    return std::holds_alternative<Fraction>(v) ? std::get<Fraction>(v) : fallback;
}

Id Object::idOr(Field field, Id fallback) const
{
    const Value& v = get(field);
    return std::holds_alternative<Id>(v) ? std::get<Id>(v) : fallback;
}

const IdList& Object::children() const
{
    const Value& v = get(fields::CHILDREN);
    return std::holds_alternative<IdList>(v) ? std::get<IdList>(v) : noChildren();
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
    default: break;
    }
    return field >= NAMED_BASE ? "named" : "unknown";
}
}
