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
#include "videowriter.h"

#include <algorithm>
#include <vector>

#include <cmath>

#include <QPainter>
#include <QThread>

#include "global/concurrency/concurrent.h"
#include "io/buffer.h"
#include "io/file.h"

#include "engraving/dom/page.h"
#include "engraving/dom/repeatlist.h"
#include "engraving/dom/masterscore.h"

#include "notation/imasternotation.h"
#include "notation/notationtypes.h"

#include "notationscene/qml/MuseScore/NotationScene/playbackcursor.h"

#include "draw/fontmetrics.h"

#include "jubelle/audiosplice.h"

#include "defer.h"
#include "log.h"

using namespace mu::iex::videoexport;
using namespace mu::project;
using namespace mu::notation;
using namespace muse::draw;
using namespace muse::midi;

static muse::String notationTitle(const INotationPtr notation)
{
    muse::String title;
    mu::engraving::Score* score = notation->elements()->msScore();
    mu::engraving::Score* masterScore = notation->masterNotation()->masterScore();

    if (const mu::engraving::Text* text = score->getText(mu::engraving::TextStyleType::TITLE)) {
        title = text->plainText();
    }

    if (title.isEmpty()) {
        if (const mu::engraving::Text* text = masterScore->getText(mu::engraving::TextStyleType::TITLE)) {
            title = text->plainText();
        }
    }

    if (title.isEmpty()) {
        title = masterScore->metaTag(u"workTitle");
    }

    return title;
}

static muse::String notationSubtitle(const INotationPtr notation)
{
    if (notation->isMaster()) {
        return muse::String();
    }

    return notation->name();
}

std::vector<INotationWriter::UnitType> VideoWriter::supportedUnitTypes() const
{
    return { UnitType::PER_PART };
}

bool VideoWriter::supportsUnitType(UnitType unitType) const
{
    std::vector<UnitType> unitTypes = supportedUnitTypes();
    return std::find(unitTypes.cbegin(), unitTypes.cend(), unitType) != unitTypes.cend();
}

muse::Ret VideoWriter::write(INotationPtr notation, muse::io::IODevice& device, const Options& options)
{
    std::string filePath = device.meta("file_path");
    IF_ASSERT_FAILED(!filePath.empty()) {
        return make_ret(muse::Ret::Code::InternalError);
    }

    bool withAudio = muse::value(options, OptionKey::WITH_AUDIO, muse::Val(true)).toBool();

    Config cfg = makeConfig();

    // Jubelle: check the page list against the score's pages first
    if (!cfg.pageList.empty()) {
        const int pageCount = int(notation->elements()->pages().size());
        const jubelle::PageList list = jubelle::parsePageList(cfg.pageList, pageCount);
        if (!list.ok) {
            return make_ret(muse::Ret::Code::UnknownError, list.error);
        }
    }

    muse::io::path_t finalPath(filePath);
    // Jubelle: the sound is rendered as WAV, then cut to the shown pages with crossfades and encoded as AAC
    muse::io::path_t tempWavPath = finalPath + ".tmp_audio.wav";
    muse::io::path_t tempAudioPath = finalPath + ".tmp_audio.aac";

    auto encoder = videoEncodeResolver()->currentVideoEncoder();

    muse::media::IVideoEncoder::Options encoderOptions;
    encoderOptions.format = "mp4";
    encoderOptions.width = cfg.width;
    encoderOptions.height = cfg.height;
    encoderOptions.bitrate = cfg.bitrate;
    encoderOptions.gop = cfg.fps / 2;
    encoderOptions.fps = cfg.fps;

    if (!encoder->open(finalPath, encoderOptions)) {
        LOGE() << "failed to open video encoder";
        return make_ret(muse::Ret::Code::UnknownError);
    }

    // The video part counts as finished until it starts, so cancelling during the sound doesn't wait for it
    m_isCompleted = true;
    m_audioCompleted = false;
    m_abort = false;
    m_writeRet = muse::Ret();
    m_audioRet = muse::Ret();

    // Jubelle: render the sound first and only then lay the score out for the video. Before, both ran
    // at once, so the score was being re-laid out (page size, margins) while its sound was rendered.
    if (withAudio) {
        startAudioExport(notation, tempWavPath, cfg);
        while (!m_audioCompleted) {
            application()->processEvents();
            QThread::yieldCurrentThread();
        }
        if (m_audioWriter) {
            m_audioWriter->progress()->finished().disconnect(this);
            m_audioWriter = nullptr;
        }
    } else {
        m_audioCompleted = true;
        m_audioRet = muse::make_ok();
    }

    if (m_abort) {
        encoder->close();
        muse::io::File::remove(tempWavPath);
        return make_ret(muse::Ret::Code::Cancel);
    }

    m_isCompleted = false;
    startVideoExport(encoder, notation, cfg);

    while (!m_isCompleted || !m_audioCompleted) {
        application()->processEvents();
        QThread::yieldCurrentThread();
    }

    if (m_audioWriter) {
        m_audioWriter->progress()->finished().disconnect(this);
        m_audioWriter = nullptr;
    }

    muse::Ret result = m_writeRet;

    encoder->finishEncode();

    // Release the device's file handle before add audio replaces the file
    device.close();

    if (withAudio) {
        if (result && m_audioRet) {
            // Jubelle: the AAC is already cut to the shown pages, so it is added whole
            encoder->setAudioSegments({});
            if (!encodeVideoAudio(encoder, tempWavPath, tempAudioPath, cfg, notation)) {
                result = make_ret(muse::Ret::Code::UnknownError);
            } else if (!encoder->addAudio(tempAudioPath)) {
                result = make_ret(muse::Ret::Code::UnknownError);
            }
        } else if (!result) {
            // keep video error
        } else {
            result = m_audioRet;
        }

        muse::io::File::remove(tempAudioPath);
        muse::io::File::remove(tempWavPath);
    }

    encoder->close();

    return result;
}

VideoWriter::Config VideoWriter::makeConfig() const
{
    Config cfg;

    cfg.fps = configuration()->fps();

    std::string resolution = configuration()->resolution();
    if (resolution == "2160p") {
        cfg.width = 3840;
        cfg.height = 2160;
    } else if (resolution == "1440p") {
        cfg.width = 2560;
        cfg.height = 1440;
    } else if (resolution == "1080p") {
        cfg.width = 1920;
        cfg.height = 1080;
    } else if (resolution == "720p") {
        cfg.width = 1280;
        cfg.height = 720;
    } else if (resolution == "480p") {
        cfg.width = 854;
        cfg.height = 480;
    } else if (resolution == "360p") {
        cfg.width = 640;
        cfg.height = 360;
    } else {
        cfg.width = 1920;
        cfg.height = 1080;
    }

    // compute bitrate according to Google recommended settings
    // https://support.google.com/youtube/answer/1722171?hl=en
    float br = 8;
    if (cfg.height == 2160) {
        br = cfg.fps < 35 ? 40 : 60;
    } else if (cfg.height == 1440) {
        br = cfg.fps < 35 ? 16 : 24;
    } else if (cfg.height == 1080) {
        br = cfg.fps < 35 ? 10 : 15;
    } else if (cfg.height == 720) {
        br = cfg.fps < 35 ? 5 : 7.5;
    } else if (cfg.height == 480) {
        br = cfg.fps < 35 ? 2.5 : 4;
    } else if (cfg.height == 360) {
        br = cfg.fps < 35 ? 1 : 1.5;
    }
    cfg.bitrate = int(br * 1000000);

    cfg.leadingSec = configuration()->leadingSec();
    cfg.trailingSec = configuration()->trailingSec();

    cfg.viewMode = configuration()->viewMode();
    cfg.pageList = configuration()->pageList();
    cfg.showCursor = configuration()->showCursor();
    cfg.audioBitrateKbps = configuration()->audioBitrateKbps();

    return cfg;
}

void VideoWriter::startVideoExport(muse::media::IVideoEncoderPtr encoder, INotationPtr notation, const Config& cfg)
{
    muse::Concurrent::run([this, encoder, notation, cfg]() {
        doGenerate(encoder, notation, cfg);
    });
}

void VideoWriter::startAudioExport(INotationPtr notation, const muse::io::path_t& audioPath, const Config& cfg)
{
    m_audioWriter = writers()->writer("wav");
    if (!m_audioWriter) {
        LOGE() << "wav writer not found";
        m_audioRet = make_ret(muse::Ret::Code::InternalError);
        m_audioCompleted = true;
        return;
    }

    m_audioWriter->progress()->finished().onReceive(this, [this](const muse::ProgressResult& res) {
        m_audioRet = res.ret;
        m_audioCompleted = true;
    });

    Options audioOpts;
    audioOpts[OptionKey::WAIT_FOR_COMPLETION] = muse::Val(false);
    audioOpts[OptionKey::LEADING_SILENCE_SEC] = muse::Val(static_cast<double>(cfg.leadingSec));
    audioOpts[OptionKey::TRAILING_SILENCE_SEC] = muse::Val(static_cast<double>(cfg.trailingSec));

    muse::io::Buffer audioDevice;
    audioDevice.setMeta("file_path", audioPath.toStdString());
    audioDevice.open(muse::io::IODevice::WriteOnly);

    m_audioWriter->write(notation, audioDevice, audioOpts);
}

muse::Ret VideoWriter::writeList(const INotationPtrList&, muse::io::IODevice&, const Options&)
{
    NOT_SUPPORTED;
    return make_ret(muse::Ret::Code::NotSupported);
}

muse::Progress* VideoWriter::progress()
{
    return &m_progress;
}

void VideoWriter::abort()
{
    m_abort = true;

    if (m_audioWriter) {
        m_audioWriter->abort();
    }

    // Wait abort completion
    while (!m_isCompleted || !m_audioCompleted) {
        application()->processEvents();
        QThread::yieldCurrentThread();
    }
}

std::optional<VideoWriter::ScoreRestoreData> VideoWriter::prepareScore(INotationPtr notation, Config& config)
{
    ScoreRestoreData result;
    engraving::Score* score = notation->elements()->msScore();

    result.style = score->style();

    result.layoutMode = score->layoutMode();

    result.showFrames = score->showFrames();
    result.showInstrumentNames = score->showInstrumentNames();
    result.showInvisible = score->isShowInvisible();
    result.showPageborders = score->showPageborders();
    result.showUnprintable = score->showUnprintable();
    result.showVBox = score->layoutOptions().isShowVBox;

    score->setLayoutMode(engraving::LayoutMode::PAGE);

    score->setShowFrames(false);
    score->setShowInvisible(false);
    score->setShowPageborders(false);
    score->setShowUnprintable(false);

    if (config.viewMode == ViewMode::Flexible) {
        score->setShowInstrumentNames(false);
        score->setShowVBox(false);
    }

    score->doLayout();

    PageList pages = notation->elements()->pages();
    if (pages.empty()) {
        LOGE() << "No pages";
        restoreScore(notation, result);
        return std::nullopt;
    }

    const Page* page = pages.front();

    if (config.viewMode == ViewMode::PageFull) {
        double scaleX = config.width / page->width();
        double scaleY = config.height / page->height();
        double scale = std::min(scaleX, scaleY);
        config.canvasDpi = scale * engraving::DPI;
        config.moveToCenter = muse::PointF(0.5 * (config.width / scale - page->width()), 0.5 * (config.height / scale - page->height()));
        return result;
    }

    if (score->staves().size() > 3) {
        //! NOTE: Calculate the dpi to display all page elements
        double originalPageHeight = page->height() - score->style().styleD(engraving::Sid::pageOddTopMargin)
                                    - score->style().styleD(engraving::Sid::pageOddBottomMargin);
        double margin = 100.0;
        double ttboxHeight = originalPageHeight + margin * 2;
        double scale = config.height / ttboxHeight;
        config.canvasDpi = scale * engraving::DPI;
    }

    score->style().set(engraving::Sid::pageHeight, config.height / config.canvasDpi);
    score->style().set(engraving::Sid::pageWidth, config.width / config.canvasDpi);
    score->style().set(engraving::Sid::pagePrintableWidth, score->style().styleD(engraving::Sid::pageWidth)
                       - score->style().styleD(engraving::Sid::pageOddLeftMargin)
                       - score->style().styleD(engraving::Sid::pageEvenLeftMargin));

    score->style().set(engraving::Sid::pageEvenTopMargin, 0.0);
    score->style().set(engraving::Sid::pageEvenBottomMargin, 0.0);
    score->style().set(engraving::Sid::pageOddTopMargin, 0.0);
    score->style().set(engraving::Sid::pageOddBottomMargin, 0.0);
    score->style().set(engraving::Sid::pageTwosided, false);
    score->style().set(engraving::Sid::showHeader, false);
    score->style().set(engraving::Sid::showFooter, false);

    score->style().set(engraving::Sid::minSystemDistance, engraving::Spatium(10));
    score->style().set(engraving::Sid::maxSystemDistance, engraving::Spatium(10));
    score->style().set(engraving::Sid::staffLowerBorder, engraving::Spatium(5));
    score->style().set(engraving::Sid::staffUpperBorder, engraving::Spatium(7));

    score->setLayoutAll();
    score->doLayout();

    return result;
}

void VideoWriter::restoreScore(INotationPtr notation, const ScoreRestoreData& data)
{
    engraving::Score* score = notation->elements()->msScore();

    score->style() = data.style;

    score->setShowFrames(data.showFrames);
    score->setShowInstrumentNames(data.showInstrumentNames);
    score->setShowInvisible(data.showInvisible);
    score->setShowPageborders(data.showPageborders);
    score->setShowUnprintable(data.showUnprintable);
    score->setShowVBox(data.showVBox);

    score->setLayoutMode(data.layoutMode);

    score->setLayoutAll();
    score->update();
}

void VideoWriter::doGenerate(muse::media::IVideoEncoderPtr encoder, INotationPtr notation, const Config& config)
{
    Config actualConfig = config;
    auto restoreData = prepareScore(notation, actualConfig);
    if (!restoreData) {
        m_writeRet = make_ret(muse::Ret::Code::UnknownError);
        m_isCompleted = true;
        return;
    }

    DEFER {
        restoreScore(notation, restoreData.value());
        m_isCompleted = true;
    };

    // Setup painting
    QImage frame(actualConfig.width, actualConfig.height, QImage::Format_RGB32);
    frame.setDotsPerMeterX(std::lrint((actualConfig.canvasDpi * 1000) / engraving::INCH));
    frame.setDotsPerMeterY(std::lrint((actualConfig.canvasDpi * 1000) / engraving::INCH));

    QPainter qp(&frame);
    qp.setRenderHint(QPainter::Antialiasing, true);
    qp.setRenderHint(QPainter::TextAntialiasing, true);

    Painter painter(&qp, "video_writer");

    // Setup duration
    INotationPlaybackPtr playback = notation->masterNotation()->playback();
    float totalPlayTimeSec = playback->totalPlayTime();

    // Jubelle: which pieces of the music to show, from the pages as laid out for the video
    m_segments.clear();
    m_usePageList = false;
    {
        PageList laidOut = notation->elements()->pages();
        const jubelle::PageList list = jubelle::parsePageList(actualConfig.pageList, int(laidOut.size()));
        if (list.ok && !list.pages.empty()) {
            auto pageAt = [&](double secs) -> int {
                const tick_t tick = playback->secToTick(secs);
                for (const Page* p : laidOut) {
                    if (tick < static_cast<tick_t>(p->endTick().ticks())) {
                        return p->firstMeasure() ? int(p->pageNumber()) + 1 : 0;
                    }
                }
                return 0;
            };
            const double step = 1.0 / std::max(1, actualConfig.fps);
            m_segments = jubelle::planSegments(jubelle::pageVisits(totalPlayTimeSec, step, pageAt), list);
            m_usePageList = true;
            totalPlayTimeSec = float(jubelle::totalLength(m_segments));
        }
    }

    int leadingFrameCount = static_cast<int>(actualConfig.leadingSec * actualConfig.fps);
    int scoreFrameCount = static_cast<int>(totalPlayTimeSec * actualConfig.fps);
    int trailingFrameCount = static_cast<int>(actualConfig.trailingSec * actualConfig.fps);
    int totalFrameCount = leadingFrameCount + scoreFrameCount + trailingFrameCount;
    LOGI() << "totalPlayTime: " << totalPlayTimeSec << " sec" << " frame count " << totalFrameCount;

    m_progress.start();

    // Add score title
    if (!generateLeadingFrames(encoder, notation, painter, frame, actualConfig, totalFrameCount)) {
        return;
    }

    // Add score frames
    if (!generateScoreFrames(encoder, notation, painter, frame, actualConfig, totalPlayTimeSec, leadingFrameCount, totalFrameCount)) {
        return;
    }

    // Add the end card (off in Jubelle unless an end time is given)
    if (!generateTrailingFrames(encoder, actualConfig)) {
        return;
    }

    m_writeRet = muse::make_ok();
    m_progress.finish(muse::make_ok());
}

bool VideoWriter::generateLeadingFrames(muse::media::IVideoEncoderPtr encoder, INotationPtr notation,
                                        Painter& painter, QImage& frame,
                                        const Config& config, int totalFrameCount)
{
    int leadingFrameCount = static_cast<int>(config.leadingSec * config.fps);
    if (leadingFrameCount <= 0) {
        return true;
    }

    // Jubelle: no title screen. The lead-in is the first page shown, still and silent, on white paper.
    const Page* first = firstShownPage(notation);
    muse::RectF frameRect = muse::RectF::fromQRectF(QRectF(frame.rect()));

    for (int f = 0; f < leadingFrameCount; f++) {
        if (m_abort) {
            m_writeRet = make_ret(muse::Ret::Code::Cancel);
            m_progress.finish(m_writeRet);
            return false;
        }

        m_progress.progress(f, totalFrameCount);

        if (first) {
            paintPage(notation, first, painter, frame, config);
        } else {
            painter.fillRect(frameRect, Color::WHITE);
        }

        encoder->encodeImage(frame);
    }

    return true;
}

const Page* VideoWriter::firstShownPage(INotationPtr notation) const
{
    PageList pages = notation->elements()->pages();
    if (m_usePageList && !m_segments.empty()) {
        const size_t idx = size_t(m_segments.front().page - 1);
        if (idx < pages.size()) {
            return pages[idx];
        }
    }
    for (const Page* p : pages) {
        if (p->firstMeasure()) {
            return p;
        }
    }
    return pages.empty() ? nullptr : pages.front();
}

void VideoWriter::paintPage(INotationPtr notation, const Page* page, Painter& painter, QImage& frame, const Config& config) const
{
    muse::RectF frameRect = muse::RectF::fromQRectF(QRectF(frame.rect()));
    // White paper across the whole frame, as in a PDF viewer; never black bars
    painter.fillRect(frameRect, Color::WHITE);

    INotationPainting::Options opt;
    opt.fromPage = static_cast<int>(page->pageNumber());
    opt.toPage = opt.fromPage;
    opt.deviceDpi = config.canvasDpi;

    painter.save();
    painter.translate(config.moveToCenter);
    notation->painting()->paintPrint(&painter, opt);
    painter.restore();
}

bool VideoWriter::generateTrailingFrames(muse::media::IVideoEncoderPtr encoder, const Config& config)
{
    int trailingFrameCount = static_cast<int>(config.trailingSec * config.fps);
    if (trailingFrameCount <= 0) {
        return true;
    }

    static const muse::io::path_t RESOURCE_PATH = ":/videoexport/internal/resources/video_made_with.mp4";

    muse::io::File resource(RESOURCE_PATH);
    if (!resource.open(muse::io::File::ReadOnly)) {
        return true;
    }

    muse::ByteArray videoData = resource.readAll();
    resource.close();

    encoder->encodeVideo(videoData, trailingFrameCount);

    return true;
}

bool VideoWriter::generateScoreFrames(muse::media::IVideoEncoderPtr encoder, INotationPtr notation,
                                      Painter& painter, QImage& frame,
                                      const Config& config, float totalPlayTimeSec,
                                      int leadingFrameCount, int totalFrameCount)
{
    int scoreFrameCount = static_cast<int>(totalPlayTimeSec * config.fps);
    if (scoreFrameCount <= 0) {
        return true;
    }

    PageList pages = notation->elements()->pages();
    INotationPlaybackPtr playback = notation->masterNotation()->playback();

    auto pageByTick = [](const PageList& pages, tick_t tick) -> const Page* {
        for (const Page* p : pages) {
            if (tick < static_cast<tick_t>(p->endTick().ticks())) {
                return p;
            }
        }
        return nullptr;
    };

    const Color CURSOR_COLOR = Color(2, 109, 203, 127);

    PlaybackCursor cursor(iocContext());
    cursor.setNotation(notation);

    for (int f = 0; f < scoreFrameCount; f++) {
        if (m_abort) {
            m_writeRet = make_ret(muse::Ret::Code::Cancel);
            m_progress.finish(m_writeRet);
            return false;
        }

        m_progress.progress(leadingFrameCount + f, totalFrameCount);

        float currentTimeSec = static_cast<float>(f) / config.fps;
        if (currentTimeSec > totalPlayTimeSec) {
            currentTimeSec = totalPlayTimeSec;
        }

        // Jubelle: with a page list, video time runs through the chosen pieces of music, each on its own page
        const Page* forcedPage = nullptr;
        if (m_usePageList) {
            double musicSecs = 0.0;
            const int seg = jubelle::segmentAt(m_segments, currentTimeSec, &musicSecs);
            if (seg < 0) {
                break;
            }
            currentTimeSec = float(musicSecs);
            const size_t idx = size_t(m_segments[size_t(seg)].page - 1);
            forcedPage = idx < pages.size() ? pages[idx] : nullptr;
        }

        tick_t tick = playback->secToTick(currentTimeSec);

        const Page* page = forcedPage ? forcedPage : pageByTick(pages, tick);
        if (!page) {
            break;
        }

        if (!page->firstMeasure()) {
            // Skip pages with no notation
            continue;
        }

        paintPage(notation, page, painter, frame, config);

        if (config.showCursor) {
            painter.save();
            painter.translate(config.moveToCenter);
            cursor.move(tick);
            muse::RectF cursorRect = cursor.rect();
            muse::PointF pagePos = page->pos();
            muse::RectF cursorAbsRect = cursorRect.translated(-pagePos);
            painter.fillRect(cursorAbsRect, CURSOR_COLOR);
            painter.restore();
        }

        encoder->encodeImage(frame);
    }

    return true;
}

bool VideoWriter::encodeVideoAudio(const muse::media::IVideoEncoderPtr& encoder, const muse::io::path_t& wavPath,
                                   const muse::io::path_t& aacPath, const Config& cfg, INotationPtr notation) const
{
    jubelle::WavReader wav;
    if (!wav.open(wavPath.toStdString())) {
        LOGE() << "video sound: " << wav.error();
        return false;
    }

    // The pieces of the WAV to keep: the lead-in, the music of the shown pages, and any end time.
    // With no page list the whole rendering is kept.
    std::vector<jubelle::Piece> pieces;
    if (m_usePageList) {
        const double lead = cfg.leadingSec;
        pieces.push_back({ 0.0, lead });
        for (const jubelle::VideoSegment& seg : m_segments) {
            pieces.push_back({ lead + seg.start, lead + seg.end });
        }
        if (cfg.trailingSec > 0.0) {
            const double total = notation->masterNotation()->playback()->totalPlayTime();
            pieces.push_back({ lead + total, lead + total + cfg.trailingSec });
        }
    }

    // Jubelle: FFmpeg's own AAC encoder, from the same LGPL FFmpeg libraries as the video (D-E15)
    muse::media::IAacEncoderPtr aac = encoder ? encoder->createAacEncoder() : nullptr;
    if (!aac) {
        LOGE() << "video sound: these FFmpeg libraries have no AAC encoder";
        return false;
    }
    const int64_t totalFrames = jubelle::splicedFrames(wav, pieces);
    if (!aac->open(aacPath, wav.sampleRate(), wav.channels(), int(cfg.audioBitrateKbps))) {
        LOGE() << "video sound: AAC encoder could not start: " << aac->lastError();
        return false;
    }
    // The AAC encoder starts its output with a few hundredths of a second of priming, and a raw AAC stream
    // can't tell the player to skip it, so the sound would run that much behind the pictures. The silent
    // lead-in absorbs it: that much less lead-in at the start, the same amount of silence at the end, so the
    // music lands exactly on its pages and the length is unchanged.
    const double rate = double(wav.sampleRate());
    const double skip = rate > 0 ? std::min(double(aac->encoderDelay()) / rate, double(cfg.leadingSec)) : 0.0;
    int64_t padFrames = 0;
    if (skip > 0.0) {
        if (pieces.empty()) {
            pieces.push_back({ 0.0, double(wav.frames()) / rate });
        }
        pieces.front().first += skip;
        padFrames = std::max<int64_t>(0, totalFrames - jubelle::splicedFrames(wav, pieces));
    }
    jubelle::SpliceOptions options;
    bool ok = true;
    jubelle::splice(wav, pieces, options, [&aac, &ok](int64_t frames, const float* samples) {
        ok = ok && aac->encode(samples, size_t(frames));
    });
    if (ok && padFrames > 0) {
        const std::vector<float> silence(size_t(padFrames) * size_t(std::max(1, wav.channels())), 0.0f);
        ok = aac->encode(silence.data(), size_t(padFrames));
    }
    ok = aac->finish() && ok;
    if (!ok) {
        LOGE() << "video sound: " << aac->lastError();
    }
    LOGI() << "video sound encoded with " << aac->encoderName();
    return ok;
}
