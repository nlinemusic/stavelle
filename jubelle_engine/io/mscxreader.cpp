/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::io). Not part of MuseScore.
 */
#include "mscxreader.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <sstream>

#include "model/fragment.h"
#include "model/bulkbuilder.h"
#include "details.h"

namespace F = je::model::fields;
// a field only while reading: the element an object was read from
constexpr je::model::Field TEMP_NODE = je::model::fields::ENGINE_BASE + 999;
using je::model::Kind;
using je::model::Text;
using je::model::Value;
using je::io::xml::Node;
using Order = je::model::BulkBuilder::Order;

namespace je::io {
namespace {
using Fields = std::vector<std::pair<model::Field, Value> >;

bool durationOf(const std::string& type, Fraction& out)
{
    static const std::map<std::string, Fraction> types {
        { "long", Fraction(4, 1) }, { "breve", Fraction(2, 1) }, { "whole", Fraction(1, 1) }, { "half", Fraction(1, 2) },
        { "quarter", Fraction(1, 4) }, { "eighth", Fraction(1, 8) }, { "16th", Fraction(1, 16) }, { "32nd", Fraction(1, 32) },
        { "64th", Fraction(1, 64) }, { "128th", Fraction(1, 128) }, { "256th", Fraction(1, 256) }, { "512th", Fraction(1, 512) },
        { "1024th", Fraction(1, 1024) },
    };
    auto it = types.find(type);
    if (it == types.end()) {
        return false;
    }
    out = it->second;
    return true;
}

bool parseFraction(const std::string& s, Fraction& out)
{
    const size_t slash = s.find('/');
    try {
        if (slash == std::string::npos) {
            out = Fraction(std::stoll(s), 1);
        } else {
            const long long d = std::stoll(s.substr(slash + 1));
            if (d == 0) {
                return false;
            }
            out = Fraction(std::stoll(s.substr(0, slash)), d);
        }
    } catch (...) {
        return false;
    }
    return true;
}

int64_t toInt(const std::string& s, int64_t fallback = 0)
{
    try {
        return s.empty() ? fallback : std::stoll(s);
    } catch (...) {
        return fallback;
    }
}

double toReal(const std::string& s, double fallback = 0)
{
    try {
        return s.empty() ? fallback : std::stod(s);
    } catch (...) {
        return fallback;
    }
}

Fraction dotted(const Fraction& base, int64_t dots)
{
    Fraction total = base, add = base;
    for (int64_t i = 0; i < dots && i < 4; ++i) {
        add = add * Fraction(1, 2);
        total += add;
    }
    return total;
}

std::string family(const std::string& instrumentId)
{
    const std::string head = instrumentId.substr(0, instrumentId.find('.'));
    if (head == "wind") {
        return "woodwinds";
    }
    if (head == "brass" || head == "strings" || head == "keyboard" || head == "voice" || head == "pitched-percussion"
        || head == "drum" || head == "plucked" || head == "electronic") {
        return head == "drum" ? "percussion" : head;
    }
    return head;
}

const std::set<std::string>& graceTags()
{
    static const std::set<std::string> t { "acciaccatura", "appoggiatura", "grace4", "grace16", "grace32",
                                           "grace8after", "grace16after", "grace32after" };
    return t;
}

//! Flow items read into the model: element name → subkind
const std::map<std::string, std::string>& flowItemKinds()
{
    static const std::map<std::string, std::string> k {
        { "Clef", "clef" }, { "KeySig", "keySig" }, { "TimeSig", "timeSig" }, { "Tempo", "tempo" }, { "Dynamic", "dynamic" },
        { "Harmony", "harmony" }, { "StaffText", "staffText" }, { "SystemText", "systemText" }, { "RehearsalMark", "rehearsalMark" },
        { "InstrumentChange", "instrumentChange" }, { "Fermata", "fermata" }, { "Breath", "breath" }, { "Jump", "jump" },
        { "Marker", "marker" }, { "Expression", "expression" }, { "PlayTechAnnotation", "playTechnique" },
    };
    return k;
}

//! System-wide items (no staff anchor)
bool systemWide(const std::string& name)
{
    return name == "Tempo" || name == "SystemText" || name == "RehearsalMark" || name == "Jump" || name == "Marker";
}

std::string lowerFirst(std::string s)
{
    if (!s.empty() && s[0] >= 'A' && s[0] <= 'Z') {
        s[0] = char(s[0] - 'A' + 'a');
    }
    return s;
}

class Reader
{
public:
    Reader(const ScoreFile& f, model::Model& m, IdGenerator& ids, ReadReport& r)
        : file(f), model(m), bb(ids), report(r) {}

    bool run(std::string& error)
    {
        const Node* score = file.score();
        if (!score) {
            error = "no <Score> element";
            return false;
        }
        if (!file.isVersion4()) {
            error = "MuseScore format " + file.formatVersion() + " (the native reader handles 4.x)";
            return false;
        }
        scoreId = add(Kind::Score, Id(), scoreFields(*score));
        flowId = add(Kind::Flow, scoreId, { { F::NAME, Text(std::string("Flow 1")) } });

        // players and staves first, then the measure grid from the first staff, then the music
        for (const Node& c : score->children) {
            if (c.name == "Part") {
                readPart(c);
            }
        }
        linkStaves();
        std::vector<const Node*> musicStaves;
        for (const Node& c : score->children) {
            if (c.name == "Staff") {
                musicStaves.push_back(&c);
            }
        }
        if (musicStaves.empty()) {
            error = "no music";
            return false;
        }
        readGrid(*musicStaves.front());
        byPlaceFormat = file.formatVersion() < std::string("4.60");
        for (const Node* s : musicStaves) {
            readStaffMusic(*s);
        }

        resolveTies();
        pairNoteLines();
        if (byPlaceFormat) {
            resolveLinksByPlace(*score);   // after every object is made (the note lines last)
        }
        readLayouts();
        // each object's details carry the fingerprint of its finished fields (some are set after it was added:
        // tie targets, completed durations), so an edit to its music later can be told apart
        report.objects = bb.finish(model, [](model::Object o) {
            if (o.kind() != Kind::Score && (o.has(F::DETAILS) || o.has(F::DETAILS_2) || o.has(F::DETAILS_AFTER) || o.has(F::DETAILS_KEY))) {
                return o.with(F::DETAILS_KEY, detailsKey(o));
            }
            return o;
        });
        return true;
    }

private:
    //! A linked staff (a guitar's tablature under its standard staff) shows the music of another staff: by identity
    //! from 4.60, by the staff's number before
    void linkStaves()
    {
        const bool byIdentity = !(file.formatVersion() < std::string("4.60"));
        for (const auto& [staff, to] : staffLinks) {
            Id main;
            if (byIdentity) {
                auto it = staffByEid.find(to);
                main = it == staffByEid.end() ? Id() : it->second;
            } else {
                auto it = staffById.find(toInt(to));
                main = it == staffById.end() ? Id() : it->second;
            }
            // MuseScore links a staff only to one read before it (a link to a later staff, as some files have both
            // ways, is the other half of the same pair)
            if (main.isValid() && main != staff && staffOrder[main] < staffOrder[staff]) {
                bb.setField(staff, F::LINKED_STAFF, main);
            }
        }
    }

    //! Adds an object; an element with links by place (before 4.60) is remembered with its object, for
    //! resolveLinksByPlace
    Id add(Kind kind, Id parent, Fields fields, Order order = Order::Append)
    {
        const Node* node = nullptr;
        for (auto it = fields.begin(); it != fields.end(); ++it) {
            if (it->first == TEMP_NODE) {
                node = reinterpret_cast<const Node*>(std::get<int64_t>(it->second));
                fields.erase(it);
                break;
            }
        }
        const Id id = bb.add(kind, parent, std::move(fields), order);
        if (node) {
            nodeObject[node] = id;
        }
        return id;
    }

    //! Links by place (formats before 4.60): MuseScore's reader (read410 ReadContext::addLink and getLink, with
    //! its LinksIndexer) finds a <linked> element's original among the <linkedMain/> elements of its staff's main
    //! staff, at the copy's position. Done here the same way, so each copy that is an object of the model knows its
    //! original (LINKED_TO), and a save can work out every link again however the music changed
    void resolveLinksByPlace(const Node& score)
    {
        struct Loc {
            int64_t track = 0;
            Fraction tick;
            bool operator==(const Loc& o) const { return track == o.track && tick == o.tick; }
        };
        struct Entry {
            Loc loc;
            const Node* node;
        };
        std::map<int64_t, std::vector<Entry> > lists;   // by staff index
        Loc last;
        bool hasLast = false;
        int lastIndex = -1;
        auto assign = [&](const Loc& l) {
            if (hasLast && last == l) {
                return ++lastIndex;
            }
            hasLast = true;
            last = l;
            lastIndex = 0;
            return 0;
        };
        std::map<int64_t, int64_t> mainIndex;   // staff index → the first staff of its linked group
        for (const auto& [staff, to] : staffLinks) {
            auto it = staffById.find(toInt(to));
            if (it != staffById.end() && staffOrder[it->second] < staffOrder[staff]) {
                mainIndex[staffOrder[staff] - 1] = staffOrder[it->second] - 1;
            }
        }
        for (auto& [s, m] : mainIndex) {
            for (int guard = 0; guard < 8 && mainIndex.count(m); ++guard) {
                m = mainIndex.at(m);
            }
        }
        std::function<void(const Node&, int64_t, Loc)> walk = [&](const Node& n, int64_t staff, Loc loc) {
            auto pl = placeOf.find(&n);
            if (pl != placeOf.end()) {
                loc = { pl->second.first, pl->second.second };
            }
            for (const Node& c : n.children) {
                if (c.type != Node::Type::Element) {
                    continue;
                }
                if (c.name == "linkedMain") {
                    assign(loc);
                    lists[staff].push_back({ loc, &n });
                } else if (c.name == "linked") {
                    auto mi = mainIndex.find(staff);
                    Loc ml { (mi == mainIndex.end() ? staff : mi->second) * 4 + loc.track % 4, loc.tick };
                    if (const Node* rel = c.child("location")) {
                        ml.track += 4 * toInt(rel->childText("staves")) + toInt(rel->childText("voices"));
                        Fraction df;
                        if (!rel->childText("fractions").empty() && parseFraction(rel->childText("fractions"), df)) {
                            ml.tick += df;
                        }
                    }
                    const int local = assign(ml) + int(toInt(c.childText("indexDiff")));
                    std::vector<Entry>& list = lists[ml.track / 4];
                    if (!list.empty() && list.back().loc == ml) {
                        list.push_back(list.back());   // as getLink does
                    }
                    for (size_t i = 0; i < list.size(); ++i) {
                        if (list[i].loc == ml) {
                            const size_t at = i + size_t(std::max(local, 0));
                            if (local >= 0 && at < list.size() && list[at].loc == ml) {
                                auto copy = nodeObject.find(&n);
                                auto orig = nodeObject.find(list[at].node);
                                if (copy != nodeObject.end() && orig != nodeObject.end() && list[at].node->name == n.name) {
                                    bb.setField(copy->second, F::LINKED_TO, orig->second);
                                }
                            }
                            break;
                        }
                    }
                } else if (!c.leaf) {
                    walk(c, staff, loc);
                }
            }
        };
        for (const Node& c : score.children) {
            if (c.name == "Staff" && c.hasAttr("id")) {
                walk(c, toInt(c.attr("id")) - 1, Loc());
            }
        }
    }

    // ---- score-wide data: style, properties, settings, other archive entries -----------------------
    Fields scoreFields(const Node& score)
    {
        Fields f { { F::NAME, Text(metaTag(file, "workTitle")) } };
        // the carried details are in this format's dialect (MuseScore reads some properties by version), so a
        // score is written back in the format it was read from; 4.70 is Jubelle's own
        if (file.formatVersion() != "4.70") {
            f.push_back({ F::SOURCE_FORMAT, Text(file.formatVersion()) });
        }
        // MuseScore reads score_style.mss first, then a <Style> in the score itself over it (MscLoader, read460)
        const Node* style = styleNode(file);
        Node merged;
        const Node* inline_ = file.hasStyleEntry && file.score() ? file.score()->child("Style") : nullptr;
        if (style && inline_ && inline_ != style) {
            // the k-th element of a name replaces the k-th of that name (old files repeat some names)
            merged = *style;
            std::map<std::string, int> seen;
            for (const Node& c : inline_->children) {
                if (c.type != Node::Type::Element) {
                    continue;
                }
                int k = seen[c.name]++;
                auto same = std::find_if(merged.children.begin(), merged.children.end(), [&](const Node& m) {
                    return m.type == Node::Type::Element && m.name == c.name && k-- == 0;
                });
                if (same != merged.children.end()) {
                    *same = c;
                } else {
                    merged.children.push_back(c);
                }
            }
            style = &merged;
        } else if (!style) {
            style = inline_;
        }
        documentFields(f, style, score, file.main.root);
        if (file.hasStyleEntry && file.style.root.attr("version") != file.formatVersion() && !file.style.root.attr("version").empty()) {
            f.push_back({ F::STYLE_FORMAT, Text(file.style.root.attr("version")) });
        }
        // other score data in the archive (mixer, view and automation settings); thumbnails are regenerated
        std::string extras;
        for (const zip::Entry& e : file.entries) {
            if (e.name == file.mainEntry || e.name == "score_style.mss" || e.name == "META-INF/container.xml"
                || e.name.rfind("Excerpts/", 0) == 0 || e.name.rfind("Thumbnails/", 0) == 0) {
                continue;
            }
            extras += e.name + '\0' + std::to_string(e.data.size()) + '\0' + e.data;
        }
        if (!extras.empty()) {
            f.push_back({ F::ARCHIVE_EXTRAS, Text(extras) });
        }
        return f;
    }

    //! A score document's own settings (the main score's or a part's): style, properties, other parts of the head
    static void documentFields(Fields& f, const Node* style, const Node& score, const Node& root)
    {
        if (style) {
            std::vector<std::pair<std::string, std::string> > values;
            std::vector<const Node*> structured;
            for (const Node& c : style->children) {
                if (c.type != Node::Type::Element) {
                    continue;
                }
                if (c.leaf && c.attrs.empty()) {
                    values.emplace_back(c.name, c.selfClosing ? std::string("\x01") : c.raw);
                } else {
                    structured.push_back(&c);
                }
            }
            if (!values.empty()) {
                f.push_back({ F::STYLE, Text(packPairs(values)) });
            }
            if (!structured.empty()) {
                f.push_back({ F::DETAILS_2, Text(fragment(structured)) });
            }
        }
        std::vector<std::pair<std::string, std::string> > meta;
        std::vector<const Node*> pre, post;
        bool seenPart = false, seenMusic = false;
        for (const Node& c : score.children) {
            if (c.type != Node::Type::Element) {
                continue;
            }
            if (c.name == "metaTag") {
                if (c.attr("name") != "jubelleEngine") {   // the engine store is written afresh on every save
                    meta.emplace_back(c.attr("name"), c.raw);
                }
            } else if (c.name == "Part") {
                seenPart = true;
            } else if (c.name == "Staff") {
                seenMusic = true;
            } else if (c.name == "Division" || c.name == "Style" || c.name == "eid" || c.name == "Score") {
                continue;   // (a <Score> inside the score is a part, read as a layout)
            } else if (!seenPart && !seenMusic) {
                pre.push_back(&c);
            } else {
                post.push_back(&c);
            }
        }
        if (!meta.empty()) {
            f.push_back({ F::META, Text(packPairs(meta)) });
        }
        if (!pre.empty()) {
            f.push_back({ F::SCORE_PRE, Text(fragment(pre)) });
        }
        if (!post.empty()) {
            f.push_back({ F::SCORE_POST, Text(fragment(post)) });
        }
        std::vector<const Node*> head;
        for (const Node& c : root.children) {
            if (c.type == Node::Type::Element && c.name != "Score") {
                head.push_back(&c);
            }
        }
        // the head Jubelle writes for a score without one is left out, so that writing and reading again is stable
        if (!head.empty() && fragment(head) != defaultScoreHead()) {
            f.push_back({ F::SCORE_HEAD, Text(fragment(head)) });
        }
    }

    // ---- players and staves --------------------------------------------------------------------
    void readPart(const Node& part)
    {
        const Node* instr = part.child("Instrument");
        std::string name = instr ? instr->childText("longName") : std::string();
        if (name.empty()) {
            name = part.childText("trackName");
        }
        Fields f { { F::NAME, Text(name) } };
        if (instr) {
            const std::string iid = instr->childText("instrumentId");
            f.push_back({ F::INSTRUMENT, Text(instr->attr("id")) });
            f.push_back({ F::FAMILY, Text(family(iid)) });
            if (!instr->childText("shortName").empty()) {
                f.push_back({ F::SHORT_NAME, Text(instr->childText("shortName")) });
            }
            if (const int64_t c = toInt(instr->childText("transposeChromatic"))) {
                f.push_back({ F::TRANSPOSE_CHROMATIC, c });
            }
            if (const int64_t d = toInt(instr->childText("transposeDiatonic"))) {
                f.push_back({ F::TRANSPOSE_DIATONIC, d });
            }
            if (!iid.empty()) {
                f.push_back({ F::MUSESCORE_INSTRUMENT, Text(iid) });
            }
            // a fretted instrument's strings (also kept in the instrument's details, in place)
            if (const Node* sd = instr->child("StringData")) {
                std::string tuning;
                for (const Node& c : sd->children) {
                    if (c.name == "string") {
                        tuning += (tuning.empty() ? "" : " ") + c.text() + (toInt(c.attr("open")) ? "*" : "");
                    }
                }
                if (!tuning.empty()) {
                    f.push_back({ F::STRING_TUNING, Text(tuning) });
                    f.push_back({ F::FRETS, toInt(sd->childText("frets")) });
                }
            }
        }
        {
            const std::string partDetails = unmodelled(part, { "Staff", "Instrument" });
            const std::string instrDetails = instr ? unmodelled(*instr, { "longName", "shortName", "instrumentId", "transposeChromatic",
                                                                          "transposeDiatonic" }) : std::string();
            if (!partDetails.empty() || !instrDetails.empty()) {
                f.push_back({ F::DETAILS_KEY, detailsKey(f) });
                if (!partDetails.empty()) {
                    f.push_back({ F::DETAILS, Text(partDetails) });
                }
                if (!instrDetails.empty()) {
                    f.push_back({ F::DETAILS_2, Text(instrDetails) });
                }
            }
            f.push_back({ F::ORIGIN_EID, Text("part:" + part.attr("id")) });   // "part:" for a part without an id
        }
        const Id player = add(Kind::Player, scoreId, f);
        players.push_back({ player, name });
        report.modelled["Part"]++;
        for (const Node& c : part.children) {
            if (c.name == "Staff") {
                Fields sf;
                if (const Node* st = c.child("StaffType")) {
                    sf.push_back({ F::SUBKIND, Text(st->attr("group")) });
                }
                eid(c, sf);
                withDetails(sf, c, {});
                const Id staff = add(Kind::Staff, player, sf);
                // staves are numbered through the score in order; older 4.x files also write the number
                ++staffCount;
                staffById[c.hasAttr("id") ? toInt(c.attr("id")) : staffCount] = staff;
                if (!c.childText("eid").empty()) {
                    staffByEid[c.childText("eid")] = staff;
                }
                staffOrder[staff] = staffCount;
                playerOfStaff[staff] = player;
                if (!c.childText("linkedTo").empty()) {
                    staffLinks.push_back({ staff, c.childText("linkedTo") });
                }
                report.staves++;
            }
        }
    }

    // ---- the measure grid ------------------------------------------------------------------------
    void readGrid(const Node& firstStaff)
    {
        Fraction t;
        int64_t num = 4, den = 4;
        int64_t nextNum = 0, nextDen = 0;   // a time signature at the end of a measure: it starts with the next one
        for (const Node& m : firstStaff.children) {
            if (m.name == "VBox" || m.name == "HBox" || m.name == "TBox" || m.name == "FBox") {
                placeOf[&m] = { 0, t };
                // a frame (title, text, fret diagrams) between measures, carried whole until S7/S8 model frames
                add(Kind::FlowItem, flowId, { { F::SUBKIND, Text(std::string("frame")) }, { F::TAG, Text(m.name) }, { F::TIME, t },
                                                 { F::SEQ, int64_t(++seq) }, { F::DETAILS, Text(wholeFragment({ &m })) } }, Order::ByTime);
                report.modelled["(frame) " + m.name]++;
                continue;
            }
            if (m.name != "Measure" || m.child("multiMeasureRest")) {
                continue;   // a <multiMeasureRest> measure is MuseScore's layout-only combined rest, not music
            }
            if (nextNum > 0) {
                num = nextNum;
                den = nextDen;
                nextNum = 0;
            }
            // a time signature at the start of the measure (any voice) sets the nominal length; one after the
            // measure's music belongs to the next measure
            for (const Node& v : m.children) {
                if (v.name != "voice") {
                    continue;
                }
                bool afterMusic = false;
                for (const Node& e : v.children) {
                    if (e.name == "TimeSig") {
                        int64_t n = toInt(e.childText("sigN"), num), d = toInt(e.childText("sigD"), den);
                        // a local time signature (stretched) on the first staff: the measure's own length is the
                        // local one over the stretch (12/8 stretched 3/2 in a 4/4 score: 4/4)
                        const int64_t sn = toInt(e.childText("stretchN"), 1), sd = toInt(e.childText("stretchD"), 1);
                        if (sn > 0 && sd > 0 && sn != sd) {
                            Fraction g = Fraction(n, d) / Fraction(sn, sd);
                            n = g.numerator();
                            d = g.denominator();
                            while (d < 4) {
                                n *= 2;
                                d *= 2;
                            }
                        }
                        if (afterMusic) {
                            nextNum = n;
                            nextDen = d;
                        } else {
                            num = n;
                            den = d;
                        }
                    }
                    if (e.name == "Chord" || e.name == "Rest" || e.name == "MeasureRepeat") {
                        afterMusic = true;
                    }
                }
            }
            Fraction len(num, den);
            if (m.hasAttr("len")) {
                parseFraction(m.attr("len"), len);
            }
            Fields f { { F::TIME, t }, { F::DURATION, len }, { F::TIMESIG_NUM, num }, { F::TIMESIG_DEN, den } };
            for (const Node& c : m.children) {
                if (c.name == "startRepeat") {
                    f.push_back({ F::REPEAT_START, true });
                } else if (c.name == "endRepeat") {
                    f.push_back({ F::REPEAT_END, std::max<int64_t>(2, toInt(c.text(), 2)) });
                } else if (c.name == "irregular") {
                    f.push_back({ F::IRREGULAR, true });
                } else if (c.name == "LayoutBreak") {
                    f.push_back({ F::BREAK, Text(c.childText("subtype")) });
                } else if (c.name == "eid") {
                    f.push_back({ F::ORIGIN_EID, Text(c.text()) });
                }
            }
            measureIds.push_back(add(Kind::Measure, flowId, f, Order::ByTime));
            measureStart.push_back(t);
            measureLen.push_back(len);
            t += len;
            report.measures++;
        }
        flowEnd = t;
    }

    //! The absolute time a relative MuseScore location points to, from a position in measure `mi`
    Fraction locate(int mi, const Fraction& at, const Node* location)
    {
        if (!location) {
            return at;
        }
        const int64_t dm = toInt(location->childText("measures"));
        Fraction df;
        parseFraction(location->childText("fractions").empty() ? "0" : location->childText("fractions"), df);
        const Fraction posInMeasure = at - measureStart[size_t(mi)];
        const int64_t target = int64_t(mi) + dm;
        if (target < 0) {
            return Fraction();
        }
        if (target >= int64_t(measureStart.size())) {
            return flowEnd;
        }
        return measureStart[size_t(target)] + posInMeasure + df;
    }

    Id voiceFor(Id staff, int index)
    {
        const auto key = std::make_pair(staff.value(), index);
        auto it = voices.find(key);
        if (it != voices.end()) {
            return it->second;
        }
        const Id v = add(Kind::Voice, staff, { { F::FLOW, flowId }, { F::NUMBER, int64_t(index) } });
        voices.emplace(key, v);
        return v;
    }

    // ---- music -------------------------------------------------------------------------------------
    struct OpenTuplet {
        Id id;
        Fraction factor;   // actual/written, including enclosing tuplets
        Fraction end;      // where the tuplet's span ends
    };

    void readStaffMusic(const Node& staffNode)
    {
        auto st = staffById.find(toInt(staffNode.attr("id")));
        if (st == staffById.end()) {
            report.warnings.push_back("music for staff " + staffNode.attr("id") + ", which no part has");
            return;
        }
        const Id staff = st->second;
        int mi = -1;
        for (const Node& m : staffNode.children) {
            if (m.name != "Measure") {
                if (m.name != "eid") {
                    report.keptOnly[m.name]++;
                }
                continue;
            }
            if (m.child("multiMeasureRest")) {
                // MuseScore's combined rest for the measures that follow: layout, not music, but other data (system
                // locks) may refer to it, so it is carried whole and written back before the measure it starts at
                const size_t next = size_t(mi + 1);
                add(Kind::FlowItem, flowId, { { F::SUBKIND, Text(std::string("mmrMeasure")) }, { F::ANCHOR, staff },
                                                 { F::TIME, next < measureStart.size() ? measureStart[next] : flowEnd },
                                                 { F::SEQ, int64_t(++seq) }, { F::DETAILS, Text(wholeFragment({ &m })) } },
                       Order::ByTime);
                report.keptOnly["Measure (multi-measure rest, layout only)"]++;
                continue;
            }
            if (++mi >= int(measureStart.size())) {
                report.warnings.push_back("a staff has more measures than the first staff");
                return;
            }
            placeOf[&m] = { (staffOrder[staff] - 1) * 4, measureStart[size_t(mi)] };
            int vi = 0;
            voice0End = Fraction(-1, 1);
            bool afterVoices = false;
            std::vector<const Node*> measureDetails;
            for (const Node& v : m.children) {
                if (v.name == "voice") {
                    readVoice(v, staff, mi, vi++);
                    afterVoices = true;
                } else if (flowItemKinds().count(v.name)) {
                    // a few items sit directly in the measure (no voice index); AT_END: written after the voices
                    const Id item = addFlowItem(v, staff, measureStart[size_t(mi)], -1);
                    if (afterVoices) {
                        bb.setField(item, F::AT_END, true);
                    }
                } else if (v.type == Node::Type::Element && v.name != "eid" && v.name != "startRepeat" && v.name != "endRepeat"
                           && v.name != "irregular" && v.name != "LayoutBreak") {
                    measureDetails.push_back(&v);
                    report.keptOnly[v.name]++;
                }
            }
            if (!measureDetails.empty()) {
                add(Kind::FlowItem, flowId, { { F::SUBKIND, Text(std::string("measureDetail")) }, { F::ANCHOR, staff },
                                                 { F::TIME, measureStart[size_t(mi)] }, { F::SEQ, int64_t(++seq) },
                                                 { F::DETAILS, Text(wholeFragment(measureDetails)) } }, Order::ByTime);
            }
            completeVoice1(staff, mi);
        }
    }

    void readVoice(const Node& voice, Id staff, int mi, int index)
    {
        gracesInFile = 0;
        Id voiceId;   // created on the first event, so empty voices don't appear
        auto vid = [&]() {
            if (!voiceId.isValid()) {
                voiceId = voiceFor(staff, index);
            }
            return voiceId;
        };
        Fraction t = measureStart[size_t(mi)];
        // a local time signature in this staff stretches its music (actual = written / stretch)
        for (const Node& e : voice.children) {
            if (e.name == "TimeSig" && e.child("stretchN")) {
                const int64_t n = toInt(e.childText("stretchN"), 1), d = toInt(e.childText("stretchD"), 1);
                staffStretch[staff.value()] = (n > 0 && d > 0) ? Fraction(n, d) : Fraction(1, 1);
            } else if (e.name == "TimeSig") {
                staffStretch[staff.value()] = Fraction(1, 1);
            }
            if (e.name == "Chord" || e.name == "Rest") {
                break;
            }
        }
        const Fraction stretch = stretchOf(staff);
        Fraction lastMainTime = t;
        Id lastMainChord;
        std::vector<OpenTuplet> tuplets;
        std::vector<const Node*> pendingAfter;
        std::vector<Id> itemsSinceEvent;
        int closedFull = 0;
        Fraction lastEventEnd = measureStart[size_t(mi)];
        // the tuplets' ratio, and the local time signature's stretch
        auto factor = [&tuplets, &stretch]() { return (tuplets.empty() ? Fraction(1, 1) : tuplets.back().factor) / stretch; };

        const int64_t track = (staffOrder[staff] - 1) * 4 + index;
        placeOf[&voice] = { track, t };
        for (const Node& e : voice.children) {
            const std::string& n = e.name;
            if (byPlaceFormat) {
                placeOf[&e] = { track, t };
            }
            if (n == "location") {
                Fraction df;
                if (parseFraction(e.childText("fractions").empty() ? "0" : e.childText("fractions"), df)) {
                    t += df;   // a location is in actual time, not stretched (checked against MuseScore)
                }
                if (const int64_t dm = toInt(e.childText("measures"))) {
                    report.warnings.push_back("a voice location that moves across measures");
                    (void)dm;
                }
            } else if (n == "Tuplet") {
                const int64_t actual = toInt(e.childText("actualNotes"));
                const int64_t normal = toInt(e.childText("normalNotes"));
                Fraction base;
                if (actual <= 0 || normal <= 0 || !durationOf(e.childText("baseNote"), base)) {
                    report.warnings.push_back("a tuplet without a valid ratio or note value");
                    tuplets.push_back({ Id(), factor(), t });
                    continue;
                }
                const Fraction outer = factor();
                Fields f { { F::TIME, t }, { F::DURATION, base * Fraction(normal, 1) * outer }, { F::BASE_DURATION, base },
                           { F::RATIO_NUM, actual }, { F::RATIO_DEN, normal } };
                if (!(stretch == Fraction(1, 1))) {
                    f.push_back({ F::STRETCH, stretch });
                }
                if (!tuplets.empty() && tuplets.back().id.isValid()) {
                    f.push_back({ F::TUPLET, tuplets.back().id });
                }
                withDetails(f, e, { "normalNotes", "actualNotes", "baseNote" });
                eid(e, f);
                tuplets.push_back({ add(Kind::Tuplet, vid(), f, Order::ByTime), outer * stretch * Fraction(normal, actual),
                                    t + base * Fraction(normal, 1) * outer });
                report.tuplets++;
                report.modelled["Tuplet"]++;
            } else if (n == "endTuplet") {
                if (tuplets.empty() && closedFull > 0) {
                    // the file meant more notes in a tuplet that was already full
                    --closedFull;
                    report.warnings.push_back("a tuplet whose notes last longer than the tuplet (an invalid file; the notes "
                                              "past its end are read after it)");
                } else if (!tuplets.empty()) {
                    if (tuplets.back().id.isValid() && tuplets.back().end < t) {
                        report.warnings.push_back("a tuplet whose notes last longer than the tuplet (an invalid file; kept as written)");
                    }
                    tuplets.pop_back();
                }
            } else if (n == "Chord" || n == "Rest" || n == "MeasureRepeat") {
                // a tuplet holds nothing past its span: one that is full is closed even without <endTuplet/>
                // (files written that way read like this in MuseScore too)
                while (!tuplets.empty() && tuplets.back().id.isValid() && !isGrace(e) && !(t < tuplets.back().end)) {
                    tuplets.pop_back();
                    ++closedFull;
                }
                // MuseScore numbers a chord's grace notes in file order (its locations use the number)
                if (isGrace(e)) {
                    graceFileIndex[&e] = gracesInFile++;
                } else {
                    gracesInFile = 0;
                }
                if (isAfterGrace(e)) {
                    pendingAfter.push_back(&e);   // MuseScore writes it before its main chord; it follows it in the voice
                    continue;
                }
                readEvent(e, vid(), staff, mi, t, tuplets.empty() ? Id() : tuplets.back().id, factor(), lastMainTime, lastMainChord);
                itemsSinceEvent.clear();
                if (lastEventEnd < t) {
                    lastEventEnd = t;
                }
                if (isGrace(e)) {
                    continue;   // after-graces wait for their main chord (grace notes before it may come first)
                }
                // MuseScore orders a chord's after-graces the other way round from the file
                std::reverse(pendingAfter.begin(), pendingAfter.end());
                for (const Node* g : pendingAfter) {
                    Fraction at = lastMainTime;
                    readEvent(*g, vid(), staff, mi, at, Id(), factor(), lastMainTime, lastMainChord);
                }
                pendingAfter.clear();
            } else if (n == "Spanner") {
                addSpanner(e, staff, mi, t, index);
            } else if (flowItemKinds().count(n)) {
                itemsSinceEvent.push_back(addFlowItem(e, staff, t, index));
            } else if (n != "eid" && e.type == Node::Type::Element) {
                // anything else in the voice (beams, bar lines, images…), carried at its place in the stream
                itemsSinceEvent.push_back(add(Kind::FlowItem, flowId, { { F::SUBKIND, Text(std::string("detail")) }, { F::ANCHOR, staff },
                                                                          { F::VOICE_INDEX, int64_t(index) }, { F::TIME, t },
                                                                          { F::SEQ, int64_t(++seq) }, { F::TAG, Text(n) },
                                                                          { F::DETAILS, Text(wholeFragment({ &e })) } }, Order::ByTime));
                report.keptOnly[n]++;
            }
        }
        for (Id item : itemsSinceEvent) {
            bb.setField(item, F::AT_END, true);   // after the last chord or rest of this voice in this measure
        }
        if (index == 0) {
            voice0End = lastEventEnd;   // where the last chord or rest ends (a <location> may rewind the cursor after it)
        }
    }

    //! As MuseScore's reader does (Measure::checkMeasure): voice 1 of every measure ends at the measure's end; a short
    //! one is completed with rests, split on the beat (a whole empty measure gets one measure rest). Other voices'
    //! holes become MuseScore's invisible gap rests, which aren't music, so nothing is added for them.
    //! Rests for a hole in voice 1 from `at` to `to` (MuseScore's split: the largest note value that fits and starts
    //! on a multiple of itself, from the measure start)
    //! On a staff with a local time signature the rests are split in its own (written) values and carry its stretch.
    void fillVoice1(Id staff, Id voice, Fraction at, const Fraction& to, const Fraction& measureStartTime)
    {
        const Fraction st = stretchOf(staff);
        Fraction written = (at - measureStartTime) * st;
        const Fraction writtenEnd = (to - measureStartTime) * st;
        for (int guard = 0; written < writtenEnd && guard < 64; ++guard) {
            const Fraction rem = writtenEnd - written;
            Fraction d(1, 1);
            while (Fraction(1, 1024) < d && (rem < d || (written / d).denominator() != 1)) {
                d = d * Fraction(1, 2);
            }
            if (rem < d) {
                break;
            }
            Fields f { { F::TIME, at }, { F::DURATION, d / st }, { F::BASE_DURATION, d }, { F::TAG, Text(std::string("Rest")) } };
            if (!(st == Fraction(1, 1))) {
                f.push_back({ F::STRETCH, st });
            }
            add(Kind::Rest, voice, f, Order::ByTime);
            report.rests++;
            report.modelled["(rest completing voice 1)"]++;
            at += d / st;
            written += d;
        }
    }

    void completeVoice1(Id staff, int mi)
    {
        const Fraction start = measureStart[size_t(mi)];
        const Fraction end = start + measureLen[size_t(mi)];
        const Fraction from = voice0End < Fraction() ? start : voice0End;
        if (!(from < end)) {
            return;
        }
        const Id voice = voiceFor(staff, 0);
        if (from == start) {
            Fields f { { F::TIME, start }, { F::DURATION, measureLen[size_t(mi)] }, { F::MEASURE_REST, true },
                       { F::TAG, Text(std::string("Rest")) } };
            if (!(stretchOf(staff) == Fraction(1, 1))) {
                f.push_back({ F::STRETCH, stretchOf(staff) });
            }
            add(Kind::Rest, voice, f, Order::ByTime);
            report.rests++;
            report.modelled["(rest completing voice 1)"]++;
            return;
        }
        fillVoice1(staff, voice, from, end, start);
    }

    //! Whole elements carried as they are, but without their links to or from parts (made again from the model's
    //! layouts when the score is written)
    static void stripLinks(Node& n)
    {
        n.children.erase(std::remove_if(n.children.begin(), n.children.end(), [](const Node& c) {
            return c.name == "linked" || c.name == "linkedMain" || c.name == "linkedTo";
        }), n.children.end());
        for (Node& c : n.children) {
            stripLinks(c);
        }
    }

    static std::string wholeFragment(const std::vector<const Node*>& nodes)
    {
        std::vector<Node> copies;
        copies.reserve(nodes.size());
        for (const Node* n : nodes) {
            copies.push_back(*n);   // links kept: whole elements are written as they were
        }
        std::vector<const Node*> ptrs;
        for (const Node& c : copies) {
            ptrs.push_back(&c);
        }
        return fragment(ptrs);
    }

    //! The element's children the model doesn't hold (eid aside), as a fragment
    static std::string unmodelled(const Node& e, std::vector<const char*> modelled)
    {
        std::vector<const Node*> rest;
        for (const Node& c : e.children) {
            // the links between a score and its parts are made again from the model's layouts when it is written
            if (c.type != Node::Type::Element || c.name == "eid" || c.name == "linked" || c.name == "linkedMain"
                || c.name == "linkedTo") {
                continue;
            }
            bool known = false;
            for (const char* m : modelled) {
                known = known || c.name == m;
            }
            if (!known) {
                rest.push_back(&c);
            }
        }
        return fragment(rest);
    }

    //! Adds the details of `e` (children outside `modelled`) with the fingerprint of the fields given so far
    static void withDetails(Fields& f, const Node& e, std::vector<const char*> modelled)
    {
        const std::string d = unmodelled(e, modelled);
        if (!d.empty()) {
            f.push_back({ F::DETAILS_KEY, detailsKey(f) });
            f.push_back({ F::DETAILS, Text(d) });
        }
    }

    //! A text child as written (MuseScore rich text: markup kept, still escaped)
    //! A text element's content as written: a leaf's verbatim, or (formatted text, "<b>bold</b>", which the parser
    //! keeps as elements) its elements written back inline, as MuseScore writes them
    static std::string innerXml(const Node& n)
    {
        if (n.leaf) {
            return n.raw;
        }
        std::string out;
        for (const Node& c : n.children) {
            if (c.type != Node::Type::Element) {
                out += c.name;   // a comment, as it was
                continue;
            }
            out += "<" + c.name;
            for (const auto& [k, v] : c.attrs) {
                out += " " + k + "=\"" + v + "\"";
            }
            if (c.selfClosing) {
                out += "/>";
                continue;
            }
            out += ">" + innerXml(c) + "</" + c.name + ">";
        }
        return out;
    }

    static std::string rawText(const Node& e, const char* tag)
    {
        const Node* c = e.child(tag);
        return c ? innerXml(*c) : std::string();
    }

    static bool isGrace(const Node& e)
    {
        for (const Node& c : e.children) {
            if (graceTags().count(c.name)) {
                return true;
            }
        }
        return false;
    }

    static bool isAfterGrace(const Node& e)
    {
        return e.child("grace8after") || e.child("grace16after") || e.child("grace32after");
    }

    //! The element's identity, and its link to another element of the same score (a linked staff's copy)
    void eid(const Node& e, Fields& f)
    {
        const std::string id = e.childText("eid");
        if (!id.empty()) {
            f.push_back({ F::ORIGIN_EID, Text(id) });
        }
        if (const Node* l = e.child("linkedTo")) {
            f.push_back({ F::LINK_EID, Text(l->text()) });
        }
        if (byPlaceFormat && (e.child("linkedMain") || e.child("linked"))) {
            f.push_back({ TEMP_NODE, int64_t(reinterpret_cast<intptr_t>(&e)) });
        }
        if (e.child("linkedMain")) {
            f.push_back({ F::LINK_MARK, Text(std::string("main")) });
        } else if (const Node* l = e.child("linked")) {
            std::vector<const Node*> inner;
            for (const Node& c : l->children) {
                inner.push_back(&c);
            }
            f.push_back({ F::LINK_MARK, Text("linked:" + fragment(inner)) });
        }
    }

    void readEvent(const Node& e, Id voice, Id staff, int mi, Fraction& t, Id tuplet, const Fraction& factor, Fraction& lastMainTime,
                   Id& lastMainChord)
    {
        const bool isRest = e.name != "Chord";
        // a value given twice: the last one counts (MuseScore reads in order and overwrites)
        auto last = [&e](const char* tag) {
            std::string v;
            for (const Node& c : e.children) {
                if (c.name == tag) {
                    v = c.text();
                }
            }
            return v;
        };
        const std::string type = last("durationType");
        const int64_t dots = toInt(last("dots"));
        std::string grace;
        for (const Node& c : e.children) {
            if (graceTags().count(c.name)) {
                grace = c.name;
            }
        }
        Fields f;
        Fraction duration;
        if (type == "measure" || e.name == "MeasureRepeat") {
            // a measure rest fills the rest of its measure (MuseScore corrects a stored <duration> that disagrees;
            // the file's value stays in the XML)
            duration = measureLen[size_t(mi)] - (t - measureStart[size_t(mi)]);
            f.push_back({ F::MEASURE_REST, true });
            if (!(stretchOf(staff) == Fraction(1, 1))) {
                f.push_back({ F::STRETCH, stretchOf(staff) });   // on a local time signature: its written length differs
            }
            if (e.name == "MeasureRepeat") {
                f.push_back({ F::SUBKIND, Text(std::string("measureRepeat")) });
            }
        } else {
            Fraction base;
            if (!durationOf(type, base)) {
                report.warnings.push_back("an event with an unknown duration \"" + type + "\"");
                return;
            }
            f.push_back({ F::BASE_DURATION, base });
            if (dots) {
                f.push_back({ F::DOTS, dots });
            }
            duration = grace.empty() ? dotted(base, dots) * factor : dotted(base, dots);
            const Fraction st = stretchOf(staff);
            if (grace.empty() && !(st == Fraction(1, 1))) {
                f.push_back({ F::STRETCH, st });
            }
            Fraction given;
            if (!grace.empty() && !e.childText("duration").empty() && parseFraction(e.childText("duration"), given)) {
                f.push_back({ F::GRACE_LENGTH, given });   // how long the grace note plays
            }
            if (grace.empty() && !e.childText("duration").empty() && parseFraction(e.childText("duration"), given)
                && !(given * factor == duration)) {
                duration = given * factor;   // two-note tremolo: each chord sounds for half its written value (in a tuplet, scaled)
                f.push_back({ F::EXPLICIT_DURATION, true });
            }
            if (tuplet.isValid() && grace.empty()) {
                f.push_back({ F::TUPLET, tuplet });
            }
        }
        const Fraction at = t;   // a grace chord sits at its main chord's time (after-graces are read with t = that time)
        f.push_back({ F::TIME, at });
        f.push_back({ F::DURATION, duration });
        if (!grace.empty()) {
            f.push_back({ F::GRACE, true });
            f.push_back({ F::GRACE_TYPE, Text(grace) });
            report.graces++;
        }
        if (e.name != "Chord") {
            f.push_back({ F::TAG, Text(e.name) });
        }
        if (const int64_t move = toInt(e.childText("staffMove"))) {
            f.push_back({ F::STAFF_MOVE, move });   // cross-staff
        }
        {
            // the details after the first note are written after the notes again (some refer to them by index)
            static const std::vector<const char*> modelled { "durationType", "dots", "duration", "staffMove", "Note", "Articulation", "Ornament",
                                                             "Lyrics", "Spanner", "Arpeggio", "TremoloSingleChord", "ChordLine",
                                                             "Fermata", "acciaccatura", "appoggiatura", "grace4", "grace16",
                                                             "grace32", "grace8after", "grace16after", "grace32after" };
            Node before, after;
            bool afterNotes = false;
            for (const Node& c : e.children) {
                afterNotes = afterNotes || c.name == "Note";
                (afterNotes ? after : before).children.push_back(c);
            }
            withDetails(f, before, modelled);
            const std::string tail = unmodelled(after, modelled);
            if (!tail.empty()) {
                f.push_back({ F::DETAILS_AFTER, Text(tail) });
                if (std::none_of(f.begin(), f.end(), [](const auto& p) { return p.first == F::DETAILS_KEY; })) {
                    f.push_back({ F::DETAILS_KEY, int64_t(0) });   // set when the object is finished
                }
            }
        }
        eid(e, f);

        bool hasNotes = false;
        for (const Node& c : e.children) {
            hasNotes = hasNotes || c.name == "Note";
        }
        const Kind kind = (isRest || !hasNotes) ? Kind::Rest : Kind::Chord;
        // in file order (BulkBuilder keeps equal times in order: grace notes before, after-graces after their chord)
        const Id ev = add(kind, voice, f, Order::ByTime);
        report.modelled[e.name]++;
        kind == Kind::Chord ? report.chords++ : report.rests++;
        {
            // the grace index MuseScore's locations use: before-graces from 0, then the after-graces
            currentVoice = voiceIndexOf(voice);
            auto it = graceFileIndex.find(&e);
            currentGrace = grace.empty() || it == graceFileIndex.end() ? -1 : it->second;
        }

        currentNoteIndex = 0;
        for (const Node& c : e.children) {
            if (c.name == "Note" && kind == Kind::Chord) {
                readNote(c, ev, staff, mi, at);
                ++currentNoteIndex;
            } else if (c.name == "Articulation" || c.name == "Ornament") {
                Fields af { { F::SUBKIND, Text(c.childText("subtype").empty() ? lowerFirst(c.name) : c.childText("subtype")) },
                            { F::TAG, Text(c.name) } };
                withDetails(af, c, { "subtype" });
                eid(c, af);
                add(Kind::Attachment, ev, af);
                report.modelled[c.name]++;
            } else if (c.name == "Lyrics") {
                Fields lf { { F::SUBKIND, Text(std::string("lyric")) }, { F::VERSE, toInt(c.childText("no")) },
                            { F::TEXT, Text(rawText(c, "text")) } };
                const std::string syl = c.childText("syllabic");
                if (!syl.empty()) {
                    lf.push_back({ F::SYLLABIC, Text(syl) });
                }
                Fraction ticks;
                if (!c.childText("ticks_f").empty() && parseFraction(c.childText("ticks_f"), ticks)) {
                    lf.push_back({ F::DURATION, ticks });
                }
                withDetails(lf, c, { "no", "syllabic", "text", "ticks_f" });
                eid(c, lf);
                add(Kind::Attachment, ev, lf);
                report.lyrics++;
                report.modelled["Lyrics"]++;
            } else if (c.name == "Spanner") {
                addSpanner(c, staff, mi, at, voiceIndexOf(voice), ev);
            } else if (c.name == "Arpeggio" || c.name == "TremoloSingleChord" || c.name == "ChordLine" || c.name == "Fermata") {
                Fields af { { F::SUBKIND, Text(lowerFirst(c.name)) }, { F::TAG, Text(c.name) } };
                withDetails(af, c, {});
                eid(c, af);
                add(Kind::Attachment, ev, af);
                report.modelled[c.name]++;
            } else if (c.name != "Note" && c.name != "durationType" && c.name != "dots" && c.name != "duration" && c.name != "eid"
                       && c.name != "staffMove"
                       && !graceTags().count(c.name)) {
                report.keptOnly[e.name + "/" + c.name]++;
            }
        }
        if (grace.empty()) {
            lastMainTime = t;
            lastMainChord = ev;
            t += duration;
        }
    }

    void readNote(const Node& n, Id chord, Id staff, int mi, const Fraction& at)
    {
        Fields f { { F::PITCH, toInt(n.childText("pitch"), 60) } };
        if (!n.childText("tpc").empty()) {
            f.push_back({ F::TPC, toInt(n.childText("tpc")) });
        }
        if (!n.childText("tpc2").empty()) {
            f.push_back({ F::TPC2, toInt(n.childText("tpc2")) });
        }
        // fretted instruments: where the note is played (also kept in the details, in place)
        if (!n.childText("fret").empty()) {
            f.push_back({ F::FRET, toInt(n.childText("fret")) });
        }
        if (!n.childText("string").empty()) {
            f.push_back({ F::STRING, toInt(n.childText("string")) });
        }
        // a note's ties are modelled; its other lines to another note (glissando, bend, note line) are carried
        // with its details, in place
        {
            // those after the pitch are written after it again (MuseScore finds a note's lines by its place in the
            // chord, which its pitch decides)
            std::vector<const Node*> rest, after;
            bool pitchSeen = false;
            for (const Node& c : n.children) {
                pitchSeen = pitchSeen || c.name == "pitch";
                if (c.type != Node::Type::Element || c.name == "eid" || c.name == "pitch" || c.name == "tpc" || c.name == "tpc2"
                    || c.name == "Fingering" || c.name == "linked" || c.name == "linkedMain" || c.name == "linkedTo"
                    || (c.name == "Spanner" && (c.attr("type") == "Tie" || noteLineTypes().count(c.attr("type"))))) {
                    continue;
                }
                (pitchSeen ? after : rest).push_back(&c);
            }
            if (!rest.empty()) {
                f.push_back({ F::DETAILS, Text(wholeFragment(rest)) });
            }
            if (!after.empty()) {
                f.push_back({ F::DETAILS_AFTER, Text(wholeFragment(after)) });
            }
            if (!rest.empty() || !after.empty()) {
                f.push_back({ F::DETAILS_KEY, int64_t(0) });   // set when the object is finished
            }
        }
        for (const Node& c : n.children) {
            if (c.name == "Spanner" && c.attr("type") == "Tie" && c.child("next") && c.child("Tie")) {
                const std::string tie = unmodelled(*c.child("Tie"), {});
                if (!tie.empty()) {
                    f.push_back({ F::DETAILS_2, Text(tie) });
                }
                if (!c.child("Tie")->childText("eid").empty()) {
                    f.push_back({ F::TIE_EID, Text(c.child("Tie")->childText("eid")) });
                }
                if (!c.child("Tie")->childText("linkedTo").empty()) {
                    f.push_back({ F::TIE_LINK, Text(c.child("Tie")->childText("linkedTo")) });
                }
                if (c.child("Tie")->child("linkedMain")) {   // links by place (before 4.60)
                    f.push_back({ F::TIE_MARK, Text(std::string("main")) });
                } else if (const Node* l = c.child("Tie")->child("linked")) {
                    std::vector<const Node*> inner;
                    for (const Node& x : l->children) {
                        inner.push_back(&x);
                    }
                    f.push_back({ F::TIE_MARK, Text("linked:" + fragment(inner)) });
                }
            }
        }
        eid(n, f);
        const Id note = add(Kind::Note, chord, f);
        // lines from one note to another (glissandi, bends, note lines): paired once every note is read
        for (const Node& c : n.children) {
            if (c.name == "Spanner" && noteLineTypes().count(c.attr("type"))) {
                const Node* inner = c.child(c.attr("type"));
                const Node* next = c.child("next");
                const Node* prev = c.child("prev");
                PendingLine pl { note, &c, staff, currentVoice, mi, at, currentGrace, currentNoteIndex };
                if (inner && next) {
                    lineStarts.push_back(pl);
                } else if (prev && !inner) {
                    lineEnds.push_back(pl);
                } else {
                    unpairedLines[note].push_back(&c);
                }
            }
        }
        report.notes++;
        report.modelled["Note"]++;
        notesAt[{ staff.value(), at }].push_back({ note, toInt(n.childText("pitch"), 60), currentGrace, currentVoice });
        for (const Node& c : n.children) {
            if (c.name == "Spanner" && c.attr("type") == "Tie") {
                if (const Node* next = c.child("next")) {
                    const Node* loc = next->child("location");
                    const int64_t g = loc && !loc->childText("grace").empty() ? toInt(loc->childText("grace")) : -1;
                    // to another staff or voice (cross-staff and cross-voice ties)
                    Id toStaff = staff;
                    if (loc && !loc->childText("staves").empty()) {
                        int64_t number = 0;
                        for (const auto& [k, v] : staffById) {
                            if (v == staff) {
                                number = k;
                            }
                        }
                        auto it = staffById.find(number + toInt(loc->childText("staves")));
                        toStaff = it == staffById.end() ? staff : it->second;
                    }
                    const int toVoice = currentVoice + int(loc ? toInt(loc->childText("voices")) : 0);
                    ties.push_back({ note, toStaff, toInt(n.childText("pitch"), 60), locate(mi, at, loc), g, toVoice });
                }
            } else if (c.name == "Fingering") {
                Fields af { { F::SUBKIND, Text(std::string("fingering")) }, { F::TEXT, Text(rawText(c, "text")) }, { F::TAG, Text(c.name) } };
                withDetails(af, c, { "text" });
                eid(c, af);
                add(Kind::Attachment, note, af);
                report.modelled["Fingering"]++;
            } else if (c.name != "pitch" && c.name != "tpc" && c.name != "tpc2" && c.name != "eid" && c.name != "Spanner") {
                report.keptOnly["Note/" + c.name]++;
            }
        }
    }

    int voiceIndexOf(Id voice) const
    {
        for (const auto& [key, v] : voices) {
            if (v == voice) {
                return key.second;
            }
        }
        return 0;
    }

    void addSpanner(const Node& s, Id staff, int mi, const Fraction& at, int voiceIndex, Id inEvent = Id())
    {
        const std::string type = s.attr("type");
        const Node* next = s.child("next");
        if (!next) {
            return;   // the end marker of a spanner started earlier (its extent comes from the start)
        }
        if (type == "Tie") {
            return;   // ties are read with their notes
        }
        const Fraction end = locate(mi, at, next->child("location"));
        Fields f { { F::SUBKIND, Text(lowerFirst(type)) }, { F::ANCHOR, staff }, { F::TIME, at },
                   { F::DURATION, end < at ? Fraction() : end - at } };
        if (const Node* body = s.child(type)) {
            eid(*body, f);
            if (type == "Volta") {
                f.push_back({ F::TEXT, Text(body->childText("endings")) });
                f.push_back({ F::INT_VALUE, toInt(body->childText("endings").substr(0, body->childText("endings").find(',')), 1) });
            } else if (type == "HairPin") {
                f.push_back({ F::INT_VALUE, toInt(body->childText("subtype")) });
            } else if (type == "Ottava") {
                f.push_back({ F::TEXT, Text(body->childText("subtype")) });
            }
            f.push_back({ F::VOICE_INDEX, int64_t(voiceIndex) });
            f.push_back({ F::SEQ, int64_t(++seq) });
            if (inEvent.isValid()) {
                f.push_back({ F::START_EVENT, inEvent });   // written back inside that chord or rest
                // where it ends, when that isn't its own staff, voice or a main chord
                if (const Node* loc = next->child("location")) {
                    if (!loc->childText("staves").empty()) {
                        int64_t number = 0;
                        for (const auto& [k, v] : staffById) {
                            if (v == staff) {
                                number = k;
                            }
                        }
                        auto it = staffById.find(number + toInt(loc->childText("staves")));
                        if (it != staffById.end()) {
                            f.push_back({ F::END_STAFF, it->second });
                        }
                    }
                    if (!loc->childText("voices").empty()) {
                        f.push_back({ F::END_VOICE, int64_t(voiceIndex + toInt(loc->childText("voices"))) });
                    }
                    if (!loc->childText("grace").empty()) {
                        f.push_back({ F::END_GRACE, toInt(loc->childText("grace")) });
                    }
                }
            }
            withDetails(f, *body, type == "Volta" ? std::vector<const char*> { "endings" }
                        : type == "HairPin" || type == "Ottava" ? std::vector<const char*> { "subtype" }
                        : std::vector<const char*> {});
        }
        add(Kind::Spanner, scoreId, f);
        report.spanners++;
        report.modelled["Spanner/" + type]++;
    }

    Id addFlowItem(const Node& e, Id staff, const Fraction& at, int voiceIndex)
    {
        Fields f { { F::SUBKIND, Text(flowItemKinds().at(e.name)) }, { F::TIME, at }, { F::TAG, Text(e.name) },
                   { F::SEQ, int64_t(++seq) } };
        if (voiceIndex >= 0) {
            f.push_back({ F::VOICE_INDEX, int64_t(voiceIndex) });
        }
        if (!systemWide(e.name)) {
            f.push_back({ F::ANCHOR, staff });
        } else {
            f.push_back({ F::HOME_STAFF, staff });
        }
        const std::string& n = e.name;
        if (n == "Clef") {
            f.push_back({ F::TEXT, Text(e.childText("concertClefType").empty() ? e.childText("clefType") : e.childText("concertClefType")) });
        } else if (n == "KeySig") {
            f.push_back({ F::INT_VALUE, toInt(e.childText("concertKey").empty() ? e.childText("accidental") : e.childText("concertKey")) });
            if (e.childText("concertKey").empty() && !e.childText("accidental").empty()) {
                // MuseScore's 4.00 reader knows only <accidental> (the key as written on the staff)
                f.push_back({ F::VALUE_TAG, Text(std::string("accidental")) });
            }
            if (!e.childText("mode").empty()) {
                f.push_back({ F::TEXT, Text(e.childText("mode")) });
            }
        } else if (n == "TimeSig") {
            f.push_back({ F::TIMESIG_NUM, toInt(e.childText("sigN"), 4) });
            f.push_back({ F::TIMESIG_DEN, toInt(e.childText("sigD"), 4) });
        } else if (n == "Tempo") {
            f.push_back({ F::REAL_VALUE, toReal(e.childText("tempo"), 2.0) });
            f.push_back({ F::TEXT, Text(rawText(e, "text")) });
        } else if (n == "Dynamic" || n == "Fermata" || n == "Breath") {
            f.push_back({ F::TEXT, Text(e.childText("subtype")) });
        } else if (n == "Harmony") {
            // the chord's root, name and bass: directly in the element before 4.60, in its <harmonyInfo> from 4.60
            // (a polychord, with several, is kept as written)
            const Node* info = nullptr;
            int infos = 0;
            for (const Node& c : e.children) {
                if (c.name == "harmonyInfo") {
                    info = infos++ ? info : &c;
                }
            }
            const Node& from = infos == 1 ? *info : e;
            std::string name = from.childText("name");
            if (name.empty() && infos != 1) {
                name = e.childText("harmonyType");
            }
            f.push_back({ F::TEXT, Text(name) });
            if (infos <= 1 && !from.childText("root").empty()) {
                f.push_back({ F::INT_VALUE, toInt(from.childText("root")) });
            }
            const char* bassTag = infos == 1 ? "bass" : "base";   // MuseScore's tag before 4.60
            if (infos <= 1 && !from.childText(bassTag).empty()) {
                f.push_back({ F::BASS, toInt(from.childText(bassTag)) });
            }
            if (infos == 1) {
                f.push_back({ F::VALUE_TAG, Text(std::string("harmonyInfo")) });
                const std::string rest = unmodelled(*info, { "name", "root", "bass" });   // its chord description id
                if (!rest.empty()) {
                    f.push_back({ F::DETAILS_2, Text(rest) });
                }
            }
        } else if (n == "InstrumentChange") {
            if (const Node* in = e.child("Instrument")) {
                f.push_back({ F::INSTRUMENT, Text(in->attr("id")) });
                f.push_back({ F::NAME, Text(in->childText("longName")) });
                if (const int64_t c = toInt(in->childText("transposeChromatic"))) {
                    f.push_back({ F::TRANSPOSE_CHROMATIC, c });
                }
            }
            f.push_back({ F::TEXT, Text(rawText(e, "text")) });
        } else {
            f.push_back({ F::TEXT, Text(rawText(e, "text")) });
        }
        static const std::map<std::string, std::vector<const char*> > modelled {
            { "Clef", { "concertClefType", "clefType" } }, { "KeySig", { "concertKey", "accidental", "mode" } },
            { "TimeSig", { "sigN", "sigD" } }, { "Tempo", { "tempo", "text" } }, { "Dynamic", { "subtype" } },
            { "Fermata", { "subtype" } }, { "Breath", { "subtype" } }, { "Harmony", { "name", "root", "base" } },
            { "InstrumentChange", { "text" } },
        };
        auto it = modelled.find(n);
        if (n == "Harmony" && std::count_if(e.children.begin(), e.children.end(), [](const Node& c) { return c.name == "harmonyInfo"; }) == 1) {
            withDetails(f, e, { "harmonyInfo" });   // modelled, with its details apart (DETAILS_2)
        } else if (it != modelled.end()) {
            withDetails(f, e, it->second);
        } else {
            withDetails(f, e, { "text" });
        }
        eid(e, f);
        const Id item = add(Kind::FlowItem, flowId, f, Order::ByTime);
        report.modelled[n]++;
        return item;
    }

    static const std::set<std::string>& noteLineTypes()
    {
        static const std::set<std::string> t { "Glissando", "GuitarBend", "NoteLine" };
        return t;
    }

    //! A line from note to note becomes a spanner of the model between the two notes (S3: so an edit to either note
    //! keeps it whole, or removes it whole); one whose other end isn't found is kept with its note as it was
    void pairNoteLines()
    {
        // the ends, by where they are: staff, voice, time, grace chord, note in the chord
        using Key = std::tuple<uint64_t, int, Fraction, int64_t, int>;
        std::map<Key, std::vector<size_t> > ends;
        for (size_t i = 0; i < lineEnds.size(); ++i) {
            const PendingLine& e = lineEnds[i];
            ends[{ e.staff.value(), e.voice, e.at, e.grace, e.noteIndex }].push_back(i);
        }
        std::set<size_t> usedEnds;
        for (const PendingLine& st : lineStarts) {
            const Node& sp = *st.node;
            const std::string type = sp.attr("type");
            const Node* loc = sp.child("next")->child("location");
            Id staff = st.staff;
            if (loc && !loc->childText("staves").empty()) {
                const int64_t number = staffOrder[st.staff] + toInt(loc->childText("staves"));
                auto it = staffById.find(number);
                staff = it == staffById.end() ? Id() : it->second;
            }
            const Key key { staff.value(), st.voice + int(loc ? toInt(loc->childText("voices")) : 0), locate(st.mi, st.at, loc),
                            loc && !loc->childText("grace").empty() ? toInt(loc->childText("grace")) : -1,
                            st.noteIndex + int(loc ? toInt(loc->childText("notes")) : 0) };
            size_t found = SIZE_MAX;
            auto it = ends.find(key);
            if (it != ends.end()) {
                for (size_t i : it->second) {
                    if (!usedEnds.count(i) && lineEnds[i].node->attr("type") == type) {
                        found = i;
                        break;
                    }
                }
            }
            if (found == SIZE_MAX) {
                unpairedLines[st.note].push_back(st.node);
                continue;
            }
            usedEnds.insert(found);
            const PendingLine& en = lineEnds[found];
            const Node& inner = *sp.child(type);
            Fields f { { F::SUBKIND, Text(lowerFirst(type)) }, { F::TAG, Text(type) }, { F::ANCHOR, st.note }, { F::END_ANCHOR, en.note },
                       { F::TIME, st.at }, { F::DURATION, en.at - st.at } };
            const std::string details = unmodelled(inner, {});
            if (!details.empty()) {
                f.push_back({ F::DETAILS, Text(details) });
                f.push_back({ F::DETAILS_KEY, int64_t(0) });
            }
            eid(inner, f);
            add(Kind::Spanner, scoreId, f);
            report.modelled[type]++;
        }
        for (size_t i = 0; i < lineEnds.size(); ++i) {
            if (!usedEnds.count(i)) {
                unpairedLines[lineEnds[i].note].push_back(lineEnds[i].node);
            }
        }
        // the ones not paired: with their note, as written (in file order)
        for (auto& [note, nodes] : unpairedLines) {
            std::sort(nodes.begin(), nodes.end());
            nodes.erase(std::unique(nodes.begin(), nodes.end()), nodes.end());
            bb.setField(note, F::LINE_DETAILS, Text(wholeFragment(nodes)));
            report.keptOnly["Note/Spanner (a line whose other end wasn't found)"] += int(nodes.size());
        }
    }

    void resolveTies()
    {
        for (const PendingTie& t : ties) {
            auto it = notesAt.find({ t.staff.value(), t.target });
            Id to;
            if (it != notesAt.end()) {
                // to the grace note named by index; otherwise a main chord's note first, then any
                for (int pass = 0; pass < 3 && !to.isValid(); ++pass) {
                    for (const NoteAt& n : it->second) {
                        // the voice named, then any; the grace note named, else a main chord's note first
                        const bool voiceFits = pass > 0 || n.voice == t.voice;
                        const bool fits = voiceFits && (t.grace >= 0 ? n.grace == t.grace : (pass == 2 || n.grace < 0));
                        // (a unison: each tie ends on its own note)
                        if (n.pitch == t.pitch && n.note != t.from && fits && !tiedInto.count(n.note)) {
                            to = n.note;
                            break;
                        }
                    }
                }
            }
            if (!to.isValid()) {
                report.warnings.push_back("a tie whose end wasn't found (pitch " + std::to_string(t.pitch) + ")");
                bb.setField(t.from, F::TIE_EID, model::Value());   // nothing to write it with
                continue;
            }
            bb.setField(t.from, F::TIE_TO, to);
            tiedInto.insert(to);
            report.ties++;
        }
    }

    void readLayouts()
    {
        // the parts: their own files in an archive, and any written inside the main score (as MuseScore writes a
        // plain .mscx's parts)
        struct Source {
            std::string entry;
            const Node* score;
            const Node* root;
            bool inlinePart;
        };
        std::vector<Source> sources;
        for (const auto& [entry, doc] : file.excerpts) {
            if (const Node* s = doc.root.child("Score")) {
                sources.push_back({ entry, s, &doc.root, false });
            }
        }
        static const Node noHead;
        if (const Node* main = file.score()) {
            int n = 0;
            for (const Node& c : main->children) {
                if (c.name == "Score") {
                    sources.push_back({ "part " + std::to_string(++n), &c, &noHead, true });
                }
            }
        }
        for (const Source& src : sources) {
            const std::string& entry = src.entry;
            const Node* s = src.score;
            std::string name;
            for (const Node& c : s->children) {
                if (c.name == "metaTag" && c.attr("name") == "partName") {
                    name = c.text();
                }
            }
            if (name.empty()) {
                name = entry.substr(entry.rfind('/') + 1);
            }
            std::vector<Id> list;
            for (const Node& c : s->children) {
                if (c.name != "Part") {
                    continue;
                }
                // the main score's player whose staff the part's staff is linked to (by number before 4.60, by
                // identity from 4.60); else the one of the same name
                Id byLink;
                for (const Node& st : c.children) {
                    if (st.name != "Staff" || byLink.isValid()) {
                        continue;
                    }
                    for (const Node& l : st.children) {
                        if (l.name != "linkedTo") {
                            continue;
                        }
                        Id mainStaff;
                        if (auto e = staffByEid.find(l.text()); e != staffByEid.end()) {
                            mainStaff = e->second;
                        } else if (auto n = staffById.find(toInt(l.text())); file.formatVersion() < std::string("4.60") && n != staffById.end()) {
                            mainStaff = n->second;
                        }
                        if (mainStaff.isValid()) {
                            byLink = playerOfStaff[mainStaff];
                            break;
                        }
                    }
                }
                if (byLink.isValid()) {
                    if (std::find(list.begin(), list.end(), byLink) == list.end()) {
                        list.push_back(byLink);
                    }
                    continue;
                }
                const Node* instr = c.child("Instrument");
                std::string pname = instr ? instr->childText("longName") : std::string();
                if (pname.empty()) {
                    pname = c.childText("trackName");
                }
                for (const auto& [pid, n] : players) {
                    if (n == pname && std::find(list.begin(), list.end(), pid) == list.end()) {
                        list.push_back(pid);
                        break;
                    }
                }
            }
            // the part's own settings: its style (its .mss, else the one inside), properties and archive entries, and
            // its own breaks; its music is the main score's (made again from the model when it is written)
            Fields f { { F::NAME, Text(name) }, { F::PLAYERS, model::IdList(list) } };
            const size_t slash = entry.rfind('/');
            const std::string folder = src.inlinePart ? std::string("\x01") : entry.substr(0, slash + 1);   // "Excerpts/0_Piano/"
            if (src.inlinePart) {
                f.push_back({ F::INLINE_PART, true });
            } else {
                f.push_back({ F::TAG, Text(folder.size() > 10 ? folder.substr(9, folder.size() - 10) : std::string()) });
            }
            xml::Document mss;
            std::string ignored;
            const Node* style = s->child("Style");
            std::string extras;
            for (const zip::Entry& z : file.entries) {
                if (z.name.rfind(folder, 0) != 0 || z.name == entry) {
                    continue;
                }
                if (z.name.size() > 4 && z.name.compare(z.name.size() - 4, 4, ".mss") == 0 && xml::parse(z.data, mss, ignored)
                    && mss.root.child("Style")) {
                    style = mss.root.child("Style");
                } else {
                    extras += z.name.substr(folder.size()) + '\0' + std::to_string(z.data.size()) + '\0' + z.data;
                }
            }
            documentFields(f, style, *s, *src.root);
            if (!extras.empty()) {
                f.push_back({ F::ARCHIVE_EXTRAS, Text(extras) });
            }
            std::vector<std::pair<std::string, std::string> > breaks;
            if (const Node* first = s->child("Staff")) {
                int index = -1;
                for (const Node& m : first->children) {
                    if (m.name != "Measure" || m.child("multiMeasureRest")) {
                        continue;
                    }
                    ++index;
                    for (const Node& c : m.children) {
                        if (c.name == "LayoutBreak") {
                            breaks.emplace_back(std::to_string(index), c.childText("subtype"));
                        }
                    }
                }
            }
            if (!breaks.empty()) {
                f.push_back({ F::LAYOUT_BREAKS, Text(packPairs(breaks)) });
            }
            add(Kind::Layout, scoreId, f);
            report.modelled["Excerpt"]++;
        }
    }

    struct PendingTie {
        Id from;
        Id staff;
        int64_t pitch;
        Fraction target;
        int64_t grace = -1;   // the target grace chord's index at that time (-1: not a grace)
        int voice = 0;        // the target voice (index in its staff)
    };
    struct NoteAt {
        Id note;
        int64_t pitch;
        int64_t grace;
        int voice;
    };
    int currentVoice = 0;   // the voice index of the event being read
    std::set<Id> tiedInto;  // notes already a tie's end
    int64_t currentGrace = -1;    // the grace index of the chord being read (-1: a main chord)
    int currentNoteIndex = 0;     // the note being read, in its chord
    struct PendingLine {
        Id note;
        const Node* node;   // its <Spanner> in the note
        Id staff;
        int voice;
        int mi;
        Fraction at;
        int64_t grace;
        int noteIndex;
    };
    std::vector<PendingLine> lineStarts, lineEnds;
    std::map<Id, std::vector<const Node*> > unpairedLines;
    std::map<const Node*, int64_t> graceFileIndex;   // a grace chord's number among its main chord's, in file order
    int64_t gracesInFile = 0;

    const ScoreFile& file;
    model::Model& model;
    model::BulkBuilder bb;
    ReadReport& report;
    Id scoreId, flowId;
    std::vector<std::pair<Id, std::string> > players;
    std::map<int64_t, Id> staffById;
    std::map<std::string, Id> staffByEid;
    std::map<Id, int64_t> staffOrder;   // staves in the order they are read
    std::map<Id, Id> playerOfStaff;
    bool byPlaceFormat = false;           // links by place (before 4.60)
    std::map<const Node*, Id> nodeObject;                          // elements with links by place → their objects
    std::map<const Node*, std::pair<int64_t, Fraction> > placeOf;  // where MuseScore's reader is at an element
    std::vector<std::pair<Id, std::string> > staffLinks;   // a linked staff and the staff it links to, as written
    int64_t staffCount = 0;
    std::map<uint64_t, Fraction> staffStretch;
    Fraction stretchOf(Id staff) const
    {
        auto it = staffStretch.find(staff.value());
        return it == staffStretch.end() ? Fraction(1, 1) : it->second;
    }
    int64_t seq = 0;
    Fraction voice0End;
    std::map<std::pair<uint64_t, int>, Id> voices;
    std::vector<Id> measureIds;
    std::vector<Fraction> measureStart, measureLen;
    Fraction flowEnd;
    std::map<std::pair<uint64_t, Fraction>, std::vector<NoteAt> > notesAt;
    std::vector<PendingTie> ties;
};
}

bool readScore(const ScoreFile& file, model::Model& model, IdGenerator& ids, ReadReport& report, std::string& error)
{
    report = ReadReport();
    Reader r(file, model, ids, report);
    return r.run(error);
}

std::string musicListing(const model::Snapshot& s, bool appearance, bool lyrics, bool texts)
{
    // staff order: players in score order, staves in player order
    std::ostringstream out;
    const model::Object* score = s.find(s.score());
    if (!score) {
        return std::string();
    }
    int staffIndex = 0;
    std::map<Id, int> staffIndexOf;
    for (Id p : score->children()) {
        const model::Object* player = s.find(p);
        if (!player || player->kind() != Kind::Player) {
            continue;
        }
        for (Id st : player->children()) {
            const model::Object* staff = s.find(st);
            std::vector<Id> staffVoices = staff->children().ids();
            std::stable_sort(staffVoices.begin(), staffVoices.end(), [&s](Id a, Id b) {
                return s.find(a)->intOr(F::NUMBER, 0) < s.find(b)->intOr(F::NUMBER, 0);
            });
            for (Id v : staffVoices) {
                const model::Object* voice = s.find(v);
                for (Id e : voice->children()) {
                    const model::Object* ev = s.find(e);
                    if (ev->kind() != Kind::Chord && ev->kind() != Kind::Rest) {
                        continue;
                    }
                    out << staffIndex << ' ' << voice->intOr(F::NUMBER, 0) << ' ' << ev->fractionOr(F::TIME, Fraction()).toString() << ' '
                        << ev->fractionOr(F::DURATION, Fraction()).toString() << (ev->has(F::GRACE) ? " g" : "");
                    std::vector<int64_t> pitches;
                    for (Id n : ev->children()) {
                        const model::Object* note = s.find(n);
                        if (note->kind() == Kind::Note) {
                            pitches.push_back(note->intOr(F::PITCH, -1));
                        }
                    }
                    std::sort(pitches.begin(), pitches.end());
                    for (int64_t p2 : pitches) {
                        out << ' ' << p2;
                    }
                    if (appearance) {
                        // as written: details only while they fit the music (D-E27)
                        auto shown = [](const model::Object& o) {
                            return !o.has(F::DETAILS_KEY) || detailsKey(o) == o.intOr(F::DETAILS_KEY, 0)
                                   ? o.text(F::DETAILS) + o.text(F::DETAILS_AFTER) : std::string();
                        };
                        const model::Object* top = nullptr;
                        for (Id n : ev->children()) {
                            const model::Object* note = s.find(n);
                            if (note->kind() == Kind::Note && (!top || note->intOr(F::PITCH, -1) >= top->intOr(F::PITCH, -1))) {
                                top = note;
                            }
                        }
                        if (top && shown(*top).find("<fixed>1</fixed>") != std::string::npos) {
                            out << " /";   // a slash (Chord::slash)
                        }
                        if (ev->kind() == Kind::Rest && voice->intOr(F::NUMBER, 0) >= 2 && shown(*ev).find("<small>1</small>") != std::string::npos) {
                            out << " acc";   // an accent rest (Rest::accent)
                        }
                        if (ev->intOr(F::STAFF_MOVE, 0) != 0) {
                            out << " sm:" << ev->intOr(F::STAFF_MOVE, 0);   // cross-staff
                        }
                    }
                    if (lyrics && !ev->has(F::GRACE)) {
                        // lyrics: verse (a: above), syllabic, extension, text (the reference listing's form)
                        std::vector<std::string> ls;
                        for (Id c : ev->children()) {
                            const model::Object* l = s.find(c);
                            if (l->kind() != Kind::Attachment || l->text(F::SUBKIND) != "lyric") {
                                continue;
                            }
                            const std::string syl = l->text(F::SYLLABIC);
                            const bool above = model::fragment::value(l->text(F::DETAILS), "placement") == "above";
                            ls.push_back(" L" + std::to_string(l->intOr(F::VERSE, 0)) + (above ? "a" : "") + ":"
                                         + (syl.empty() ? "s" : syl.substr(0, 1)) + ":"
                                         + l->fractionOr(F::DURATION, Fraction()).toString() + ":" + l->text(F::TEXT));
                        }
                        std::sort(ls.begin(), ls.end());
                        for (const std::string& x : ls) {
                            out << x;
                        }
                    }
                    out << '\n';
                }
            }
            staffIndexOf[st] = staffIndex;
            ++staffIndex;
        }
    }
    if (texts) {
        // texts, dynamics, tempos, rehearsal marks… (the reference listing's form): system-wide ones on the top staff
        static const std::map<std::string, std::string> names { { "staffText", "StaffText" }, { "systemText", "SystemText" },
                                                                { "dynamic", "Dynamic" }, { "expression", "Expression" },
                                                                { "rehearsalMark", "RehearsalMark" }, { "tempo", "Tempo" },
                                                                { "playTechnique", "PlayTechAnnotation" },
                                                                { "instrumentChange", "InstrumentChange" } };
        std::vector<std::string> lines;
        for (Id fl : score->children()) {
            const model::Object* flow = s.find(fl);
            if (!flow || flow->kind() != Kind::Flow) {
                continue;
            }
            for (Id c : flow->children()) {
                const model::Object* o = s.find(c);
                auto n = names.find(o->text(F::SUBKIND));
                if (o->kind() != Kind::FlowItem || n == names.end()) {
                    continue;
                }
                const bool system = o->has(F::HOME_STAFF);
                const int staff = staffIndexOf.count(o->idOr(system ? F::HOME_STAFF : F::ANCHOR)) ? staffIndexOf[o->idOr(system ? F::HOME_STAFF : F::ANCHOR)] : -1;
                const int64_t voice = o->intOr(F::VOICE_INDEX, 0);
                if (system && (staff != 0 || voice != 0)) {
                    continue;
                }
                std::string t = o->text(F::TEXT);
                // as MuseScore reads them: an empty <font …></font> is <font …/>, and a staff text in the expression
                // style an expression (files before 4.0)
                for (size_t at = t.find("></"); at != std::string::npos; at = t.find("></", at + 1)) {
                    // an empty element <x …></x>: <x …/>
                    const size_t open = t.rfind('<', at);
                    if (open == std::string::npos || t[open + 1] == '/') {
                        continue;
                    }
                    size_t nameEnd = open + 1;
                    while (nameEnd < at && t[nameEnd] != ' ' && t[nameEnd] != '/') {
                        ++nameEnd;
                    }
                    const std::string name = t.substr(open + 1, nameEnd - open - 1);
                    if (t.compare(at + 3, name.size() + 1, name + ">") == 0) {
                        t.replace(at, name.size() + 4, "/>");
                    }
                }
                // a line break as <br/>; whitespace alone between two tags is not kept by MuseScore's reader
                for (size_t at = t.find('\n'); at != std::string::npos; at = t.find('\n', at)) {
                    t.replace(at, 1, "<br/>");
                }
                for (size_t at = t.find('>'); at != std::string::npos; at = t.find('>', at + 1)) {
                    size_t e = at + 1;
                    while (e < t.size() && (t[e] == ' ' || t[e] == '\t' || t[e] == '\r')) {
                        ++e;
                    }
                    if (e > at + 1 && e < t.size() && t[e] == '<') {
                        t.erase(at + 1, e - at - 1);
                    }
                }
                std::string name = n->second;
                if (name == "StaffText" && model::fragment::value(o->text(F::DETAILS), "style") == "expression") {
                    name = "Expression";
                }
                if (o->text(F::SUBKIND) == "tempo") {
                    char b[32];
                    std::snprintf(b, sizeof(b), " @%.4g", std::holds_alternative<double>(o->get(F::REAL_VALUE)) ? std::get<double>(o->get(F::REAL_VALUE)) : 2.0);
                    t += b;
                }
                lines.push_back("# text " + name + " " + o->fractionOr(F::TIME, Fraction()).toString() + " " + std::to_string(staff) + " "
                                + std::to_string(voice) + " " + t);
            }
        }
        // fingerings, on their notes
        int si = 0;
        for (Id p : score->children()) {
            const model::Object* player = s.find(p);
            if (!player || player->kind() != Kind::Player) {
                continue;
            }
            for (Id st : player->children()) {
                for (Id v : s.find(st)->children()) {
                    const model::Object* voice = s.find(v);
                    if (voice->kind() != Kind::Voice) {
                        continue;
                    }
                    for (Id e : voice->children()) {
                        const model::Object* ev = s.find(e);
                        if (ev->kind() != Kind::Chord || ev->has(F::GRACE)) {
                            continue;
                        }
                        for (Id n : ev->children()) {
                            const model::Object* note = s.find(n);
                            if (note->kind() != Kind::Note) {
                                continue;
                            }
                            for (Id a : note->children()) {
                                const model::Object* fg = s.find(a);
                                if (fg->text(F::SUBKIND) == "fingering") {
                                    std::string style = model::fragment::value(fg->text(F::DETAILS), "style");
                                    lines.push_back("# fingering " + ev->fractionOr(F::TIME, Fraction()).toString() + " " + std::to_string(si) + " "
                                                    + std::to_string(voice->intOr(F::NUMBER, 0)) + " " + std::to_string(note->intOr(F::PITCH, 0)) + " "
                                                    + (style.empty() ? std::string("fingering") : style) + " " + fg->text(F::TEXT));
                                }
                            }
                        }
                    }
                }
                ++si;
            }
        }
        std::sort(lines.begin(), lines.end());
        for (const std::string& l : lines) {
            out << l << '\n';
        }
    }
    if (lyrics) {
        // partial dashes and extension lines at repeats
        std::vector<std::string> lines;
        for (Id c : score->children()) {
            const model::Object* o = s.find(c);
            if (!o || o->kind() != Kind::Spanner || o->text(F::SUBKIND) != "partialLyricsLine") {
                continue;
            }
            const std::string& d = o->text(F::DETAILS);
            const std::string m = model::fragment::value(d, "isEndMelisma");
            lines.push_back("# partial " + std::to_string(staffIndexOf[o->idOr(F::ANCHOR)]) + " " + std::to_string(o->intOr(F::VOICE_INDEX, 0))
                            + " " + o->fractionOr(F::TIME, Fraction()).toString() + " " + o->fractionOr(F::DURATION, Fraction()).toString()
                            + " " + std::to_string(std::atoi(model::fragment::value(d, "no").c_str())) + " "
                            + (m == "1" || m == "true" ? "m" : "d") + " " + (model::fragment::value(d, "placement") == "above" ? "a" : "b"));
        }
        std::sort(lines.begin(), lines.end());
        for (const std::string& l : lines) {
            out << l << '\n';
        }
    }
    return out.str();
}

static std::string valueString(const model::Value& v, const std::map<Id, std::string>& paths)
{
    auto idText = [&paths](Id id) {
        auto it = paths.find(id);
        return it == paths.end() ? std::string("?") : it->second;
    };
    if (std::holds_alternative<bool>(v)) {
        return std::get<bool>(v) ? "true" : "false";
    }
    if (std::holds_alternative<int64_t>(v)) {
        return std::to_string(std::get<int64_t>(v));
    }
    if (std::holds_alternative<double>(v)) {
        char b[40];
        std::snprintf(b, sizeof(b), "%.17g", std::get<double>(v));
        return b;
    }
    if (std::holds_alternative<model::Text>(v)) {
        std::string t = std::get<model::Text>(v).str();
        for (char& c : t) {
            if (c == '\n') {
                c = '|';
            } else if (c == '\0') {
                c = '~';
            } else if (c == '\x1E') {
                c = '|';
            } else if (c == '\x1F') {
                c = '=';
            }
        }
        return '"' + t + '"';
    }
    if (std::holds_alternative<Fraction>(v)) {
        const Fraction& f = std::get<Fraction>(v);
        return std::to_string(f.numerator()) + "/" + std::to_string(f.denominator());
    }
    if (std::holds_alternative<Id>(v)) {
        return "@" + idText(std::get<Id>(v));
    }
    if (std::holds_alternative<model::IdList>(v)) {
        std::string out = "[";
        for (Id id : std::get<model::IdList>(v)) {
            out += "@" + idText(id) + " ";
        }
        return out + "]";
    }
    return "-";
}

//! Every object below the score in tree order, with its fields; ids are written as tree paths so two readings
//! of the same music compare equal whatever ids they were given
std::string modelDump(const model::Snapshot& s)
{
    std::map<Id, std::string> paths;
    std::vector<Id> order;
    std::vector<std::pair<Id, std::string> > stack { { s.score(), "s" } };
    while (!stack.empty()) {
        auto [id, path] = stack.back();
        stack.pop_back();
        const model::Object* o = s.find(id);
        if (!o) {
            continue;
        }
        paths[id] = path;
        order.push_back(id);
        const auto& ch = o->children();
        for (size_t i = ch.size(); i-- > 0;) {
            stack.emplace_back(ch[i], path + "." + std::to_string(i));
        }
    }
    std::string out;
    for (Id id : order) {
        const model::Object* o = s.find(id);
        out += paths[id];
        o->forEachField([&](model::Field f, const model::Value& v) {
            // SEQ is a read-order counter, not music: the order it gives shows as the tree order and the paths.
            // DETAILS_KEY is a fingerprint that includes object ids, which differ between two readings (whether
            // it still matches is checked by the writer: no details may be dropped without an edit)
            if (f == F::CHILDREN || f == F::PARENT || f == F::SEQ
                || f == F::DETAILS_KEY) {
                return;
            }
            out += " " + std::string(model::fieldName(f)) + "=" + valueString(v, paths);
        });
        out += "\n";
    }
    return out;
}

}
