// SPDX-License-Identifier: GPL-3.0-only
// Jubelle engine: the file-handling tool (S2). Not part of MuseScore.
//
//   je_io_tool corpus <folder> [--private] [--report <file.md>]
//       every .mscz/.mscx under the folder: XML and archive round trips, and for MuseScore 4.x files the
//       native reading into the model with its consistency rules. --private names files "private #n" only.
//       --known <file>: files listed there (one relative path per line, # comments) are known issues: reported,
//       but only a failure in another file, or a known file that now passes, changes the exit code
//   je_io_tool rewrite <in> <out>     read and write a score (with the engine store), for MuseScore tests
//   je_io_tool set-jubelle-style <in> <out>   the same, with all 11 Jubelle style settings set (for the store test)
//   je_io_tool listing <file>         the music as read (staff, voice, time, duration, pitches)
//   je_io_tool store <file>           the engine store and what it would restore into <Style>
//   je_io_tool bench <file>           times and memory for reading and writing one score
//   je_io_tool generate <in> <out>    open a score into the model and save a file generated from the model alone
//   je_io_tool regenerate <folder> [--known <file>] [--private] [--out <dir>]
//       every MuseScore 4.x score: read → generate from the model → read again; the music and every model field
//       must be equal, the model consistent, and generating again must give the same file. --out keeps the files
//   je_io_tool dump <file>            every model object and field as read (ids shown as tree paths)
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "edit/chordsymbol.h"
#include "edit/commands.h"
#include "edit/history.h"
#include "edit/links.h"
#include "edit/operations.h"
#include "edit/structure.h"
#include "io/mscxreader.h"
#include "io/mscxwriter.h"
#include "io/scorefile.h"
#include "model/invariants.h"
#include "model/packing.h"
#include "perform/repeatlist.h"
#include "edit/lyrics.h"
#include "edit/journal.h"
#include "edit/session.h"
#include "style/style.h"
#include "layout/context.h"
#include "layout/beams.h"
#include "layout/stems.h"
#include "layout/chords.h"
#include "layout/notes.h"

namespace fs = std::filesystem;
using namespace je;

namespace {
double ms(std::chrono::steady_clock::time_point t0)
{
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
}

double heapMb()
{
#if defined(__GLIBC__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 33))
    return double(mallinfo2().uordblks) / (1024.0 * 1024.0);
#else
    return -1;
#endif
}

bool isScore(const fs::path& p)
{
    const std::string e = p.extension().string();
    return e == ".mscz" || e == ".mscx";
}

struct Totals {
    int files = 0, unreadable = 0, xmlIdentical = 0, xmlSameContent = 0, xmlDifferent = 0, zipOk = 0, zipBad = 0;
    int v4 = 0, v4Read = 0, v4Consistent = 0, v4Problems = 0;
    long chords = 0, notes = 0, rests = 0, tuplets = 0, graces = 0, ties = 0, spanners = 0, lyrics = 0, measures = 0;
    std::map<std::string, long> modelled, keptOnly;
    std::map<std::string, int> warnings;
    std::vector<std::string> failures;
};

//! The first differing line of two texts, for a report: its start and, for model dumps, only the fields that differ
std::string firstDifference(const std::string& a, const std::string& b)
{
    auto cut = [](const std::string& t) { return t.size() > 160 ? t.substr(0, 160) + "…" : t; };
    auto tokens = [](const std::string& line) {
        // "path f=v f=v" with quoted texts that may hold spaces
        std::vector<std::string> out;
        std::string cur;
        bool quoted = false;
        for (char c : line) {
            if (c == '"') {
                quoted = !quoted;
            }
            if (c == ' ' && !quoted) {
                out.push_back(cur);
                cur.clear();
            } else {
                cur += c;
            }
        }
        out.push_back(cur);
        return out;
    };
    size_t la = 0, lb = 0;
    while (la < a.size() || lb < b.size()) {
        const size_t ea = std::min(a.find('\n', la), a.size()), eb = std::min(b.find('\n', lb), b.size());
        const std::string x = a.substr(la, ea - la), y = b.substr(lb, eb - lb);
        if (x != y) {
            const auto tx = tokens(x), ty = tokens(y);
            std::string only;
            for (const auto& t : tx) {
                if (std::find(ty.begin(), ty.end(), t) == ty.end()) {
                    only += " -" + cut(t);
                }
            }
            for (const auto& t : ty) {
                if (std::find(tx.begin(), tx.end(), t) == tx.end()) {
                    only += " +" + cut(t);
                }
            }
            return "at " + (tx.empty() ? std::string() : tx[0]) + ":" + only;
        }
        la = ea + 1;
        lb = eb + 1;
    }
    return "";
}

//! `b` with the element identities removed that `a` doesn't have, line by line (the writer gives identities to the
//! elements of a score with part layouts, which link to them); `added` counts them
std::string withoutAddedIdentities(const std::string& a, const std::string& b, int& added)
{
    std::string out;
    size_t la = 0, lb = 0;
    while (lb < b.size()) {
        const size_t ea = std::min(a.find('\n', la), a.size()), eb = std::min(b.find('\n', lb), b.size());
        std::string y = b.substr(lb, eb - lb);
        const std::string x = la < a.size() ? a.substr(la, ea - la) : std::string();
        const size_t at = y.find(" originEid=\"");
        if (at != std::string::npos && x.find(" originEid=\"") == std::string::npos) {
            const size_t end = y.find('"', at + 12);
            y.erase(at, end == std::string::npos ? std::string::npos : end + 1 - at);
            ++added;
        }
        out += y + "\n";
        la = ea + 1;
        lb = eb + 1;
    }
    return out;
}

int generate(const std::string& in, const std::string& out)
{
    model::Model m;
    IdGenerator ids(1);
    std::string error;
    if (!io::openScore(in, m, ids, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    io::WriteReport w;
    if (!io::saveScore(m.snapshot(), out, error, &w)) {
        std::cerr << error << "\n";
        return 1;
    }
    std::cout << "generated; details written " << w.detailsWritten << ", dropped " << w.detailsDropped << "\n";
    return 0;
}

int dump(const std::string& path)
{
    model::Model m;
    IdGenerator ids(1);
    std::string error;
    if (!io::openScore(path, m, ids, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    std::cout << io::modelDump(m.snapshot());
    return 0;
}

int regenerate(const std::string& folder, const std::string& knownPath, bool privateNames, const std::string& outDir)
{
    std::map<std::string, bool> known;
    if (!knownPath.empty()) {
        std::ifstream k(knownPath);
        for (std::string l; std::getline(k, l);) {
            if (!l.empty() && l.back() == '\r') {
                l.pop_back();
            }
            const size_t hash = l.find(" #");
            if (hash != std::string::npos) {
                l = l.substr(0, hash);
            }
            while (!l.empty() && l.back() == ' ') {
                l.pop_back();
            }
            if (!l.empty() && l[0] != '#') {
                known[l] = false;
            }
        }
    }
    std::vector<fs::path> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!ec && it->is_regular_file() && isScore(it->path())) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    int v4 = 0, same = 0, unexpected = 0, fixedKnown = 0;
    long written = 0, dropped = 0, identitiesGiven = 0;
    int withIdentities = 0;
    std::map<std::string, int> kinds;
    std::vector<std::string> lines;
    int index = 0;
    for (const fs::path& p : files) {
        ++index;
        const std::string rel = fs::relative(p, folder).generic_string();
        const std::string label = privateNames ? "private #" + std::to_string(index) : rel;
        std::string bytes, error;
        io::zip::readFile(p.string(), bytes);
        io::ScoreFile file;
        if (!io::loadBytes(bytes, p.extension() == ".mscx", file, error) || !file.isVersion4()) {
            continue;
        }
        io::restoreFromEngineStore(file);
        model::Model a;
        IdGenerator idsA(1);
        io::ReadReport ra;
        if (!io::readScore(file, a, idsA, ra, error)) {
            continue;
        }
        ++v4;
        std::string problem;
        io::WriteReport w;
        io::ScoreFile gen = io::generateScoreFile(a.snapshot(), &w);
        written += w.detailsWritten;
        dropped += w.detailsDropped;
        const std::string genBytes = io::saveBytes(gen);
        io::ScoreFile back;
        model::Model b;
        IdGenerator idsB(1);
        io::ReadReport rb;
        if (!io::loadBytes(genBytes, false, back, error)) {
            problem = "generated file not readable: " + error;
        } else if (!io::readScore(back, b, idsB, rb, error)) {
            problem = "generated file not read into the model: " + error;
        } else if (w.detailsDropped) {
            problem = std::to_string(w.detailsDropped) + " details dropped without an edit";
        } else {
            const std::string la = io::musicListing(a.snapshot()), lb = io::musicListing(b.snapshot());
            const std::string da = io::modelDump(a.snapshot());
            std::string db = io::modelDump(b.snapshot());
            if (da != db) {
                int added = 0;
                db = withoutAddedIdentities(da, db, added);
                if (da == db) {
                    identitiesGiven += added;
                    ++withIdentities;
                }
            }
            // a score the reader already finds inconsistent (the corpus known issues) must stay as it was: the
            // same number of problems, none new
            const size_t before = model::checkInvariants(a.snapshot(), 1000).size();
            const auto invAll = model::checkInvariants(b.snapshot(), 1000);
            const auto inv = invAll.size() > before ? invAll : std::vector<std::string>();
            if (la != lb) {
                problem = "music differs: " + firstDifference(la, lb);
            } else if (da != db) {
                problem = "model differs: " + firstDifference(da, db);
            } else if (!inv.empty()) {
                problem = "regenerated model inconsistent: " + inv.front();
            } else if (io::saveBytes(io::generateScoreFile(b.snapshot())) != genBytes) {
                problem = "generating again gives another file";
            }
        }
        if (!outDir.empty()) {
            fs::create_directories(fs::path(outDir) / fs::path(rel).parent_path());
            std::ofstream((fs::path(outDir) / rel).replace_extension(".mscz"), std::ios::binary) << genBytes;
        }
        auto k = known.find(rel);
        if (problem.empty()) {
            ++same;
            if (k != known.end()) {
                ++fixedKnown;
                lines.push_back("- now passes (remove from the known list): " + label);
            }
        } else {
            kinds[problem.substr(0, problem.find(':'))]++;
            if (k != known.end()) {
                k->second = true;
                lines.push_back("- known: " + label + ": " + problem);
            } else {
                ++unexpected;
                lines.push_back("- **" + label + "**: " + problem);
            }
        }
    }
    std::cout << "## Model-based writing: read → generate → read again\n\n";
    std::cout << "| | |\n|---|---:|\n";
    std::cout << "| MuseScore 4.x scores | " << v4 << " |\n";
    std::cout << "| regenerated with the same music and model | " << same << " |\n";
    std::cout << "| different | " << (v4 - same) << " (" << unexpected << " not in the known list) |\n";
    std::cout << "| appearance details written / dropped | " << written << " / " << dropped << " |\n";
    std::cout << "| scores with part layouts whose elements were given identities (to link the parts) | " << withIdentities
              << " (" << identitiesGiven << " elements) |\n\n";
    for (const auto& [kind, n] : kinds) {
        std::cout << "- " << kind << ": " << n << "\n";
    }
    std::cout << "\n";
    for (const std::string& l : lines) {
        std::cout << l << "\n";
    }
    return (unexpected || fixedKnown) ? 1 : 0;
}

int corpus(const std::string& folder, bool privateNames, const std::string& reportPath, const std::string& knownPath)
{
    std::map<std::string, bool> known;   // path → seen failing
    if (!knownPath.empty()) {
        std::ifstream k(knownPath);
        for (std::string l; std::getline(k, l);) {
            if (!l.empty() && l.back() == '\r') {
                l.pop_back();
            }
            if (!l.empty() && l[0] != '#') {
                known[l] = false;
            }
        }
    }
    std::vector<fs::path> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!ec && it->is_regular_file() && isScore(it->path())) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    Totals t;
    const auto t0 = std::chrono::steady_clock::now();
    int index = 0;
    for (const fs::path& p : files) {
        ++index;
        const std::string label = privateNames ? "private #" + std::to_string(index) : fs::relative(p, folder).generic_string();
        ++t.files;
        std::string bytes;
        io::zip::readFile(p.string(), bytes);
        io::ScoreFile file;
        std::string error;
        if (!io::loadBytes(bytes, p.extension() == ".mscx", file, error)) {
            ++t.unreadable;
            t.failures.push_back(label + ": not read: " + error);
            continue;
        }
        // XML: the main score written back unchanged
        std::string original = bytes;
        if (!file.plainMscx) {
            for (const auto& e : file.entries) {
                if (e.name == file.mainEntry) {
                    original = e.data;
                }
            }
        }
        const std::string rewritten = io::xml::write(file.main);
        if (rewritten == original) {
            ++t.xmlIdentical;
        } else {
            io::xml::Document again;
            std::string e2;
            if (io::xml::parse(rewritten, again, e2) && io::xml::sameContent(again, file.main)) {
                ++t.xmlSameContent;
            } else {
                ++t.xmlDifferent;
                t.failures.push_back(label + ": XML content changed on writing");
            }
        }
        // archive: written and read back, every entry equal
        if (!file.plainMscx) {
            const std::string out = io::saveBytes(file);
            io::ScoreFile back;
            std::string e3;
            bool same = io::loadBytes(out, false, back, e3) && back.entries.size() == file.entries.size();
            for (size_t i = 0; same && i < back.entries.size(); ++i) {
                same = back.entries[i].name == file.entries[i].name
                       && (back.entries[i].data == file.entries[i].data || back.entries[i].name == file.mainEntry
                           || file.excerpts.count(back.entries[i].name));
            }
            same = same && io::xml::write(back.main) == rewritten;
            same ? ++t.zipOk : ++t.zipBad;
            if (!same) {
                t.failures.push_back(label + ": the archive didn't round-trip" + (e3.empty() ? "" : ": " + e3));
            }
        }
        // native reading (MuseScore 4.x)
        if (!file.isVersion4()) {
            continue;
        }
        ++t.v4;
        model::Model m;
        IdGenerator ids(1);
        io::ReadReport r;
        if (!io::readScore(file, m, ids, r, error)) {
            t.failures.push_back(label + ": not read into the model: " + error);
            continue;
        }
        ++t.v4Read;
        const auto problems = model::checkInvariants(m.snapshot(), 3);
        if (problems.empty()) {
            ++t.v4Consistent;
        } else {
            ++t.v4Problems;
            t.failures.push_back(label + ": " + problems.front());
        }
        t.chords += r.chords;
        t.notes += r.notes;
        t.rests += r.rests;
        t.tuplets += r.tuplets;
        t.graces += r.graces;
        t.ties += r.ties;
        t.spanners += r.spanners;
        t.lyrics += r.lyrics;
        t.measures += r.measures;
        for (const auto& [k, v] : r.modelled) {
            t.modelled[k] += v;
        }
        for (const auto& [k, v] : r.keptOnly) {
            t.keptOnly[k] += v;
        }
        for (const std::string& w : r.warnings) {
            t.warnings[w]++;
        }
    }

    std::string md;
    auto line = [&md](const std::string& s) { md += s + "\n"; };
    line("## je::io corpus check");
    line("");
    line("| | |");
    line("|---|---:|");
    line("| scores | " + std::to_string(t.files) + " |");
    line("| not readable as XML/archives | " + std::to_string(t.unreadable) + " |");
    line("| XML written back byte for byte | " + std::to_string(t.xmlIdentical) + " |");
    line("| XML written back with the same content (other layout) | " + std::to_string(t.xmlSameContent) + " |");
    line("| **XML content changed** | " + std::to_string(t.xmlDifferent) + " |");
    line("| archives round-tripped / failed | " + std::to_string(t.zipOk) + " / " + std::to_string(t.zipBad) + " |");
    line("| MuseScore 4.x scores | " + std::to_string(t.v4) + " |");
    line("| read into the model | " + std::to_string(t.v4Read) + " |");
    line("| consistent (every model rule) | " + std::to_string(t.v4Consistent) + " |");
    line("| **with a consistency problem** | " + std::to_string(t.v4Problems) + " |");
    line("| measures, chords, notes, rests | " + std::to_string(t.measures) + ", " + std::to_string(t.chords) + ", "
         + std::to_string(t.notes) + ", " + std::to_string(t.rests) + " |");
    line("| tuplets, grace chords, ties, spanners, lyrics | " + std::to_string(t.tuplets) + ", " + std::to_string(t.graces) + ", "
         + std::to_string(t.ties) + ", " + std::to_string(t.spanners) + ", " + std::to_string(t.lyrics) + " |");
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f s", ms(t0) / 1000.0);
    line(std::string("| time | ") + buf + " |");
    line("");
    std::vector<std::pair<long, std::string> > kept;
    for (const auto& [k, v] : t.keptOnly) {
        kept.push_back({ v, k });
    }
    std::sort(kept.rbegin(), kept.rend());
    line("Kept in the XML only (not yet in the model), most frequent:");
    for (size_t i = 0; i < kept.size() && i < 40; ++i) {
        line("- " + kept[i].second + ": " + std::to_string(kept[i].first));
    }
    line("");
    line("Warnings:");
    for (const auto& [w, n] : t.warnings) {
        line("- " + w + ": " + std::to_string(n));
    }
    line("");
    std::map<std::string, int> kinds;
    for (const std::string& f : t.failures) {
        std::string k = f.substr(f.find(": ") + 2);
        const size_t hash = k.find(" #");
        if (hash != std::string::npos) {
            k = k.substr(0, hash) + k.substr(k.find(':', hash));
        }
        kinds[k]++;
    }
    // known issues: failures in listed files don't count; a listed file that now passes is reported (update the list)
    int unexpected = 0;
    for (const std::string& f : t.failures) {
        const std::string file = f.substr(0, f.find(": "));
        auto k = known.find(file);
        if (k != known.end()) {
            k->second = true;
        } else {
            ++unexpected;
        }
    }
    int fixedKnown = 0;
    for (const auto& [file, seen] : known) {
        fixedKnown += seen ? 0 : 1;
    }
    if (!known.empty()) {
        line("Known issues (`" + knownPath + "`): " + std::to_string(known.size()) + " listed; failures outside the list: **"
             + std::to_string(unexpected) + "**; listed files now passing (remove them from the list): " + std::to_string(fixedKnown));
        line("");
    }
    line("Failures by kind:");
    for (const auto& [k, n] : kinds) {
        line("- " + k + ": " + std::to_string(n));
    }
    line("");
    line("Failures (first 60 of " + std::to_string(t.failures.size()) + "):");
    for (size_t i = 0; i < t.failures.size() && i < 60; ++i) {
        line("- " + t.failures[i]);
    }
    std::cout << md;
    if (!reportPath.empty()) {
        std::ofstream(reportPath) << md;
    }
    if (!known.empty()) {
        return (unexpected || fixedKnown) ? 1 : 0;
    }
    return (t.unreadable || t.xmlDifferent || t.zipBad || t.v4Problems || t.v4Read != t.v4) ? 1 : 0;
}

int rewrite(const std::string& in, const std::string& out)
{
    io::ScoreFile f;
    std::string error;
    if (!io::load(in, f, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    const auto restored = io::restoreFromEngineStore(f);
    io::updateEngineStore(f);
    if (!io::save(out, f)) {
        std::cerr << "can't write " << out << "\n";
        return 1;
    }
    std::cout << "written; restored from the engine store: " << restored.size() << "\n";
    return 0;
}

int setJubelleStyle(const std::string& in, const std::string& out)
{
    io::ScoreFile f;
    std::string error;
    if (!io::load(in, f, error) || !f.score()) {
        std::cerr << error << "\n";
        return 1;
    }
    std::vector<io::xml::Node*> styles { io::styleNode(f) };   // what MuseScore reads, and the score's own copy
    if (f.hasStyleEntry && f.score()->child("Style")) {
        styles.push_back(f.score()->child("Style"));
    }
    if (!styles.front()) {
        std::cerr << "no <Style>\n";
        return 1;
    }
    // non-default values for all 11 (defaults: false, false, 1, false, false, false, true, true, 0.5, false, "")
    const std::vector<std::pair<std::string, std::string> > values {
        { "jubelleCourtesyAccidentals", "1" }, { "jubelleCourtesyAccidentalsAllOctaves", "1" },
        { "jubelleCourtesyAccidentalsBars", "3" }, { "jubelleCourtesyAccidentalsOtherStaves", "1" },
        { "jubelleCourtesyAccidentalsParentheses", "1" }, { "jubelleMagneticLayout", "1" }, { "jubelleMagneticRows", "0" },
        { "jubelleSidewaysNudge", "0" }, { "jubelleNudgeLimit", "0.75" }, { "jubelleCollisionAvoidance", "1" },
        { "jubelleEngravingRules", "store-test" },
    };
    for (io::xml::Node* style : styles) {
        if (style->leaf) {
            style->leaf = false;
            style->selfClosing = false;
            style->raw.clear();
        }
        for (const auto& [name, value] : values) {
            if (io::xml::Node* v = style->child(name)) {
                v->raw = value;
            } else {
                io::xml::Node n;
                n.name = name;
                n.leaf = true;
                n.raw = value;
                style->children.push_back(std::move(n));
            }
        }
    }
    io::updateEngineStore(f);
    return io::save(out, f) ? 0 : 1;
}

int listing(const std::string& path)
{
    io::ScoreFile f;
    std::string error;
    model::Model m;
    IdGenerator ids(1);
    io::ReadReport r;
    if (!io::load(path, f, error) || !io::readScore(f, m, ids, r, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    std::cout << io::musicListing(m.snapshot());
    return 0;
}

int store(const std::string& path)
{
    io::ScoreFile f;
    std::string error;
    if (!io::load(path, f, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    std::cout << "store: " << io::metaTag(f, "jubelleEngine") << "\n";
    for (const std::string& n : io::restoreFromEngineStore(f)) {
        std::cout << "restored " << n << "\n";
    }
    const io::xml::Node* style = io::styleNode(f);
    for (const std::string& n : io::jubelleStyleNames()) {
        const io::xml::Node* v = style ? style->child(n) : nullptr;
        std::cout << n << " = " << (v ? v->raw : std::string("(default)")) << "\n";
    }
    return 0;
}

int bench(const std::string& path)
{
    const double h0 = heapMb();
    auto t0 = std::chrono::steady_clock::now();
    std::string bytes;
    io::zip::readFile(path, bytes);
    io::ScoreFile f;
    std::string error;
    if (!io::loadBytes(bytes, path.size() > 5 && path.substr(path.size() - 5) == ".mscx", f, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    const double tLoad = ms(t0);
    const double h1 = heapMb();
    t0 = std::chrono::steady_clock::now();
    model::Model m;
    IdGenerator ids(1);
    io::ReadReport r;
    if (!io::readScore(f, m, ids, r, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    const double tRead = ms(t0);
    const double h2 = heapMb();
    t0 = std::chrono::steady_clock::now();
    const std::string out = io::saveBytes(f);
    const double tSave = ms(t0);
    std::printf("{\"name\":\"io: unzip and parse the XML\",\"ms\":%.1f}\n", tLoad);
    std::printf("{\"name\":\"io: read into the model (%zu objects)\",\"ms\":%.1f}\n", r.objects, tRead);
    std::printf("{\"name\":\"io: open total\",\"ms\":%.1f}\n", tLoad + tRead);
    std::printf("{\"name\":\"io: write the file (XML and archive)\",\"ms\":%.1f}\n", tSave);
    if (h0 >= 0) {
        std::printf("{\"name\":\"io: heap of the XML trees\",\"mb\":%.1f}\n", h1 - h0);
        std::printf("{\"name\":\"io: heap of the model\",\"mb\":%.1f}\n", h2 - h1);
    }
    return 0;
}
}

//! The same operations as MuseScore's engine baseline (tools/jubelle_engine_tests/jubelle_baselines.cpp), on the
//! same scores: open (file to model), ten notes entered on the first staff from the middle of the score (a quarter,
//! C5, one command each, with the linked-staff hook), undo and redo of each, save (model to a 4.x archive), and the
//! model's memory. One line of JSON per score.
int benchScores(const std::vector<std::string>& paths)
{
    for (const std::string& path : paths) {
        const double h0 = heapMb();
        auto t0 = std::chrono::steady_clock::now();
        model::Model m;
        IdGenerator ids(1);
        std::string error;
        if (!io::openScore(path, m, ids, error)) {
            std::cerr << path << ": " << error << "\n";
            continue;
        }
        const double openMs = ms(t0);
        const double heap = heapMb() - h0;
        edit::Operations ops(m, ids);
        const std::vector<Id> measures = ops.measures();
        Id staff, voice;
        m.snapshot().forEachObject([&](const model::Object& o) {
            if (!staff.isValid() && o.kind() == model::Kind::Staff) {
                staff = o.id();
            }
        });
        for (Id v : m.find(staff)->children()) {
            if (m.find(v)->kind() == model::Kind::Voice && m.find(v)->intOr(model::fields::NUMBER, 0) == 0) {
                voice = v;
            }
        }
        edit::History history;
        const int edits = 10;
        double editMs = 0, undoMs = 0, redoMs = 0;
        int done = 0;
        for (size_t i = measures.size() / 2; done < edits && i < measures.size(); ++i, ++done) {
            t0 = std::chrono::steady_clock::now();
            edit::Transaction tx(m, "baseline edit", { edit::linkedStavesHook(m, ids) });
            ops.enterChord(tx, voice, ops.measureStart(measures[i]), { Fraction(1, 4), 0 }, { 72 });
            history.push(tx.commit());
            editMs += ms(t0);
        }
        for (int i = 0; i < done; ++i) {
            t0 = std::chrono::steady_clock::now();
            history.undo(m);
            undoMs += ms(t0);
        }
        for (int i = 0; i < done; ++i) {
            t0 = std::chrono::steady_clock::now();
            history.redo(m);
            redoMs += ms(t0);
        }
        t0 = std::chrono::steady_clock::now();
        const std::string bytes = io::saveBytes(io::generateScoreFile(m.snapshot()));
        const double saveMs = ms(t0);
        size_t staves = 0;
        m.snapshot().forEachObject([&](const model::Object& o) { staves += o.kind() == model::Kind::Staff; });
        std::printf("{\"score\": \"%s\", \"staves\": %zu, \"bars\": %zu, \"objects\": %zu, \"openMs\": %.1f, \"editMs\": %.3f, "
                    "\"undoMs\": %.3f, \"redoMs\": %.3f, \"saveMs\": %.1f, \"modelMb\": %.1f}\n",
                    path.c_str(), staves, measures.size(), m.snapshot().objectCount(), openMs, done ? editMs / done : 0,
                    done ? undoMs / done : 0, done ? redoMs / done : 0, saveMs, heap);
    }
    return 0;
}

//! The linked-staff check (S3): every 4.x score with a linked staff is edited on both staves through the engine
//! (a chord entered on the main staff, a note changed on the linked one) and saved, for MuseScore's engine to read
//! back: the two staves must hold the same music and every copy must be linked to its original
int linkedit(const std::string& folder, const std::string& outDir)
{
    std::vector<fs::path> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!ec && it->is_regular_file() && isScore(it->path())) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    int edited = 0;
    for (const fs::path& p : files) {
        const std::string rel = fs::relative(p, folder).generic_string();
        std::string bytes, error;
        io::zip::readFile(p.string(), bytes);
        io::ScoreFile file;
        if (!io::loadBytes(bytes, p.extension() == ".mscx", file, error) || !file.isVersion4()) {
            continue;
        }
        model::Model m;
        IdGenerator ids(1);
        io::ReadReport r;
        if (!io::readScore(file, m, ids, r, error)) {
            continue;
        }
        Id main, copy;
        m.snapshot().forEachObject([&](const model::Object& o) {
            if (!copy.isValid() && o.kind() == model::Kind::Staff && o.has(model::fields::LINKED_STAFF)) {
                copy = o.id();
                main = o.idOr(model::fields::LINKED_STAFF);
            }
        });
        if (!copy.isValid()) {
            continue;
        }
        edit::Operations ops(m, ids);
        auto voice1 = [&](Id staff) {
            for (Id v : m.find(staff)->children()) {
                if (m.find(v)->kind() == model::Kind::Voice && m.find(v)->intOr(model::fields::NUMBER, 0) == 0) {
                    return v;
                }
            }
            return Id();
        };
        const std::vector<Id> ms = ops.measures();
        if (ms.empty() || !voice1(main).isValid() || !voice1(copy).isValid()) {
            continue;
        }
        bool ok = true;
        try {
            {
                edit::Transaction tx(m, "Enter a chord", { edit::linkedStavesHook(m, ids) });
                ops.enterChord(tx, voice1(main), ops.measureStart(ms[ms.size() > 1 ? 1 : 0]), { Fraction(1, 4), 0 }, { 52, 55, 59 });
                tx.commit();
            }
            {
                edit::Transaction tx(m, "Change a pitch", { edit::linkedStavesHook(m, ids) });
                for (Id e : ops.eventsOf(voice1(copy))) {
                    if (m.find(e)->kind() == model::Kind::Chord && !m.find(e)->has(model::fields::GRACE)) {
                        for (Id n : m.find(e)->children()) {
                            if (m.find(n)->kind() == model::Kind::Note) {
                                ops.setPitch(tx, n, int(m.find(n)->intOr(model::fields::PITCH, 60)) + 2);
                                break;
                            }
                        }
                        break;
                    }
                }
                tx.commit();
            }
            {
                // a dynamic added on the main staff, a hairpin on the linked one: each appears on the other
                edit::Transaction tx(m, "Add a dynamic and a hairpin", { edit::linkedStavesHook(m, ids) });
                Id flow;
                for (Id c : m.find(m.score())->children()) {
                    if (m.find(c)->kind() == model::Kind::Flow) {
                        flow = c;
                        break;
                    }
                }
                edit::create(tx, ids, flow, { model::Kind::FlowItem, { { model::fields::SUBKIND, model::Text("dynamic") },
                                                                      { model::fields::ANCHOR, main },
                                                                      { model::fields::TIME, ops.measureStart(ms.back()) },
                                                                      { model::fields::TEXT, model::Text("mf") } } },
                             edit::Placement::ByTime);
                edit::create(tx, ids, m.score(), { model::Kind::Spanner, { { model::fields::SUBKIND, model::Text("hairpin") },
                                                                          { model::fields::ANCHOR, copy },
                                                                          { model::fields::TIME, ops.measureStart(ms.back()) },
                                                                          { model::fields::DURATION, Fraction(1, 4) },
                                                                          { model::fields::INT_VALUE, int64_t(0) } } });
                tx.commit();
            }
        } catch (const std::exception& e) {
            std::cout << "not edited: " << rel << ": " << e.what() << "\n";
            ok = false;
        }
        if (!ok) {
            continue;
        }
        const auto problems = model::checkInvariants(m.snapshot(), 20);
        const fs::path out = fs::path(outDir) / (rel.substr(0, rel.size() - p.extension().string().size()) + ".mscz");
        fs::create_directories(out.parent_path());
        if (!io::saveScore(m.snapshot(), out.string(), error)) {
            std::cout << "not saved: " << rel << ": " << error << "\n";
            continue;
        }
        ++edited;
        std::cout << "edited: " << rel << " (format " << file.formatVersion() << ")" << (problems.empty() ? "" : " PROBLEMS") << "\n";
        for (const std::string& pr : problems) {
            std::cout << "  " << pr << "\n";
        }
    }
    std::cout << edited << " scores with linked staves edited\n";
    return 0;
}

//! Every corpus score edited with the editing commands on its first pitched staff, saved for MuseScore to read back:
//! with the edited model's own listing next to it (<name>.mscz.je.txt), MuseScore must read the same music
int cmdedit(const std::string& folder, const std::string& outDir)
{
    namespace F = model::fields;
    std::vector<fs::path> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!ec && it->is_regular_file() && isScore(it->path())) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    int edited = 0;
    for (const fs::path& p : files) {
        const std::string rel = fs::relative(p, folder).generic_string();
        std::string bytes, error;
        io::zip::readFile(p.string(), bytes);
        io::ScoreFile file;
        if (!io::loadBytes(bytes, p.extension() == ".mscx", file, error) || !file.isVersion4()) {
            continue;
        }
        model::Model m;
        IdGenerator ids(1);
        io::ReadReport r;
        if (!io::readScore(file, m, ids, r, error)) {
            continue;
        }
        const model::Snapshot before = m.snapshot();
        edit::Operations ops(m, ids);
        edit::Commands cmd(ops);
        // the first staff of a pitched player that is not a copy, and its voice 1
        Id staff, voice;
        m.snapshot().forEachObject([&](const model::Object& o) {
            if (staff.isValid() || o.kind() != model::Kind::Staff || o.has(F::LINKED_STAFF)) {
                return;
            }
            const model::Object* player = m.find(o.parent());
            if (!player || player->kind() != model::Kind::Player || player->text(F::FAMILY) == "percussion"
                || o.text(F::SUBKIND) == "tablature") {
                return;
            }
            for (Id v : o.children()) {
                if (m.find(v)->kind() == model::Kind::Voice && m.find(v)->intOr(F::NUMBER, 0) == 0) {
                    staff = o.id();
                    voice = v;
                }
            }
        });
        const std::vector<Id> ms = ops.measures();
        if (!voice.isValid() || ms.size() < 2) {
            continue;
        }
        bool ok = true;
        try {
            const Fraction t = ops.measureStart(ms[1]);
            // JE_CMDEDIT_STEPS=n: only the first n edits (to find the one a reading difference comes from)
            const char* limit = std::getenv("JE_CMDEDIT_STEPS");
            int steps = 0;
            auto run = [&](const char* name, auto&& fn) {
                if (limit && ++steps > std::atoi(limit)) {
                    return;
                }
                edit::Transaction tx(m, name, { edit::linkedStavesHook(m, ids) });
                fn(tx);
                tx.commit();
            };
            // the chord at a time in voice 1 and its top note (found again after every step: an edit may replace them)
            auto chordAt = [&](const Fraction& at) {
                for (Id e : ops.eventsOf(voice)) {
                    const model::Object* o = m.find(e);
                    if (o->kind() == model::Kind::Chord && !o->has(F::GRACE) && o->fractionOr(F::TIME, Fraction(-1, 1)) == at) {
                        return e;
                    }
                }
                throw std::runtime_error("no chord where one was entered");
            };
            auto top = [&](Id chord) { return m.find(chord)->children().ids().back(); };
            const Fraction t2 = t + Fraction(1, 4);
            if (ms.size() > 2 && ops.measureStart(ms[2]) - t < Fraction(1, 2)) {
                throw std::runtime_error("measure 2 is shorter than a half note");
            }
            run("Enter", [&](edit::Transaction& tx) {
                ops.enterChord(tx, voice, t, { Fraction(1, 4), 0 }, { 61 });
                ops.enterChord(tx, voice, t2, { Fraction(1, 4), 0 }, { 67 });
            });
            run("Respell", [&](edit::Transaction& tx) { cmd.changeEnharmonicSpelling(tx, { top(chordAt(t)) }, true, true); });
            run("Third", [&](edit::Transaction& tx) { cmd.addInterval(tx, { top(chordAt(t)) }, 3); });
            run("Sharp", [&](edit::Transaction& tx) { cmd.changeAccidental(tx, top(chordAt(t2)), 1, true); });
            run("Staccato", [&](edit::Transaction& tx) { cmd.toggleArticulation(tx, { chordAt(t), chordAt(t2) }, "articStaccatoAbove"); });
            run("Grace", [&](edit::Transaction& tx) { cmd.addGraceNote(tx, top(chordAt(t2)), "acciaccatura", Fraction(1, 8)); });
            run("Slur", [&](edit::Transaction& tx) { cmd.addSlur(tx, chordAt(t), chordAt(t2)); });
            run("Hairpin", [&](edit::Transaction& tx) { cmd.addLine(tx, "hairPin", staff, 0, t, t + Fraction(1, 2), 1); });
            run("Glissando", [&](edit::Transaction& tx) { cmd.addNoteLine(tx, "glissando", top(chordAt(t)), top(chordAt(t2))); });
            run("Voice 2", [&](edit::Transaction& tx) { cmd.changeVoice(tx, { chordAt(t2) }, 1); });
        } catch (const std::exception& e) {
            std::cout << "not edited: " << rel << ": " << e.what() << "\n";
            ok = false;
        }
        if (!ok) {
            continue;
        }
        // only problems the edits made (some scores come with their own)
        std::vector<std::string> problems;
        {
            auto strip = [](std::string x) { return x.substr(x.find(':') == std::string::npos ? 0 : x.find(':')); };
            std::multiset<std::string> had;
            for (const std::string& pr : model::checkInvariants(before, 1000)) {
                had.insert(strip(pr));
            }
            for (const std::string& pr : model::checkInvariants(m.snapshot(), 1000)) {
                auto it = had.find(strip(pr));
                if (it != had.end()) {
                    had.erase(it);
                } else if (problems.size() < 20) {
                    problems.push_back(pr);
                }
            }
        }
        const fs::path out = fs::path(outDir) / (rel.substr(0, rel.size() - p.extension().string().size()) + ".mscz");
        fs::create_directories(out.parent_path());
        if (!io::saveScore(m.snapshot(), out.string(), error)) {
            std::cout << "not saved: " << rel << ": " << error << "\n";
            continue;
        }
        std::ofstream(out.string() + ".je.txt") << io::musicListing(m.snapshot());
        ++edited;
        std::cout << "edited: " << rel << (problems.empty() ? "" : " PROBLEMS") << "\n";
        for (const std::string& pr : problems) {
            std::cout << "  " << pr << "\n";
        }
    }
    std::cout << edited << " scores edited with the commands\n";
    return 0;
}

//! Every 4.x score under `folder` read into a model (sorted by path); `fn(rel, path, model, ids)`
template<typename Fn>
void forEachScore(const std::string& folder, Fn&& fn)
{
    std::vector<fs::path> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!ec && it->is_regular_file() && isScore(it->path())) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    for (const fs::path& p : files) {
        const std::string rel = fs::relative(p, folder).generic_string();
        std::string bytes, error;
        io::zip::readFile(p.string(), bytes);
        io::ScoreFile file;
        if (!io::loadBytes(bytes, p.extension() == ".mscx", file, error) || !file.isVersion4()) {
            continue;
        }
        model::Model m;
        IdGenerator ids(1);
        io::ReadReport r;
        if (!io::readScore(file, m, ids, r, error)) {
            continue;
        }
        fn(rel, p, m, ids);
    }
}

//! Every corpus score given a time signature by the engine (changeTimeSignature: measure index, num, den), with the
//! edited model's listing next to each saved file (<name>.mscz.je.txt, headed "# timesig changed|refused"), for
//! comparing with MuseScore's own command (the reference listing with JUBELLE_REFERENCE_TIMESIG set the same)
int tsedit(const std::string& folder, const std::string& outDir, int index, int num, int den)
{
    int changed = 0, refused = 0;
    forEachScore(folder, [&](const std::string& rel, const fs::path& p, model::Model& m, IdGenerator& ids) {
        edit::Operations ops(m, ids);
        edit::Commands cmd(ops);
        const model::Snapshot before = m.snapshot();
        bool ok = false;
        try {
            edit::Transaction tx(m, "Time signature", { edit::linkedStavesHook(m, ids) });
            const auto start = std::chrono::steady_clock::now();
            ok = cmd.changeTimeSignature(tx, size_t(index), num, den);
            if (ok) {
                tx.commit();
            }
            if (std::getenv("JE_TSEDIT_TIME")) {
                std::cout << "time: " << rel << " " << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() << " ms\n";
            }
        } catch (const std::exception& e) {
            std::cout << "failed: " << rel << ": " << e.what() << "\n";
            return;
        }
        std::vector<std::string> problems;
        {
            std::multiset<std::string> had;
            auto strip = [](std::string x) { return x.substr(x.find(':') == std::string::npos ? 0 : x.find(':')); };
            for (const std::string& pr : model::checkInvariants(before, 1000)) {
                had.insert(strip(pr));
            }
            for (const std::string& pr : model::checkInvariants(m.snapshot(), 1000)) {
                auto it = had.find(strip(pr));
                if (it != had.end()) {
                    had.erase(it);
                } else if (problems.size() < 20) {
                    problems.push_back(pr);
                }
            }
        }
        const fs::path out = fs::path(outDir) / (rel.substr(0, rel.size() - p.extension().string().size()) + ".mscz");
        fs::create_directories(out.parent_path());
        std::string error;
        if (!io::saveScore(m.snapshot(), out.string(), error)) {
            std::cout << "not saved: " << rel << ": " << error << "\n";
            return;
        }
        std::ofstream(out.string() + ".je.txt") << "# timesig " << (ok ? "changed" : "refused") << "\n" << io::musicListing(m.snapshot());
        (ok ? changed : refused)++;
        std::cout << (ok ? "changed: " : "refused: ") << rel << (problems.empty() ? "" : " PROBLEMS") << "\n";
        for (const std::string& pr : problems) {
            std::cout << "  " << pr << "\n";
        }
    });
    std::cout << changed << " scores changed, " << refused << " refused\n";
    return 0;
}

//! Every corpus score's chord symbols as the engine understands and realizes them (edit/chordsymbol), one line each
//! in the reference listing's JUBELLE_REFERENCE_HARMONY form, written to <out>/<name>.txt: for comparing with MuseScore
int chordlist(const std::string& folder, const std::string& outDir)
{
    int scores = 0;
    forEachScore(folder, [&](const std::string& rel, const fs::path&, model::Model& m, IdGenerator& ids) {
        edit::Operations ops(m, ids);
        edit::Commands cmd(ops);
        const auto hs = cmd.chordSymbols();
        std::ostringstream out;
        for (size_t i = 0; i < hs.size(); ++i) {
            const auto& h = hs[i];
            if (h.inFretDiagram || h.polychord || h.root < -8) {
                continue;   // (MuseScore's listing: the realizable chord symbols by themselves)
            }
            edit::chords::ParsedChord p;
            p.parse(h.name);
            edit::chords::Next next;
            for (size_t j = i + 1; j < hs.size() && !next.present; ++j) {
                if (hs[j].track == h.track && h.time < hs[j].time) {
                    next.present = true;
                    next.rootTpc = hs[j].root;
                    next.chord.parse(hs[j].name);
                }
            }
            out << "# harmony " << h.time.toString() << " " << h.track << " " << h.root << "/" << h.bass << " " << h.name << " | "
                << p.quality << " " << p.extension << " [";
            for (const std::string& mod : p.modifiers) {
                out << " " << mod;
            }
            out << " ]" << (p.understandable ? "" : " ?");
            for (int literal = 1; literal >= 0; --literal) {
                for (int v = 0; v <= 6; ++v) {
                    out << (v ? " " : " |");
                    for (const auto& [pitch, tpc] : edit::chords::generateNotes(p, h.root, h.bass, literal, edit::chords::Voicing(v), 0, next)) {
                        out << pitch << ":" << tpc << ",";
                    }
                }
            }
            out << "\n";
        }
        const fs::path f = fs::path(outDir) / (rel + ".txt");
        fs::create_directories(f.parent_path());
        std::ofstream(f) << out.str();
        ++scores;
    });
    std::cout << scores << " scores\n";
    return 0;
}

//! Every corpus score with measures split (split <index>: at the middle of that measure, where a chord or rest
//! starts) or joined (join <first> <last>) by the engine, its listing next to each saved file headed "# measures
//! changed|unchanged|refused", for comparing with MuseScore's SplitJoinMeasure (JUBELLE_REFERENCE_SPLIT / _JOIN)
//! Lyric entry keys, as typed: text, then '-' (next syllable), '_' (extension), ' ' (next word), '<' (previous word),
//! '/' (Enter: a new verse), '^' and '!' (verse up and down); typing replaces the lyric's text (MuseScore selects it
//! on arrival); the entry is finished at the end (Escape)
void lyricsScript(edit::LyricsEntry& entry, edit::Transaction& tx, const std::string& script)
{
    std::string typed;   // since arriving at the lyric (a key that doesn't move keeps it)
    auto key = [&](char k) {
        if (!typed.empty()) {
            entry.setText(typed);
        }
        if (!entry.editing()) {
            typed.clear();
            return;
        }
        const Id was = entry.current();
        struct Clear {
            std::string& t;
            edit::LyricsEntry& e;
            Id was;
            ~Clear()
            {
                if (!e.editing() || e.current() != was) {
                    t.clear();
                }
            }
        } clear { typed, entry, was };
        switch (k) {
        case '-': entry.nextSyllable(tx); break;
        case '_': entry.melisma(tx); break;
        case ' ': entry.nextWord(tx); break;
        case '<': entry.nextWord(tx, true); break;
        case '/': entry.addVerse(tx); break;
        case '^': entry.changeVerse(tx, true); break;
        case '!': entry.changeVerse(tx, false); break;
        default: break;
        }
    };
    for (char c : script) {
        if (std::string("-_ </^!").find(c) != std::string::npos) {
            key(c);
        } else {
            typed += c;
        }
    }
    if (!typed.empty()) {
        entry.setText(typed);
    }
    entry.finish(tx);
}

int measedit(const std::string& folder, const std::string& outDir, const std::string& op, int a, int b, int c, int d)
{
    namespace F = model::fields;
    std::map<std::string, int> count;
    forEachScore(folder, [&](const std::string& rel, const fs::path& p, model::Model& m, IdGenerator& ids) {
        edit::Operations ops(m, ids);
        edit::Commands cmd(ops);
        const model::Snapshot before = m.snapshot();
        const std::string listingBefore = io::musicListing(before, op == "slashfill" || op == "slashrhythm" || op == "movestaff", op == "lyrics", op == "addtext");
        const std::vector<Id> ms = ops.measures();
        std::string label = "refused";
        try {
            edit::Transaction tx(m, op, { edit::linkedStavesHook(m, ids) });
            bool ok = false;
            if (op == "split" && size_t(a) < ms.size()) {
                const Fraction tick = ops.measureStart(ms[size_t(a)]) + (ops.measureEnd(ms[size_t(a)]) - ops.measureStart(ms[size_t(a)])) * Fraction(1, 2);
                const bool starts = cmd.canSplitAt(tick);
                if (starts) {
                    label = "unchanged";
                    ok = cmd.splitMeasure(tx, tick);
                }
            } else if (op == "join" && a < b && size_t(b) < ms.size()) {
                label = "unchanged";
                ok = cmd.joinMeasures(tx, size_t(a), size_t(b));
            } else if ((op == "explode" || op == "implode") && size_t(c) < ms.size() && size_t(d) < ms.size()) {
                // staves a … b-1, measures c … d (as a range selection of whole measures)
                size_t staves = 0;
                m.snapshot().forEachObject([&](const model::Object& o) { staves += o.kind() == model::Kind::Staff; });
                if (a < b && size_t(b) <= staves) {
                    label = "unchanged";
                    const Fraction from = ops.measureStart(ms[size_t(c)]), to = ops.measureEnd(ms[size_t(d)]);
                    ok = op == "explode" ? cmd.explode(tx, size_t(a), size_t(b), from, to) >= 0
                                         : cmd.implode(tx, size_t(a), size_t(b), from, to);
                }
            } else if (op == "realize" && size_t(c) < ms.size() && size_t(d) < ms.size()) {
                // the chord symbols of staves a … b-1, measures c … d; JE_REALIZE="voicing duration literal" overrides
                // their own settings (MuseScore's dialog)
                const std::vector<Id> all = cmd.stavesInOrder();
                if (a < b && size_t(b) <= all.size()) {
                    label = "unchanged";
                    const Fraction from = ops.measureStart(ms[size_t(c)]), to = ops.measureEnd(ms[size_t(d)]);
                    edit::Commands::RealizeOptions o;
                    if (const char* spec = std::getenv("JE_REALIZE")) {
                        std::istringstream in(spec);
                        int lit = 1;
                        in >> o.voicing >> o.duration >> lit;
                        o.literal = lit != 0;
                        o.override = true;
                    }
                    bool concert = false;
                    for (const auto& [name, value] : model::unpackPairs(m.find(m.score())->text(F::STYLE))) {
                        concert = concert || (name == "concertPitch" && value == "1");
                    }
                    std::vector<Id> hs;
                    for (const auto& h : cmd.chordSymbols()) {
                        const Id st = m.find(h.item)->idOr(F::ANCHOR);
                        if (from <= h.time && h.time < to
                            && std::find(all.begin() + a, all.begin() + b, st) != all.begin() + b) {
                            hs.push_back(h.item);
                        }
                    }
                    ok = cmd.realizeChordSymbols(tx, hs, o, concert) > 0;
                }
            } else if (op == "movestaff" && size_t(c) < ms.size() && size_t(d) < ms.size()) {
                // the chords and rests of staves a … b-1, measures c … d, up (JE_MOVESTAFF=0) or down (1)
                const std::vector<Id> all = cmd.stavesInOrder();
                if (a < b && size_t(b) <= all.size()) {
                    label = "unchanged";
                    const Fraction from = ops.measureStart(ms[size_t(c)]), to = ops.measureEnd(ms[size_t(d)]);
                    std::vector<Id> events;
                    for (size_t si = size_t(a); si < size_t(b); ++si) {
                        for (Id v : m.find(all[si])->children()) {
                            if (m.find(v)->kind() != model::Kind::Voice) {
                                continue;
                            }
                            for (Id e : ops.eventsOf(v)) {
                                const Fraction t = m.find(e)->fractionOr(F::TIME, Fraction());
                                if (from <= t && t < to) {
                                    events.push_back(e);
                                }
                            }
                        }
                    }
                    const char* dir = std::getenv("JE_MOVESTAFF");
                    ok = cmd.moveToStaff(tx, events, !(dir && std::string(dir) == "1")) > 0;
                }
            } else if ((op == "slashfill" || op == "slashrhythm") && size_t(c) < ms.size() && size_t(d) < ms.size()) {
                // staves a … b-1, measures c … d selected
                const std::vector<Id> all = cmd.stavesInOrder();
                if (a < b && size_t(b) <= all.size()) {
                    label = "unchanged";
                    const Fraction from = ops.measureStart(ms[size_t(c)]), to = ops.measureEnd(ms[size_t(d)]);
                    if (op == "slashfill") {
                        // as the score is shown: in concert pitch when its style says so
                        bool concert = false;
                        for (const auto& [name, value] : model::unpackPairs(m.find(m.score())->text(F::STYLE))) {
                            concert = concert || (name == "concertPitch" && value == "1");
                        }
                        ok = cmd.slashFill(tx, size_t(a), size_t(b), from, to, concert) > 0;
                    } else {
                        // the selection's elements: its chords and rests (MuseScore selects their notes)
                        std::vector<Id> events;
                        for (size_t si = size_t(a); si < size_t(b); ++si) {
                            for (Id v : m.find(all[si])->children()) {
                                if (m.find(v)->kind() != model::Kind::Voice) {
                                    continue;
                                }
                                for (Id e : ops.eventsOf(v)) {
                                    const Fraction t = m.find(e)->fractionOr(F::TIME, Fraction());
                                    if (from <= t && t < to && !m.find(e)->has(F::GRACE)) {
                                        events.push_back(e);
                                    }
                                }
                            }
                        }
                        ok = cmd.slashRhythm(tx, events);
                    }
                }
            } else if (op == "addtext") {
                // Score::addText (JE_ADDTEXT: "<style> <text>") on the first chord (its top note) or rest of voice 1 of
                // staff a in measure b
                const std::vector<Id> all = cmd.stavesInOrder();
                const char* spec = std::getenv("JE_ADDTEXT");
                if (spec && size_t(a) < all.size() && size_t(b) < ms.size()) {
                    std::string style = spec, text;
                    if (const size_t sp = style.find(' '); sp != std::string::npos) {
                        text = style.substr(sp + 1);
                        style = style.substr(0, sp);
                    }
                    Id dest;
                    for (Id v : m.find(all[size_t(a)])->children()) {
                        const model::Object* vo = m.find(v);
                        if (vo->kind() != model::Kind::Voice || vo->intOr(F::NUMBER, 0) != 0) {
                            continue;
                        }
                        for (Id e : ops.eventsOf(v)) {
                            const model::Object* eo = m.find(e);
                            const Fraction t = eo->fractionOr(F::TIME, Fraction());
                            if (eo->has(F::GRACE) || t < ops.measureStart(ms[size_t(b)]) || t >= ops.measureEnd(ms[size_t(b)])) {
                                continue;
                            }
                            dest = e;
                            int64_t top = -1;
                            for (Id n : eo->children()) {
                                const model::Object* no = m.find(n);
                                if (no->kind() == model::Kind::Note && no->intOr(F::PITCH, 0) > top) {
                                    top = no->intOr(F::PITCH, 0);
                                    dest = n;
                                }
                            }
                            break;
                        }
                    }
                    if (dest.isValid()) {
                        ok = cmd.addText(tx, style, dest, text).isValid();
                        label = ok ? "unchanged" : "refused";   // nothing made: refused, as MuseScore's
                    }
                }
            } else if (op == "lyrics") {
                // lyric entry typed on staff a from the first chord or rest of measure b's voice 1 (b = -1: editing
                // the score's first lyric on staff a), with the keys of JE_LYRICS (lyricsScript)
                const std::vector<Id> all = cmd.stavesInOrder();
                if (size_t(a) < all.size() && (b < 0 || size_t(b) < ms.size())) {
                    Id start, lyric;
                    for (Id v : m.find(all[size_t(a)])->children()) {
                        const model::Object* vo = m.find(v);
                        if (vo->kind() != model::Kind::Voice || vo->intOr(F::NUMBER, 0) != 0) {
                            continue;
                        }
                        for (Id e : ops.eventsOf(v)) {
                            const model::Object* eo = m.find(e);
                            const Fraction t = eo->fractionOr(F::TIME, Fraction());
                            if (eo->has(F::GRACE)) {
                                continue;
                            }
                            if (b >= 0 && ops.measureStart(ms[size_t(b)]) <= t && t < ops.measureEnd(ms[size_t(b)])) {
                                start = e;
                                break;
                            }
                            if (b < 0) {
                                for (Id c : eo->children()) {
                                    if (m.find(c)->text(F::SUBKIND) == "lyric") {
                                        lyric = c;
                                        break;
                                    }
                                }
                                if (lyric.isValid()) {
                                    break;
                                }
                            }
                        }
                    }
                    const char* script = std::getenv("JE_LYRICS");
                    edit::LyricsEntry entry(ops);
                    if ((start.isValid() && entry.start(tx, start)) || (lyric.isValid() && entry.edit(lyric))) {
                        label = "unchanged";
                        lyricsScript(entry, tx, script ? script : "");
                        ok = true;
                    }
                }
            }
            if (ok) {
                tx.commit();
            }
        } catch (const std::exception& e) {
            std::cout << "failed: " << rel << ": " << e.what() << "\n";
            return;
        }
        const bool appearance = op == "slashfill" || op == "slashrhythm" || op == "movestaff";
        const std::string listingAfter = io::musicListing(m.snapshot(), appearance, op == "lyrics", op == "addtext");
        if (label != "refused" && listingAfter != listingBefore) {
            label = "changed";
        }
        const auto problems = model::checkInvariants(m.snapshot(), 1000);
        const auto had = model::checkInvariants(before, 1000);
        const bool newProblems = problems.size() > had.size();
        const fs::path out = fs::path(outDir) / (rel.substr(0, rel.size() - p.extension().string().size()) + ".mscz");
        fs::create_directories(out.parent_path());
        std::string error;
        if (!io::saveScore(m.snapshot(), out.string(), error)) {
            std::cout << "not saved: " << rel << ": " << error << "\n";
            return;
        }
        std::ofstream(out.string() + ".je.txt") << "# measures " << label << "\n" << listingAfter;
        count[label]++;
        std::cout << label << ": " << rel << (newProblems ? " PROBLEMS" : "") << "\n";
        if (newProblems) {
            for (const std::string& pr : problems) {
                std::cout << "  " << pr << "\n";
            }
        }
    });
    for (const auto& [k, v] : count) {
        std::cout << v << " " << k << "\n";
    }
    return 0;
}

//! The play order of every 4.x score in a folder (perform::RepeatList), in the reference tool's format
//! ("# repeats: first-last/pass…", measure indices from 0): <out>/<score>.txt, and "listed: <score>" printed
int repeats(const std::string& folder, const std::string& outDir)
{
    int done = 0;
    forEachScore(folder, [&](const std::string& rel, const fs::path&, model::Model& m, IdGenerator&) {
        const perform::RepeatList rl = perform::RepeatList::build(m.snapshot());
        std::string line = "# repeats:";
        for (const perform::RepeatSegment& rs : rl.segments()) {
            line += " " + std::to_string(rs.measures.front()) + "-" + std::to_string(rs.measures.back()) + "/"
                    + std::to_string(rs.playbackCount);
        }
        const fs::path out = fs::path(outDir) / (rel + ".txt");
        fs::create_directories(out.parent_path());
        std::ofstream(out) << line << "\n";
        std::cout << "listed: " << rel << "\n";
        ++done;
    });
    std::cerr << "repeats: " << done << " scores\n";
    return 0;
}

//! The crash-recovery check (S4): every 4.x score is opened in an edit session with its journal, edited at random
//! (notes entered, pitches changed, undo, redo; saved halfway), and left without closing, as a crash leaves it. The
//! journal is then recovered in a new session, which must give back exactly the score as it was (every object, id
//! and field, and the undo history) and save the same file. Times: starting the journal (on opening and saving),
//! journalling an edit, recovering. One line of JSON per score with --json.
int journalcheck(const std::string& folder, const std::string& workDir, bool json)
{
    std::vector<fs::path> files;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(folder, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!ec && it->is_regular_file() && isScore(it->path())) {
            files.push_back(it->path());
        }
    }
    std::sort(files.begin(), files.end());
    fs::create_directories(workDir);
    int checked = 0, failed = 0, skipped = 0;
    double worstStart = 0, worstRecord = 0;
    std::string worstStartScore, worstRecordScore;
    for (const fs::path& p : files) {
        const std::string rel = fs::relative(p, folder).generic_string();
        const fs::path copy = fs::path(workDir) / ("score" + p.extension().string());
        fs::copy_file(p, copy, fs::copy_options::overwrite_existing, ec);
        std::string error;
        std::string journal, liveDump, liveFile;
        std::vector<std::pair<uint64_t, int>> liveIds;
        size_t liveIndex = 0, liveSize = 0;
        bool liveClean = false;
        double startMs = 0, recordMs = 0, saveMs = 0;
        int records = 0;
        uint64_t bytes = 0;
        {
            edit::EditSession s;
            auto t0 = std::chrono::steady_clock::now();
            if (!s.open(copy.string(), workDir, error)) {
                ++skipped;   // not a 4.x score
                continue;
            }
            const double openMs = ms(t0);
            // the journal's start alone: written again, as a save does
            {
                edit::Journal j;
                edit::JournalBase base;
                edit::describeFile(copy.string(), base);
                t0 = std::chrono::steady_clock::now();
                j.start((fs::path(workDir) / "start.jej").string(), base, s.model().snapshot(), s.history(), error);
                startMs = ms(t0);
                bytes = j.bytes();
                j.close(true);
            }
            (void)openMs;
            uint32_t seed = 2166136261u;   // from the score's name (FNV-1a), the same on every platform
            for (unsigned char c : rel) {
                seed = (seed ^ c) * 16777619u;
            }
            std::mt19937 rng(seed);
            auto pick = [&](int n) { return int(rng() % uint32_t(n)); };   // the same edits on every platform
            edit::Operations ops(s.model(), s.ids());
            std::vector<Id> voices = {};
            s.model().snapshot().forEachObject([&](const model::Object& o) {
                if (o.kind() == model::Kind::Voice && o.intOr(model::fields::NUMBER, 0) == 0) {
                    voices.push_back(o.id());
                }
            });
            std::sort(voices.begin(), voices.end());
            const std::vector<Id> measures = ops.measures();
            if (voices.empty() || measures.empty()) {
                std::cout << "skipped (no music to edit): " << rel << "\n";
                ++skipped;
                s.close();
                continue;
            }
            for (int i = 0; i < 24; ++i) {
                if (i == 12) {
                    t0 = std::chrono::steady_clock::now();
                    if (!s.save(error)) {
                        std::cout << "FAIL " << rel << ": saving: " << error << "\n";
                        break;
                    }
                    saveMs = ms(t0);
                    continue;
                }
                const int what = pick(10);
                t0 = std::chrono::steady_clock::now();
                if (what < 2 && s.canUndo()) {
                    s.undo();
                } else if (what == 2 && s.canRedo()) {
                    s.redo();
                } else {
                    try {
                        std::vector<Id> notes;
                        if (what < 5) {
                            s.model().snapshot().forEachObject([&](const model::Object& o) {
                                if (o.kind() == model::Kind::Note) {
                                    notes.push_back(o.id());
                                }
                            });
                            std::sort(notes.begin(), notes.end());
                        }
                        t0 = std::chrono::steady_clock::now();
                        if (!notes.empty()) {
                            edit::Transaction tx(s.model(), "Change pitch", { edit::linkedStavesHook(s.model(), s.ids()) });
                            ops.setPitch(tx, notes[size_t(pick(int(notes.size())))], 48 + pick(36));
                            s.commit(tx);
                        } else {
                            edit::Transaction tx(s.model(), "Enter note", { edit::linkedStavesHook(s.model(), s.ids()) });
                            const Fraction t = ops.measureStart(measures[size_t(pick(int(measures.size())))]);
                            ops.enterChord(tx, voices[size_t(pick(int(voices.size())))], t, { Fraction(1, 4), 0 },
                                           { 55 + pick(24) });
                            s.commit(tx);
                        }
                    } catch (const std::exception&) {
                        continue;   // an edit the score doesn't allow there (a measure repeat, a tablature limit…)
                    }
                }
                recordMs += ms(t0);   // the edit and its journal record (the record alone is measured below)
                ++records;
            }
            // the journal record alone: the last step written again to a scratch journal
            if (s.history().size()) {
                edit::Journal j;
                edit::JournalBase base;
                j.start((fs::path(workDir) / "record.jej").string(), base, model::Snapshot(), edit::History(), error);
                t0 = std::chrono::steady_clock::now();
                for (int k = 0; k < 20; ++k) {
                    j.recordStep(s.history().step(s.history().size() - 1));
                }
                recordMs = ms(t0) / 20;
                j.close(true);
            } else {
                recordMs = 0;
            }
            liveDump = io::modelDump(s.model().snapshot());
            s.model().snapshot().forEachObject([&](const model::Object& o) { liveIds.emplace_back(o.id().value(), int(o.kind())); });
            std::sort(liveIds.begin(), liveIds.end());
            liveIndex = s.history().index();
            liveSize = s.history().size();
            liveClean = !s.isModified();
            liveFile = io::saveBytes(io::generateScoreFile(s.model().snapshot()));
            journal = s.journalPath();
        }   // ended without closing: the journal stays, as after a crash
        edit::EditSession r;
        edit::RecoveryReport report;
        auto t0 = std::chrono::steady_clock::now();
        const bool ok = r.recover(journal, report, error);
        const double recoverMs = ms(t0);
        std::string why;
        if (!ok) {
            why = "recovering: " + error;
        } else {
            std::vector<std::pair<uint64_t, int>> ids;
            r.model().snapshot().forEachObject([&](const model::Object& o) { ids.emplace_back(o.id().value(), int(o.kind())); });
            std::sort(ids.begin(), ids.end());
            if (io::modelDump(r.model().snapshot()) != liveDump) {
                why = "the recovered model differs";
            } else if (ids != liveIds) {
                why = "the recovered ids differ";
            } else if (r.history().index() != liveIndex || r.history().size() != liveSize || r.isModified() == liveClean) {
                why = "the recovered history differs";
            } else if (io::saveBytes(io::generateScoreFile(r.model().snapshot())) != liveFile) {
                why = "the recovered score saves differently";
            } else if (report.truncated || report.baseChanged) {
                why = "the journal was read as cut short or for a changed file";
            }
        }
        r.close();
        fs::remove(journal, ec);
        ++checked;
        if (!why.empty()) {
            ++failed;
            std::cout << "FAIL " << rel << ": " << why << "\n";
        }
        if (startMs > worstStart) {
            worstStart = startMs;
            worstStartScore = rel;
        }
        if (recordMs > worstRecord) {
            worstRecord = recordMs;
            worstRecordScore = rel;
        }
        if (json) {
            std::printf("{\"score\": \"%s\", \"objects\": %zu, \"journalKb\": %.1f, \"startMs\": %.2f, \"recordMs\": %.3f, "
                        "\"saveMs\": %.1f, \"recoverMs\": %.1f, \"edits\": %d}\n",
                        rel.c_str(), report.objects, bytes / 1024.0, startMs, recordMs, saveMs, recoverMs, records);
        }
    }
    std::cerr << "journal: " << checked << " scores recovered, " << failed << " failed, " << skipped
              << " skipped; slowest journal start " << worstStart << " ms (" << worstStartScore << "), slowest record "
              << worstRecord << " ms (" << worstRecordScore << ")\n";
    return failed ? 1 : 0;
}

//! Recovers a journal left by a crash and saves the score to `out` (the journal is left as it was)
int recoverJournal(const std::string& journal, const std::string& out)
{
    const edit::RecoveryReport info = edit::inspectJournal(journal);
    if (!info.ok) {
        std::cerr << info.error << "\n";
        return 1;
    }
    std::cout << "journal for " << info.base.file << (info.baseChanged ? " (the file has changed since)" : "") << ": "
              << info.objects << " objects, " << info.historySteps << " earlier steps, " << info.steps << " steps, "
              << info.undos << " undos, " << info.redos << " redos" << (info.truncated ? ", the last record cut short" : "")
              << "\n";
    const std::string kept = out + ".journal";
    fs::copy_file(journal, kept, fs::copy_options::overwrite_existing);
    edit::EditSession r;
    edit::RecoveryReport report;
    std::string error;
    if (!r.recover(kept, report, error) || !r.saveAs(out, error)) {
        std::cerr << error << "\n";
        return 1;
    }
    r.close();
    std::cout << "saved " << out << "\n";
    return 0;
}

//! Each listed score's style as the engine reads it, written as MuseScore writes a whole style (MStyle::save without
//! leaving out defaults), to compare with MuseScore's reading (je_style_reference.py)
int stylelist(const std::string& root, const std::string& list, const std::string& out)
{
    std::ifstream in(list);
    int done = 0;
    for (std::string rel; std::getline(in, rel);) {
        if (rel.empty()) {
            continue;
        }
        model::Model m;
        IdGenerator ids(1);
        std::string error;
        const fs::path target = fs::path(out) / (rel + ".style");
        fs::create_directories(target.parent_path());
        std::ofstream o(target);
        if (!io::openScore(root + "/" + rel, m, ids, error)) {
            o << "NOT READ\n";
            continue;
        }
        const style::Style st = style::scoreStyle(m.snapshot());
        o << "<Style>\n";
        for (int i = 0; i < int(style::Sid::STYLES); ++i) {
            const style::Sid sid = style::Sid(i);
            if (sid == style::Sid::spatium) {
                continue;
            }
            const style::StyleDef& d = style::Style::def(sid);
            const std::string_view type = d.type;
            std::ostringstream v;
            switch (d.kind) {
            case style::ValueKind::Number:
                if (type == "REAL" || type == "SPATIUM" || type == "MILLIMETRE") {
                    v << st.value(sid);
                } else {
                    v << st.integer(sid);
                }
                o << "  <" << d.name << ">" << v.str() << "</" << d.name << ">\n";
                break;
            case style::ValueKind::Text:
                o << "  <" << d.name << ">" << io::xml::encode(st.text(sid)) << "</" << d.name << ">\n";
                break;
            case style::ValueKind::Pair: {
                const style::PointF p = st.point(sid);
                const char* a = type == "POINT" ? "x" : "w";
                const char* b = type == "POINT" ? "y" : "h";
                v << "<" << d.name << " " << a << "=\"" << p.x << "\" " << b << "=\"" << p.y << "\"/>";
                o << "  " << v.str() << "\n";
            } break;
            case style::ValueKind::Color: {
                const uint32_t c = st.color(sid);
                o << "  <" << d.name << " r=\"" << (c >> 24) << "\" g=\"" << ((c >> 16) & 0xFF) << "\" b=\""
                  << ((c >> 8) & 0xFF) << "\" a=\"" << (c & 0xFF) << "\"/>\n";
            } break;
            }
        }
        o << "  <spatium>" << st.spatium() / style::DPMM << "</spatium>\n</Style>\n";
        ++done;
    }
    std::cerr << "stylelist: " << done << " scores\n";
    return 0;
}

//! Each listed score's chords as the engine shapes them so far (S7: lines, noteheads, accidentals), in the form of
//! the reference tool's chord layout listing (je_layout_reference.py compares them)
int chordshapes(const std::string& root, const std::string& list, const std::string& out)
{
    std::ifstream in(list);
    int done = 0;
    for (std::string rel; std::getline(in, rel);) {
        if (rel.empty()) {
            continue;
        }
        model::Model m;
        IdGenerator ids(1);
        std::string error;
        const fs::path target = fs::path(out) / (rel + ".chords");
        fs::create_directories(target.parent_path());
        std::ofstream o(target);
        if (!io::openScore(root + "/" + rel, m, ids, error)) {
            o << "NOT READ\n";
            continue;
        }
        const model::Snapshot snap = m.snapshot();
        const layout::ScoreContext ctx(snap);
        std::vector<layout::ChordLayout> chords = layout::layoutNotes(ctx);
        const std::vector<layout::Beam> beams = layout::layoutBeams(ctx, chords);
        layout::layoutStemDirections(ctx, chords, beams);
        layout::layoutMirrors(ctx, chords);
        layout::layoutNotePositions(ctx, chords);
        layout::layoutStemLengths(ctx, chords, beams);
        layout::layoutChordOffsets(ctx, chords);
        // Fraction::ticks: rounded to the nearest tick
        auto ticks = [](const Fraction& t) { return (t.numerator() * 1920 + t.denominator() / 2) / t.denominator(); };
        std::map<Id, std::string> beamOf;
        for (const layout::Beam& b : beams) {
            const model::Object* first = snap.find(b.elements.front());
            const std::string text = std::to_string(b.staff * 4 + b.voice) + ":" + std::to_string(ticks(first->fractionOr(model::fields::TIME, Fraction())))
                                     + ":" + std::to_string(b.elements.size()) + (b.grace ? "g" : "");
            for (Id e : b.elements) {
                beamOf[e] = text;
            }
        }
        for (const layout::ChordLayout& c : chords) {
            o << c.kind << " " << c.staff << " " << c.voice << " " << ticks(c.tick) << (c.standard && c.shown ? "" : " nonstandard");
            if (c.upKnown) {
                o << " up=" << (c.up ? 1 : 0);
            }
            if (!c.upKnown || !c.mirrorKnown) {
                o << " mirror=?";
            }
            if (c.xKnown && c.kind == 'C') {
                char cx[32];
                std::snprintf(cx, sizeof(cx), " x=%.3f", c.x + 0.0);
                o << cx;
            }
            if (c.hook && c.upKnown) {
                o << " hook=" << style::SYMBOL_CODES[int(c.hookSym)].name;
            }
            if (c.stemLengthKnown) {
                char sl[32];
                std::snprintf(sl, sizeof(sl), " stem=%.3f", c.stemLength);
                o << sl;
            }
            if (const auto it = beamOf.find(c.chord); it != beamOf.end() && !c.stemless) {   // ChordRest::beam: none shown in a stemless measure
                o << " beam=" << it->second;
            }
            for (const layout::NoteLayout& n : c.notes) {
                o << " | " << n.line << " " << style::SYMBOL_CODES[int(n.headUp)].name << "/" << style::SYMBOL_CODES[int(n.headDown)].name
                  << (n.headKnown ? "" : "?");
                if (n.accidental != style::SymId::noSym) {
                    o << " acc=" << style::SYMBOL_CODES[int(n.accidental)].name;
                }
                if (n.mirror && c.upKnown && c.mirrorKnown) {
                    o << " mirror";
                }
                if (c.upKnown && c.mirrorKnown) {
                    // x only where every notehead of the chord is known (their widths place the notes)
                    char xy[64];
                    const bool heads = std::all_of(c.notes.begin(), c.notes.end(), [](const layout::NoteLayout& k) { return k.headKnown; });
                    if (heads) {
                        std::snprintf(xy, sizeof(xy), " x=%.3f y=%.3f", n.x + 0.0, n.y + 0.0);
                    } else {
                        std::snprintf(xy, sizeof(xy), " y=%.3f", n.y + 0.0);
                    }
                    o << xy;
                }
            }
            o << "\n";
        }
        ++done;
    }
    std::cerr << "chordshapes: " << done << " scores\n";
    return 0;
}

int main(int argc, char** argv)
{
    const std::vector<std::string> a(argv + 1, argv + argc);
    if (a.size() >= 2 && a[0] == "corpus") {
        const bool priv = std::find(a.begin(), a.end(), "--private") != a.end();
        auto r = std::find(a.begin(), a.end(), "--report");
        auto k = std::find(a.begin(), a.end(), "--known");
        return corpus(a[1], priv, r != a.end() && r + 1 != a.end() ? *(r + 1) : std::string(),
                      k != a.end() && k + 1 != a.end() ? *(k + 1) : std::string());
    }
    if (a.size() == 3 && a[0] == "rewrite") {
        return rewrite(a[1], a[2]);
    }
    if (a.size() == 3 && a[0] == "set-jubelle-style") {
        return setJubelleStyle(a[1], a[2]);
    }
    if (a.size() == 2 && a[0] == "listing") {
        return listing(a[1]);
    }
    if (a.size() == 2 && a[0] == "store") {
        return store(a[1]);
    }
    if (a.size() == 3 && a[0] == "generate") {
        return generate(a[1], a[2]);
    }
    if (a.size() == 2 && a[0] == "dump") {
        return dump(a[1]);
    }
    if (a.size() >= 2 && a[0] == "regenerate") {
        auto k = std::find(a.begin(), a.end(), "--known");
        auto o = std::find(a.begin(), a.end(), "--out");
        return regenerate(a[1], k != a.end() && k + 1 != a.end() ? *(k + 1) : std::string(),
                          std::find(a.begin(), a.end(), "--private") != a.end(),
                          o != a.end() && o + 1 != a.end() ? *(o + 1) : std::string());
    }
    if (a.size() == 2 && a[0] == "linkdiff") {
        model::Model m;
        IdGenerator ids(1);
        std::string error;
        if (!io::openScore(a[1], m, ids, error)) {
            std::cerr << error << "\n";
            return 1;
        }
        for (const std::string& d : edit::linkedStaffDifferences(m)) {
            std::cout << d << "\n";
        }
        return 0;
    }
    if (a.size() == 4 && a[0] == "linkedit" && a[2] == "--out") {
        return linkedit(a[1], a[3]);
    }
    if (a.size() == 4 && a[0] == "repeats" && a[2] == "--out") {   // every score's play order
        return repeats(a[1], a[3]);
    }
    if (a.size() == 4 && a[0] == "chordlist" && a[2] == "--out") {
        return chordlist(a[1], a[3]);
    }
    if (a.size() >= 6 && a[0] == "measedit" && a[2] == "--out") {
        auto arg = [&a](size_t i) { return a.size() > i ? std::atoi(a[i].c_str()) : 0; };
        if (a[4] == "addtext" && a.size() >= 8) {   // the style and the text typed
            std::string spec = a[7];
            for (size_t i = 8; i < a.size(); ++i) {
                spec += " " + a[i];
            }
#ifdef _WIN32
            _putenv_s("JE_ADDTEXT", spec.c_str());
#else
            setenv("JE_ADDTEXT", spec.c_str(), 1);
#endif
        }
        if (a[4] == "lyrics" && a.size() >= 8) {   // the keys typed
#ifdef _WIN32
            _putenv_s("JE_LYRICS", a[7].c_str());
#else
            setenv("JE_LYRICS", a[7].c_str(), 1);
#endif
        }
        if (a[4] == "movestaff" && a.size() >= 10) {   // the direction: 0 up, 1 down
#ifdef _WIN32
            _putenv_s("JE_MOVESTAFF", a[9].c_str());
#else
            setenv("JE_MOVESTAFF", a[9].c_str(), 1);
#endif
        }
        if (a[4] == "realize" && a.size() >= 12) {   // voicing, duration, literal given: instead of each symbol's own
#ifdef _WIN32
            _putenv_s("JE_REALIZE", (a[9] + " " + a[10] + " " + a[11]).c_str());
#else
            setenv("JE_REALIZE", (a[9] + " " + a[10] + " " + a[11]).c_str(), 1);
#endif
        }
        return measedit(a[1], a[3], a[4], arg(5), arg(6), arg(7), arg(8));
    }
    if (a.size() == 7 && a[0] == "tsedit" && a[2] == "--out") {
        return tsedit(a[1], a[3], std::atoi(a[4].c_str()), std::atoi(a[5].c_str()), std::atoi(a[6].c_str()));
    }
    if (a.size() == 4 && a[0] == "cmdedit" && a[2] == "--out") {
        return cmdedit(a[1], a[3]);
    }
    if (a.size() >= 4 && a[0] == "journal" && a[2] == "--work") {   // crash recovery on every score
        return journalcheck(a[1], a[3], std::find(a.begin(), a.end(), "--json") != a.end());
    }
    if (a.size() == 4 && a[0] == "chordshapes") {   // each listed score's chords as shaped (root, list, output folder)
        return chordshapes(a[1], a[2], a[3]);
    }
    if (a.size() == 4 && a[0] == "stylelist") {   // each listed score's style (root, list, output folder)
        return stylelist(a[1], a[2], a[3]);
    }
    if (a.size() == 3 && a[0] == "recover") {   // a journal left by a crash, saved as a score
        return recoverJournal(a[1], a[2]);
    }
    if (a.size() >= 2 && a[0] == "bench-scores") {
        return benchScores(std::vector<std::string>(a.begin() + 1, a.end()));
    }
    if (a.size() == 2 && a[0] == "bench") {
        return bench(a[1]);
    }
    std::cerr << "usage: je_io_tool corpus <folder> [--private] [--report file] | rewrite <in> <out> | listing <file> | "
                 "store <file> | bench <file>\n";
    return 2;
}
