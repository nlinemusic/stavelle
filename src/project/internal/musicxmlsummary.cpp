/*
 * SPDX-License-Identifier: GPL-3.0-only
 */
#include "musicxmlsummary.h"

#include <algorithm>
#include <cctype>

namespace mu::project::jubelle {
namespace {
//! position of the next "<tag" followed by a space, '>' or '/', from `from`
size_t findTag(const std::string& xml, const std::string& tag, size_t from, size_t until = std::string::npos)
{
    const std::string open = "<" + tag;
    size_t at = from;
    while ((at = xml.find(open, at)) != std::string::npos && at < until) {
        const size_t after = at + open.size();
        if (after < xml.size() && (xml[after] == ' ' || xml[after] == '>' || xml[after] == '/' || xml[after] == '\n'
                                   || xml[after] == '\r' || xml[after] == '\t')) {
            return at;
        }
        at = after;
    }
    return std::string::npos;
}

int countTag(const std::string& xml, const std::string& tag, size_t from = 0, size_t until = std::string::npos)
{
    int n = 0;
    for (size_t at = findTag(xml, tag, from, until); at != std::string::npos; at = findTag(xml, tag, at + 1, until)) {
        ++n;
    }
    return n;
}

//! how many "<tag …>" have attribute text `attr` (e.g. type="start") in their opening tag
int countTagWith(const std::string& xml, const std::string& tag, const std::vector<std::string>& attrs)
{
    int n = 0;
    for (size_t at = findTag(xml, tag, 0); at != std::string::npos; at = findTag(xml, tag, at + 1)) {
        const size_t end = xml.find('>', at);
        if (end == std::string::npos) {
            break;
        }
        const std::string opening = xml.substr(at, end - at);
        for (const std::string& a : attrs) {
            if (opening.find(a) != std::string::npos) {
                ++n;
                break;
            }
        }
    }
    return n;
}

std::string unescape(std::string s)
{
    const std::pair<const char*, const char*> entities[] = {
        { "&amp;", "&" }, { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" }, { "&apos;", "'" },
    };
    for (const auto& [from, to] : entities) {
        size_t at = 0;
        while ((at = s.find(from, at)) != std::string::npos) {
            s.replace(at, std::char_traits<char>::length(from), to);
            at += 1;
        }
    }
    // trim
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
        s.pop_back();
    }
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    return s.substr(start);
}

//! text of the first <tag>…</tag> from `from`
std::string textOf(const std::string& xml, const std::string& tag, size_t from = 0, size_t until = std::string::npos)
{
    const size_t at = findTag(xml, tag, from, until);
    if (at == std::string::npos) {
        return {};
    }
    const size_t open = xml.find('>', at);
    const size_t close = xml.find("</" + tag, at);
    if (open == std::string::npos || close == std::string::npos || close < open) {
        return {};
    }
    return unescape(xml.substr(open + 1, close - open - 1));
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    return s;
}
}

MusicXmlSummary summarizeMusicXml(const std::string& xml)
{
    MusicXmlSummary s;
    s.isMusicXml = findTag(xml, "score-partwise", 0) != std::string::npos || findTag(xml, "score-timewise", 0) != std::string::npos;
    if (!s.isMusicXml) {
        return s;
    }
    s.software = textOf(xml, "software");
    s.fromNoteflight = lower(s.software).find("noteflight") != std::string::npos;
    s.title = textOf(xml, "work-title");
    if (s.title.empty()) {
        s.title = textOf(xml, "movement-title");
    }

    for (size_t at = findTag(xml, "score-part", 0); at != std::string::npos; at = findTag(xml, "score-part", at + 1)) {
        const size_t end = xml.find("</score-part>", at);
        s.partNames.push_back(textOf(xml, "part-name", at, end));
        ++s.counts.parts;
    }

    for (size_t at = findTag(xml, "note", 0); at != std::string::npos; at = findTag(xml, "note", at + 1)) {
        const size_t end = xml.find("</note>", at);
        if (end == std::string::npos) {
            break;
        }
        if (findTag(xml, "rest", at, end) != std::string::npos) {
            continue;
        }
        const bool printedOff = xml.compare(at, 5, "<note") == 0 && xml.substr(at, xml.find('>', at) - at).find("print-object=\"no\"")
                                != std::string::npos;
        if (printedOff || findTag(xml, "cue", at, end) != std::string::npos) {
            continue;   // hidden and cue notes may be handled differently by the importer
        }
        ++s.counts.notes;
        for (size_t l = findTag(xml, "lyric", at, end); l != std::string::npos; l = findTag(xml, "lyric", l + 1, end)) {
            const size_t lend = xml.find("</lyric>", l);
            if (findTag(xml, "text", l, std::min(lend, end)) != std::string::npos) {
                ++s.counts.lyrics;
            }
        }
    }

    s.counts.dynamics = countTag(xml, "dynamics");
    s.counts.chordSymbols = countTag(xml, "harmony");
    s.counts.slurs = countTagWith(xml, "slur", { "type=\"start\"" });
    s.counts.hairpins = countTagWith(xml, "wedge", { "type=\"crescendo\"", "type=\"diminuendo\"" });
    s.counts.tuplets = countTagWith(xml, "tuplet", { "type=\"start\"" });
    s.counts.tempoMarks = countTag(xml, "metronome");
    s.counts.texts = countTag(xml, "words") + countTag(xml, "rehearsal");
    return s;
}

std::vector<std::string> shortfalls(const MusicXmlCounts& f, const MusicXmlCounts& sc)
{
    std::vector<std::string> out;
    auto check = [&out](const char* what, int inFile, int inScore) {
        if (inScore < inFile) {
            out.push_back(std::string(what) + ": " + std::to_string(inScore) + " of " + std::to_string(inFile) + " arrived");
        }
    };
    check("Instruments", f.parts, sc.parts);
    check("Notes", f.notes, sc.notes);
    check("Lyrics (syllables)", f.lyrics, sc.lyrics);
    check("Dynamics", f.dynamics, sc.dynamics);
    check("Chord symbols", f.chordSymbols, sc.chordSymbols);
    check("Slurs", f.slurs, sc.slurs);
    check("Crescendo and diminuendo hairpins", f.hairpins, sc.hairpins);
    check("Tuplets", f.tuplets, sc.tuplets);
    check("Tempo marks", f.tempoMarks, sc.tempoMarks);
    check("Text (expressions, staff text, rehearsal marks)", f.texts, sc.texts);
    return out;
}

std::string importReport(const MusicXmlSummary& file, const MusicXmlCounts& sc)
{
    std::string r;
    r += "Imported from MusicXML";
    if (!file.software.empty()) {
        r += " written by " + file.software;
    }
    r += ".\n";
    r += "Arrived: " + std::to_string(sc.parts) + " instruments, " + std::to_string(sc.notes) + " notes, "
         + std::to_string(sc.lyrics) + " lyric syllables, " + std::to_string(sc.dynamics) + " dynamics, "
         + std::to_string(sc.chordSymbols) + " chord symbols, " + std::to_string(sc.slurs) + " slurs, "
         + std::to_string(sc.hairpins) + " hairpins, " + std::to_string(sc.tuplets) + " tuplets.\n";
    const std::vector<std::string> missing = shortfalls(file.counts, sc);
    if (missing.empty()) {
        r += "Everything counted in the file arrived in the score.\n";
    } else {
        r += "Please check these (fewer arrived than the file contains):\n";
        for (const std::string& m : missing) {
            r += "  - " + m + "\n";
        }
    }
    r += "MusicXML doesn't carry everything: playback sounds, some layout and page formatting, and anything "
         "special to the other program are set up again by Jubelle.\n";
    return r;
}
}
