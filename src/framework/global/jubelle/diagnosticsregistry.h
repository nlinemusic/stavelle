/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore).
 *
 * Report a Bug: where a bug report's automatic details come from.
 *
 * Any part of Jubelle can add a "diagnostic source": a named section with a
 * few label/value lines (the application adds its version and the system;
 * the notation engine adds which engine is in use). The bug report asks every
 * source when the report is made, so nothing is gathered in the background
 * and nothing costs anything until then.
 *
 * The engine section is a source like any other: today the notation module
 * registers "MuseScore Studio 4.7.7 engine (current)"; a future Jubelle engine
 * registers itself under the same id ("engine") and the report shows it,
 * without the bug report knowing anything about either engine.
 *
 * Plain C++ (no Qt), header only, so every module can use it.
 */
#pragma once

#include <algorithm>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace muse::jubelle {
struct DiagnosticItem {
    std::string label;
    std::string value;
};

struct DiagnosticSection {
    std::string id;       // e.g. "engine"
    std::string title;    // e.g. "Notation engine"
    std::vector<DiagnosticItem> items;
    int order = 100;      // lower comes first in the report
};

class DiagnosticsRegistry
{
public:
    using Collector = std::function<DiagnosticSection()>;

    static DiagnosticsRegistry& instance()
    {
        static DiagnosticsRegistry r;
        return r;
    }

    //! Adds a source, or replaces the source with the same id
    void add(const std::string& id, Collector collector)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto& s : m_sources) {
            if (s.first == id) {
                s.second = std::move(collector);
                return;
            }
        }
        m_sources.emplace_back(id, std::move(collector));
    }

    void remove(const std::string& id)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_sources.begin(); it != m_sources.end(); ++it) {
            if (it->first == id) {
                m_sources.erase(it);
                return;
            }
        }
    }

    bool has(const std::string& id) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (const auto& s : m_sources) {
            if (s.first == id) {
                return true;
            }
        }
        return false;
    }

    //! Asks every source now. A source that fails is reported as such and
    //! never stops the report.
    std::vector<DiagnosticSection> collectAll() const
    {
        std::vector<std::pair<std::string, Collector> > sources;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            sources = m_sources;
        }
        std::vector<DiagnosticSection> out;
        for (const auto& s : sources) {
            DiagnosticSection section;
            try {
                section = s.second ? s.second() : DiagnosticSection();
            } catch (...) {
                section.items = { { "Error", "this information could not be read" } };
            }
            if (section.id.empty()) {
                section.id = s.first;
            }
            if (section.title.empty()) {
                section.title = s.first;
            }
            out.push_back(std::move(section));
        }
        std::stable_sort(out.begin(), out.end(), [](const DiagnosticSection& a, const DiagnosticSection& b) {
            return a.order < b.order;
        });
        return out;
    }

    //! For tests
    void clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_sources.clear();
    }

private:
    mutable std::mutex m_mutex;
    std::vector<std::pair<std::string, Collector> > m_sources;
};
}
