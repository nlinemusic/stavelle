/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 */
#include "model.h"

#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <vector>

namespace je::model {
namespace {
void writeValue(std::ostringstream& out, const Value& v)
{
    struct Visitor {
        std::ostringstream& out;
        void operator()(std::monostate) const { out << "-"; }
        void operator()(bool b) const { out << (b ? "true" : "false"); }
        void operator()(int64_t i) const { out << i; }
        void operator()(double d) const { out.precision(17); out << d; }
        void operator()(const std::string& s) const { out << '"' << s << '"'; }
        void operator()(const Fraction& f) const { out << f.toString(); }
        void operator()(Id id) const { out << '#' << id.toString(); }
        void operator()(const IdList& list) const
        {
            out << '[';
            for (size_t i = 0; i < list.size(); ++i) {
                out << (i ? " " : "") << '#' << list[i].toString();
            }
            out << ']';
        }
    };
    std::visit(Visitor { out }, v);
}
}

std::string Snapshot::dump() const
{
    std::vector<const Object*> all;
    all.reserve(m_objects.size());
    m_objects.forEach([&all](Id, const ObjectPtr& o) { all.push_back(o.get()); });
    std::sort(all.begin(), all.end(), [](const Object* a, const Object* b) { return a->id() < b->id(); });

    std::ostringstream out;
    out << "score #" << m_score.toString() << '\n';
    for (const Object* o : all) {
        out << '#' << o->id().toString() << ' ' << kindName(o->kind());
        for (const auto& [field, value] : o->properties()) {
            out << ' ' << fieldName(field) << '(' << field << ")=";
            writeValue(out, value);
        }
        out << '\n';
    }
    return out.str();
}

std::vector<Field> Model::fieldsOf(Id object) const
{
    std::vector<Field> out;
    if (const Object* o = m_current.find(object)) {
        for (const auto& p : o->properties()) {
            out.push_back(p.first);
        }
    }
    return out;
}

Value Model::get(Id object, Field field) const
{
    const Object* o = m_current.find(object);
    if (field == fields::EXISTS) {
        return o ? Value(true) : Value();
    }
    return o ? o->get(field) : Value();
}

void Model::set(Id object, Field field, const Value& value)
{
    if (!object.isValid()) {
        throw std::logic_error("je::model: invalid object id");
    }
    const ObjectPtr* existing = m_current.m_objects.find(object);

    if (field == fields::EXISTS) {
        const bool wanted = std::holds_alternative<bool>(value) && std::get<bool>(value);
        if (wanted && !existing) {
            m_current.m_objects = m_current.m_objects.set(object, std::make_shared<const Object>(object));
            ++m_current.m_version;
        } else if (!wanted && existing) {
            m_current.m_objects = m_current.m_objects.erase(object);
            ++m_current.m_version;
        }
        return;
    }

    if (!existing) {
        throw std::logic_error("je::model: setting a field of an object that doesn't exist");
    }
    if ((*existing)->get(field) == value) {
        return;
    }
    m_current.m_objects = m_current.m_objects.set(object, std::make_shared<const Object>((*existing)->with(field, value)));
    ++m_current.m_version;
}
}
