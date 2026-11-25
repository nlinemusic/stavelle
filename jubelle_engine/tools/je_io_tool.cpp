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
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(__GLIBC__)
#include <malloc.h>
#endif

#include "edit/commands.h"
#include "edit/history.h"
#include "edit/links.h"
#include "edit/operations.h"
#include "edit/structure.h"
#include "io/mscxreader.h"
#include "io/mscxwriter.h"
#include "io/scorefile.h"
#include "model/invariants.h"

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
    if (a.size() == 4 && a[0] == "cmdedit" && a[2] == "--out") {
        return cmdedit(a[1], a[3]);
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
