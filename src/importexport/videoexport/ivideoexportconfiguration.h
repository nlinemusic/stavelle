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
#ifndef MU_IMPORTEXPORT_IVIDEOEXPORTCONFIGURATION_H
#define MU_IMPORTEXPORT_IVIDEOEXPORTCONFIGURATION_H

#include <string>
#include <vector>
#include <optional>

#include "modularity/imoduleinterface.h"

#include <functional>
#include <string>
#include <vector>

#include "videoexporttypes.h"

namespace mu::iex::videoexport {
class IVideoExportConfiguration : MODULE_GLOBAL_INTERFACE
{
    INTERFACE_ID(IVideoExportConfiguration)

public:
    virtual ~IVideoExportConfiguration() = default;

    virtual ViewMode viewMode() const = 0;
    virtual void setViewMode(std::optional<ViewMode> viewMode) = 0;

    virtual bool showPiano() const = 0;
    virtual void setShowPiano(std::optional<bool> showPiano) = 0;
    virtual PianoPosition pianoPosition() const = 0;
    virtual void setPianoPosition(std::optional<PianoPosition> position) = 0;

    virtual std::string resolution() const = 0;
    virtual void setResolution(std::optional<std::string> resolution) = 0;
    virtual const std::vector<std::string>& availableResolutions() const = 0;

    virtual int fps() const = 0;
    virtual void setFps(std::optional<int> fps) = 0;

    virtual double leadingSec() const = 0;
    virtual void setLeadingSec(std::optional<double> leadingSec) = 0;

    virtual double trailingSec() const = 0;
    virtual void setTrailingSec(std::optional<double> trailingSec) = 0;

    //! Jubelle: the pages to show and their order, like "1-3, 5"; empty = every page
    virtual std::string pageList() const { return std::string(); }
    virtual void setPageList(std::optional<std::string>) {}
    //! Jubelle: what is wrong with a page list for a score of `pageCount` pages; empty = fine
    virtual std::string checkPageList(const std::string&, int) const { return std::string(); }

    //! Jubelle: the page sequence a page list gives, as (page, seconds) entries in playing order, for a score
    //! whose music lasts `totalSecs`; `pageAt(seconds)` gives the page on screen at a moment (1 = first page).
    //! Uses the same engine as the export, so the builder and the video always agree.
    struct SequenceEntry {
        int page = 0;
        double seconds = 0.0;
    };
    virtual std::vector<SequenceEntry> planPageSequence(const std::string&, int, double, const std::function<int(double)>&) const
    {
        return {};
    }

    //! Jubelle: draw MuseScore's playback cursor over the music (off by default)
    virtual bool showCursor() const { return false; }
    virtual void setShowCursor(std::optional<bool>) {}
    //! Jubelle: the video's own sound quality (AAC, kbit/s), separate from MP3 export
    virtual int audioBitrateKbps() const { return 256; }
    virtual void setAudioBitrateKbps(std::optional<int>) {}
    virtual const std::vector<int>& availableAudioBitratesKbps() const
    {
        static const std::vector<int> rates { 128, 192, 256, 320 };
        return rates;
    }
};
}

#endif // MU_IMPORTEXPORT_IVIDEOEXPORTCONFIGURATION_H
