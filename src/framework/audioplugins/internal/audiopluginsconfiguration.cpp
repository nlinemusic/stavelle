/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-CLA-applies
 *
 * MuseScore
 * Music Composition & Notation
 *
 * Copyright (C) 2024 MuseScore Limited and others
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
#include "audiopluginsconfiguration.h"

using namespace muse;
#include <fstream>

using namespace muse::audioplugins;

io::path_t AudioPluginsConfiguration::knownAudioPluginsFilePath() const
{
    return globalConfiguration()->userAppDataPath() + "/known_audio_plugins.json";
}

// Jubelle: one plugin ID per line in a small text file next to the known-plugins list

io::path_t AudioPluginsConfiguration::userDisabledFilePath() const
{
    return globalConfiguration()->userAppDataPath() + "/jubelle_disabled_audio_plugins.txt";
}

void AudioPluginsConfiguration::loadUserDisabled() const
{
    if (m_userDisabledLoaded) {
        return;
    }
    m_userDisabledLoaded = true;
    std::ifstream in(userDisabledFilePath().toStdString());
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (!line.empty()) {
            m_userDisabled.insert(line);
        }
    }
}

bool AudioPluginsConfiguration::isUserDisabled(const std::string& resourceId) const
{
    std::lock_guard lock(m_mutex);
    loadUserDisabled();
    return m_userDisabled.count(resourceId) > 0;
}

void AudioPluginsConfiguration::setUserDisabled(const std::string& resourceId, bool disabled)
{
    std::lock_guard lock(m_mutex);
    loadUserDisabled();
    if (disabled) {
        m_userDisabled.insert(resourceId);
    } else {
        m_userDisabled.erase(resourceId);
    }
    std::ofstream out(userDisabledFilePath().toStdString(), std::ios::trunc);
    for (const std::string& id : m_userDisabled) {
        out << id << "\n";
    }
}
