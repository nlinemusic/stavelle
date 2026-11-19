/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 *
 * Model: the score's stored music, as objects in a persistent map (S1,
 * docs/14_full_engine_roadmap.md). It implements ChangeTarget, so every edit goes through
 * a je::edit::Transaction and is undoable with no per-type code.
 *
 * Snapshot: an immutable version of the whole score. Taking one is O(1); it can be read on
 * any thread while the model keeps changing (layout, playback and saving read snapshots).
 *
 * Rules for set(): an object is created by setting EXISTS to true (it starts with no fields)
 * and removed by setting EXISTS to false or absent. Setting a field of an object that isn't
 * there is a programming error (std::logic_error), because undo couldn't restore it.
 */
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "idmap.h"
#include "object.h"
#include "value.h"

namespace je::model {
class Snapshot
{
public:
    Snapshot() = default;

    const Object* find(Id id) const
    {
        const ObjectPtr* p = m_objects.find(id);
        return p ? p->get() : nullptr;
    }
    bool contains(Id id) const { return m_objects.contains(id); }
    size_t objectCount() const { return m_objects.size(); }
    Id score() const { return m_score; }
    uint64_t version() const { return m_version; }

    template<typename Fn>
    void forEachObject(Fn&& fn) const
    {
        m_objects.forEach([&fn](Id, const ObjectPtr& o) { fn(*o); });
    }

    //! A canonical text form of everything stored (objects sorted by id, fields by number),
    //! for exact comparisons in tests: equal dumps = equal content
    std::string dump() const;

    //! True if `other` is the very same version (no change in between)
    bool sameVersion(const Snapshot& other) const { return m_objects.sameVersion(other.m_objects); }

    const IdMap<ObjectPtr>& objects() const { return m_objects; }

private:
    friend class Model;
    IdMap<ObjectPtr> m_objects;
    Id m_score;
    uint64_t m_version = 0;
};

class Model : public ChangeTarget
{
public:
    Model() = default;

    // ChangeTarget
    Value get(Id object, Field field) const override;
    void set(Id object, Field field, const Value& value) override;
    std::vector<Field> fieldsOf(Id object) const override;

    const Object* find(Id id) const { return m_current.find(id); }
    Snapshot snapshot() const { return m_current; }

    //! The score object (set once, when the score is created)
    void setScore(Id score) { m_current.m_score = score; }
    Id score() const { return m_current.m_score; }

    //! Replaces the whole content (a file just read; not undoable). Objects must have distinct ids.
    void replaceAll(std::vector<Object> objects, Id score);

    //! Number of set() calls that changed something (each new version)
    uint64_t version() const { return m_current.m_version; }

private:
    Snapshot m_current;
};
}
