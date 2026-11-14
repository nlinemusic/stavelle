/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "articulationplayback.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>

#include "dom/masterscore.h"
#include "dom/part.h"
#include "dom/spanner.h"
#include "dom/staff.h"
#include "dom/textline.h"
#include "editing/editscoreproperties.h"

namespace mu::engraving::jubelle {
using namespace muse::mpe;
using muse::String;
// Defaults are the values MuseScore's own articulation profiles use, so an
// unchanged setting sounds exactly like MuseScore.
static const std::array<ArtParamInfo, size_t(ArtParam::Count)> PARAMS = { {
    { ArtParam::StaccatoLength,      "staccato",           5, 100, 50 },
    { ArtParam::StaccatissimoLength, "staccatissimo",      5, 100, 25 },
    { ArtParam::TenutoLength,        "tenuto",            50, 100, 99 },
    { ArtParam::MarcatoLength,       "marcato",            5, 100, 99 },
    { ArtParam::AccentLoudness,      "accentLoudness",     0,  40, 10 },
    { ArtParam::MarcatoLoudness,     "marcatoLoudness",    0,  40, 10 },
    { ArtParam::FallStart,           "fallStart",          0,  90, 50 },
    { ArtParam::FallRange,           "fallRange",          1,  24, 10 },
    { ArtParam::FallCurve,           "fallCurve",         -1,   1,  0 },
    { ArtParam::ScoopLength,         "scoopLength",        5,  50, 30 },
    { ArtParam::ScoopRange,          "scoopRange",         1,  12,  4 },
    { ArtParam::GlissandoStart,      "glissandoStart",     0,  90,  0 },
    { ArtParam::GlissandoCurve,      "glissandoCurve",    -1,   1,  0 },
    { ArtParam::TurnSpeed,           "turnSpeed",         50, 200, 100 },
} };

const std::array<ArtParamInfo, size_t(ArtParam::Count)>& artParams()
{
    return PARAMS;
}

const ArtParamInfo& artParamInfo(ArtParam p)
{
    return PARAMS[size_t(p)];
}

// ---------------------------------------------------------------------------
// Values
// ---------------------------------------------------------------------------

int ArticulationPlaybackValues::valueOrDefault(ArtParam p) const
{
    return get(p).value_or(artParamInfo(p).defaultValue);
}

void ArticulationPlaybackValues::set(ArtParam p, std::optional<int> v)
{
    if (v) {
        const ArtParamInfo& info = artParamInfo(p);
        v = std::clamp(*v, info.minValue, info.maxValue);
    }
    values[size_t(p)] = v;
}

bool ArticulationPlaybackValues::isEmpty() const
{
    return std::none_of(values.begin(), values.end(), [](const std::optional<int>& v) { return v.has_value(); });
}

ArticulationPlaybackValues ArticulationPlaybackValues::over(const ArticulationPlaybackValues& base) const
{
    ArticulationPlaybackValues result = base;
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i]) {
            result.values[i] = values[i];
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Settings and storage
//
// Text format (one line per scope, space-separated key=value pairs):
//   jubelle-articulation-playback 1
//   score staccato=45 accentLoudness=15
//   part 1234567 staccato=35
// ---------------------------------------------------------------------------

bool ArticulationPlaybackSettings::isEmpty() const
{
    if (!scoreValues.isEmpty()) {
        return false;
    }
    return std::all_of(partValues.begin(), partValues.end(), [](const auto& p) { return p.second.isEmpty(); });
}

ArticulationPlaybackValues ArticulationPlaybackSettings::effective(uint64_t partId) const
{
    auto it = partValues.find(partId);
    if (it == partValues.end()) {
        return scoreValues;
    }
    return it->second.over(scoreValues);
}

static void writeValues(std::ostringstream& out, const ArticulationPlaybackValues& values)
{
    for (const ArtParamInfo& info : PARAMS) {
        if (auto v = values.get(info.param)) {
            out << ' ' << info.key << '=' << *v;
        }
    }
}

String ArticulationPlaybackSettings::toText() const
{
    if (isEmpty()) {
        return String();
    }

    std::ostringstream out;
    out << "jubelle-articulation-playback 1\n";
    if (!scoreValues.isEmpty()) {
        out << "score";
        writeValues(out, scoreValues);
        out << '\n';
    }
    for (const auto& [partId, values] : partValues) {
        if (values.isEmpty()) {
            continue;
        }
        out << "part " << partId;
        writeValues(out, values);
        out << '\n';
    }
    return String::fromUtf8(out.str().c_str());
}

static void readValues(std::istringstream& line, ArticulationPlaybackValues& values)
{
    std::string token;
    while (line >> token) {
        const size_t eq = token.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = token.substr(0, eq);
        const std::string valueText = token.substr(eq + 1);
        for (const ArtParamInfo& info : PARAMS) {
            if (key != info.key) {
                continue;
            }
            try {
                size_t used = 0;
                const int v = std::stoi(valueText, &used);
                if (used == valueText.size()) {
                    values.set(info.param, v);
                }
            } catch (...) {
                // ignore an unreadable value: the default is used instead
            }
        }
    }
}

ArticulationPlaybackSettings ArticulationPlaybackSettings::fromText(const String& text)
{
    ArticulationPlaybackSettings result;
    std::istringstream in(text.toStdString());
    std::string lineText;
    bool headerSeen = false;

    while (std::getline(in, lineText)) {
        std::istringstream line(lineText);
        std::string kind;
        if (!(line >> kind)) {
            continue;
        }
        if (!headerSeen) {
            headerSeen = (kind == "jubelle-articulation-playback");
            if (!headerSeen) {
                return ArticulationPlaybackSettings();   // not ours: ignore it all
            }
            continue;
        }
        if (kind == "score") {
            readValues(line, result.scoreValues);
        } else if (kind == "part") {
            std::string idText;
            line >> idText;
            try {
                size_t used = 0;
                const uint64_t id = std::stoull(idText, &used);
                if (used == idText.size()) {
                    readValues(line, result.partValues[id]);
                }
            } catch (...) {
            }
        }
    }
    return result;
}

ArticulationPlaybackSettings ArticulationPlaybackSettings::read(const Score* score)
{
    if (!score || !score->masterScore()) {
        return ArticulationPlaybackSettings();
    }
    return fromText(score->masterScore()->metaTag(String(META_TAG)));
}

void ArticulationPlaybackSettings::write(Score* score) const
{
    if (!score || !score->masterScore()) {
        return;
    }
    MasterScore* master = score->masterScore();
    std::map<String, String> tags = master->metaTags();
    const String text = toText();
    const String key(META_TAG);
    if (text.isEmpty()) {
        if (tags.find(key) == tags.end()) {
            return;
        }
        tags.erase(key);
    } else {
        auto it = tags.find(key);
        if (it != tags.end() && it->second == text) {
            return;
        }
        tags[key] = text;
    }
    master->undo(new ChangeMetaTags(master, tags));
}

// ---------------------------------------------------------------------------
// Applying the values to MuseScore's articulation profile
// ---------------------------------------------------------------------------

static void setLength(ArticulationPattern& pattern, int percent)
{
    for (auto& pair : pattern) {
        pair.second.arrangementPattern.durationFactor = percent * ONE_PERCENT;
    }
}

static void setLoudness(ArticulationPattern& pattern, int percentLouder)
{
    constexpr dynamic_level_t natural = dynamicLevelFromType(muse::mpe::DynamicType::Natural);
    const dynamic_level_t target = natural + percentLouder * ONE_PERCENT;

    for (auto& pair : pattern) {
        ExpressionCurve& curve = pair.second.expressionPattern.dynamicOffsetMap;
        const dynamic_level_t oldMax = curve.maxAmplitudeLevel();
        if (oldMax <= 0) {
            continue;
        }
        for (auto& point : curve) {
            point.second = static_cast<dynamic_level_t>(std::lround(double(point.second) * target / oldMax));
        }
    }
}

// Shapes a 0..1 progress value: -1 starts fast and slows down, +1 starts
// slowly and speeds up, 0 is even.
static double shaped(double t, int curve)
{
    t = std::clamp(t, 0.0, 1.0);
    if (curve > 0) {
        return t * t;
    }
    if (curve < 0) {
        return 1.0 - (1.0 - t) * (1.0 - t);
    }
    return t;
}

// Falls, doits, scoops and plops are played exactly as their pattern says:
// one semitone is PITCH_LEVEL_STEP (MuseScore's own fall drops 10 semitones).
static constexpr int PATTERN_UNITS_PER_SEMITONE = PITCH_LEVEL_STEP;

static void setPitchCurve(ArticulationPattern& pattern, const std::function<double(double)>& semitonesAt)
{
    for (auto& pair : pattern) {
        PitchCurve& curve = pair.second.pitchPattern.pitchOffsetMap;
        // Keep the same positions as the loudness curve, which MuseScore
        // expects when it combines several articulations on one note.
        for (auto& point : curve) {
            const double position = double(point.first) / HUNDRED_PERCENT;
            point.second = static_cast<pitch_level_t>(std::lround(semitonesAt(position) * PATTERN_UNITS_PER_SEMITONE));
        }
    }
}

static void setFall(ArticulationPattern& pattern, int startPercent, int semitones, int curve, int direction)
{
    const double start = startPercent / 100.0;
    setPitchCurve(pattern, [=](double position) {
        if (position <= start) {
            return 0.0;
        }
        const double t = (position - start) / (1.0 - start);
        return direction * semitones * shaped(t, curve);
    });
}

static void setScoop(ArticulationPattern& pattern, int lengthPercent, int semitones, int direction)
{
    // The sounding note starts earlier by lengthPercent of the written note
    // and slides into the written pitch during that time.
    const double total = 99.0 + lengthPercent;
    const double boundary = lengthPercent / total;
    for (auto& pair : pattern) {
        pair.second.arrangementPattern.timestampOffset = -lengthPercent * ONE_PERCENT;
        pair.second.arrangementPattern.durationFactor = static_cast<duration_percentage_t>(std::lround(total * ONE_PERCENT));
    }
    setPitchCurve(pattern, [=](double position) {
        if (position >= boundary) {
            return 0.0;
        }
        const double t = position / boundary;
        return direction * semitones * (1.0 - shaped(t, -1));
    });
}

static void setGlissando(ArticulationPattern& pattern, int startPercent, int curve)
{
    // A continuous glissando's curve runs from 0 to 1 (= the written interval).
    const double start = startPercent / 100.0;
    for (auto& pair : pattern) {
        PitchCurve& pitchCurve = pair.second.pitchPattern.pitchOffsetMap;
        if (pitchCurve.empty()) {
            continue;
        }
        const pitch_level_t full = pitchCurve.rbegin()->second;
        for (auto& point : pitchCurve) {
            const double position = double(point.first) / HUNDRED_PERCENT;
            const double t = position <= start ? 0.0 : (position - start) / (1.0 - start);
            point.second = static_cast<pitch_level_t>(std::lround(full * shaped(t, curve)));
        }
    }
}

static void modify(ArticulationsProfile& profile, ArticulationType type, const std::function<void(ArticulationPattern&)>& change)
{
    if (!profile.contains(type)) {
        return;
    }
    ArticulationPattern pattern = profile.pattern(type);
    change(pattern);
    profile.setPattern(type, pattern);
}

ArticulationsProfilePtr applyArticulationPlayback(const ArticulationsProfilePtr& base, const ArticulationPlaybackValues& values)
{
    if (!base || values.isEmpty()) {
        return base;
    }

    auto profile = std::make_shared<ArticulationsProfile>(*base);
    auto has = [&](ArtParam p) { return values.get(p).has_value(); };
    auto val = [&](ArtParam p) { return values.valueOrDefault(p); };

    if (has(ArtParam::StaccatoLength)) {
        modify(*profile, ArticulationType::Staccato, [&](ArticulationPattern& p) { setLength(p, val(ArtParam::StaccatoLength)); });
    }
    if (has(ArtParam::StaccatissimoLength)) {
        modify(*profile, ArticulationType::Staccatissimo,
               [&](ArticulationPattern& p) { setLength(p, val(ArtParam::StaccatissimoLength)); });
    }
    if (has(ArtParam::TenutoLength)) {
        modify(*profile, ArticulationType::Tenuto, [&](ArticulationPattern& p) { setLength(p, val(ArtParam::TenutoLength)); });
    }
    if (has(ArtParam::MarcatoLength)) {
        modify(*profile, ArticulationType::Marcato, [&](ArticulationPattern& p) { setLength(p, val(ArtParam::MarcatoLength)); });
    }
    if (has(ArtParam::AccentLoudness)) {
        modify(*profile, ArticulationType::Accent, [&](ArticulationPattern& p) { setLoudness(p, val(ArtParam::AccentLoudness)); });
    }
    if (has(ArtParam::MarcatoLoudness)) {
        modify(*profile, ArticulationType::Marcato, [&](ArticulationPattern& p) { setLoudness(p, val(ArtParam::MarcatoLoudness)); });
    }
    if (has(ArtParam::FallStart) || has(ArtParam::FallRange) || has(ArtParam::FallCurve)) {
        const int start = val(ArtParam::FallStart);
        const int range = val(ArtParam::FallRange);
        const int curve = val(ArtParam::FallCurve);
        modify(*profile, ArticulationType::Fall, [&](ArticulationPattern& p) { setFall(p, start, range, curve, -1); });
        modify(*profile, ArticulationType::Doit, [&](ArticulationPattern& p) { setFall(p, start, range, curve, +1); });
    }
    if (has(ArtParam::ScoopLength) || has(ArtParam::ScoopRange)) {
        const int length = val(ArtParam::ScoopLength);
        const int range = val(ArtParam::ScoopRange);
        modify(*profile, ArticulationType::Scoop, [&](ArticulationPattern& p) { setScoop(p, length, range, -1); });
        modify(*profile, ArticulationType::Plop, [&](ArticulationPattern& p) { setScoop(p, length, range, +1); });
    }
    if (has(ArtParam::GlissandoStart) || has(ArtParam::GlissandoCurve)) {
        const int start = val(ArtParam::GlissandoStart);
        const int curve = val(ArtParam::GlissandoCurve);
        modify(*profile, ArticulationType::ContinuousGlissando, [&](ArticulationPattern& p) { setGlissando(p, start, curve); });
    }
    return profile;
}

// ---------------------------------------------------------------------------
// Cached lookup for renderers
// ---------------------------------------------------------------------------

double turnSpeedFactor(const Score* score, uint64_t partId)
{
    if (!score || !score->masterScore()) {
        return 1.0;
    }
    const String text = score->masterScore()->metaTag(String(ArticulationPlaybackSettings::META_TAG));
    if (text.isEmpty()) {
        return 1.0;
    }

    // Playback rendering can run on more than one thread
    static std::mutex mutex;
    static String cachedText;
    static ArticulationPlaybackSettings cached;

    std::lock_guard<std::mutex> lock(mutex);
    if (text != cachedText) {
        cached = ArticulationPlaybackSettings::fromText(text);
        cachedText = text;
    }
    return cached.effective(partId).valueOrDefault(ArtParam::TurnSpeed) / 100.0;
}

// ---------------------------------------------------------------------------
// Passages
// ---------------------------------------------------------------------------

String passageText(const ArticulationPlaybackValues& values)
{
    std::ostringstream out;
    out << "Jubelle playback:";
    for (const ArtParamInfo& info : PARAMS) {
        if (auto v = values.get(info.param)) {
            out << ' ' << info.key << '=';
            const bool signedValue = info.param == ArtParam::AccentLoudness || info.param == ArtParam::MarcatoLoudness;
            if (signedValue && *v >= 0) {
                out << '+';
            }
            out << *v;
            const bool percent = info.param != ArtParam::FallRange && info.param != ArtParam::ScoopRange
                                 && info.param != ArtParam::FallCurve && info.param != ArtParam::GlissandoCurve;
            out << (percent ? "%" : "");
        }
    }
    return String::fromUtf8(out.str().c_str());
}

std::optional<ArticulationPlaybackValues> parsePassageText(const String& text)
{
    const std::string t = text.toStdString();
    const std::string prefix = String(PASSAGE_PREFIX).toStdString();
    const size_t at = t.find(prefix);
    if (at == std::string::npos) {
        return std::nullopt;
    }
    std::string rest = t.substr(at + prefix.size());
    for (char& c : rest) {
        if (c == '%' || c == '+' || c == ',') {
            c = ' ';
        }
    }
    // "staccato= 25" → "staccato=25" after removing the sign/percent marks
    std::string cleaned;
    for (size_t i = 0; i < rest.size(); ++i) {
        if (rest[i] == ' ' && !cleaned.empty() && cleaned.back() == '=') {
            continue;
        }
        cleaned += rest[i];
    }
    ArticulationPlaybackValues values;
    std::istringstream line(cleaned);
    readValues(line, values);
    return values;
}

const TextLine* passageLine(const EngravingItem* item)
{
    if (!item) {
        return nullptr;
    }
    if (item->isTextLineSegment()) {
        item = toTextLineSegment(item)->textLine();
    }
    if (!item || !item->isTextLine()) {
        return nullptr;
    }
    const TextLine* line = toTextLine(item);
    return line->beginText().contains(String(PASSAGE_PREFIX)) ? line : nullptr;
}

std::vector<PassageAt> passagesAt(const Score* score, const Part* part, int tick)
{
    std::vector<PassageAt> result;
    if (!score || !part) {
        return result;
    }
    for (const auto& interval : score->spannerMap().findOverlapping(tick, tick)) {
        const Spanner* sp = interval.value;
        const TextLine* line = passageLine(sp);
        if (!line || !line->staff() || line->staff()->part() != part) {
            continue;
        }
        if (tick < line->tick().ticks() || tick >= line->tick2().ticks()) {
            continue;
        }
        if (auto values = parsePassageText(line->beginText())) {
            result.push_back({ line, *values });
        }
    }
    std::stable_sort(result.begin(), result.end(), [](const PassageAt& a, const PassageAt& b) {
        return a.line->tick() < b.line->tick();
    });
    return result;
}

ArticulationPlaybackValues mergedPassageValues(const std::vector<PassageAt>& passages)
{
    ArticulationPlaybackValues merged;
    for (const PassageAt& p : passages) {
        merged = p.values.over(merged);
    }
    return merged;
}

double turnSpeedFactor(const Score* score, const Part* part, int tick)
{
    const ArticulationPlaybackValues passage = mergedPassageValues(passagesAt(score, part, tick));
    if (auto v = passage.get(ArtParam::TurnSpeed)) {
        return *v / 100.0;
    }
    return turnSpeedFactor(score, part ? part->id().toUint64() : 0);
}
}
