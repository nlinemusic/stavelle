/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * MuseScore Studio
 * Music Composition & Notation
 *
 * Copyright (C) 2021 MuseScore Limited and others
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
#include "videoexportconfiguration.h"

#include "jubelle/videopages.h"

#include <algorithm>
#include <map>

using namespace mu::iex::videoexport;

static const ViewMode DEFAULT_VIEW_MODE = ViewMode::PageFull;
static const bool DEFAULT_SHOW_PIANO = false;
static const PianoPosition DEFAULT_PIANO_POSITION = PianoPosition::Bottom;
static const std::string DEFAULT_RESOLUTION = "1080p";
static const int DEFAULT_FPS = 24;
static const double DEFAULT_LEADING_SEC = 3.0;
// Jubelle: no "Made with MuseScore" end card on videos made with Jubelle (it can be switched on by giving a trailing time)
static const double DEFAULT_TRAILING_SECONDS = 0.0;

static const std::vector<std::string> AVAILABLE_RESOLUTIONS = { "2160p", "1440p", "1080p" };

ViewMode VideoExportConfiguration::viewMode() const
{
    return m_viewMode ? m_viewMode.value() : DEFAULT_VIEW_MODE;
}

void VideoExportConfiguration::setViewMode(std::optional<ViewMode> viewMode)
{
    m_viewMode = viewMode;
}

bool VideoExportConfiguration::showPiano() const
{
    return m_showPiano ? m_showPiano.value() : DEFAULT_SHOW_PIANO;
}

void VideoExportConfiguration::setShowPiano(std::optional<bool> showPiano)
{
    m_showPiano = showPiano;
}

PianoPosition VideoExportConfiguration::pianoPosition() const
{
    return m_pianoPosition ? m_pianoPosition.value() : DEFAULT_PIANO_POSITION;
}

void VideoExportConfiguration::setPianoPosition(std::optional<PianoPosition> position)
{
    m_pianoPosition = position;
}

std::string VideoExportConfiguration::resolution() const
{
    return m_resolution ? m_resolution.value() : DEFAULT_RESOLUTION;
}

void VideoExportConfiguration::setResolution(std::optional<std::string> resolution)
{
    m_resolution = resolution;
}

const std::vector<std::string>& VideoExportConfiguration::availableResolutions() const
{
    return AVAILABLE_RESOLUTIONS;
}

int VideoExportConfiguration::fps() const
{
    return m_fps ? m_fps.value() : DEFAULT_FPS;
}

void VideoExportConfiguration::setFps(std::optional<int> fps)
{
    m_fps = fps;
}

double VideoExportConfiguration::leadingSec() const
{
    return m_leadingSec ? m_leadingSec.value() : DEFAULT_LEADING_SEC;
}

void VideoExportConfiguration::setLeadingSec(std::optional<double> leadingSec)
{
    m_leadingSec = leadingSec;
}

double VideoExportConfiguration::trailingSec() const
{
    return m_trailingSec ? m_trailingSec.value() : DEFAULT_TRAILING_SECONDS;
}

void VideoExportConfiguration::setTrailingSec(std::optional<double> trailingSec)
{
    m_trailingSec = trailingSec;
}

std::string VideoExportConfiguration::checkPageList(const std::string& pages, int pageCount) const
{
    const jubelle::PageList list = jubelle::parsePageList(pages, pageCount);
    return list.ok ? std::string() : list.error;
}

int VideoExportConfiguration::audioBitrateKbps() const
{
    const int kbps = m_audioBitrateKbps.value_or(256);
    const std::vector<int>& rates = availableAudioBitratesKbps();
    return std::find(rates.begin(), rates.end(), kbps) != rates.end() ? kbps : 256;
}

std::vector<IVideoExportConfiguration::SequenceEntry> VideoExportConfiguration::planPageSequence(
    const std::string& pages, int pageCount, double totalSecs, const std::function<int(double)>& pageAt) const
{
    // One entry per page in the list (or per page with music, for "every page"), with the length of music
    // it shows. Follows the same rules as planSegments: a page listed once shows all its visits; a page
    // listed more than once shows its next visit each time.
    std::vector<SequenceEntry> out;
    const jubelle::PageList list = jubelle::parsePageList(pages, pageCount);
    if (!list.ok || totalSecs <= 0.0) {
        return out;
    }
    const std::vector<jubelle::PageVisit> visits = jubelle::pageVisits(totalSecs, 0.1, pageAt);
    auto visitsOf = [&visits](int page) {
        std::vector<double> lengths;
        for (const jubelle::PageVisit& v : visits) {
            if (v.page == page) {
                lengths.push_back(v.end - v.start);
            }
        }
        return lengths;
    };
    if (list.pages.empty()) {
        std::vector<int> order;
        for (const jubelle::PageVisit& v : visits) {
            if (std::find(order.begin(), order.end(), v.page) == order.end()) {
                order.push_back(v.page);
            }
        }
        for (int page : order) {
            double total = 0.0;
            for (double l : visitsOf(page)) {
                total += l;
            }
            out.push_back({ page, total });
        }
        return out;
    }
    std::map<int, size_t> used;
    for (int page : list.pages) {
        const std::vector<double> mine = visitsOf(page);
        if (mine.empty()) {
            out.push_back({ page, 0.0 });   // no music on this page: the video skips it
            continue;
        }
        if (std::count(list.pages.begin(), list.pages.end(), page) == 1) {
            double total = 0.0;
            for (double l : mine) {
                total += l;
            }
            out.push_back({ page, total });
            continue;
        }
        size_t& n = used[page];
        out.push_back({ page, mine[n < mine.size() ? n : 0] });
        ++n;
    }
    return out;
}
