/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
// Jubelle G6: Live Tempo Recording. See tempotap.h.

#include "tempotap.h"

#include <algorithm>
#include <cmath>
#include <set>

#include "engraving/dom/factory.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/segment.h"
#include "engraving/dom/tempotext.h"
#include "engraving/editing/editscoreproperties.h"
#include "engraving/infrastructure/eid.h"

using namespace mu::engraving;
using muse::String;

namespace mu::notation::jubelle {
namespace {
double tapBpm(double dt, const TapSettings& s)
{
    // quarter notes per minute for one beat of s.beat lasting dt seconds
    const double quarters = s.beat.toDouble() * 4.0;
    return dt > 0.0 ? quarters * 60.0 / dt : 0.0;
}

std::set<std::string> pointIds(const Score* score)
{
    std::set<std::string> ids;
    const String tag = score->masterScore()->metaTag(String(TEMPO_POINTS_TAG));
    for (const String& id : tag.split(u'|')) {
        if (!id.isEmpty()) {
            ids.insert(id.toStdString());
        }
    }
    return ids;
}

void setPointIds(Score* score, const std::set<std::string>& ids)
{
    MasterScore* master = score->masterScore();
    String joined;
    for (const std::string& id : ids) {
        joined += (joined.isEmpty() ? u"" : u"|") + String::fromStdString(id);
    }
    std::map<String, String> tags = master->metaTags();
    if (joined.isEmpty()) {
        tags.erase(String(TEMPO_POINTS_TAG));
    } else {
        tags[String(TEMPO_POINTS_TAG)] = joined;
    }
    if (tags != master->metaTags()) {
        master->undo(new ChangeMetaTags(master, tags));
    }
}

std::string idOf(EngravingItem* item)
{
    EID eid = item->eid();
    if (!eid.isValid()) {
        eid = item->assignNewEID();
    }
    return eid.toStdString();
}

String tempoLabel(double bpm)
{
    return u"<sym>metNoteQuarterUp</sym> = " + String::number(int(std::lround(bpm)));
}

TempoText* addPoint(Score* score, const Fraction& tick, double bpm)
{
    Measure* m = score->tick2measure(tick);
    Segment* seg = m ? m->undoGetSegment(SegmentType::ChordRest, tick) : nullptr;
    if (!seg) {
        return nullptr;
    }
    TempoText* t = Factory::createTempoText(seg);
    t->setParent(seg);
    t->setTrack(0);
    t->setFollowText(false);
    t->setTempo(BeatsPerSecond::fromBPM(BeatsPerMinute(bpm)));
    t->setXmlText(tempoLabel(bpm));
    t->setVisible(false);   // never printed
    score->undoAddElement(t);
    return t;
}
}

std::vector<TempoPoint> tapsToTempo(const std::vector<double>& taps, const Fraction& startTick, const TapSettings& s)
{
    std::vector<TempoPoint> out;
    if (taps.size() < 2) {
        return out;
    }
    double smoothed = 0.0;
    double written = 0.0;
    for (size_t k = 0; k + 1 < taps.size(); ++k) {
        const double raw = tapBpm(taps[k + 1] - taps[k], s);
        if (raw < s.minBpm || raw > s.maxBpm) {
            continue;   // a missed or doubled tap
        }
        smoothed = (smoothed == 0.0) ? raw : s.smoothing * smoothed + (1.0 - s.smoothing) * raw;
        const Fraction tick = startTick + s.beat * int(k);
        if (written == 0.0 || std::abs(smoothed - written) / written * 100.0 >= s.jitterPercent) {
            out.push_back({ tick, smoothed });
            written = smoothed;
        }
    }
    return out;
}

double liveTempo(const std::vector<double>& taps, const TapSettings& s)
{
    if (taps.size() < 2) {
        return 0.0;
    }
    // the last three intervals, weighted towards the newest
    double sum = 0.0;
    double weights = 0.0;
    double w = 1.0;
    for (size_t i = taps.size() - 1; i > 0 && weights < 6.0; --i) {
        const double bpm = tapBpm(taps[i] - taps[i - 1], s);
        if (bpm >= s.minBpm && bpm <= s.maxBpm) {
            sum += bpm * w;
            weights += w;
        }
        w *= 0.6;
        if (taps.size() - i >= 3) {
            break;
        }
    }
    return weights > 0.0 ? sum / weights : 0.0;
}

std::vector<TempoText*> recordedTempoPoints(const Score* score)
{
    std::vector<TempoText*> out;
    if (!score) {
        return out;
    }
    const std::set<std::string> ids = pointIds(score);
    if (ids.empty()) {
        return out;
    }
    for (const Measure* m = score->firstMeasure(); m; m = m->nextMeasure()) {
        for (const Segment* seg = m->first(SegmentType::ChordRest); seg; seg = seg->next(SegmentType::ChordRest)) {
            for (EngravingItem* a : seg->annotations()) {
                if (a && a->isTempoText() && a->eid().isValid() && ids.count(a->eid().toStdString())) {
                    out.push_back(toTempoText(a));
                }
            }
        }
    }
    return out;
}

bool isRecordedTempoPoint(const Score* score, const TempoText* t)
{
    return score && t && t->eid().isValid() && pointIds(score).count(t->eid().toStdString()) > 0;
}

void clearRecordedTempo(Score* score, const Fraction& from, const Fraction& to)
{
    if (!score) {
        return;
    }
    std::set<std::string> ids = pointIds(score);
    for (TempoText* t : recordedTempoPoints(score)) {
        if (t->tick() >= from && t->tick() < to) {
            ids.erase(t->eid().toStdString());
            score->undoRemoveElement(t);
        }
    }
    setPointIds(score, ids);
}

void applyTempoPoints(Score* score, const std::vector<TempoPoint>& points, const Fraction& from, const Fraction& to)
{
    if (!score) {
        return;
    }
    // the tempo the rest of the score had at `to`, before anything changes
    const double tempoAfter = score->tempo(to).toBPM().val;

    clearRecordedTempo(score, from, to);
    std::set<std::string> ids = pointIds(score);

    for (const TempoPoint& p : points) {
        if (p.tick < from || p.tick >= to) {
            continue;
        }
        if (TempoText* t = addPoint(score, p.tick, p.bpm)) {
            ids.insert(idOf(t));
        }
    }

    // join smoothly: back to the previous tempo where the section ends,
    // unless a tempo marking of the score's own is already there
    if (to < score->endTick() && !points.empty()) {
        bool hasTempoAtEnd = false;
        if (Measure* m = score->tick2measure(to)) {
            if (Segment* seg = m->findSegment(SegmentType::ChordRest, to)) {
                for (EngravingItem* a : seg->annotations()) {
                    hasTempoAtEnd = hasTempoAtEnd || (a && a->isTempoText());
                }
            }
        }
        if (!hasTempoAtEnd) {
            if (TempoText* t = addPoint(score, to, tempoAfter)) {
                ids.insert(idOf(t));
            }
        }
    }
    setPointIds(score, ids);
}

void setRecordedPointTempo(Score* score, TempoText* point, double bpm)
{
    if (!score || !isRecordedTempoPoint(score, point) || bpm <= 0.0) {
        return;
    }
    static_cast<EngravingItem*>(point)->undoChangeProperty(Pid::TEMPO, PropertyValue(BeatsPerSecond::fromBPM(BeatsPerMinute(bpm))));
    static_cast<EngravingItem*>(point)->undoChangeProperty(Pid::TEXT, PropertyValue(tempoLabel(bpm)));
}
}
