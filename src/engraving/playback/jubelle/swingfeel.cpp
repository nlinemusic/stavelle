/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 3 as
 * published by the Free Software Foundation.
 */

#include "swingfeel.h"

#include <algorithm>
#include <cmath>
#include <locale>
#include <mutex>
#include <sstream>
#include <string>

#include "dom/chord.h"
#include "dom/masterscore.h"
#include "dom/articulation.h"
#include "dom/measure.h"
#include "dom/navigate.h"
#include "dom/note.h"
#include "dom/instrument.h"
#include "dom/part.h"
#include "dom/staff.h"
#include "dom/tempo.h"
#include "editing/editscoreproperties.h"

namespace mu::engraving::jubelle {
using muse::String;

// ---------------------------------------------------------------------------
// Feels
// ---------------------------------------------------------------------------

double SwingFeel::ratioToPercent(double r)
{
    return 100.0 * r / (r + 1.0);
}

double SwingFeel::percentToRatio(double percent)
{
    percent = std::clamp(percent, 50.0, 90.0);
    return percent / (100.0 - percent);
}

double SwingFeel::ratioAt(double bpm) const
{
    if (!tempoDependent) {
        return ratio;
    }
    if (highTempo <= lowTempo || bpm <= lowTempo) {
        return lowTempoRatio;
    }
    if (bpm >= highTempo) {
        return highTempoRatio;
    }
    const double t = (bpm - lowTempo) / (highTempo - lowTempo);
    return lowTempoRatio + t * (highTempoRatio - lowTempoRatio);
}

double SwingFeel::playedRatioAt(double bpm) const
{
    double r = ratioAt(bpm);
    if (minShortMs > 0 && bpm > 0) {
        // the beat of the swing unit (a quarter for eighths) in ms
        const double beatMs = 60000.0 / bpm * (unit == 16 ? 0.5 : 1.0);
        const double maxRatio = beatMs / double(minShortMs) - 1.0;
        r = std::min(r, std::max(1.0, maxRatio));
    }
    return r;
}

double SwingFeel::hornDelayAt(double bpm) const
{
    if (!roleAware) {
        return 0.0;
    }
    if (highTempo <= lowTempo || bpm <= lowTempo) {
        return hornDelayLowMs;
    }
    if (bpm >= highTempo) {
        return hornDelayHighMs;
    }
    const double t = (bpm - lowTempo) / (highTempo - lowTempo);
    return hornDelayLowMs + t * (hornDelayHighMs - hornDelayLowMs);
}

double SwingFeel::rhythmPlacementAt(double bpm) const
{
    // After Lee Bash ("Where's the Groove"): behind when slow, together at
    // moderate tempos (about 120-180), ahead when fast
    if (!roleAware) {
        return 0.0;
    }
    if (bpm <= 100.0) {
        return rhythmSlowMs;
    }
    if (bpm < 120.0) {
        return rhythmSlowMs * (120.0 - bpm) / 20.0;
    }
    if (bpm <= 180.0) {
        return 0.0;
    }
    if (bpm < 220.0) {
        return rhythmFastMs * (bpm - 180.0) / 40.0;
    }
    return rhythmFastMs;
}

void SwingFeel::clampValues()
{
    unit = (unit == 16) ? 16 : 8;
    lowTempo = std::clamp(lowTempo, 20.0, 400.0);
    highTempo = std::clamp(highTempo, 20.0, 400.0);
    if (highTempo < lowTempo) {
        std::swap(lowTempo, highTempo);
    }
    // 1:1 (straight) up to 4:1 (80% of the beat)
    lowTempoRatio = std::clamp(lowTempoRatio, 1.0, 4.0);
    highTempoRatio = std::clamp(highTempoRatio, 1.0, 4.0);
    ratio = std::clamp(ratio, 1.0, 4.0);
    offbeatLoudness = std::clamp(offbeatLoudness, 50, 120);
    offbeatLength = std::clamp(offbeatLength, 50, 100);
    minShortMs = std::clamp(minShortMs, 0, 250);
    hornDelayLowMs = std::clamp(hornDelayLowMs, 0.0, 80.0);
    hornDelayHighMs = std::clamp(hornDelayHighMs, 0.0, 80.0);
    rhythmSlowMs = std::clamp(rhythmSlowMs, -40.0, 40.0);
    rhythmFastMs = std::clamp(rhythmFastMs, -40.0, 40.0);
    guitarQuarterLength = std::clamp(guitarQuarterLength, 30, 100);
}

bool SwingFeel::operator==(const SwingFeel& o) const
{
    auto same = [](double a, double b) { return std::abs(a - b) < 1e-6; };
    return id == o.id && name == o.name && builtIn == o.builtIn && unit == o.unit && tempoDependent == o.tempoDependent
           && same(lowTempo, o.lowTempo) && same(highTempo, o.highTempo) && same(lowTempoRatio, o.lowTempoRatio)
           && same(highTempoRatio, o.highTempoRatio) && same(ratio, o.ratio)
           && offbeatLoudness == o.offbeatLoudness && offbeatLength == o.offbeatLength
           && minShortMs == o.minShortMs && roleAware == o.roleAware && same(hornDelayLowMs, o.hornDelayLowMs)
           && same(hornDelayHighMs, o.hornDelayHighMs) && same(rhythmSlowMs, o.rhythmSlowMs) && same(rhythmFastMs, o.rhythmFastMs)
           && guitarQuarterLength == o.guitarQuarterLength && hornArticulation == o.hornArticulation;
}

static SwingFeel makeFixed(const char16_t* id, const char16_t* name, double ratio)
{
    SwingFeel f;
    f.id = id;
    f.name = name;
    f.builtIn = true;
    f.tempoDependent = false;
    f.ratio = ratio;
    return f;
}

static SwingFeel makeTempoDependent(const char16_t* id, const char16_t* name, double low, double high,
                                    double lowRatio, double highRatio)
{
    SwingFeel f;
    f.id = id;
    f.name = name;
    f.builtIn = true;
    f.tempoDependent = true;
    f.lowTempo = low;
    f.highTempo = high;
    f.lowTempoRatio = lowRatio;
    f.highTempoRatio = highRatio;
    f.ratio = lowRatio;
    return f;
}

const std::vector<SwingFeel>& builtInFeels()
{
    // Swing v2 (R5, 15 Nov 2026), from the research summary in "Windows Build
    // Refinement Phase", section 2. The four swing presets are spread apart so
    // they are clearly different at real big-band tempos (at 160 bpm the
    // offbeat lands about 212, 246, 262 and 277 ms after the beat; straight is
    // 188 ms), instead of the 10-25 ms that separated them before.
    //  - Light: soloist-like, only slightly uneven (Corcoran & Frieler: about
    //    1.3:1 is typical in solos).
    //  - Medium: a moderate triplet feel at medium tempos.
    //  - Heavy: a strong, shuffle-like swing that stays uneven at speed.
    //  - Basie: role-aware. Drums and rhythm section follow the drummers' ride
    //    curve (Friberg & Sundstrom 2002: about 3:1 slow, 2:1 at medium-fast,
    //    flattening when fast, the short note not much under 100 ms). Horns
    //    keep their offbeats with the drums and sit a little behind on the
    //    beat (Friberg & Sundstrom 2002; Datseris et al. 2019). Guitar plays a
    //    short, light, even four (Freddie Green). The rhythm section is a
    //    little behind when slow, together at 120-180, a little ahead when fast
    //    (Lee Bash). Horn phrasing as taught for Basie charts: short quarters,
    //    long eighths except before a rest, accented syncopations. No study
    //    measures the Basie band's own recordings, so the delay and placement
    //    amounts are moderate starting points, adjustable, for listening.
    static const std::vector<SwingFeel> feels = [] {
        std::vector<SwingFeel> v;
        v.push_back(makeFixed(u"straight", u"Straight", 1.0));
        v.push_back(makeTempoDependent(u"light", u"Light swing", 80, 240, 1.45, 1.15));
        v.push_back(makeTempoDependent(u"medium", u"Medium swing", 80, 240, 2.1, 1.7));
        v.push_back(makeTempoDependent(u"heavy", u"Heavy swing", 80, 240, 3.0, 2.6));
        v.push_back(makeFixed(u"fixed21", u"Swing 2:1 (triplet)", 2.0));
        v.push_back(makeFixed(u"fixed31", u"Swing 3:1 (dotted)", 3.0));
        SwingFeel basie = makeTempoDependent(u"basie", u"Basie swing", 80, 200, 3.0, 2.0);
        basie.minShortMs = 100;
        basie.roleAware = true;
        basie.hornDelayLowMs = 25.0;
        basie.hornDelayHighMs = 12.0;
        basie.rhythmSlowMs = 10.0;
        basie.rhythmFastMs = -8.0;
        basie.guitarQuarterLength = 55;
        basie.hornArticulation = true;
        v.push_back(basie);
        return v;
    }();
    return feels;
}

SwingRole swingRoleFor(const String& id, bool drumStaff)
{
    if (drumStaff || id.startsWith(u"drum.") || id.startsWith(u"metal.cymbal") || id.startsWith(u"metal.hi-hat")) {
        return SwingRole::Drums;
    }
    if (id.startsWith(u"pluck.bass") || id == u"strings.contrabass") {
        return SwingRole::Bass;
    }
    if (id.startsWith(u"pluck.guitar")) {
        return SwingRole::Guitar;
    }
    if (id.startsWith(u"keyboard.") || id == u"pitched-percussion.vibraphone") {
        return SwingRole::Keyboard;
    }
    return SwingRole::Horn;
}

static const SwingFeel* findBuiltIn(const String& id)
{
    for (const SwingFeel& f : builtInFeels()) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

const SwingFeel* SwingSettings::feel(const String& id) const
{
    for (const SwingFeel& f : feels) {
        if (f.id == id) {
            return &f;
        }
    }
    return findBuiltIn(id);
}

std::vector<SwingFeel> SwingSettings::allFeels() const
{
    std::vector<SwingFeel> result;
    for (const SwingFeel& b : builtInFeels()) {
        result.push_back(*feel(b.id));
    }
    for (const SwingFeel& f : feels) {
        if (!findBuiltIn(f.id)) {
            result.push_back(f);
        }
    }
    return result;
}

void SwingSettings::setFeel(const SwingFeel& f)
{
    SwingFeel copy = f;
    copy.clampValues();
    const SwingFeel* builtIn = findBuiltIn(copy.id);
    copy.builtIn = builtIn != nullptr;
    if (builtIn) {
        copy.name = builtIn->name;   // built-in names stay as they are
    }
    removeFeel(copy.id);
    if (builtIn && copy == *builtIn) {
        return;   // back to its built-in values: nothing to store
    }
    feels.push_back(copy);
}

void SwingSettings::removeFeel(const String& id)
{
    feels.erase(std::remove_if(feels.begin(), feels.end(), [&](const SwingFeel& f) { return f.id == id; }), feels.end());
}

String SwingSettings::newCustomId() const
{
    for (int n = 1;; ++n) {
        const String id = String(u"custom") + String::number(n);
        if (!feel(id)) {
            return id;
        }
    }
}

bool SwingSettings::isEmpty() const
{
    return scoreFeel.isEmpty() && partFeels.empty() && feels.empty()
           && markingStyle == SwingMarkingStyle::TextAndEquation && markingText.isEmpty() && markingEid.isEmpty();
}

bool SwingSettings::operator==(const SwingSettings& o) const
{
    return toText() == o.toText();
}

// Names may contain any character; they are stored with %XX escapes.
static std::string encodeName(const String& name)
{
    const std::string utf8 = name.toStdString();
    std::string out;
    static const char* hex = "0123456789ABCDEF";
    for (unsigned char c : utf8) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.') {
            out += char(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

static String decodeName(const std::string& text)
{
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            try {
                out += char(std::stoi(text.substr(i + 1, 2), nullptr, 16));
                i += 2;
                continue;
            } catch (...) {
            }
        }
        out += text[i];
    }
    return String::fromStdString(out);
}

String SwingSettings::toText() const
{
    if (isEmpty()) {
        return String();
    }
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "jubelle-swing 1\n";
    if (!scoreFeel.isEmpty()) {
        out << "score " << encodeName(scoreFeel) << '\n';
    }
    for (const auto& [partId, id] : partFeels) {
        out << "part " << partId << ' ' << encodeName(id) << '\n';
    }
    if (markingStyle != SwingMarkingStyle::TextAndEquation || !markingText.isEmpty() || !markingEid.isEmpty()) {
        out << "marking style=" << int(markingStyle) << " text=" << encodeName(markingText)
            << " eid=" << encodeName(markingEid) << '\n';
    }
    for (const SwingFeel& f : feels) {
        out << "feel id=" << encodeName(f.id) << " name=" << encodeName(f.name) << " unit=" << f.unit
            << " tempo=" << (f.tempoDependent ? 1 : 0) << " low=" << f.lowTempo << " high=" << f.highTempo
            << " lowRatio=" << f.lowTempoRatio << " highRatio=" << f.highTempoRatio << " ratio=" << f.ratio
            << " loud=" << f.offbeatLoudness << " len=" << f.offbeatLength;
        // Swing v2 values, only when used (older Jubelle builds ignore them)
        if (f.minShortMs != 0) {
            out << " minShort=" << f.minShortMs;
        }
        if (f.roleAware) {
            out << " roles=1 hornLow=" << f.hornDelayLowMs << " hornHigh=" << f.hornDelayHighMs
                << " rhythmSlow=" << f.rhythmSlowMs << " rhythmFast=" << f.rhythmFastMs << " guitarLen=" << f.guitarQuarterLength;
        }
        if (f.hornArticulation) {
            out << " phrasing=1";
        }
        out << '\n';
    }
    return String::fromStdString(out.str());
}

SwingSettings SwingSettings::fromText(const String& text)
{
    SwingSettings result;
    std::istringstream in(text.toStdString());
    in.imbue(std::locale::classic());
    std::string lineText;
    bool headerSeen = false;

    while (std::getline(in, lineText)) {
        std::istringstream line(lineText);
        line.imbue(std::locale::classic());
        std::string kind;
        if (!(line >> kind)) {
            continue;
        }
        if (!headerSeen) {
            if (kind != "jubelle-swing") {
                return SwingSettings();
            }
            headerSeen = true;
            continue;
        }
        try {
            if (kind == "score") {
                std::string id;
                line >> id;
                result.scoreFeel = decodeName(id);
            } else if (kind == "part") {
                std::string partText, id;
                line >> partText >> id;
                if (!id.empty()) {
                    result.partFeels[std::stoull(partText)] = decodeName(id);
                }
            } else if (kind == "marking") {
                std::string token;
                while (line >> token) {
                    const size_t eq = token.find('=');
                    if (eq == std::string::npos) {
                        continue;
                    }
                    const std::string key = token.substr(0, eq);
                    const std::string v = token.substr(eq + 1);
                    if (key == "style") {
                        const int style = std::stoi(v);
                        if (style >= 0 && style <= int(SwingMarkingStyle::Custom)) {
                            result.markingStyle = SwingMarkingStyle(style);
                        }
                    } else if (key == "text") {
                        result.markingText = decodeName(v);
                    } else if (key == "eid") {
                        result.markingEid = decodeName(v);
                    }
                }
            } else if (kind == "feel") {
                SwingFeel f;
                std::string token;
                while (line >> token) {
                    const size_t eq = token.find('=');
                    if (eq == std::string::npos) {
                        continue;
                    }
                    const std::string key = token.substr(0, eq);
                    const std::string v = token.substr(eq + 1);
                    if (key == "id") {
                        f.id = decodeName(v);
                    } else if (key == "name") {
                        f.name = decodeName(v);
                    } else if (key == "unit") {
                        f.unit = std::stoi(v);
                    } else if (key == "tempo") {
                        f.tempoDependent = std::stoi(v) != 0;
                    } else if (key == "low") {
                        f.lowTempo = std::stod(v);
                    } else if (key == "high") {
                        f.highTempo = std::stod(v);
                    } else if (key == "lowRatio") {
                        f.lowTempoRatio = std::stod(v);
                    } else if (key == "highRatio") {
                        f.highTempoRatio = std::stod(v);
                    } else if (key == "ratio") {
                        f.ratio = std::stod(v);
                    } else if (key == "loud") {
                        f.offbeatLoudness = std::stoi(v);
                    } else if (key == "len") {
                        f.offbeatLength = std::stoi(v);
                    } else if (key == "minShort") {
                        f.minShortMs = std::stoi(v);
                    } else if (key == "roles") {
                        f.roleAware = std::stoi(v) != 0;
                    } else if (key == "hornLow") {
                        f.hornDelayLowMs = std::stod(v);
                    } else if (key == "hornHigh") {
                        f.hornDelayHighMs = std::stod(v);
                    } else if (key == "rhythmSlow") {
                        f.rhythmSlowMs = std::stod(v);
                    } else if (key == "rhythmFast") {
                        f.rhythmFastMs = std::stod(v);
                    } else if (key == "guitarLen") {
                        f.guitarQuarterLength = std::stoi(v);
                    } else if (key == "phrasing") {
                        f.hornArticulation = std::stoi(v) != 0;
                    }
                }
                if (!f.id.isEmpty()) {
                    result.setFeel(f);
                }
            }
        } catch (...) {
            // an unreadable line is skipped; everything else is kept
        }
    }
    return result;
}

SwingSettings SwingSettings::read(const Score* score)
{
    if (!score || !score->masterScore()) {
        return SwingSettings();
    }
    return fromText(score->masterScore()->metaTag(String(META_TAG)));
}

void SwingSettings::write(Score* score) const
{
    if (!score || !score->masterScore()) {
        return;
    }
    MasterScore* master = score->masterScore();
    std::map<String, String> tags = master->metaTags();
    const String key(META_TAG);
    const String text = toText();
    auto it = tags.find(key);
    if (text.isEmpty()) {
        if (it == tags.end()) {
            return;
        }
        tags.erase(it);
    } else {
        if (it != tags.end() && it->second == text) {
            return;
        }
        tags[key] = text;
    }
    master->undo(new ChangeMetaTags(master, tags));
}

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

// As MuseScore: a swing note that is split into shorter notes is not swung
static bool isSubdivided(const ChordRest* cr, int swingUnit)
{
    if (!cr) {
        return false;
    }
    const ChordRest* prev = prevChordRest(cr);
    return cr->actualTicks().ticks() < swingUnit || (prev && prev->actualTicks().ticks() < swingUnit);
}

SwingTiming swingTimingForRatio(const Chord* chord, int swingUnit, double ratioPercent, bool* offbeat)
{
    SwingTiming t;
    t.jubelleFeel = true;
    if (offbeat) {
        *offbeat = false;
    }
    if (!chord || !chord->measure() || swingUnit <= 0 || ratioPercent <= 50.0) {
        return t;
    }

    // The same rule as MuseScore's Swing::swingAdjustParams, without rounding
    const int startTick = (chord->rtick() + chord->measure()->anacrusisOffset()).ticks();
    const int swingBeat = swingUnit * 2;
    const double duration = double(chord->actualTicks().ticks());
    if (duration <= 0) {
        return t;
    }
    const double adjust = swingBeat * (ratioPercent - 50.0) / 100.0 / duration;

    if (startTick % swingBeat == swingUnit && !isSubdivided(chord, swingUnit)) {
        t.startShift = adjust;
        t.durationFactor -= adjust;
        t.offbeat = true;
        if (offbeat) {
            *offbeat = true;
        }
    }
    const int endTick = startTick + int(duration);
    if (endTick % swingBeat == swingUnit && !isSubdivided(nextChordRest(chord), swingUnit)) {
        t.durationFactor += adjust;
    }
    if (t.durationFactor <= 0.0) {
        t.durationFactor = 1.0;
    }
    return t;
}

static SwingSettings cachedSettings(const Score* score)
{
    static std::mutex mutex;
    static String cachedText;
    static SwingSettings cached;

    const String text = score->masterScore()->metaTag(String(SwingSettings::META_TAG));
    std::lock_guard<std::mutex> lock(mutex);
    if (text != cachedText) {
        cached = SwingSettings::fromText(text);
        cachedText = text;
    }
    return cached;
}

SwingTiming swingTiming(const Chord* chord)
{
    SwingTiming none;
    if (!chord || !chord->staff() || !chord->score() || !chord->score()->masterScore()) {
        return none;
    }
    const Score* score = chord->score();
    const SwingParameters sp = chord->staff()->swing(chord->tick());
    const SwingSettings settings = cachedSettings(score);

    const uint64_t partId = chord->part() ? chord->part()->id().toUint64() : 0;
    auto chosen = [&]() {
        auto it = settings.partFeels.find(partId);
        return it != settings.partFeels.end() ? it->second : settings.scoreFeel;
    };

    String feelId;
    if (sp.fromText && !sp.jubelleFeel.isEmpty()) {
        if (sp.swingUnit == 0) {
            SwingTiming off;   // the text switches swing off
            off.jubelleFeel = true;
            return off;
        }
        feelId = sp.jubelleFeel;
    } else if (sp.fromText) {
        // An ordinary MuseScore Swing or Straight text. "Straight" plays straight.
        // "Swing" switches swing on, and the feel chosen in the Swing dialog (for
        // the instrument, else the score) decides how it swings; before R0 (15 Nov
        // 2026) MuseScore's ratio in the text silently replaced the chosen feel.
        // With no feel chosen, MuseScore's own swing plays, as before.
        if (sp.swingUnit == 0) {
            return none;
        }
        feelId = chosen();
        if (feelId.isEmpty() || feelId == u"straight") {
            return none;
        }
    } else {
        feelId = chosen();
        if (feelId.isEmpty()) {
            return none;   // MuseScore's style swing
        }
    }

    const SwingFeel* feel = settings.feel(feelId);
    if (!feel) {
        return none;   // unknown feel (e.g. deleted): the text's own ratio still plays
    }

    const double bpm = score->tempomap()->tempo(chord->tick().ticks()).val * 60.0;
    SwingRole role = SwingRole::Horn;
    if (feel->roleAware && chord->part()) {
        const Instrument* instrument = chord->part()->instrument(chord->tick());
        role = swingRoleFor(instrument ? instrument->musicXmlId() : String(), chord->staff()->isDrumStaff(chord->tick()));
    }
    return swingTimingForFeel(chord, *feel, bpm, role);
}

static bool hasTieForward(const Chord* chord)
{
    for (const Note* n : chord->notes()) {
        if (n->tieFor()) {
            return true;
        }
    }
    return false;
}

static bool followedByRest(const Chord* chord)
{
    const ChordRest* next = nextChordRest(chord);
    return !next || next->isRest();
}

SwingTiming swingTimingForFeel(const Chord* chord, const SwingFeel& feel, double bpm, SwingRole role)
{
    // Horn phrasing amounts (playback only)
    static constexpr double SHORT_QUARTER = 0.65;   // "dit": short, not staccato
    static constexpr double CLIPPED_EIGHTH = 0.5;   // the last eighth before a rest
    static constexpr double ACCENT = 1.12;          // phrase-end and syncopated notes

    const double percent = SwingFeel::ratioToPercent(feel.playedRatioAt(bpm));
    const int unitTicks = Constants::DIVISION / (feel.unit == 16 ? 4 : 2);

    bool offbeat = false;
    SwingTiming t = swingTimingForRatio(chord, unitTicks, percent, &offbeat);
    if (offbeat) {
        t.durationFactor *= feel.offbeatLength / 100.0;
        t.loudnessFactor = feel.offbeatLoudness / 100.0;
    }
    if (!feel.roleAware || !chord || !chord->measure() || bpm <= 0) {
        return t;
    }

    const int ticks = chord->actualTicks().ticks();
    if (ticks <= 0) {
        return t;
    }
    const double chordMs = double(ticks) / Constants::DIVISION * 60000.0 / bpm;
    const int startTick = (chord->rtick() + chord->measure()->anacrusisOffset()).ticks();
    const bool onBeat = startTick % (unitTicks * 2) == 0;

    if (role == SwingRole::Horn) {
        // On the beat a little behind, ending where it would have; offbeats stay with the drums
        const double delay = feel.hornDelayAt(bpm);
        if (onBeat && delay > 0.0) {
            const double shift = std::min(delay / chordMs, 0.25);
            t.startShift += shift;
            t.durationFactor -= shift;
        }
        if (feel.hornArticulation && chord->articulations().empty() && !hasTieForward(chord)) {
            if (ticks == Constants::DIVISION && onBeat) {
                t.durationFactor *= SHORT_QUARTER;
            } else if (ticks == unitTicks && followedByRest(chord)) {
                t.durationFactor *= CLIPPED_EIGHTH;
                t.loudnessFactor *= ACCENT;
            }
        }
        if (feel.hornArticulation && offbeat && (ticks > unitTicks || hasTieForward(chord))) {
            t.loudnessFactor *= ACCENT;   // a syncopation: pushed and lifted
        }
    } else {
        // The rhythm section sits behind, with or ahead of the band, by tempo
        const double placement = feel.rhythmPlacementAt(bpm);
        if (placement != 0.0) {
            t.startShift += std::clamp(placement / chordMs, -0.25, 0.25);
        }
        if (role == SwingRole::Guitar && ticks == Constants::DIVISION && onBeat && !hasTieForward(chord)) {
            t.durationFactor *= feel.guitarQuarterLength / 100.0;
        }
    }
    if (t.durationFactor <= 0.05) {
        t.durationFactor = 0.05;
    }
    return t;
}
}
