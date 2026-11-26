/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::io). Not part of MuseScore.
 */
#include "mscxwriter.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <memory>
#include <set>
#include <sstream>

#include "details.h"
#include "mscxreader.h"
#include "model/notation.h"
#include "model/pitch.h"

namespace F = je::model::fields;
using je::model::Kind;
using je::model::Object;
using je::io::xml::Node;
using je::model::defaultTpc;
using je::model::diatonicOf;

namespace je::io {
namespace {
Node leaf(const std::string& name, const std::string& raw)
{
    Node n;
    n.name = name;
    n.leaf = true;
    n.raw = raw;
    return n;
}

Node empty(const std::string& name)
{
    Node n;
    n.name = name;
    n.leaf = true;
    n.selfClosing = true;
    return n;
}

Node elem(const std::string& name)
{
    Node n;
    n.name = name;
    return n;
}

//! A container written empty (no children) becomes <name></name>, as MuseScore reads either form
void finishNode(Node& n)
{
    if (!n.leaf && n.children.empty()) {
        n.leaf = true;
    }
}

std::string frac(const Fraction& f)
{
    return std::to_string(f.numerator()) + "/" + std::to_string(f.denominator());
}

std::string durationName(const Fraction& f)
{
    static const std::map<std::pair<int64_t, int64_t>, std::string> names {
        { { 4, 1 }, "long" }, { { 2, 1 }, "breve" }, { { 1, 1 }, "whole" }, { { 1, 2 }, "half" }, { { 1, 4 }, "quarter" },
        { { 1, 8 }, "eighth" }, { { 1, 16 }, "16th" }, { { 1, 32 }, "32nd" }, { { 1, 64 }, "64th" }, { { 1, 128 }, "128th" },
        { { 1, 256 }, "256th" }, { { 1, 512 }, "512th" }, { { 1, 1024 }, "1024th" },
    };
    auto it = names.find({ f.numerator(), f.denominator() });
    return it == names.end() ? std::string("quarter") : it->second;
}

std::string upperFirst(std::string s)
{
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') {
        s[0] = char(s[0] - 'a' + 'A');
    }
    return s;
}

//! MuseScore's element identity: two numbers, each in base-64 digits from the least significant, joined by "_"
std::string eidText(uint64_t a, uint64_t b)
{
    static const char digits[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    auto one = [](uint64_t n) {
        std::string out;
        do {
            out += digits[n % 64];
            n /= 64;
        } while (n);
        return out;
    };
    return one(a) + "_" + one(b);
}

uint64_t fnv(const std::string& s, uint64_t seed)
{
    uint64_t h = 1469598103934665603ULL ^ seed;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    return h;
}

//! The identity of a part's copy of a score element: derived from the element's and the part's, so that it is the
//! same on every save and doesn't meet another
std::string partEid(const std::string& mainEid, int layoutNo)
{
    const std::string key = mainEid + "#" + std::to_string(layoutNo);
    return eidText(fnv(key, 0x9E3779B97F4A7C15ULL) >> 1, fnv(key, 0x2545F4914F6CDD1DULL) >> 1);
}

//! Part elements are linked to the main score's (decision D-E28): each copy gets its own identity and a
//! <linkedTo> naming the main element's. Measures, beams and accidentals carry an identity but no link, as
//! MuseScore writes them.
void linkToMain(Node& n, int layoutNo)
{
    static const std::set<std::string> unlinked { "Score", "Measure", "Beam", "Accidental" };
    for (size_t i = 0; i < n.children.size(); ++i) {
        Node& c = n.children[i];
        if (c.name == "eid" && c.leaf && !c.raw.empty()) {
            const std::string main = c.raw;
            c.raw = partEid(main, layoutNo);
            if (!unlinked.count(n.name)) {
                n.children.insert(n.children.begin() + long(i) + 1, leaf("linkedTo", main));
                ++i;
            }
        } else if (c.name == "linkedTo" || c.name == "linked" || c.name == "linkedMain") {
            n.children.erase(n.children.begin() + long(i));
            --i;
        } else {
            linkToMain(c, layoutNo);
        }
    }
}

//! MuseScore's element type for a spanner kind (the model's names are case-insensitive: "hairpin", "hairPin")
std::string spannerType(const std::string& kind)
{
    static const std::map<std::string, std::string> types {
        { "slur", "Slur" }, { "hairpin", "HairPin" }, { "volta", "Volta" }, { "ottava", "Ottava" }, { "pedal", "Pedal" },
        { "trill", "Trill" }, { "textline", "TextLine" }, { "letring", "LetRing" }, { "palmmute", "PalmMute" },
        { "vibrato", "Vibrato" }, { "whammybar", "WhammyBar" }, { "rasgueado", "Rasgueado" }, { "harmonicmark", "HarmonicMark" },
        { "pickscrape", "PickScrape" }, { "gradualtempochange", "GradualTempoChange" }, { "glissando", "Glissando" },
        { "note line", "NoteLine" }, { "noteline", "NoteLine" }, { "tie", "Tie" }, { "laissezvib", "LaissezVib" },
    };
    std::string lower;
    for (char c : kind) {
        lower += char(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    }
    auto it = types.find(lower);
    return it == types.end() ? upperFirst(kind) : it->second;
}

using model::articulationSymbol;
using model::isOrnamentSymbol;

//! Common jumps: where they go, where play stops, where it continues
void jumpTargets(const std::string& text, std::string& to, std::string& until, std::string& cont)
{
    std::string t;
    for (char c : text) {
        t += char(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    }
    const bool ds = t.find("D.S") != std::string::npos || t.find("DAL SEGNO") != std::string::npos;
    to = ds ? "segno" : "start";
    until = "end";
    cont.clear();
    if (t.find("FINE") != std::string::npos) {
        until = "fine";
    } else if (t.find("CODA") != std::string::npos) {
        until = "coda";
        cont = "codab";
    }
}

//! Archive entries kept as one text: name, NUL, size, NUL, data, ...
std::vector<zip::Entry> unpackEntries(const std::string& extras)
{
    std::vector<zip::Entry> out;
    for (size_t at = 0; at < extras.size();) {
        const size_t z1 = extras.find('\0', at);
        const size_t z2 = z1 == std::string::npos ? z1 : extras.find('\0', z1 + 1);
        if (z2 == std::string::npos) {
            break;
        }
        zip::Entry e;
        e.name = extras.substr(at, z1 - at);
        const size_t size = size_t(std::stoull(extras.substr(z1 + 1, z2 - z1 - 1)));
        e.data = extras.substr(z2 + 1, size);
        out.push_back(e);
        at = z2 + 1 + size;
    }
    return out;
}

//! Links by place (formats before 4.60): in the main score <linkedMain/>, in a part <linked></linked>, first in
//! each linkable element of the music (after its identity); a part's copies lose the main score's identities
void linkByLocation(Node& n, bool mainScore)
{
    if (n.name == "Measure" && n.child("multiMeasureRest")) {
        return;   // a carried multi-measure rest: as it was
    }
    static const std::set<std::string> linkable {
        "Chord", "Rest", "Note", "MeasureRepeat", "KeySig", "TimeSig", "Clef", "Lyrics", "Articulation", "Ornament", "Dynamic",
        "StaffText", "SystemText", "RehearsalMark", "Tempo", "Harmony", "Fingering", "Breath", "Fermata", "Expression",
        "InstrumentChange", "BarLine", "Tuplet", "Slur", "HairPin", "Volta", "Ottava", "Pedal", "Trill", "TextLine", "Tie",
        "Arpeggio", "Jump", "Marker", "ChordLine", "Glissando", "Tremolo", "TremoloSingleChord", "TremoloTwoChord",
    };
    for (size_t i = 0; i < n.children.size(); ++i) {
        Node& c = n.children[i];
        if (!mainScore && (c.name == "linkedTo" || c.name == "linked" || c.name == "linkedMain" || c.name == "eid")) {
            n.children.erase(n.children.begin() + long(i));
            --i;
            continue;
        }
        linkByLocation(c, mainScore);
    }
    if (mainScore && std::any_of(n.children.begin(), n.children.end(), [](const Node& c) { return c.name == "linkedMain" || c.name == "linked"; })) {
        return;   // linked within the score already (a linked staff): as it is
    }
    if (linkable.count(n.name) && (!n.leaf || (n.raw.empty() && !n.selfClosing) || n.name == "Tie")) {
        if (n.leaf) {   // an empty element (a tie with nothing of its own): it gets the link inside
            n.leaf = false;
            n.selfClosing = false;
            n.raw.clear();
        }
        size_t at = (!n.children.empty() && n.children.front().name == "eid") ? 1 : 0;
        Node link;
        link.name = mainScore ? "linkedMain" : "linked";
        link.leaf = true;
        link.selfClosing = mainScore;
        if (n.hasAttr("je-obj")) {   // which element it is (PlaceLinks works out the part's links from it)
            link.attrs.emplace_back(mainScore ? "je-id" : "je-main", n.attr("je-obj"));
        }
        n.children.insert(n.children.begin() + long(at), link);
    }
}

//! Links by place (formats before 4.60) for copies Jubelle made on a linked staff. MuseScore's reader
//! (read410 ReadContext::addLink / getLink with its LinksIndexer) gives each <linkedMain/> element a place in its
//! staff's list, in file order, and finds a <linked> element's original at the copy's position on the main staff:
//! the n-th original there, n counted along consecutive copies at that position, plus <indexDiff>. The writer marked
//! each element with the reader's time and track (je-tick, je-track) and each new copy with its original (je-main);
//! this works out the <indexDiff> that leads each copy to its original, and removes the marks.
class PlaceLinks
{
public:
    explicit PlaceLinks(const std::map<int, int>& mainStaff)
        : m_mainStaff(mainStaff) {}

    void run(Node& score)
    {
        // first the places where something changed (an original or a copy made by Jubelle): only there are the
        // copies' links worked out again; elsewhere they are written as the file had them
        m_findDirty = true;
        walkAll(score);
        m_findDirty = false;
        m_links.clear();
        m_hasLast = false;
        m_lastIndex = -1;
        walkAll(score);
    }

    //! A part score, read after the main score (MuseScore's reader carries on with the main score's lists and
    //! indexer): its <linked> elements find their originals in the main score's staves
    void runPart(Node& score, const std::map<int, int>& partToMain)
    {
        m_partToMain = &partToMain;
        walkAll(score);
        m_partToMain = nullptr;
    }

    void walkAll(Node& score)
    {
        for (Node& c : score.children) {
            if (c.name == "Staff" && c.hasAttr("id")) {
                walk(c, std::atoi(c.attr("id").c_str()) - 1, 0, Fraction());
            }
        }
    }

    static void strip(Node& n)
    {
        n.attrs.erase(std::remove_if(n.attrs.begin(), n.attrs.end(), [](const auto& a) { return a.first.rfind("je-", 0) == 0; }),
                      n.attrs.end());
        for (Node& c : n.children) {
            strip(c);
        }
    }

private:
    struct Loc {
        int track = 0;
        Fraction tick;
        bool operator==(const Loc& o) const { return track == o.track && tick == o.tick; }
    };
    struct Entry {
        Loc loc;
        std::string id;
    };

    static Fraction parseFrac(const std::string& s)
    {
        const size_t slash = s.find('/');
        return slash == std::string::npos ? Fraction(std::atoll(s.c_str()), 1)
                                          : Fraction(std::atoll(s.substr(0, slash).c_str()), std::atoll(s.substr(slash + 1).c_str()));
    }

    int assign(const Loc& l)   // LinksIndexer::assignLocalIndex
    {
        if (m_hasLast && m_last == l) {
            return ++m_lastIndex;
        }
        m_hasLast = true;
        m_last = l;
        m_lastIndex = 0;
        return 0;
    }

    void walk(Node& n, int staff, int track, Fraction tick)
    {
        if (n.hasAttr("je-track")) {
            track = std::atoi(n.attr("je-track").c_str());
        }
        if (n.hasAttr("je-tick")) {
            tick = parseFrac(n.attr("je-tick"));
        }
        for (Node& c : n.children) {
            if (c.type != Node::Type::Element) {
                continue;
            }
            if (c.name == "linkedMain") {
                const Loc loc { track, tick };
                if (m_findDirty) {
                    if (c.hasAttr("je-new")) {
                        m_dirty.insert({ loc.track, loc.tick });
                    }
                    continue;
                }
                assign(loc);
                // a part's own originals are kept apart from the main score's (MuseScore numbers them negatively)
                m_links[m_partToMain ? -1000 - staff : staff].push_back({ loc, c.attr("je-id") });
            } else if (c.name == "linked") {
                linked(c, staff, track, tick);
            } else if (!c.leaf) {
                walk(c, staff, track, tick);
            }
        }
    }

    void linked(Node& c, int staff, int track, const Fraction& tick)
    {
        if (m_partToMain) {
            auto pm = m_partToMain->find(staff);
            const int mainStaff = pm == m_partToMain->end() ? staff : pm->second;
            place(c, Loc { mainStaff * 4 + track % 4, tick }, true);
            return;
        }
        if (m_findDirty) {
            if (c.hasAttr("je-main") && !c.hasAttr("je-mark")) {
                auto ms = m_mainStaff.find(staff);
                const int mainStaff = ms == m_mainStaff.end() ? staff : ms->second;
                m_dirty.insert({ mainStaff * 4 + track % 4, tick });
            }
            return;
        }
        auto ms = m_mainStaff.find(staff);
        const int mainStaff = ms == m_mainStaff.end() ? staff : ms->second;
        Loc loc { mainStaff * 4 + track % 4, tick };
        place(c, loc, false);
    }

    void place(Node& c, Loc loc, bool part)
    {
        if (const Node* rel = c.child("location")) {   // a link read from the file with a place of its own
            loc.track += 4 * std::atoi(rel->childText("staves").c_str()) + std::atoi(rel->childText("voices").c_str());
            if (!rel->childText("fractions").empty()) {
                loc.tick += parseFrac(rel->childText("fractions"));
            }
        }
        const int guessed = assign(loc);
        std::vector<Entry>& list = m_links[loc.track / 4];
        if (!list.empty() && list.back().loc == loc) {
            list.push_back(list.back());   // as getLink does
        }
        if (!c.hasAttr("je-main")) {
            return;   // as it was read
        }
        if (!part && c.hasAttr("je-mark") && !m_dirty.count({ loc.track, loc.tick })) {
            Node was = elem("linked");   // nothing changed here: as the file had it
            was.children = parseFragment(c.attr("je-mark"));
            finishNode(was);
            c.children = was.children;
            c.leaf = false;
            c.raw.clear();
            return;
        }
        const std::string want = c.attr("je-main");
        int first = -1, at = -1;
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i].loc == loc && first < 0) {
                first = int(i);
            }
            if (list[i].id == want && list[i].loc == loc && at < 0) {
                at = int(i);
            }
        }
        if (first < 0 || at < 0) {
            return;   // the original isn't at the copy's place: written plain (MuseScore leaves it unlinked)
        }
        const int diff = (at - first) - guessed;
        c.raw.clear();
        c.children.clear();
        if (c.hasAttr("je-mark")) {
            // read from the file: written as it was when it still says the same
            const std::vector<Node> was = parseFragment(c.attr("je-mark"));
            int wasDiff = 0;
            for (const Node& w : was) {
                if (w.name == "indexDiff") {
                    wasDiff = std::atoi(w.text().c_str());
                }
            }
            if (wasDiff == diff) {
                c.leaf = false;
                c.children = was;
                finishNode(c);
                return;
            }
        }
        c.leaf = diff == 0;
        if (diff != 0) {
            c.leaf = false;
            c.children.push_back(leaf("indexDiff", std::to_string(diff)));
            finishNode(c);
        }
    }

    const std::map<int, int>& m_mainStaff;
    std::map<int, std::vector<Entry> > m_links;
    bool m_findDirty = false;
    const std::map<int, int>* m_partToMain = nullptr;
    std::set<std::pair<int, Fraction> > m_dirty;   // track and time of the places that changed
    Loc m_last;
    int m_lastIndex = -1;
    bool m_hasLast = false;
};

std::string folderName(const std::string& name, size_t index)
{
    std::string f = std::to_string(index) + "_";
    for (char c : name) {
        f += (c == ' ' || c == '/' || c == '\\' || c == ':') ? '_' : c;
    }
    return f;
}

class Writer
{
public:
    Writer(const model::Snapshot& snapshot, WriteReport& r)
        : s(snapshot), report(r) {}

    //! A part score: only the layout's players; system items go to its first staff
    Writer(const model::Snapshot& snapshot, WriteReport& r, const Object& partLayout, std::map<Id, std::string>& sharedEids)
        : s(snapshot), report(r), layout(&partLayout), generatedEids(&sharedEids) {}

    bool plainFile = false;   // a plain .mscx: the parts written inside the main score, no archive

    ScoreFile run(const std::string& scoreName)
    {
        score = s.find(s.score());
        ScoreFile file;
        if (!score) {
            return file;
        }
        index();

        file.mainEntry = scoreName;
        file.main.prolog.push_back(declaration());
        file.main.hasRoot = true;
        file.main.trailing = "\n";
        Node& root = file.main.root;
        root.name = "museScore";
        root.attrs.emplace_back("version", format);
        const std::vector<Node> head = parseFragment(score->has(F::SCORE_HEAD) ? score->text(F::SCORE_HEAD) : defaultScoreHead());
        root.children.insert(root.children.end(), head.begin(), head.end());
        root.children.push_back(scoreNode());
        if (!modern && !layouts.empty()) {
            // before 4.60 a part's elements link by place: the main score's say <linkedMain/>, the part's <linked>
            std::set<int> linkedStaves;
            for (const Object* l : layouts) {
                const model::IdList shown = std::get<model::IdList>(l->get(F::PLAYERS));
                for (Id p : shown) {
                    if (const Object* po = s.find(p)) {
                        for (Id st : po->children()) {
                            if (staffNumber.count(st)) {
                                linkedStaves.insert(staffNumber.at(st));
                            }
                        }
                    }
                }
            }
            Node* sc = root.child("Score");
            for (Node& c : sc->children) {
                if (c.name == "Staff" && linkedStaves.count(std::atoi(c.attr("id").c_str()))) {
                    linkByLocation(c, true);
                }
            }
        }
        std::map<int, int> mainStaff;   // staff index → the index of the staff it is linked to
        for (const Object* st : staves) {
            Id root = st->id();
            std::set<Id> seen;
            while (s.find(root) && s.find(root)->has(F::LINKED_STAFF) && seen.insert(root).second) {
                root = s.find(root)->idOr(F::LINKED_STAFF);
            }
            if (root != st->id() && staffNumber.count(root)) {
                mainStaff[staffNumber.at(st->id()) - 1] = staffNumber.at(root) - 1;   // the first staff of its group
            }
        }
        std::unique_ptr<PlaceLinks> placeLinks;
        if (annotate) {
            placeLinks = std::make_unique<PlaceLinks>(mainStaff);
            placeLinks->run(*root.child("Score"));
            PlaceLinks::strip(root);
        }

        // the style also as score_style.mss, which MuseScore reads from an archive
        file.hasStyleEntry = true;
        file.style.prolog.push_back(declaration());
        file.style.hasRoot = true;
        file.style.trailing = "\n";
        file.style.root.name = "museScore";
        file.style.root.attrs.emplace_back("version", score->has(F::STYLE_FORMAT) ? score->text(F::STYLE_FORMAT) : format);
        file.style.root.children.push_back(styleNode());

        zip::Entry container;
        container.name = "META-INF/container.xml";
        container.data = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<container>\n  <rootfiles>\n    <rootfile full-path=\""
                         + xml::encode(scoreName) + "\"/>\n    </rootfiles>\n  </container>\n";
        zip::Entry styleEntry;
        styleEntry.name = "score_style.mss";
        zip::Entry main;
        main.name = scoreName;
        file.entries = { styleEntry, main };
        // the other score data kept in the archive (mixer, view, automation settings)
        for (const zip::Entry& e : unpackEntries(score->text(F::ARCHIVE_EXTRAS))) {
            file.entries.push_back(e);
        }
        file.entries.push_back(container);
        if (plainFile) {
            file.plainMscx = true;
        }

        // part layouts: each a part score made from the same model, linked to the main score
        size_t layoutNo = 0;
        for (const Object* l : layouts) {
            WriteReport partReport;
            Writer pw(s, partReport, *l, *generatedEids);
            xml::Document doc = pw.partDocument();
            if (plainFile || l->has(F::INLINE_PART)) {
                // written inside the main score, as MuseScore writes a plain .mscx's parts (and reads them anywhere)
                Node* psc = doc.root.child("Score");
                Node* msc = file.main.root.child("Score");
                if (psc && msc) {
                    if (modern) {
                        linkToMain(*psc, int(layoutNo));
                    } else {
                        for (Node& c : psc->children) {
                            if (c.name == "Staff") {
                                linkByLocation(c, false);
                            }
                        }
                        if (placeLinks) {
                            placeLinks->runPart(*psc, pw.partToMainStaves());
                        }
                    }
                    PlaceLinks::strip(*psc);
                    msc->children.push_back(std::move(*psc));
                    finishNode(*msc);
                }
                ++layoutNo;
                continue;
            }
            if (modern) {
                linkToMain(doc.root, int(layoutNo));
            } else if (Node* psc = doc.root.child("Score")) {
                for (Node& c : psc->children) {
                    if (c.name == "Staff") {   // the music (a part's staves say which main staff they copy)
                        linkByLocation(c, false);
                    }
                }
                if (placeLinks) {   // each copy led to its original among the main score's at its place
                    placeLinks->runPart(*psc, pw.partToMainStaves());
                }
                PlaceLinks::strip(doc.root);
            }
            const std::string folder = l->text(F::TAG).empty() ? folderName(l->text(F::NAME), layoutNo) : l->text(F::TAG);
            const std::string base = "Excerpts/" + folder + "/" + folder;
            zip::Entry mscx;
            mscx.name = base + ".mscx";
            // the part's style also as its own .mss, as MuseScore keeps it
            xml::Document mss;
            mss.prolog.push_back(declaration());
            mss.hasRoot = true;
            mss.trailing = "\n";
            mss.root.name = "museScore";
            mss.root.attrs.emplace_back("version", format);
            mss.root.children.push_back(pw.styleNode());
            zip::Entry styleEntry;
            styleEntry.name = base + ".mss";
            styleEntry.data = xml::write(mss);
            file.excerpts[mscx.name] = std::move(doc);
            file.entries.push_back(mscx);
            file.entries.push_back(styleEntry);
            const std::string extras = l->text(F::ARCHIVE_EXTRAS);
            for (const zip::Entry& e : unpackEntries(extras)) {
                zip::Entry x = e;
                x.name = "Excerpts/" + folder + "/" + e.name;
                file.entries.push_back(x);
            }
            ++layoutNo;
        }
        return file;
    }

    //! A part's staves (index) → the main score's staves they show (index)
    std::map<int, int> partToMainStaves() const
    {
        // MuseScore looks a part's copies up in the first staff of the main score's linked group (a part staff of
        // a linked staff finds its originals on that staff's main staff)
        std::map<int, int> out;
        for (const auto& [st, n] : staffNumber) {
            Id root = st;
            std::set<Id> seen;
            while (s.find(root) && s.find(root)->has(F::LINKED_STAFF) && seen.insert(root).second) {
                root = s.find(root)->idOr(F::LINKED_STAFF);
            }
            auto it = mainStaffNumber.find(root);
            if (it != mainStaffNumber.end()) {
                out[n - 1] = it->second - 1;
            }
        }
        return out;
    }

    //! The part score of the layout this writer was made for
    xml::Document partDocument()
    {
        score = s.find(s.score());
        xml::Document doc;
        index();
        doc.prolog.push_back(declaration());
        doc.hasRoot = true;
        doc.trailing = "\n";
        doc.root.name = "museScore";
        doc.root.attrs.emplace_back("version", format);
        const std::vector<Node> head = parseFragment(layout->has(F::SCORE_HEAD) ? layout->text(F::SCORE_HEAD) : defaultScoreHead());
        doc.root.children.insert(doc.root.children.end(), head.begin(), head.end());
        doc.root.children.push_back(scoreNode());
        return doc;
    }

private:
    static Node declaration()
    {
        Node d;
        d.type = Node::Type::Declaration;
        d.name = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>";
        return d;
    }

    // ---- indexes ------------------------------------------------------------------------------------------------
    void index()
    {
        for (Id c : score->children()) {
            const Object* o = s.find(c);
            if (!o) {
                continue;
            }
            if (o->kind() == Kind::Flow && !flow) {
                flow = o;
            } else if (o->kind() == Kind::Player) {
                players.push_back(o);
                for (Id st : o->children()) {
                    staffNumber[st] = int(staves.size()) + 1;
                    staves.push_back(s.find(st));
                }
            } else if (o->kind() == Kind::Spanner && s.find(o->idOr(F::ANCHOR)) && s.find(o->idOr(F::ANCHOR))->kind() == Kind::Note) {
                lineStarts[o->idOr(F::ANCHOR)].push_back(o);   // a line from note to note: written in the notes
                lineEnds[o->idOr(F::END_ANCHOR)].push_back(o);
            } else if (o->kind() == Kind::Spanner) {
                spanners.push_back(o);
            } else if (o->kind() == Kind::Layout && o->has(F::PLAYERS)) {
                layouts.push_back(o);
            }
        }
        if (layout) {
            // a part: its players only, staves numbered from 1
            const model::IdList keep = std::get<model::IdList>(layout->get(F::PLAYERS));   // a copy: get() returns a value
            std::vector<const Object*> chosen;
            for (Id p : keep) {
                for (const Object* o : players) {
                    if (o->id() == p) {
                        chosen.push_back(o);
                    }
                }
            }
            players = chosen;
            mainStaffNumber = staffNumber;
            staves.clear();
            staffNumber.clear();
            for (const Object* o : players) {
                for (Id st : o->children()) {
                    staffNumber[st] = int(staves.size()) + 1;
                    staves.push_back(s.find(st));
                }
            }
        }
        anyFromFile = score->has(F::SCORE_HEAD) || score->has(F::STYLE) || score->has(F::META);
        format = score->has(F::SOURCE_FORMAT) ? score->text(F::SOURCE_FORMAT) : std::string("4.70");
        modern = !(format < std::string("4.60"));   // identities and <linkedTo> links from 4.60 on
        // part layouts link to the main score's elements by identity: every element written gets one
        // elements copied onto a linked staff by Jubelle link to their originals (as MuseScore links them)
        s.forEachObject([this](const Object& o) {
            if (o.has(F::LINKED_TO)) {
                linkMains.insert(o.idOr(F::LINKED_TO));
                newLinks = true;
            }
            if (o.kind() == Kind::Staff && o.has(F::LINKED_STAFF) && !o.has(F::LINK_EID)) {
                newLinks = true;
            }
        });
        needLinks = (!layouts.empty() || layout || newLinks) && modern;
        byPlace = newLinks && !modern && !layout;
        // before 4.60, where anything links by place, the reader's position is noted on each element as it is
        // written, for PlaceLinks
        annotate = !modern && (byPlace || !layouts.empty() || layout);
        if (!generatedEids) {
            generatedEids = &ownEids;
        }
        indexEventSpanners();
        s.forEachObject([this](const Object& o) {
            if (o.has(F::ORIGIN_EID)) {
                allEids.insert(o.text(F::ORIGIN_EID));
            }
            if (o.has(F::TIE_EID)) {
                allEids.insert(o.text(F::TIE_EID));
            }
        });
        if (needLinks && usedEids.empty()) {
            s.forEachObject([this](const Object& o) {
                if (o.has(F::ORIGIN_EID)) {
                    usedEids.insert(o.text(F::ORIGIN_EID));
                }
            });
        }
        if (!flow) {
            return;
        }
        Fraction t;
        for (Id c : flow->children()) {
            const Object* o = s.find(c);
            if (o->kind() == Kind::Measure) {
                measures.push_back(o);
                measureStart.push_back(o->fractionOr(F::TIME, t));
                measureLen.push_back(o->fractionOr(F::DURATION, Fraction(1, 1)));
                t = measureStart.back() + measureLen.back();
            } else if (o->kind() == Kind::FlowItem) {
                items.push_back(o);
            }
        }
        flowEnd = t;
        // tie targets: the note a tie ends on gets the end marker
        s.forEachObject([this](const Object& o) {
            if (o.kind() == Kind::Note && o.has(F::TIE_TO)) {
                tieFrom[o.idOr(F::TIE_TO)] = o.id();
            }
        });
    }

    int measureAt(const Fraction& t) const
    {
        // the measure holding time t (the last one for t at or past the end)
        auto it = std::upper_bound(measureStart.begin(), measureStart.end(), t);
        return it == measureStart.begin() ? 0 : int(it - measureStart.begin()) - 1;
    }

    //! The measure an end-of-measure item belongs to: the one it ends, unless it is at the start of the music
    int measureEnding(const Fraction& t) const
    {
        const int m = measureAt(t);
        if (m > 0 && t == measureStart[size_t(m)]) {
            return m - 1;
        }
        return m;
    }

    //! Writes an object's carried details if its music is unchanged since they were read
    bool detailsFit(const Object& o)
    {
        if (!o.has(F::DETAILS)) {
            return false;
        }
        if (o.has(F::DETAILS_KEY) && detailsKey(o) != o.intOr(F::DETAILS_KEY, 0)) {
            ++report.detailsDropped;
            return false;
        }
        ++report.detailsWritten;
        return true;
    }

    void addDetails(Node& n, const Object& o, model::Field field = F::DETAILS)
    {
        if (field == F::DETAILS ? detailsFit(o) : (o.has(field) && (!o.has(F::DETAILS_KEY) || detailsKey(o) == o.intOr(F::DETAILS_KEY, 0)))) {
            for (Node& c : parseFragment(o.text(field))) {
                n.children.push_back(std::move(c));
            }
        }
    }

    //! The element's identity: as read, or (when part layouts need it to link to) a new one, the same in every
    //! part written in this save
    std::string eidOf(const Object& o)
    {
        const std::string eid = o.text(F::ORIGIN_EID);
        if (!eid.empty() && eid.rfind("part:", 0) != 0) {
            return eid;
        }
        if (!needLinks) {
            return std::string();
        }
        auto it = generatedEids->find(o.id());
        if (it != generatedEids->end()) {
            return it->second;
        }
        std::string made;
        do {
            made = eidText(0x4A75626CULL, ++eidCounter);   // "Jubl", then a counter
        } while (usedEids.count(made));
        usedEids.insert(made);
        (*generatedEids)[o.id()] = made;
        return made;
    }

    void addEid(Node& n, const Object& o)
    {
        if (annotate) {
            n.attrs.emplace_back("je-obj", std::to_string(o.id().value()));
        }
        const std::string eid = eidOf(o);
        if (!eid.empty()) {
            n.children.push_back(leaf("eid", eid));
        }
        addLink(n, o, F::LINK_EID);
    }

    //! A link to another element of the same score (a linked staff's copy), while that element is there; a staff's
    //! link before 4.60 is its number
    void addLink(Node& n, const Object& o, model::Field field)
    {
        if (layout) {
            return;   // a part's links are made by linkToMain / linkByLocation
        }
        std::string to = o.text(field);
        if (to.empty() && field == F::LINK_EID) {   // a link Jubelle made: to the original's identity or staff
            if (o.kind() == Kind::Staff && o.has(F::LINKED_STAFF)) {
                if (const Object* main = s.find(o.idOr(F::LINKED_STAFF))) {
                    to = modern ? eidOf(*main) : (staffNumber.count(main->id()) ? std::to_string(staffNumber.at(main->id())) : "");
                }
            } else if (o.has(F::LINKED_TO) && modern) {
                if (const Object* main = s.find(o.idOr(F::LINKED_TO))) {
                    to = eidOf(*main);
                }
            }
        }
        // written as read (MuseScore warns about a link whose element isn't in the file, as it does for the original)
        if (!to.empty()) {
            n.children.push_back(leaf("linkedTo", xml::encode(to)));
        }
        if (field == F::LINK_EID && o.kind() != Kind::Staff) {
            const std::string mark = o.text(F::LINK_MARK);
            // a copy that knows its original (made by Jubelle, or read by place) is placed again once the score is
            // written, as its original may have moved among the others at its place; one read with a place of its
            // own (<location>) is kept as it was
            const bool placed = byPlace && o.has(F::LINKED_TO) && mark != "main" && mark.find("<location>") == std::string::npos;
            if (byPlace && mark.empty() && linkMains.count(o.id())) {
                n.children.push_back(empty("linkedMain"));
                n.children.back().attrs.emplace_back("je-new", "1");
            } else if (placed) {
                Node l = elem("linked");
                l.leaf = true;
                l.attrs.emplace_back("je-main", std::to_string(o.idOr(F::LINKED_TO).value()));
                if (mark.rfind("linked:", 0) == 0) {
                    l.attrs.emplace_back("je-mark", mark.substr(7));
                }
                n.children.push_back(std::move(l));
            }
            if (byPlace && linkMains.count(o.id()) && !n.children.empty() && n.children.back().name == "linkedMain") {
                n.children.back().attrs.emplace_back("je-id", std::to_string(o.id().value()));
            }
            if (mark == "main") {
                n.children.push_back(empty("linkedMain"));
                if (annotate) {
                    n.children.back().attrs.emplace_back("je-id", std::to_string(o.id().value()));
                }
            } else if (!placed && mark.rfind("linked:", 0) == 0) {
                Node l = elem("linked");
                for (Node& c : parseFragment(mark.substr(7))) {
                    l.children.push_back(std::move(c));
                }
                finishNode(l);
                n.children.push_back(std::move(l));
            }
        }
    }

    // ---- score, style, parts ------------------------------------------------------------------------------------
    //! The object holding the document's own settings: the score, or the part's layout
    const Object& settings() const { return layout ? *layout : *score; }

    Node styleNode()
    {
        if (!score) {
            score = s.find(s.score());
        }
        Node style = elem("Style");
        for (const auto& [name, raw] : unpackPairs(settings().text(F::STYLE))) {
            style.children.push_back(raw == "\x01" ? empty(name) : leaf(name, raw));
        }
        for (Node& c : parseFragment(settings().text(F::DETAILS_2))) {
            style.children.push_back(std::move(c));
        }
        finishNode(style);
        return style;
    }

    Node scoreNode()
    {
        Node sc = elem("Score");
        sc.children.push_back(leaf("Division", "480"));
        sc.children.push_back(styleNode());
        if (layout && !layout->has(F::SCORE_PRE) && !layout->has(F::INLINE_PART)) {
            sc.children.push_back(leaf("name", xml::encode(layout->text(F::NAME))));   // a part made in Jubelle
        }
        for (Node& c : parseFragment(settings().text(F::SCORE_PRE))) {
            sc.children.push_back(std::move(c));
        }
        bool partName = false;
        for (const auto& [name, raw] : unpackPairs(settings().text(F::META))) {
            Node m = leaf("metaTag", raw);
            m.attrs.emplace_back("name", xml::encode(name));
            sc.children.push_back(std::move(m));
            partName = partName || name == "partName";
        }
        if (layout && !partName && !layout->has(F::INLINE_PART)) {   // (a part read from a file keeps what it had)
            Node m = leaf("metaTag", xml::encode(layout->text(F::NAME)));
            m.attrs.emplace_back("name", "partName");
            sc.children.push_back(std::move(m));
        }
        int partNo = 0;
        for (const Object* p : players) {
            sc.children.push_back(partNode(*p, ++partNo));
        }
        for (size_t i = 0; i < staves.size(); ++i) {
            sc.children.push_back(musicStaff(*staves[i], int(i) + 1));
        }
        for (Node& c : parseFragment(settings().text(F::SCORE_POST))) {
            sc.children.push_back(std::move(c));
        }
        return sc;
    }

    //! Where MuseScore's reader is when it reads this element (by-place links before 4.60): its time, and track
    void place(Node& n, const Fraction& t, int track = -1)
    {
        if (!annotate) {
            return;
        }
        n.attrs.emplace_back("je-tick", frac(t));
        if (track >= 0) {
            n.attrs.emplace_back("je-track", std::to_string(track));
        }
    }

    //! A tablature staff made in Jubelle: MuseScore's default tablature ("Tab. 6-str. common"), with a line for
    //! each of the instrument's strings, written as MuseScore writes it
    void tablatureType(Node& type, const Object& player)
    {
        size_t strings = 0;
        std::istringstream in(player.text(F::STRING_TUNING));
        for (std::string w; in >> w;) {
            ++strings;
        }
        if (strings == 0) {
            strings = 6;
        }
        type.children.push_back(leaf("name", strings >= 4 && strings <= 8 ? "tab" + std::to_string(strings) + "StrCommon" : std::string("tab6StrCommon")));
        const std::vector<std::pair<std::string, std::string> > values {
            { "lines", std::to_string(strings) }, { "lineDistance", "1.5" }, { "durations", "0" },
            { "durationFontName", "MuseScore Tab Modern" }, { "durationFontSize", "15" }, { "durationFontY", "0" },
            { "fretUseTextStyle", "1" }, { "fretTextStyle", "tab_fret_number" }, { "linesThrough", "0" }, { "minimStyle", "1" },
            { "onLines", "1" }, { "showRests", "1" }, { "stemsDown", "1" }, { "stemsThrough", "0" }, { "upsideDown", "0" },
            { "showTabFingering", "1" }, { "useNumbers", "1" },
        };
        for (const auto& [k, v] : values) {
            if (k == "lines" && v == "5") {
                continue;
            }
            type.children.push_back(leaf(k, v));
        }
    }

    Node partNode(const Object& p, int number)
    {
        Node part = elem("Part");
        const std::string origin = p.text(F::ORIGIN_EID);
        if (origin.rfind("part:", 0) != 0) {
            part.attrs.emplace_back("id", std::to_string(number));   // a part made in Jubelle
        } else if (origin.size() > 5) {
            part.attrs.emplace_back("id", origin.substr(5));
        }
        for (Id st : p.children()) {
            const Object* staff = s.find(st);
            Node sn = elem("Staff");
            sn.attrs.emplace_back("id", std::to_string(staffNumber[st]));
            if (layout && !modern) {
                sn.children.push_back(leaf("linkedTo", std::to_string(mainStaffNumber.at(st))));   // by number before 4.60
            }
            addEid(sn, *staff);
            if (!detailsFit(*staff)) {
                Node type = elem("StaffType");
                const std::string group = staff->text(F::SUBKIND).empty() ? std::string("pitched") : staff->text(F::SUBKIND);
                type.attrs.emplace_back("group", group);
                if (group == "tablature") {
                    tablatureType(type, p);
                } else {
                    type.children.push_back(leaf("name", "stdNormal"));
                }
                finishNode(type);
                sn.children.push_back(std::move(type));
            } else {
                for (Node& c : parseFragment(staff->text(F::DETAILS))) {
                    sn.children.push_back(std::move(c));
                }
            }
            part.children.push_back(std::move(sn));
        }
        // a part read from a file has a fingerprint (even with no details of its own): its original layout is kept
        // while its music fields are unchanged; a new or changed part gets the plain form
        const bool fit = p.has(F::DETAILS_KEY) && detailsKey(p) == p.intOr(F::DETAILS_KEY, 0);
        if (fit) {
            ++report.detailsWritten;
            for (Node& c : parseFragment(p.text(F::DETAILS))) {
                part.children.push_back(std::move(c));
            }
        } else {
            if (p.has(F::DETAILS_KEY)) {
                ++report.detailsDropped;
            }
            part.children.push_back(leaf("trackName", xml::encode(p.text(F::NAME))));
        }
        Node instr = elem("Instrument");
        if (p.has(F::INSTRUMENT)) {
            instr.attrs.emplace_back("id", xml::encode(p.text(F::INSTRUMENT)));
        }
        instr.children.push_back(leaf("longName", xml::encode(p.text(F::NAME))));
        if (p.has(F::SHORT_NAME)) {
            instr.children.push_back(leaf("shortName", xml::encode(p.text(F::SHORT_NAME))));
        }
        if (p.has(F::MUSESCORE_INSTRUMENT)) {
            instr.children.push_back(leaf("instrumentId", xml::encode(p.text(F::MUSESCORE_INSTRUMENT))));
        }
        if (p.has(F::TRANSPOSE_DIATONIC) || p.intOr(F::TRANSPOSE_CHROMATIC, 0) != 0) {
            instr.children.push_back(leaf("transposeDiatonic", std::to_string(p.intOr(F::TRANSPOSE_DIATONIC,
                                                                                      diatonicOf(p.intOr(F::TRANSPOSE_CHROMATIC, 0))))));
        }
        if (p.has(F::TRANSPOSE_CHROMATIC)) {
            instr.children.push_back(leaf("transposeChromatic", std::to_string(p.intOr(F::TRANSPOSE_CHROMATIC, 0))));
        }
        if (fit) {
            for (Node& c : parseFragment(p.text(F::DETAILS_2))) {
                instr.children.push_back(std::move(c));
            }
        } else if (p.has(F::STRING_TUNING)) {   // a fretted instrument's strings
            Node sd = elem("StringData");
            sd.children.push_back(leaf("frets", std::to_string(p.intOr(F::FRETS, 24))));
            std::istringstream in(p.text(F::STRING_TUNING));
            for (std::string w; in >> w;) {
                const bool open = !w.empty() && w.back() == '*';
                Node str = leaf("string", open ? w.substr(0, w.size() - 1) : w);
                if (open) {
                    str.attrs.emplace_back("open", "1");
                }
                sd.children.push_back(std::move(str));
            }
            finishNode(sd);
            instr.children.push_back(std::move(sd));
        }
        part.children.push_back(std::move(instr));
        return part;
    }

    // ---- music ----------------------------------------------------------------------------------------------------
    Node musicStaff(const Object& staff, int number)
    {
        Node st = elem("Staff");
        st.attrs.emplace_back("id", std::to_string(number));
        const bool first = number == 1;
        std::vector<const Object*> voicesByNumber(4, nullptr);
        for (Id v : staff.children()) {
            const Object* voice = s.find(v);
            const int64_t n = voice->intOr(F::NUMBER, 0);
            if (n >= 0 && n < 4) {
                voicesByNumber[size_t(n)] = voice;
            }
        }
        int lastVoice = 0;
        for (int i = 0; i < 4; ++i) {
            if (voicesByNumber[size_t(i)]) {
                lastVoice = i;
            }
        }
        std::vector<const Object*> frames;
        for (const Object* it : items) {
            if (it->text(F::SUBKIND) == "frame") {
                frames.push_back(it);
            }
        }
        size_t frameAt = 0;
        for (size_t m = 0; m < measures.size(); ++m) {
            if (first) {
                while (frameAt < frames.size() && !(measureStart[m] < frames[frameAt]->fractionOr(F::TIME, Fraction()))) {
                    const Fraction at = frames[frameAt]->fractionOr(F::TIME, Fraction());
                    for (Node& c : parseFragment(frames[frameAt++]->text(F::DETAILS))) {
                        st.children.push_back(std::move(c));
                        place(st.children.back(), at, 0);
                    }
                }
            }
            if (!layout) {   // the main score's multi-measure rests (a part makes its own)
                for (const Object* it : itemsNear(staff.id(), int(m))) {
                    if (it->text(F::SUBKIND) == "mmrMeasure" && it->idOr(F::ANCHOR) == staff.id()
                        && it->fractionOr(F::TIME, Fraction()) == measureStart[m]) {
                        for (Node& c : parseFragment(it->text(F::DETAILS))) {
                            st.children.push_back(std::move(c));
                        }
                    }
                }
            }
            st.children.push_back(measureNode(staff, int(m), first, voicesByNumber, lastVoice));
        }
        if (!layout) {   // one after the last measure
            for (const Object* it : items) {
                if (it->text(F::SUBKIND) == "mmrMeasure" && it->idOr(F::ANCHOR) == staff.id()
                    && !(it->fractionOr(F::TIME, Fraction()) < flowEnd)) {
                    for (Node& c : parseFragment(it->text(F::DETAILS))) {
                        st.children.push_back(std::move(c));
                    }
                }
            }
        }
        if (first) {
            while (frameAt < frames.size()) {
                for (Node& c : parseFragment(frames[frameAt++]->text(F::DETAILS))) {
                    st.children.push_back(std::move(c));
                }
            }
        }
        return st;
    }

    Node measureNode(const Object& staff, int m, bool first, const std::vector<const Object*>& voices, int lastVoice)
    {
        const Object& mo = *measures[size_t(m)];
        Node me = elem("Measure");
        if (annotate) {
            me.attrs.emplace_back("je-track", std::to_string((staffNumber.at(staff.id()) - 1) * 4));
            place(me, measureStart[size_t(m)]);
        }
        const Fraction nominal(mo.intOr(F::TIMESIG_NUM, 4), mo.intOr(F::TIMESIG_DEN, 4));
        if (!(nominal == measureLen[size_t(m)])) {
            me.attrs.emplace_back("len", frac(measureLen[size_t(m)]));
        }
        if (first) {
            addEid(me, mo);
        }
        if (mo.has(F::REPEAT_START)) {
            me.children.push_back(empty("startRepeat"));
        }
        if (mo.has(F::REPEAT_END)) {
            me.children.push_back(leaf("endRepeat", std::to_string(mo.intOr(F::REPEAT_END, 2))));
        }
        if (mo.has(F::IRREGULAR)) {
            me.children.push_back(leaf("irregular", "1"));
        }
        if (first && !layout && mo.has(F::BREAK)) {
            Node lb = elem("LayoutBreak");
            lb.children.push_back(leaf("subtype", xml::encode(mo.text(F::BREAK))));
            me.children.push_back(std::move(lb));
        }
        if (first && layout) {   // a part has its own breaks
            for (const auto& [index, kind] : unpackPairs(layout->text(F::LAYOUT_BREAKS))) {
                if (index == std::to_string(m)) {
                    Node lb = elem("LayoutBreak");
                    lb.children.push_back(leaf("subtype", xml::encode(kind)));
                    me.children.push_back(std::move(lb));
                }
            }
        }
        for (const Object* it : itemsNear(staff.id(), m)) {
            if (it->text(F::SUBKIND) == "measureDetail" && it->idOr(F::ANCHOR) == staff.id()
                && it->fractionOr(F::TIME, Fraction()) == measureStart[size_t(m)]) {
                for (Node& c : parseFragment(it->text(F::DETAILS))) {
                    me.children.push_back(std::move(c));
                }
            }
        }
        auto measureItems = [&](bool atEnd) {
            for (const Object* it : itemsNear(staff.id(), m)) {
                const std::string sub = it->text(F::SUBKIND);
                const Id where = it->has(F::ANCHOR) ? it->idOr(F::ANCHOR) : homeOf(*it);
                if (sub != "frame" && sub != "measureDetail" && sub != "mmrMeasure" && measureLevel(*it) && where == staff.id()
                    && measureOfItem(*it) == m && it->has(F::AT_END) == atEnd) {
                    me.children.push_back(itemNode(*it));
                    place(me.children.back(), it->fractionOr(F::TIME, Fraction()));
                }
            }
        };
        measureItems(false);
        // the voices written: up to the highest with anything in this measure (an item may sit in an otherwise empty voice)
        for (const Object* it : itemsNear(staff.id(), m)) {
            if (it->has(F::VOICE_INDEX) && (it->has(F::ANCHOR) ? it->idOr(F::ANCHOR) : homeOf(*it)) == staff.id()) {
                const Fraction t = it->fractionOr(F::TIME, Fraction());
                if (it->has(F::AT_END) ? measureEnding(t) == m : measureAt(t) == m) {
                    lastVoice = std::max(lastVoice, int(it->intOr(F::VOICE_INDEX, 0)));
                }
            }
        }
        for (const Object* sp : spannersNear(staff.id(), m)) {
            if (spannerPlace(*sp).first == staff.id()) {
                const Fraction t = sp->fractionOr(F::TIME, Fraction());
                const Fraction e = t + sp->fractionOr(F::DURATION, Fraction());
                if (measureAt(t) == m || measureEnding(e) == m) {
                    lastVoice = std::max(lastVoice, spannerPlace(*sp).second);
                }
            }
        }
        lastVoice = std::min(lastVoice, 3);
        for (int v = 0; v <= lastVoice; ++v) {
            Node vo = elem("voice");
            if (annotate) {
                vo.attrs.emplace_back("je-track", std::to_string((staffNumber.at(staff.id()) - 1) * 4 + v));
            }
            writeVoice(vo, staff, voices[size_t(v)], v, m);
            finishNode(vo);
            me.children.push_back(std::move(vo));
        }
        measureItems(true);
        return me;
    }

    //! One element of a voice's stream at a time: items and spanner markers before the event at that time
    struct Entry {
        Fraction time;
        int order;       // 0 spanner end, 1 item, 2 spanner start, 3 event, 4 at end
        int64_t seq;
        const Object* object;
        bool spannerEnd = false;
    };

    const Object* playerOf(const Object& o) const
    {
        Id a = o.parent();
        for (int guard = 0; a.isValid() && guard < 8; ++guard) {
            const Object* x = s.find(a);
            if (!x) {
                return nullptr;
            }
            if (x->kind() == Kind::Player) {
                return x;
            }
            a = x->parent();
        }
        return nullptr;
    }

    //! The key (fifths) in effect at an event: the last key signature at or before it on its staff or for all staves
    int64_t keyAt(const Object& ev) const
    {
        Id staff;
        for (Id a = ev.parent(); a.isValid();) {
            const Object* x = s.find(a);
            if (!x) {
                break;
            }
            if (x->kind() == Kind::Staff) {
                staff = a;
                break;
            }
            a = x->parent();
        }
        const Fraction t = ev.fractionOr(F::TIME, Fraction());
        int64_t key = 0;
        Fraction best(-1, 1);
        if (!keySigsFound) {
            keySigsFound = true;
            for (const Object* it : items) {
                if (it->text(F::SUBKIND) == "keySig") {
                    keySigs.push_back(it);
                }
            }
        }
        for (const Object* it : keySigs) {
            const Fraction at = it->fractionOr(F::TIME, Fraction());
            const bool here = !it->has(F::ANCHOR) || it->idOr(F::ANCHOR) == staff;
            if (here && !(t < at) && best < at) {
                best = at;
                key = it->intOr(F::INT_VALUE, 0);
            }
        }
        return key;
    }

    //! The staff and voice a spanner is written in: its anchor if that's a staff; for one anchored to an event (or a
    //! note), that event's staff and voice; for one anchored to a measure (a volta) or nothing, the first staff
    std::pair<Id, int> spannerPlace(const Object& sp) const
    {
        Id a = sp.idOr(F::ANCHOR);
        int voice = int(sp.intOr(F::VOICE_INDEX, -1));
        for (int guard = 0; a.isValid() && guard < 8; ++guard) {
            const Object* o = s.find(a);
            if (!o) {
                break;
            }
            if (o->kind() == Kind::Staff) {
                return { a, voice < 0 ? 0 : voice };
            }
            if (o->kind() == Kind::Voice && voice < 0) {
                voice = int(o->intOr(F::NUMBER, 0));
            }
            if (o->kind() == Kind::Measure || o->kind() == Kind::Flow || o->kind() == Kind::Score) {
                break;
            }
            a = o->parent();
        }
        return { staves.empty() ? Id() : staves.front()->id(), voice < 0 ? 0 : voice };
    }

    //! Where a system item (tempo, rehearsal mark…) is written: the staff it was read from, or the first staff when
    //! that one isn't in the document (a part without it)
    Id homeOf(const Object& it) const
    {
        const Id home = it.idOr(F::HOME_STAFF);
        if (home.isValid() && staffNumber.count(home)) {
            return home;
        }
        return staves.empty() ? Id() : staves.front()->id();
    }

    //! Items written by the measure, not in a voice: those read from there (no voice index), and jumps and markers
    //! made in Jubelle (MuseScore keeps them in the measure)
    static bool measureLevel(const Object& it)
    {
        if (it.has(F::VOICE_INDEX)) {
            return false;
        }
        if (it.has(F::TAG)) {
            return true;
        }
        const std::string sub = it.text(F::SUBKIND);
        return sub == "jump" || sub == "marker";
    }

    //! A key or time signature made for all staves (no staff of its own): written on every staff
    static bool onEveryStaff(const Object& it)
    {
        const std::string sub = it.text(F::SUBKIND);
        return (sub == "keySig" || sub == "timeSig") && !it.has(F::ANCHOR) && !it.has(F::HOME_STAFF) && !it.has(F::TAG);
    }

    //! The measure a measure-level item belongs to: the one starting at its time, else the one it ends (a jump at
    //! the end of the music)
    int measureOfItem(const Object& it) const
    {
        const Fraction t = it.fractionOr(F::TIME, Fraction());
        const int m = measureAt(t);
        return measureStart[size_t(m)] == t ? m : measureEnding(t);
    }

    // ---- items, lines and events by measure (each measure looks only at its own, not the whole score) --------
    //! The items a staff writes in a measure, or could (each measure and staff looks only at its own)
    const std::vector<const Object*>& itemsNear(Id staff, int m)
    {
        if (itemBucket.empty()) {
            for (const Object* st : staves) {
                itemBucket[st->id()].assign(measures.size() + 1, {});
            }
            for (const Object* it : items) {
                const Fraction t = it->fractionOr(F::TIME, Fraction());
                std::set<int> ms { measureAt(t), measureEnding(t), measureOfItem(*it) };
                const Id target = it->has(F::ANCHOR) ? it->idOr(F::ANCHOR) : homeOf(*it);
                for (auto& [st, byMeasure] : itemBucket) {
                    if (st != target && !onEveryStaff(*it)) {
                        continue;
                    }
                    for (int x : ms) {
                        if (x >= 0 && size_t(x) < byMeasure.size()) {
                            byMeasure[size_t(x)].push_back(it);
                        }
                    }
                }
            }
        }
        static const std::vector<const Object*> none;
        auto b = itemBucket.find(staff);
        if (b == itemBucket.end()) {
            return none;
        }
        return size_t(m) < b->second.size() ? b->second[size_t(m)] : b->second.back();
    }

    const std::vector<const Object*>& spannersNear(Id staff, int m)
    {
        if (spannerBucket.empty()) {
            for (const Object* st : staves) {
                spannerBucket[st->id()].assign(measures.size() + 1, {});
            }
            for (const Object* sp : spanners) {
                const Fraction t = sp->fractionOr(F::TIME, Fraction());
                const Fraction e = t + sp->fractionOr(F::DURATION, Fraction());
                std::set<int> ms { measureAt(t), measureAt(e), measureEnding(e) };
                auto b = spannerBucket.find(spannerPlace(*sp).first);
                if (b == spannerBucket.end()) {
                    continue;
                }
                for (int x : ms) {
                    if (x >= 0 && size_t(x) < b->second.size()) {
                        b->second[size_t(x)].push_back(sp);
                    }
                }
            }
        }
        static const std::vector<const Object*> none;
        auto b = spannerBucket.find(staff);
        if (b == spannerBucket.end()) {
            return none;
        }
        return size_t(m) < b->second.size() ? b->second[size_t(m)] : b->second.back();
    }

    //! A voice's events and tuplets by measure (as measureAt places them)
    const std::vector<const Object*>& eventsNear(const Object& voice, int m)
    {
        auto it = voiceBucket.find(voice.id());
        if (it == voiceBucket.end()) {
            std::vector<std::vector<const Object*> > b(measures.size() + 1);
            for (Id c : voice.children()) {
                const Object* ev = s.find(c);
                const int x = measureAt(ev->fractionOr(F::TIME, Fraction()));
                if (x >= 0 && size_t(x) < b.size()) {
                    b[size_t(x)].push_back(ev);
                }
            }
            it = voiceBucket.emplace(voice.id(), std::move(b)).first;
        }
        return size_t(m) < it->second.size() ? it->second[size_t(m)] : it->second.back();
    }

    bool itemInVoice(const Object& it, const Object& staff, int v) const
    {
        const std::string sub = it.text(F::SUBKIND);
        if (sub == "frame" || sub == "measureDetail" || sub == "mmrMeasure" || measureLevel(it)) {
            return false;   // frames and measure-level items are written by the measure
        }
        const Id anchor = it.idOr(F::ANCHOR);
        const bool here = onEveryStaff(it) || (anchor.isValid() ? anchor == staff.id() : homeOf(it) == staff.id());
        return here && int(it.intOr(F::VOICE_INDEX, 0)) == v;
    }

    //! Whether this staff needs a time signature written at the start of measure m: the measure's signature
    //! differs from the one before (or it is the first) and no time signature item gives it
    bool needsTimeSig(const Object& staff, int m) const
    {
        const Object& mo = *measures[size_t(m)];
        const int64_t n = mo.intOr(F::TIMESIG_NUM, 4), d = mo.intOr(F::TIMESIG_DEN, 4);
        if (m > 0) {
            const Object& prev = *measures[size_t(m - 1)];
            if (prev.intOr(F::TIMESIG_NUM, 4) == n && prev.intOr(F::TIMESIG_DEN, 4) == d) {
                return false;
            }
        }
        for (const Object* it : items) {
            if (it->text(F::SUBKIND) == "timeSig" && (it->idOr(F::ANCHOR) == staff.id() || onEveryStaff(*it))
                && it->fractionOr(F::TIME, Fraction()) == measureStart[size_t(m)]) {
                return false;
            }
        }
        // a file's measures already have theirs (read as items); only measures made in Jubelle need one
        return !mo.has(F::ORIGIN_EID) && !anyFromFile;
    }

    void writeVoice(Node& out, const Object& staff, const Object* voice, int v, int m)
    {
        if (v == 0 && needsTimeSig(staff, m)) {
            const Object& mo = *measures[size_t(m)];
            Node ts = elem("TimeSig");
            if (needLinks) {   // an identity of its own, derived from its measure's and staff, the same on every save
                ts.children.push_back(leaf("eid", partEid(eidOf(mo), -1000 - staffNumber.at(staff.id()))));
            }
            ts.children.push_back(leaf("sigN", std::to_string(mo.intOr(F::TIMESIG_NUM, 4))));
            ts.children.push_back(leaf("sigD", std::to_string(mo.intOr(F::TIMESIG_DEN, 4))));
            out.children.push_back(std::move(ts));
        }
        const Fraction mStart = measureStart[size_t(m)];
        const Fraction mEnd = mStart + measureLen[size_t(m)];
        std::vector<Entry> entries;
        for (const Object* it : itemsNear(staff.id(), m)) {
            if (!itemInVoice(*it, staff, v)) {
                continue;
            }
            const Fraction t = it->fractionOr(F::TIME, Fraction());
            if (it->has(F::AT_END)) {
                if (measureEnding(t) == m) {
                    entries.push_back({ t, 4, it->intOr(F::SEQ, 0), it });
                }
            } else if (measureAt(t) == m) {   // (in the last measure, also anything past its end)
                entries.push_back({ t, 1, it->intOr(F::SEQ, 0), it });
            }
        }
        for (const Object* sp : spannersNear(staff.id(), m)) {
            if (inEvent.count(sp->id()) || spannerPlace(*sp) != std::make_pair(staff.id(), v)) {
                continue;   // (lines inside chords are written by the chords)
            }
            const Fraction t = sp->fractionOr(F::TIME, Fraction());
            const Fraction e = t + sp->fractionOr(F::DURATION, Fraction());
            if (measureAt(t) == m) {   // (in the last measure, also anything past its end)
                entries.push_back({ t, 2, sp->intOr(F::SEQ, 0), sp });
            }
            // the end marker: in the measure the end falls in (at a barline, the start of the next measure, as
            // MuseScore writes it); at the very end of the music, after the last measure's events
            if (measureAt(e) == m) {
                const int order = (e == mEnd && !(e < flowEnd)) ? 4 : (e == t ? 2 : 0);
                Entry end { e, order, sp->intOr(F::SEQ, 0), sp, true };
                entries.push_back(end);
            }
        }
        std::vector<const Object*> events;
        if (voice) {
            for (const Object* ev : eventsNear(*voice, m)) {
                if (ev->kind() == Kind::Tuplet) {
                    continue;
                }
                const Fraction t = ev->fractionOr(F::TIME, Fraction());
                if (measureAt(t) == m) {   // (in the last measure, also anything past its end)
                    events.push_back(ev);
                }
            }
        }
        // the events in groups: a main chord or rest with its grace notes, written before-graces in order, then its
        // after-graces in file order (the model holds them after the main chord, reversed), then the main chord
        std::vector<std::vector<const Object*> > groups;
        std::vector<const Object*> pending;
        for (size_t i = 0; i < events.size(); ++i) {
            const Object* ev = events[i];
            if (ev->has(F::GRACE)) {
                pending.push_back(ev);   // a before-grace (or an after-grace with no main chord before it)
                continue;
            }
            std::vector<const Object*> after;
            while (i + 1 < events.size() && events[i + 1]->has(F::GRACE) && isAfter(*events[i + 1])) {
                after.push_back(events[++i]);
            }
            // as MuseScore writes them: the after-graces (in file order, the model's reversed), then the
            // before-graces, then the main chord
            std::reverse(after.begin(), after.end());
            pending.insert(pending.begin(), after.begin(), after.end());
            pending.push_back(ev);
            groups.push_back(std::move(pending));
            pending.clear();
        }
        if (!pending.empty()) {
            groups.push_back(std::move(pending));
        }
        for (size_t g = 0; g < groups.size(); ++g) {
            entries.push_back({ groups[g].back()->fractionOr(F::TIME, Fraction()), 3, int64_t(g), nullptr });
        }
        std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
            if (a.order == 4 || b.order == 4) {
                return a.order != 4 && b.order == 4 ? true : (a.order == 4 && b.order == 4 ? a.seq < b.seq : false);
            }
            if (!(a.time == b.time)) {
                return a.time < b.time;
            }
            if (a.order != b.order) {
                return a.order < b.order;
            }
            return a.seq < b.seq;
        });

        Fraction cursor = mStart;
        std::vector<Id> openTuplets;
        auto moveTo = [&out, &cursor](const Fraction& t) {
            if (!(t == cursor)) {
                Node loc = elem("location");
                loc.children.push_back(leaf("fractions", frac(t - cursor)));
                out.children.push_back(std::move(loc));
                cursor = t;
            }
        };
        auto closeTuplets = [&out, &openTuplets](size_t keep) {
            while (openTuplets.size() > keep) {
                out.children.push_back(empty("endTuplet"));
                openTuplets.pop_back();
            }
        };
        for (const Entry& e : entries) {
            if (e.object && e.object->kind() == Kind::Spanner) {
                moveTo(e.time);
                out.children.push_back(e.spannerEnd ? spannerEnd(*e.object, m, e.time) : spannerStart(*e.object, m));
                place(out.children.back(), cursor);
            } else if (e.object) {
                moveTo(e.time);
                copyStaff = staffNumber.at(staff.id());
                out.children.push_back(itemNode(*e.object));
                place(out.children.back(), cursor);
                copyStaff = 0;
            } else {
                const auto& g = groups[size_t(e.seq)];
                const Object* main = g.back();
                // tuplets: close those this event isn't in, open the ones it is in
                std::vector<Id> chain;
                for (Id t = main->idOr(F::TUPLET); t.isValid(); t = s.find(t) ? s.find(t)->idOr(F::TUPLET) : Id()) {
                    chain.insert(chain.begin(), t);
                }
                size_t common = 0;
                while (common < chain.size() && common < openTuplets.size() && chain[common] == openTuplets[common]) {
                    ++common;
                }
                closeTuplets(common);
                for (size_t i = common; i < chain.size(); ++i) {
                    // a tuplet starts where it starts, even if its first event comes later (a hole at its start)
                    const Fraction tt = s.find(chain[i])->fractionOr(F::TIME, e.time);
                    if (cursor < tt && !(e.time < tt)) {
                        moveTo(tt);
                    }
                    out.children.push_back(tupletNode(*s.find(chain[i])));
                    place(out.children.back(), cursor);
                    openTuplets.push_back(chain[i]);
                }
                moveTo(e.time);
                for (Id c : main->children()) {
                    const Object* a = s.find(c);
                    if (a->kind() == Kind::Attachment && a->text(F::SUBKIND) == "fermata" && !a->has(F::TAG)) {
                        Node f = elem("Fermata");
                        addEid(f, *a);
                        f.children.push_back(leaf("subtype", xml::encode(a->text(F::TEXT).empty() ? std::string("fermataAbove")
                                                                                            : a->text(F::TEXT))));
                        out.children.push_back(std::move(f));
                        place(out.children.back(), cursor);
                    }
                }
                for (const Object* ev : g) {
                    out.children.push_back(eventNode(*ev, m));
                    place(out.children.back(), cursor);
                }
                if (!main->has(F::GRACE)) {
                    cursor += main->fractionOr(F::DURATION, Fraction());
                }
            }
        }
        closeTuplets(0);
    }

    static bool isAfter(const Object& o)
    {
        const std::string t = o.text(F::GRACE_TYPE);
        return t.size() > 5 && t.compare(t.size() - 5, 5, "after") == 0;
    }

    Node tupletNode(const Object& t)
    {
        Node n = elem("Tuplet");
        addEid(n, t);
        n.children.push_back(leaf("normalNotes", std::to_string(t.intOr(F::RATIO_DEN, 2))));
        n.children.push_back(leaf("actualNotes", std::to_string(t.intOr(F::RATIO_NUM, 3))));
        n.children.push_back(leaf("baseNote", durationName(t.fractionOr(F::BASE_DURATION, Fraction(1, 8)))));
        addDetails(n, t);
        return n;
    }

    //! The product of the ratios of the tuplets an event is in, divided by the stretch (actual / written)
    Fraction factorOf(const Object& ev) const
    {
        Fraction f(1, 1);
        for (Id t = ev.idOr(F::TUPLET); t.isValid();) {
            const Object* to = s.find(t);
            if (!to) {
                break;
            }
            f = f * Fraction(to->intOr(F::RATIO_DEN, 1), to->intOr(F::RATIO_NUM, 1));
            t = to->idOr(F::TUPLET);
        }
        return f / ev.fractionOr(F::STRETCH, Fraction(1, 1));
    }

    Node eventNode(const Object& ev, int m)
    {
        const std::string tag = ev.has(F::TAG) ? ev.text(F::TAG) : (ev.kind() == Kind::Chord ? "Chord" : "Rest");
        Node n = elem(tag);
        addEid(n, ev);
        addDetails(n, ev);
        if (ev.has(F::MEASURE_REST)) {
            n.children.push_back(leaf("durationType", "measure"));
            // the written length: on a staff with a local time signature, the actual one times its stretch
            n.children.push_back(leaf("duration", frac(ev.fractionOr(F::DURATION, measureLen[size_t(m)])
                                                       * ev.fractionOr(F::STRETCH, Fraction(1, 1)))));
        } else {
            if (ev.intOr(F::DOTS, 0) > 0) {
                n.children.push_back(leaf("dots", std::to_string(ev.intOr(F::DOTS, 0))));
            }
            n.children.push_back(leaf("durationType", durationName(ev.fractionOr(F::BASE_DURATION, ev.fractionOr(F::DURATION, Fraction(1, 4))))));
            if (ev.has(F::EXPLICIT_DURATION)) {
                n.children.push_back(leaf("duration", frac(ev.fractionOr(F::DURATION, Fraction()) / factorOf(ev))));
            } else if (ev.has(F::GRACE_LENGTH)) {
                n.children.push_back(leaf("duration", frac(ev.fractionOr(F::GRACE_LENGTH, Fraction()))));
            }
        }
        if (ev.intOr(F::STAFF_MOVE, 0) != 0) {
            n.children.push_back(leaf("staffMove", std::to_string(ev.intOr(F::STAFF_MOVE, 0))));
        }
        if (ev.has(F::GRACE)) {
            n.children.push_back(empty(ev.text(F::GRACE_TYPE).empty() ? std::string("acciaccatura") : ev.text(F::GRACE_TYPE)));
        }
        if (auto it = endsIn.find(ev.id()); it != endsIn.end()) {
            for (const Object* sp : it->second) {
                n.children.push_back(spannerEnd(*sp, m, ev.fractionOr(F::TIME, Fraction())));
            }
        }
        if (auto it = startsIn.find(ev.id()); it != startsIn.end()) {
            for (const Object* sp : it->second) {
                n.children.push_back(spannerStart(*sp, m));
            }
        }
        for (Id c : ev.children()) {   // in the model's order, which is the file's
            const Object* child = s.find(c);
            if (child->kind() == Kind::Attachment && child->text(F::SUBKIND) == "fermata" && !child->has(F::TAG)) {
                continue;   // a fermata is MuseScore's segment element: written before the chord
            }
            if (child->kind() == Kind::Attachment) {
                n.children.push_back(attachmentNode(*child));
            } else if (child->kind() == Kind::Note) {
                n.children.push_back(noteNode(*child, ev, m));
            }
        }
        if (ev.has(F::DETAILS_AFTER) && (!ev.has(F::DETAILS_KEY) || detailsKey(ev) == ev.intOr(F::DETAILS_KEY, 0))) {
            for (Node& c : parseFragment(ev.text(F::DETAILS_AFTER))) {
                n.children.push_back(std::move(c));
            }
        }
        return n;
    }

    Node attachmentNode(const Object& a)
    {
        const std::string sub = a.text(F::SUBKIND);
        if (sub == "lyric") {
            Node n = elem("Lyrics");
            addEid(n, a);
            addDetails(n, a);
            if (a.intOr(F::VERSE, 0) > 0) {
                n.children.push_back(leaf("no", std::to_string(a.intOr(F::VERSE, 0))));
            }
            if (a.has(F::SYLLABIC)) {
                n.children.push_back(leaf("syllabic", a.text(F::SYLLABIC)));
            }
            if (a.has(F::DURATION)) {
                n.children.push_back(leaf("ticks_f", frac(a.fractionOr(F::DURATION, Fraction()))));
            }
            n.children.push_back(leaf("text", a.text(F::TEXT)));
            return n;
        }
        if (sub == "fingering") {
            Node n = elem("Fingering");
            addEid(n, a);
            addDetails(n, a);
            n.children.push_back(leaf("text", a.text(F::TEXT)));
            return n;
        }
        const std::string symbol = a.has(F::TAG) ? sub : articulationSymbol(sub);
        const std::string tag = a.has(F::TAG) ? a.text(F::TAG) : (isOrnamentSymbol(symbol) ? "Ornament" : "Articulation");
        Node n = elem(tag);
        addEid(n, a);
        if (tag == "Articulation" || tag == "Ornament") {
            n.children.push_back(leaf("subtype", xml::encode(symbol)));
        }
        addDetails(n, a);
        finishNode(n);
        return n;
    }

    //! Lines MuseScore keeps inside their chords (slurs, hammer-ons): the chord or rest each starts in and ends in
    void indexEventSpanners()
    {
        auto isEvent = [this](Id id) {
            const Object* o = s.find(id);
            return o && (o->kind() == Kind::Chord || o->kind() == Kind::Rest);
        };
        for (const Object* sp : spanners) {
            Id start = sp->idOr(F::START_EVENT);
            const std::string type = spannerType(sp->text(F::SUBKIND));
            if (!isEvent(start) && (type == "Slur" || type == "HammerOnPullOff") && isEvent(sp->idOr(F::ANCHOR))) {
                start = sp->idOr(F::ANCHOR);   // made in Jubelle, anchored to its first chord
            }
            if (!isEvent(start)) {
                continue;
            }
            Id end = sp->idOr(F::END_ANCHOR);
            if (!isEvent(end)) {
                end = Id();
                const Fraction at = sp->fractionOr(F::TIME, Fraction()) + sp->fractionOr(F::DURATION, Fraction());
                const Object* voice = s.find(s.find(start)->parent());
                std::vector<const Object*> candidates;
                if (voice) {
                    // the staff and voice it ends in (as read), else its own voice, then the staff's other voices
                    const Object* endStaff = s.find(sp->has(F::END_STAFF) ? sp->idOr(F::END_STAFF) : voice->parent());
                    const int64_t endVoice = sp->intOr(F::END_VOICE, voice->intOr(F::NUMBER, 0));
                    if (endStaff) {
                        for (Id v : endStaff->children()) {
                            if (s.find(v)->kind() == Kind::Voice && s.find(v)->intOr(F::NUMBER, 0) == endVoice) {
                                candidates.push_back(s.find(v));
                            }
                        }
                        for (Id v : endStaff->children()) {
                            if (s.find(v)->kind() == Kind::Voice && s.find(v)->intOr(F::NUMBER, 0) != endVoice) {
                                candidates.push_back(s.find(v));
                            }
                        }
                    }
                }
                if (sp->has(F::END_GRACE)) {   // a grace chord, by its index
                    for (const Object* v : candidates) {
                        for (Id e : v->children()) {
                            const Object* o = s.find(e);
                            if (!end.isValid() && o->has(F::GRACE) && o->fractionOr(F::TIME, Fraction()) == at
                                && graceIndexOf(*o) == sp->intOr(F::END_GRACE, -1)) {
                                end = e;
                            }
                        }
                    }
                }
                for (int pass = 0; pass < 2 && !end.isValid(); ++pass) {
                    for (const Object* v : candidates) {
                        for (Id e : v->children()) {
                            const Object* o = s.find(e);
                            // (a partial slur, at a repeat, starts and ends on the same chord)
                            if (!end.isValid() && (o->kind() == Kind::Chord || o->kind() == Kind::Rest) && (e != start || pass == 1)
                                && o->fractionOr(F::TIME, Fraction()) == at && (pass == 1 || !o->has(F::GRACE))) {
                                end = e;
                            }
                        }
                    }
                }
            }
            if (!end.isValid()) {
                continue;   // no chord at its end: written in the voice
            }
            startsIn[start].push_back(sp);
            endsIn[end].push_back(sp);
            inEvent.insert(sp->id());
            spanStart[sp->id()] = start;
            spanEnd[sp->id()] = end;
        }
    }

    //! A location's staff and voice steps, when its target event is on another staff or in another voice
    void crossTo(Node& loc, const Object& target, const Object& from)
    {
        const Object* tv = s.find(target.parent());
        const Object* fv = s.find(from.parent());
        if (!tv || !fv) {
            return;
        }
        std::vector<Node> add;
        if (tv->parent() != fv->parent() && staffNumber.count(tv->parent()) && staffNumber.count(fv->parent())) {
            add.push_back(leaf("staves", std::to_string(staffNumber.at(tv->parent()) - staffNumber.at(fv->parent()))));
        }
        const int64_t dv = tv->intOr(F::NUMBER, 0) - fv->intOr(F::NUMBER, 0);
        if (dv != 0) {
            add.push_back(leaf("voices", std::to_string(dv)));
        }
        if (!add.empty()) {
            loc.leaf = false;
            loc.children.insert(loc.children.begin(), add.begin(), add.end());
        }
    }

    //! A grace chord's index as MuseScore's locations count it: its place among its main chord's grace notes in
    //! the order they are written (after-graces first, in file order, then the before-graces)
    int64_t graceIndexOf(const Object& ev) const
    {
        const Object* voice = s.find(ev.parent());
        const Fraction t = ev.fractionOr(F::TIME, Fraction());
        std::vector<Id> before, after;
        bool afterMain = false;
        for (Id e : voice->children()) {
            const Object* o = s.find(e);
            if (o->fractionOr(F::TIME, Fraction()) != t || (o->kind() != Kind::Chord && o->kind() != Kind::Rest)) {
                continue;
            }
            if (!o->has(F::GRACE)) {
                afterMain = true;
            } else if (afterMain && isAfter(*o)) {
                after.push_back(e);
            } else {
                before.push_back(e);
            }
        }
        std::reverse(after.begin(), after.end());
        std::vector<Id> order = after;
        order.insert(order.end(), before.begin(), before.end());
        auto it = std::find(order.begin(), order.end(), ev.id());
        return it == order.end() ? 0 : int64_t(it - order.begin());
    }

    //! A relative location from `from`, in measure `mf` (the measure being written: MuseScore reads a location from
    //! the measure it is in, even at its end), to `to`
    Node location(int mf, const Fraction& from, const Fraction& to)
    {
        const int mt = measureAt(to);
        Node loc = elem("location");
        const Fraction pf = from - measureStart[size_t(mf)];
        const Fraction pt = to - measureStart[size_t(mt)];
        if (mt != mf) {
            loc.children.push_back(leaf("measures", std::to_string(mt - mf)));
        }
        if (!(pt == pf)) {
            loc.children.push_back(leaf("fractions", frac(pt - pf)));
        }
        finishNode(loc);
        return loc;
    }

    static int noteIndexOf(const model::Snapshot& snap, const Object& note)
    {
        const Object* c = snap.find(note.parent());
        int i = 0;
        for (Id n : c ? c->children() : model::IdList()) {
            if (n == note.id()) {
                return i;
            }
            if (snap.find(n)->kind() == Kind::Note) {
                ++i;
            }
        }
        return 0;
    }

    //! Where a line's other note is, relative to this one (MuseScore's location: staves, voices, measures,
    //! fractions, the grace chord by its index, and the note's place in its chord)
    Node lineLocation(const Object& fromNote, const Object& toNote)
    {
        const Object* fc = s.find(fromNote.parent());
        const Object* tc = s.find(toNote.parent());
        const Fraction from = fc->fractionOr(F::TIME, Fraction()), to = tc->fractionOr(F::TIME, Fraction());
        Node loc = location(measureAt(from), from, to);
        crossTo(loc, *tc, *fc);
        if (tc->has(F::GRACE)) {
            loc.leaf = false;
            loc.children.push_back(leaf("grace", std::to_string(graceIndexOf(*tc))));
        }
        const int dn = noteIndexOf(s, toNote) - noteIndexOf(s, fromNote);
        if (dn != 0) {
            loc.leaf = false;
            loc.children.push_back(leaf("notes", std::to_string(dn)));
        }
        finishNode(loc);
        return loc;
    }

    void noteLines(Node& n, const Object& note)
    {
        auto typeOf = [](const Object& sp) {
            std::string t = sp.text(F::TAG);
            if (t.empty()) {
                t = sp.text(F::SUBKIND);
                if (!t.empty()) {
                    t[0] = char(std::toupper(static_cast<unsigned char>(t[0])));
                }
            }
            return t;
        };
        if (auto it = lineStarts.find(note.id()); it != lineStarts.end()) {
            for (const Object* sp : it->second) {
                const Object* to = s.find(sp->idOr(F::END_ANCHOR));
                if (!to || to->kind() != Kind::Note) {
                    continue;
                }
                const std::string type = typeOf(*sp);
                Node sn = elem("Spanner");
                sn.attrs.emplace_back("type", type);
                Node body = elem(type);
                addEid(body, *sp);
                if (detailsFit(*sp)) {
                    for (Node& c : parseFragment(sp->text(F::DETAILS))) {
                        body.children.push_back(std::move(c));
                    }
                }
                finishNode(body);
                sn.children.push_back(std::move(body));
                Node next = elem("next");
                next.children.push_back(lineLocation(note, *to));
                finishNode(next);
                sn.children.push_back(std::move(next));
                finishNode(sn);
                n.children.push_back(std::move(sn));
            }
        }
        if (auto it = lineEnds.find(note.id()); it != lineEnds.end()) {
            for (const Object* sp : it->second) {
                const Object* from = s.find(sp->idOr(F::ANCHOR));
                if (!from || from->kind() != Kind::Note) {
                    continue;
                }
                Node sn = elem("Spanner");
                sn.attrs.emplace_back("type", typeOf(*sp));
                Node prev = elem("prev");
                prev.children.push_back(lineLocation(note, *from));
                finishNode(prev);
                sn.children.push_back(std::move(prev));
                finishNode(sn);
                n.children.push_back(std::move(sn));
            }
        }
        for (Node& c : parseFragment(note.text(F::LINE_DETAILS))) {
            n.children.push_back(std::move(c));
        }
    }

    Node noteNode(const Object& note, const Object& chord, int m)
    {
        (void)m;
        Node n = elem("Note");
        addEid(n, note);
        addDetails(n, note);
        for (Id c : note.children()) {
            const Object* a = s.find(c);
            if (a->kind() == Kind::Attachment) {
                n.children.push_back(attachmentNode(*a));
            }
        }
        const Fraction at = chord.fractionOr(F::TIME, Fraction());
        auto from = tieFrom.find(note.id());
        if (from != tieFrom.end()) {
            const Object* fn = s.find(from->second);
            const Object* fc = fn ? s.find(fn->parent()) : nullptr;
            if (fc) {
                Node sp = elem("Spanner");
                sp.attrs.emplace_back("type", "Tie");
                Node prev = elem("prev");
                prev.children.push_back(location(m, at, fc->fractionOr(F::TIME, Fraction())));
                crossTo(prev.children.back(), *fc, chord);
                if (fc->has(F::GRACE)) {   // from a grace note: which one
                    prev.children.back().leaf = false;
                    prev.children.back().children.push_back(leaf("grace", std::to_string(graceIndexOf(*fc))));
                }
                sp.children.push_back(std::move(prev));
                n.children.push_back(std::move(sp));
            }
        }
        if (note.has(F::TIE_TO)) {
            const Object* tn = s.find(note.idOr(F::TIE_TO));
            const Object* tc = tn ? s.find(tn->parent()) : nullptr;
            if (tc) {
                Node sp = elem("Spanner");
                sp.attrs.emplace_back("type", "Tie");
                Node tie = elem("Tie");
                if (note.has(F::TIE_EID)) {
                    tie.children.push_back(leaf("eid", note.text(F::TIE_EID)));
                } else if (needLinks) {   // a tie made in Jubelle: an identity derived from its note's
                    tie.children.push_back(leaf("eid", partEid(eidOf(note), -77)));
                }
                addLink(tie, note, F::TIE_LINK);
                if (modern && !layout && note.text(F::TIE_LINK).empty() && note.has(F::LINKED_TO)) {
                    // a copy's tie: linked to its original's tie
                    if (const Object* mn = s.find(note.idOr(F::LINKED_TO)); mn && mn->has(F::TIE_TO)) {
                        const std::string te = mn->has(F::TIE_EID) ? mn->text(F::TIE_EID) : partEid(eidOf(*mn), -77);
                        if (!te.empty()) {
                            tie.children.push_back(leaf("linkedTo", xml::encode(te)));
                        }
                    }
                }
                if (!layout) {   // links by place: as read, or worked out for copies (PlaceLinks)
                    const std::string mark = note.text(F::TIE_MARK);
                    const std::string tieId = "t" + std::to_string(note.id().value());
                    if (byPlace && note.has(F::LINKED_TO) && mark.find("<location>") == std::string::npos) {
                        Node l = elem("linked");
                        l.leaf = true;
                        l.attrs.emplace_back("je-main", "t" + std::to_string(note.idOr(F::LINKED_TO).value()));
                        if (mark.rfind("linked:", 0) == 0) {
                            l.attrs.emplace_back("je-mark", mark.substr(7));
                        }
                        tie.children.push_back(std::move(l));
                    } else if (mark == "main" || (byPlace && mark.empty() && linkMains.count(note.id()))) {
                        tie.children.push_back(empty("linkedMain"));
                        if (annotate) {
                            tie.children.back().attrs.emplace_back("je-id", tieId);
                            if (mark.empty()) {
                                tie.children.back().attrs.emplace_back("je-new", "1");
                            }
                        }
                    } else if (mark.rfind("linked:", 0) == 0) {
                        Node l = elem("linked");
                        for (Node& c : parseFragment(mark.substr(7))) {
                            l.children.push_back(std::move(c));
                        }
                        finishNode(l);
                        tie.children.push_back(std::move(l));
                    }
                }
                addDetails(tie, note, F::DETAILS_2);
                finishNode(tie);
                sp.children.push_back(std::move(tie));
                Node next = elem("next");
                next.children.push_back(location(m, at, tc->fractionOr(F::TIME, Fraction())));
                crossTo(next.children.back(), *tc, chord);
                if (tc->has(F::GRACE)) {   // to a grace note: which one
                    next.children.back().leaf = false;
                    next.children.back().children.push_back(leaf("grace", std::to_string(graceIndexOf(*tc))));
                }
                sp.children.push_back(std::move(next));
                n.children.push_back(std::move(sp));
            }
        }
        const int64_t pitch = note.intOr(F::PITCH, 60);
        n.children.push_back(leaf("pitch", std::to_string(pitch)));
        // a note entered without a spelling gets the plain one for the key; on a transposing instrument its
        // written spelling follows the transposition (fifths = 7 × semitones − 12 × steps)
        const int64_t tpc = note.has(F::TPC) ? note.intOr(F::TPC, 14) : defaultTpc(pitch, keyAt(chord));
        n.children.push_back(leaf("tpc", std::to_string(tpc)));
        if (note.has(F::TPC2)) {
            n.children.push_back(leaf("tpc2", std::to_string(note.intOr(F::TPC2, 14))));
        } else if (!note.has(F::TPC)) {   // a note with no spelling at all: written spelling from the transposition
            const Object* player = playerOf(chord);
            const int64_t c = player ? player->intOr(F::TRANSPOSE_CHROMATIC, 0) : 0;
            const int64_t d = player ? player->intOr(F::TRANSPOSE_DIATONIC, diatonicOf(c)) : 0;
            if (c != 0 || d != 0) {   // written = concert moved back by the instrument's transposition
                n.children.push_back(leaf("tpc2", std::to_string(model::transposeTpc(tpc, -d, -c))));
            }
        }
        const bool afterFits = note.has(F::DETAILS_AFTER)
                               && (!note.has(F::DETAILS_KEY) || detailsKey(note) == note.intOr(F::DETAILS_KEY, 0));
        if (!afterFits) {   // the fragment, when it fits, holds them in place
            if (note.has(F::FRET)) {
                n.children.push_back(leaf("fret", std::to_string(note.intOr(F::FRET, 0))));
            }
            if (note.has(F::STRING)) {
                n.children.push_back(leaf("string", std::to_string(note.intOr(F::STRING, 0))));
            }
        }
        if (afterFits) {
            for (Node& c : parseFragment(note.text(F::DETAILS_AFTER))) {
                n.children.push_back(std::move(c));
            }
        }
        noteLines(n, note);
        return n;
    }

    Node spannerStart(const Object& sp, int m)
    {
        (void)m;
        const std::string type = spannerType(sp.text(F::SUBKIND));
        Node n = elem("Spanner");
        n.attrs.emplace_back("type", type);
        Node body = elem(type);
        addEid(body, sp);
        if (type == "Volta") {
            // the endings it applies to ("1", "1, 2"); a text such as "1." is its label, the number its ending
            const std::string t = sp.text(F::TEXT);
            const bool list = !t.empty() && t.find_first_not_of("0123456789, ") == std::string::npos;
            body.children.push_back(leaf("endings", list ? xml::encode(t) : std::to_string(sp.intOr(F::INT_VALUE, 1))));
            if (!list && !t.empty() && !sp.has(F::DETAILS)) {
                body.children.push_back(leaf("text", xml::encode(t)));
            }
        } else if (type == "HairPin") {
            body.children.push_back(leaf("subtype", std::to_string(sp.intOr(F::INT_VALUE, 0))));
        } else if (type == "Ottava") {
            body.children.push_back(leaf("subtype", xml::encode(sp.text(F::TEXT))));
        }
        addDetails(body, sp);
        finishNode(body);
        n.children.push_back(std::move(body));
        const Fraction t = sp.fractionOr(F::TIME, Fraction());
        Node next = elem("next");
        next.children.push_back(location(m, t, t + sp.fractionOr(F::DURATION, Fraction())));
        if (spanEnd.count(sp.id())) {   // inside chords: to the end chord's staff, voice and grace note
            const Object* from = s.find(spanStart.at(sp.id()));
            const Object* to = s.find(spanEnd.at(sp.id()));
            crossTo(next.children.back(), *to, *from);
            if (to->has(F::GRACE)) {
                next.children.back().leaf = false;
                next.children.back().children.push_back(leaf("grace", std::to_string(graceIndexOf(*to))));
            }
        }
        n.children.push_back(std::move(next));
        return n;
    }

    Node spannerEnd(const Object& sp, int m, const Fraction& at)
    {
        (void)m;
        Node n = elem("Spanner");
        n.attrs.emplace_back("type", spannerType(sp.text(F::SUBKIND)));
        Node prev = elem("prev");
        // relative to where the marker is: the end of its measure when the spanner ends at a barline
        prev.children.push_back(location(m, at, sp.fractionOr(F::TIME, Fraction())));
        if (spanEnd.count(sp.id())) {   // back to the start chord's staff, voice and grace note
            const Object* from = s.find(spanEnd.at(sp.id()));
            const Object* to = s.find(spanStart.at(sp.id()));
            crossTo(prev.children.back(), *to, *from);
            if (to->has(F::GRACE)) {
                prev.children.back().leaf = false;
                prev.children.back().children.push_back(leaf("grace", std::to_string(graceIndexOf(*to))));
            }
        }
        n.children.push_back(std::move(prev));
        return n;
    }

    Node itemNode(const Object& it)
    {
        const std::string sub = it.text(F::SUBKIND);
        if (sub == "detail") {
            std::vector<Node> nodes = parseFragment(it.text(F::DETAILS));
            return nodes.empty() ? empty("location") : nodes.front();
        }
        static const std::map<std::string, std::string> tags {
            { "clef", "Clef" }, { "keySig", "KeySig" }, { "timeSig", "TimeSig" }, { "tempo", "Tempo" }, { "dynamic", "Dynamic" },
            { "harmony", "Harmony" }, { "staffText", "StaffText" }, { "systemText", "SystemText" },
            { "rehearsalMark", "RehearsalMark" }, { "instrumentChange", "InstrumentChange" }, { "fermata", "Fermata" },
            { "breath", "Breath" }, { "jump", "Jump" }, { "marker", "Marker" }, { "expression", "Expression" },
            { "playTechnique", "PlayTechAnnotation" },
        };
        const std::string tag = it.has(F::TAG) ? it.text(F::TAG) : (tags.count(sub) ? tags.at(sub) : upperFirst(sub));
        Node n = elem(tag);
        if (onEveryStaff(it) && copyStaff > 1) {
            if (needLinks) {   // the copies on other staves: identities of their own
                n.children.push_back(leaf("eid", partEid(eidOf(it), -copyStaff)));
            }
        } else {
            addEid(n, it);
        }
        addDetails(n, it);
        if (tag == "Clef") {
            n.children.push_back(leaf("concertClefType", xml::encode(it.text(F::TEXT))));
        } else if (tag == "KeySig") {
            n.children.push_back(leaf(it.text(F::VALUE_TAG) == "accidental" ? "accidental" : "concertKey",
                                      std::to_string(it.intOr(F::INT_VALUE, 0))));
            if (!it.text(F::TEXT).empty()) {
                n.children.push_back(leaf("mode", xml::encode(it.text(F::TEXT))));
            }
        } else if (tag == "TimeSig") {
            n.children.push_back(leaf("sigN", std::to_string(it.intOr(F::TIMESIG_NUM, 4))));
            n.children.push_back(leaf("sigD", std::to_string(it.intOr(F::TIMESIG_DEN, 4))));
            // a staff's own (local) time signature: its stretch against the measure's, unless the file's copy is kept
            const bool kept = std::any_of(n.children.begin(), n.children.end(), [](const Node& c) { return c.name == "stretchN"; });
            const int m = measureAt(it.fractionOr(F::TIME, Fraction()));
            if (!kept && it.has(F::ANCHOR) && m >= 0 && size_t(m) < measures.size()) {
                const Object& mo = *measures[size_t(m)];
                const Fraction stretch = Fraction(it.intOr(F::TIMESIG_NUM, 4), it.intOr(F::TIMESIG_DEN, 4))
                                         / Fraction(mo.intOr(F::TIMESIG_NUM, 4), mo.intOr(F::TIMESIG_DEN, 4));
                if (!(stretch == Fraction(1, 1))) {
                    n.children.push_back(leaf("stretchN", std::to_string(stretch.numerator())));
                    n.children.push_back(leaf("stretchD", std::to_string(stretch.denominator())));
                }
            }
        } else if (tag == "Tempo") {
            const model::Value v = it.get(F::REAL_VALUE);
            // the shortest form that reads back as the same number
            const double value = std::holds_alternative<double>(v) ? std::get<double>(v) : 2.0;
            char buf[40];
            for (int precision = 1; precision <= 17; ++precision) {
                std::snprintf(buf, sizeof(buf), "%.*g", precision, value);
                if (std::strtod(buf, nullptr) == value) {
                    break;
                }
            }
            n.children.push_back(leaf("tempo", buf));
            n.children.push_back(leaf("text", it.text(F::TEXT)));
        } else if (tag == "Dynamic" || tag == "Fermata" || tag == "Breath") {
            n.children.push_back(leaf("subtype", xml::encode(it.text(F::TEXT))));
        } else if (tag == "Harmony") {
            std::string name = it.text(F::TEXT);
            int64_t root = it.intOr(F::INT_VALUE, -1);
            if (root < 0 && !it.has(F::TAG) && !name.empty() && name[0] >= 'A' && name[0] <= 'G') {
                // a chord symbol made as text ("Cmaj7", "Bb7"): its root and the rest
                static const int natural[7] = { 17, 19, 14, 16, 18, 13, 15 };   // A B C D E F G
                root = natural[name[0] - 'A'];
                size_t i = 1;
                for (; i < name.size() && (name[i] == 'b' || name[i] == '#'); ++i) {
                    root += name[i] == '#' ? 7 : -7;
                }
                name = name.substr(i);
            }
            const int64_t bass = it.intOr(F::BASS, -1);
            if (modern && (it.text(F::VALUE_TAG) == "harmonyInfo" || !detailsFit(it) || it.text(F::DETAILS).find("<harmonyInfo>") == std::string::npos)) {
                // from 4.60 the chord is in a <harmonyInfo> (MuseScore's order: description id, name, root, bass)
                Node info = elem("harmonyInfo");
                addDetails(info, it, F::DETAILS_2);
                if (!name.empty()) {
                    info.children.push_back(leaf("name", xml::encode(name)));
                }
                if (root >= 0) {
                    info.children.push_back(leaf("root", std::to_string(root)));
                }
                if (bass >= 0) {
                    info.children.push_back(leaf("bass", std::to_string(bass)));
                }
                // after the properties MuseScore writes before it
                auto at = n.children.begin();
                while (at != n.children.end() && (at->name == "eid" || at->name == "harmonyType" || at->name == "play"
                                                  || at->name == "rootCase" || at->name == "bassCase")) {
                    ++at;
                }
                n.children.insert(at, std::move(info));
            } else if (!modern || it.text(F::DETAILS).find("<harmonyInfo>") == std::string::npos) {
                if (root >= 0) {
                    n.children.push_back(leaf("root", std::to_string(root)));
                }
                if (!name.empty()) {
                    n.children.push_back(leaf("name", xml::encode(name)));
                }
                if (bass >= 0) {
                    n.children.push_back(leaf("base", std::to_string(bass)));
                }
            }
        } else if (tag == "InstrumentChange") {
            if (!it.has(F::DETAILS) || it.has(F::DETAILS_KEY) && detailsKey(it) != it.intOr(F::DETAILS_KEY, 0)) {
                Node in = elem("Instrument");
                if (it.has(F::INSTRUMENT)) {
                    in.attrs.emplace_back("id", xml::encode(it.text(F::INSTRUMENT)));
                }
                in.children.push_back(leaf("longName", xml::encode(it.text(F::NAME))));
                if (it.intOr(F::TRANSPOSE_CHROMATIC, 0) != 0) {
                    in.children.push_back(leaf("transposeDiatonic", std::to_string(diatonicOf(it.intOr(F::TRANSPOSE_CHROMATIC, 0)))));
                    in.children.push_back(leaf("transposeChromatic", std::to_string(it.intOr(F::TRANSPOSE_CHROMATIC, 0))));
                }
                n.children.push_back(std::move(in));
            }
            n.children.push_back(leaf("text", it.text(F::TEXT).empty() ? xml::encode(it.text(F::NAME)) : it.text(F::TEXT)));
        } else if (tag == "Jump" && !it.has(F::DETAILS)) {
            std::string to, until, cont;
            jumpTargets(it.text(F::TEXT), to, until, cont);
            n.children.push_back(leaf("jumpTo", to));
            n.children.push_back(leaf("playUntil", until));
            n.children.push_back(leaf("continueAt", cont));
            n.children.push_back(leaf("text", it.text(F::TEXT)));
        } else if (!it.text(F::TEXT).empty() || tag == "StaffText" || tag == "SystemText" || tag == "RehearsalMark") {
            n.children.push_back(leaf("text", it.text(F::TEXT)));
        }
        finishNode(n);
        return n;
    }

    const model::Snapshot& s;
    WriteReport& report;
    const Object* layout = nullptr;                 // writing this part layout's score
    std::map<Id, std::string>* generatedEids = nullptr;   // identities given in this save (shared with the parts)
    std::map<Id, std::string> ownEids;
    std::set<std::string> usedEids;
    std::set<std::string> allEids;   // every identity in the score (a link is written only to one that exists)
    uint64_t eidCounter = 0;
    bool needLinks = false;
    bool newLinks = false;   // copies made by Jubelle on a linked staff
    bool byPlace = false;    // their links written by place (before 4.60): placed after the score is generated
    bool annotate = false;   // positions noted for links by place (main score with parts, or a part)
    std::set<Id> linkMains;  // elements that copies link to
    std::map<Id, std::vector<const Object*> > startsIn, endsIn;   // lines kept inside chords, by chord
    std::set<Id> inEvent;
    std::map<Id, Id> spanStart, spanEnd;   // the chords a line inside chords starts and ends in
    bool anyFromFile = false;
    std::string format = "4.70";   // the MuseScore format written
    bool modern = true;            // 4.60 or later
    int copyStaff = 0;          // the staff an item for every staff is being written on
    std::vector<const Object*> layouts;
    const Object* score = nullptr;
    const Object* flow = nullptr;
    std::vector<const Object*> players, staves, spanners, measures, items;
    std::map<Id, int> staffNumber, mainStaffNumber;
    std::vector<Fraction> measureStart, measureLen;
    Fraction flowEnd;
    std::map<Id, Id> tieFrom;
    std::map<Id, std::vector<const Object*> > lineStarts, lineEnds;   // lines from note to note, by their notes
    std::map<Id, std::vector<std::vector<const Object*> > > itemBucket, spannerBucket;   // by staff, then measure
    std::map<Id, std::vector<std::vector<const Object*> > > voiceBucket;
    mutable std::vector<const Object*> keySigs;
    mutable bool keySigsFound = false;
};
}

ScoreFile generateScoreFile(const model::Snapshot& snapshot, WriteReport* report, const std::string& scoreName, bool plainFile)
{
    WriteReport local;
    Writer w(snapshot, report ? *report : local);
    w.plainFile = plainFile;
    return w.run(scoreName);
}

bool openScore(const std::string& path, model::Model& model, IdGenerator& ids, std::string& error)
{
    ScoreFile file;
    if (!load(path, file, error)) {
        return false;
    }
    restoreFromEngineStore(file);
    ReadReport report;
    return readScore(file, model, ids, report, error);
}

bool saveScore(const model::Snapshot& snapshot, const std::string& path, std::string& error, WriteReport* report)
{
    const bool plain = path.size() > 5 && path.compare(path.size() - 5, 5, ".mscx") == 0;
    ScoreFile file = generateScoreFile(snapshot, report, "score.mscx", plain);
    if (file.entries.empty()) {
        error = "nothing to save (no score)";
        return false;
    }
    updateEngineStore(file);
    if (!save(path, file)) {
        error = "couldn't write the file";
        return false;
    }
    return true;
}
}
