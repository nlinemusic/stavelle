/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::edit). Not part of MuseScore.
 *
 * Ported from MuseScore Studio 4.7.7 (GPL v3): ParsedChord::parse and RealizedHarmony (see chordsymbol.h).
 */
#include "chordsymbol.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace je::edit::chords {
namespace {
bool isDigit(char c)
{
    return c >= '0' && c <= '9';
}

std::string lower(const std::string& s)
{
    std::string out = s;
    for (char& c : out) {
        c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool contains(const std::vector<std::string>& list, const std::string& s)
{
    return std::find(list.begin(), list.end(), s) != list.end();
}

bool startsWith(const std::string& s, const std::string& prefix)
{
    return s.compare(0, prefix.size(), prefix) == 0;
}

// ParsedChord::configure
const std::vector<std::string> MAJOR { "ma", "maj", "major", "t", "^" };
const std::vector<std::string> MINOR { "mi", "min", "minor", "-", "=" };
const std::vector<std::string> DIMINISHED { "dim", "o" };
const std::vector<std::string> AUGMENTED { "aug", "+" };
const std::vector<std::string> LOWER { "b", "-", "dim" };
const std::vector<std::string> RAISE { "#", "+", "aug" };
const std::vector<std::string> MOD { "sus", "add", "no", "omit", "^", "type" };

// MuseScore's clampEnharmonic (double sharps and flats allowed: Fbb … B##, -1 … 33, with triples -8 … 40)
int clampEnharmonic(int tpc)
{
    while (tpc > 40) {
        tpc -= 12;
    }
    while (tpc < -8) {
        tpc += 12;
    }
    return tpc;
}

int tpcInterval(int startTpc, int interval, int alter)
{
    static const int intervals[7] = { 0, 2, 4, -1, 1, 3, 5 };
    return clampEnharmonic(startTpc + intervals[(interval - 1) % 7] + alter * 7);
}

int step2pitchInterval(int step, int alter)
{
    static const int intervals[7] = { 0, 2, 4, 5, 7, 9, 11 };
    return intervals[(step - 1) % 7] + alter;
}

constexpr int RANK_MULT = 128;
constexpr int RANK_3RD = 0;
constexpr int RANK_7TH = 1;
constexpr int RANK_9TH = 2;
constexpr int RANK_ADD = 3;
constexpr int RANK_OMIT = 4;
constexpr int OCTAVE = 12;

void removeKey(PitchMap& m, int key)
{
    m.erase(key);
}

//! RealizedHarmony::normalizeNoteMap
PitchMap normalizeNoteMap(const PitchMap& intervals, int rootTpc, int rootPitch, size_t max, bool enforceMaxAsGoal)
{
    PitchMap ret;
    auto it = intervals.begin();
    for (size_t i = 0; i < max; ++i) {
        if (it == intervals.end()) {
            break;
        }
        ret.insert({ (it->first % 128 + rootPitch) % OCTAVE, it->second });
        ++it;
    }
    if (enforceMaxAsGoal) {
        while (ret.size() < max) {
            ret.insert({ rootPitch, rootTpc });   // the root again
            const size_t size = max - ret.size();
            it = intervals.begin();
            for (size_t i = 0; i < size; ++i) {
                if (it == intervals.end()) {
                    break;
                }
                ret.insert({ (it->first % 128 + rootPitch) % OCTAVE, it->second });
                ++it;
            }
        }
    } else if (ret.size() < max) {
        ret.insert({ rootPitch, rootTpc });
    }
    return ret;
}
}

int tpcPitch(int tpc)
{
    // tpc2pitch: F C G D A E B, each row a sharp more (from triple flats)
    static const int pitches[] = { 2, -3, 4, -1, 6, 1, 8, 3, -2, 5, 0, 7, 2, 9, 4, -1, 6, 1, 8, 3, 10, 5, 0, 7, 2, 9, 4, 11,
                                   6, 1, 8, 3, 10, 5, 12, 7, 2, 9, 4, 11, 6, 13, 8, 3, 10, 5, 12, 7, 14 };
    const int i = tpc + 8;
    return i >= 0 && i < int(sizeof(pitches) / sizeof(pitches[0])) ? pitches[i] : 0;
}

int ParsedChord::extensionNumber() const
{
    return std::atoi(extension.c_str());
}

bool ParsedChord::parse(const std::string& s, bool preferMinor)
{
    // ParsedChord::parse, the parts that decide the chord (MuseScore's MusicXML export fields left out)
    const std::string extensionDigits = "123456789";
    const std::string special = "()[],/\\ ";
    const std::string leading = "([ ";
    const std::string trailing = ")],/\\ ";
    auto in = [](const std::string& set, char c) { return set.find(c) != std::string::npos; };
    std::string tok1, tok1L, tok2, tok2L, initial;
    const size_t len = s.size();
    size_t i = 0;

    name = s;
    quality.clear();
    extension.clear();
    modifiers.clear();
    understandable = true;

    // quality: up to the first (non-zero) digit, paren or comma
    for (; i < len; ++i) {
        if (in(extensionDigits, s[i]) || in(special, s[i])) {
            break;
        }
        tok1 += s[i];
        tok1L += char(std::tolower(static_cast<unsigned char>(s[i])));
        if (tok1L == "m" || contains(MAJOR, tok1L) || contains(MINOR, tok1L) || contains(DIMINISHED, tok1L) || contains(AUGMENTED, tok1L)) {
            initial = tok1;
        }
    }
    // "madd" parses as m, add rather than ma, dd
    if (startsWith(tok1L, "madd")) {
        initial = tok1.substr(0, 1);
    }
    // quality and first modifier ran together (mima7, augadd): keep the quality, read the modifier later
    static const std::vector<std::string> modifiersStartingWithQualityCharacters { "tristan", "omit", "type" };
    if (!initial.empty() && initial != tok1 && !contains(modifiersStartingWithQualityCharacters, tok1L)) {
        i -= tok1.size() - initial.size();
        tok1 = initial;
        tok1L = lower(initial);
    }
    if (tok1 == "M" || contains(MAJOR, tok1L)) {
        quality = "major";
    } else if (tok1 == "m" || contains(MINOR, tok1L)) {
        quality = "minor";
    } else if (contains(DIMINISHED, tok1L)) {
        quality = "diminished";
    } else if (contains(AUGMENTED, tok1L)) {
        quality = "augmented";
    } else if (tok1L == "0") {
        quality = "half-diminished";
    } else if (tok1L.empty()) {
        // major or dominant (minor with preferMinor), decided by the extension
        quality.clear();
        if (preferMinor) {
            name = "=" + name;
        }
    } else {
        // not a quality after all, but a modifier: read again as one
        quality.clear();
        tok1.clear();
        tok1L.clear();
        i = 0;
    }
    // eat trailing parens and commas
    while (i < len && in(trailing, s[i])) {
        ++i;
    }
    const size_t prevIdx = i;
    // type: "typen" or roman numerals
    for (tok1.clear(); i < len; ++i) {
        if (isDigit(s[i])) {
            break;
        }
        tok1 += s[i];
    }
    if (tok1 == "type") {
        for (; i < len && isDigit(s[i]); ++i) {
        }
    } else if (tok1.find_first_of("IVX") != std::string::npos) {
        // roman numerals: MuseScore reads them as the type, counting from the start of the text
        i = tok1.size();
    } else {
        i = prevIdx;
    }
    // extension: up to the first non-digit other than comma or slash
    for (tok1.clear(); i < len; ++i) {
        if (!isDigit(s[i]) && s[i] != ',' && s[i] != '/') {
            break;
        }
        tok1 += s[i];
    }
    extension = tok1;
    if (quality.empty()) {
        if (extension == "7" || extension == "9" || extension == "11" || extension == "13") {
            quality = preferMinor ? "minor" : "dominant";
        } else {
            quality = preferMinor ? "minor" : "major";
        }
    }
    if (tok1 == "69" || tok1 == "6,9" || tok1 == "6/9") {
        extension = "69";
    }
    while (i < len && in(trailing, s[i])) {
        ++i;
    }

    // modifiers
    bool addPending = false, susPending = false;
    while (i < len) {
        while (i < len && in(leading, s[i])) {
            ++i;
        }
        // up to the first digit or special character
        for (tok1.clear(), tok1L.clear(), initial.clear(); i < len; ++i) {
            if (isDigit(s[i]) || in(special, s[i])) {
                break;
            }
            tok1 += s[i];
            tok1L += char(std::tolower(static_cast<unsigned char>(s[i])));
            if (contains(MOD, tok1L)) {
                initial = tok1;
            }
        }
        if (i == len && tok1.empty()) {
            break;
        }
        // a modifier and what follows ran together (susb9, addb9)
        if (!initial.empty() && initial != tok1) {
            i -= tok1.size() - initial.size();
            tok1 = initial;
            tok1L = lower(initial);
        }
        if (tok1L == "add") {
            addPending = true;
            continue;
        }
        if (tok1L == "sus" && i != len) {
            susPending = true;
            continue;
        }
        while (i < len && s[i] == ' ') {
            ++i;
        }
        // the number: a digit, or 1 and 0-3 (10 … 13)
        for (tok2.clear(); i < len; ++i) {
            if (!isDigit(s[i])) {
                break;
            }
            if (tok2.size() == 1 && (tok2[0] != '1' || s[i] > '3')) {
                break;
            }
            tok2 += s[i];
        }
        std::string suffix;
        for (size_t j = 0; j < 2 && i + j < len; ++j) {
            suffix += s[i + j];
        }
        if (suffix.find("st") != std::string::npos || suffix.find("nd") != std::string::npos || suffix.find("rd") != std::string::npos
            || suffix.find("th") != std::string::npos) {
            i += 2;
        } else {
            suffix.clear();
        }
        tok2L = lower(tok2);
        for (bool* pending : { &addPending, &susPending }) {
            if (*pending) {
                if (contains(RAISE, tok1L)) {
                    tok1L = "#";
                } else if (contains(LOWER, tok1L)) {
                    tok1L = "b";
                } else if (tok1 == "M" || contains(MAJOR, tok1L)) {
                    tok1L = "major";
                }
                tok2L = tok1L + tok2L;
                tok1L = pending == &addPending ? "add" : "sus";
            }
        }
        if (tok1 == "M" || contains(MAJOR, tok1L)) {
            tok1L = "major";
        } else if (tok1L == "omit") {
            tok1L = "no";
        } else if (tok1L == "sus" && tok2L.empty()) {
            tok2L = "4";
        } else if (contains(AUGMENTED, tok1L) && tok2L.empty()) {
            if (quality == "dominant" && extension == "7") {
                quality = "augmented";
                tok1L.clear();
            } else {
                tok1L = "#";
                tok2L = "5";
            }
        } else if (contains(DIMINISHED, tok1)) {
            quality = "diminished";
            tok1L.clear();
        } else if ((contains(LOWER, tok1L) || contains(RAISE, tok1L)) && tok2L.empty()) {
            // b5, #5 written as the extension's alteration
            tok2L = extension;
            if (quality == "dominant") {
                quality = "major";
            }
            extension.clear();
            tok1L = contains(LOWER, tok1L) ? "b" : "#";
        } else if (contains(LOWER, tok1L)) {
            tok1L = "b";
        } else if (contains(RAISE, tok1L)) {
            tok1L = "#";
        }
        const std::string m = tok1L + tok2L + suffix;
        if (!m.empty()) {
            modifiers.push_back(m);
        }
        // what MuseScore doesn't understand (it is shown, but realized as the root only with the automatic voicing)
        static const std::vector<std::string> known { "add", "no", "sus", "major", "alt", "blues", "lyd", "phryg", "tristan", "" };
        if (!addPending && !susPending && !contains(known, tok1L) && !contains(LOWER, tok1L) && !contains(RAISE, tok1L)) {
            understandable = false;
        }
        // eat trailing parens and commas
        while (i < len && in(trailing, s[i])) {
            ++i;
        }
        addPending = false;
        susPending = false;
    }
    return true;
}

PitchMap intervals(const ParsedChord& p, int rootTpc, bool literal, const Next& next)
{
    // RealizedHarmony::getIntervals
    static const int FIFTH = 7 + RANK_MULT * RANK_OMIT;
    PitchMap ret;
    const std::string& quality = p.quality;
    const int ext = p.extensionNumber();
    int omit = 0;   // the degrees altered or left out, so that they aren't added later
    bool alt5 = false;

    for (const std::string& s : p.modifiers) {
        bool modded = false;
        for (size_t c = 0; c < s.size(); ++c) {
            if (!isDigit(s[c])) {
                continue;
            }
            int alter = 0;
            size_t cutoff = c;
            std::string digits;
            for (char ch : s) {
                if (isDigit(ch)) {
                    digits += ch;
                }
            }
            const int deg = std::atoi(digits.c_str());
            if (c) {
                if (s[c - 1] == '#') {
                    cutoff -= 1;
                    alter = deg == 7 ? 0 : +1;
                } else if (s[c - 1] == 'b') {
                    cutoff -= 1;
                    alter = -1;
                }
            }
            const std::string extType = s.substr(0, cutoff);
            if (deg > 0) {
                if (extType.empty() || extType == "major") {   // an alteration
                    ret.insert({ step2pitchInterval(deg, alter) + RANK_MULT * (deg == 9 ? RANK_9TH : RANK_ADD), tpcInterval(rootTpc, deg, alter) });
                    if (deg == 5) {
                        alt5 = true;
                    }
                    omit |= 1 << deg;
                    modded = true;
                } else if (extType == "sus") {
                    ret.insert({ step2pitchInterval(deg, alter) + RANK_MULT * RANK_3RD, tpcInterval(rootTpc, deg, alter) });
                    omit |= 1 << 3;
                    modded = true;
                } else if (extType == "no") {
                    omit |= 1 << deg;
                    modded = true;
                } else if (extType == "add") {
                    ret.insert({ step2pitchInterval(deg, alter) + RANK_MULT * RANK_ADD, tpcInterval(rootTpc, deg, alter) });
                    omit |= 1 << deg;
                    modded = true;
                }
            }
            break;
        }
        // special chords; anything else not understood: nothing added
        if (!modded) {
            if (s == "phryg") {
                ret.insert({ step2pitchInterval(9, -1) + RANK_MULT * RANK_9TH, tpcInterval(rootTpc, 9, -1) });
                omit |= 1 << 9;
            } else if (s == "lyd") {
                ret.insert({ step2pitchInterval(11, +1) + RANK_MULT * RANK_ADD, tpcInterval(rootTpc, 11, +1) });
                omit |= 1 << 11;
            } else if (s == "blues") {
                ret.insert({ step2pitchInterval(9, +1) + RANK_MULT * RANK_ADD, tpcInterval(rootTpc, 9, +1) });
                omit |= 1 << 9;
            } else if (s == "alt") {
                ret.insert({ step2pitchInterval(5, -1) + RANK_MULT * RANK_ADD, tpcInterval(rootTpc, 5, -1) });
                ret.insert({ step2pitchInterval(5, +1) + RANK_MULT * RANK_ADD, tpcInterval(rootTpc, 5, +1) });
                omit |= 1 << 5;
                ret.insert({ step2pitchInterval(9, -1) + RANK_MULT * RANK_9TH, tpcInterval(rootTpc, 9, -1) });
                ret.insert({ step2pitchInterval(9, +1) + RANK_MULT * RANK_9TH, tpcInterval(rootTpc, 9, +1) });
                omit |= 1 << 9;
            } else {
                omit = ~0;
            }
        }
    }
    if (ext == 5) {   // a power chord: no third
        omit |= 1 << 3;
    }
    auto has = [&omit](int deg) { return omit & (1 << deg); };
    if (quality == "minor") {
        if (!has(3)) {
            ret.insert({ step2pitchInterval(3, -1) + RANK_MULT * RANK_3RD, tpcInterval(rootTpc, 3, -1) });
        }
        if (!has(5)) {
            ret.insert({ step2pitchInterval(5, 0) + RANK_MULT * RANK_OMIT, tpcInterval(rootTpc, 5, 0) });
        }
    } else if (quality == "augmented") {
        if (!has(3)) {
            ret.insert({ step2pitchInterval(3, 0) + RANK_MULT * RANK_3RD, tpcInterval(rootTpc, 3, 0) });
        }
        if (!has(5)) {
            ret.insert({ step2pitchInterval(5, +1) + RANK_MULT * RANK_3RD, tpcInterval(rootTpc, 5, +1) });
        }
    } else if (quality == "diminished" || quality == "half-diminished") {
        if (!has(3)) {
            ret.insert({ step2pitchInterval(3, -1) + RANK_MULT * RANK_3RD, tpcInterval(rootTpc, 3, -1) });
        }
        if (!has(5)) {
            ret.insert({ step2pitchInterval(5, -1) + RANK_MULT * RANK_3RD, tpcInterval(rootTpc, 5, -1) });
        }
        if (!has(7) && quality == "half-diminished") {
            ret.insert({ step2pitchInterval(7, -1) + RANK_MULT * RANK_7TH, tpcInterval(rootTpc, 7, -1) });
        }
        alt5 = true;
    } else {   // major or dominant
        if (!has(3)) {
            ret.insert({ step2pitchInterval(3, 0) + RANK_MULT * RANK_3RD, tpcInterval(rootTpc, 3, 0) });
        }
        if (!has(5)) {
            ret.insert({ step2pitchInterval(5, 0) + RANK_MULT * RANK_OMIT, tpcInterval(rootTpc, 5, 0) });
        }
    }
    switch (ext) {
    case 13:
        if (!has(13)) {
            ret.insert({ 9 + RANK_MULT * RANK_ADD, tpcInterval(rootTpc, 13, 0) });
            omit |= 1 << 13;
        }
        [[fallthrough]];
    case 11:
        if (!has(11)) {
            if (quality == "minor") {
                ret.insert({ 5 + RANK_MULT * RANK_ADD, tpcInterval(rootTpc, 11, 0) });
            } else if (literal) {
                ret.insert({ 5 + RANK_MULT * RANK_OMIT, tpcInterval(rootTpc, 11, 0) });
            }
            omit |= 1 << 11;
        }
        [[fallthrough]];
    case 9:
        if (!has(9)) {
            ret.insert({ 2 + RANK_MULT * RANK_9TH, tpcInterval(rootTpc, 9, 0) });
            omit |= 1 << 9;
        }
        [[fallthrough]];
    case 7:
        if (!has(7)) {
            if (quality == "major") {
                ret.insert({ 11 + RANK_MULT * RANK_7TH, tpcInterval(rootTpc, 7, 0) });
            } else if (quality == "diminished") {
                ret.insert({ 9 + RANK_MULT * RANK_7TH, tpcInterval(rootTpc, 7, -2) });
            } else if (quality != "half-diminished") {   // dominant, augmented, minor
                ret.insert({ 10 + RANK_MULT * RANK_7TH, tpcInterval(rootTpc, 7, -1) });
            }
        }
        break;
    case 6:
        if (!has(6)) {
            ret.insert({ 9 + RANK_MULT * RANK_ADD, tpcInterval(rootTpc, 6, 0) });
            omit |= 1 << 13;
        }
        break;
    case 4:
        if (!has(4)) {
            ret.insert({ 5 + RANK_MULT * RANK_ADD, tpcInterval(rootTpc, 4, 0) });
        }
        break;
    case 2:
        if (!has(2)) {
            ret.insert({ 2 + RANK_MULT * RANK_ADD, tpcInterval(rootTpc, 2, 0) });
        }
        omit |= 1 << 9;
        break;
    case 69:
        ret.insert({ 9 + RANK_MULT * RANK_ADD, tpcInterval(rootTpc, 6, 0) });
        ret.insert({ 2 + RANK_MULT * RANK_ADD, tpcInterval(rootTpc, 9, 0) });
        omit = ~0;
        break;
    default:
        break;
    }
    // the jazz interpretation: a ninth, and a flat ninth or thirteenth resolving a fourth up
    if (!literal && next.present && next.rootTpc >= -8) {
        const std::string& qNext = next.chord.quality;
        const int pitchBetween = (tpcPitch(next.rootTpc) + OCTAVE - tpcPitch(rootTpc)) % OCTAVE;
        const bool maj7 = qNext == "major" && next.chord.extensionNumber() >= 7;
        if (!has(9)) {
            if (quality == "dominant" && pitchBetween == 5 && (qNext == "minor" || maj7)) {
                ret.insert({ 1 + RANK_MULT * RANK_9TH, tpcInterval(rootTpc, 9, -1) });
            } else {
                ret.insert({ 2 + RANK_MULT * RANK_9TH, tpcInterval(rootTpc, 9, 0) });
            }
        }
        if (!has(13) && !alt5) {
            if (quality == "dominant" && pitchBetween == 5 && qNext == "minor") {
                removeKey(ret, FIFTH);
                ret.insert({ 8 + RANK_MULT * RANK_ADD, tpcInterval(rootTpc, 13, -1) });
            }
        }
    }
    return ret;
}

PitchMap generateNotes(const ParsedChord& chord, int rootTpc, int bassTpc, bool literal, Voicing voicing, int transposeOffset,
                       const Next& next)
{
    // RealizedHarmony::generateNotes
    transposeOffset %= OCTAVE;
    static const int DEFAULT_OCTAVE = 5;   // the octave above middle C
    PitchMap notes;
    int rootPitch = tpcPitch(rootTpc) + transposeOffset;
    rootPitch = rootPitch < 0 ? (rootPitch % OCTAVE) + OCTAVE : rootPitch % OCTAVE;
    // the root, or the bass, in the second octave below middle C
    if (bassTpc >= -8 && voicing != Voicing::RootOnly) {
        notes.insert({ tpcPitch(bassTpc) + transposeOffset + (DEFAULT_OCTAVE - 2) * OCTAVE, bassTpc });
    } else {
        notes.insert({ rootPitch + (DEFAULT_OCTAVE - 2) * OCTAVE, rootTpc });
    }
    switch (voicing) {
    case Voicing::RootOnly:
        break;
    case Voicing::Auto:   // close position, or the root only if the chord isn't understood
        if (!chord.understandable) {
            break;
        }
        [[fallthrough]];
    case Voicing::Close: {
        notes.insert({ rootPitch + DEFAULT_OCTAVE * OCTAVE, rootTpc });
        for (const auto& p : intervals(chord, rootTpc, literal, next)) {
            notes.insert({ (rootPitch + (p.first % 128)) % OCTAVE + DEFAULT_OCTAVE * OCTAVE, p.second });
        }
        break;
    }
    case Voicing::Drop2: {
        const PitchMap iv = normalizeNoteMap(intervals(chord, rootTpc, literal, next), rootTpc, rootPitch, 4, false);
        int counter = 0;   // the second from the top dropped an octave
        for (auto it = iv.rbegin(); it != iv.rend(); ++it) {
            notes.insert({ it->first + (++counter == 2 ? DEFAULT_OCTAVE - 1 : DEFAULT_OCTAVE) * OCTAVE, it->second });
        }
        break;
    }
    case Voicing::ThreeNote: {
        const PitchMap iv = normalizeNoteMap(intervals(chord, rootTpc, literal, next), rootTpc, rootPitch, 2, true);
        auto it = iv.begin();
        notes.insert({ it->first + DEFAULT_OCTAVE * OCTAVE, it->second });
        ++it;
        notes.insert({ it->first + DEFAULT_OCTAVE * OCTAVE, it->second });
        break;
    }
    case Voicing::FourNote:
    case Voicing::SixNote: {
        const PitchMap iv = normalizeNoteMap(intervals(chord, rootTpc, literal, next), rootTpc, rootPitch,
                                             voicing == Voicing::FourNote ? 3 : 5, true);
        int counter = 0;   // every other note dropped an octave
        for (auto it = iv.rbegin(); it != iv.rend(); ++it) {
            notes.insert({ it->first + (counter % 2 ? DEFAULT_OCTAVE - 1 : DEFAULT_OCTAVE) * OCTAVE, it->second });
            ++counter;
        }
        break;
    }
    }
    return notes;
}
}
