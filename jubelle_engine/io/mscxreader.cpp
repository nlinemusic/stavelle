/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::io). Not part of MuseScore.
 */
#include "mscxreader.h"

#include <algorithm>
#include <map>
#include <set>
#include <sstream>

#include "model/bulkbuilder.h"
#include "details.h"

namespace F = je::model::fields;
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
        scoreId = bb.add(Kind::Score, Id(), scoreFields(*score));
        flowId = bb.add(Kind::Flow, scoreId, { { F::NAME, Text(std::string("Flow 1")) } });

        // players and staves first, then the measure grid from the first staff, then the music
        for (const Node& c : score->children) {
            if (c.name == "Part") {
                readPart(c);
            }
        }
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
        for (const Node* s : musicStaves) {
            readStaffMusic(*s);
        }
        resolveTies();
        readLayouts();
        // each object's details carry the fingerprint of its finished fields (some are set after it was added:
        // tie targets, completed durations), so an edit to its music later can be told apart
        report.objects = bb.finish(model, [](model::Object o) {
            if (o.kind() != Kind::Score && (o.has(F::DETAILS) || o.has(F::DETAILS_2) || o.has(F::DETAILS_KEY))) {
                return o.with(F::DETAILS_KEY, detailsKey(o));
            }
            return o;
        });
        return true;
    }

private:
    // ---- score-wide data: style, properties, settings, other archive entries -----------------------
    Fields scoreFields(const Node& score)
    {
        Fields f { { F::NAME, Text(metaTag(file, "workTitle")) } };
        if (const Node* style = styleNode(file)) {
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
            } else if (c.name == "Division" || c.name == "Style" || c.name == "eid") {
                continue;
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
        for (const Node& c : file.main.root.children) {
            if (c.type == Node::Type::Element && c.name != "Score") {
                head.push_back(&c);
            }
        }
        // the head Jubelle writes for a score without one is left out, so that writing and reading again is stable
        if (!head.empty() && fragment(head) != defaultScoreHead()) {
            f.push_back({ F::SCORE_HEAD, Text(fragment(head)) });
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
        const Id player = bb.add(Kind::Player, scoreId, f);
        players.push_back({ player, name });
        report.modelled["Part"]++;
        for (const Node& c : part.children) {
            if (c.name == "Staff") {
                Fields sf;
                if (const Node* st = c.child("StaffType")) {
                    sf.push_back({ F::SUBKIND, Text(st->attr("group")) });
                }
                withDetails(sf, c, {});
                const Id staff = bb.add(Kind::Staff, player, sf);
                // staves are numbered through the score in order; older 4.x files also write the number
                ++staffCount;
                staffById[c.hasAttr("id") ? toInt(c.attr("id")) : staffCount] = staff;
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
                // a frame (title, text, fret diagrams) between measures, carried whole until S7/S8 model frames
                bb.add(Kind::FlowItem, flowId, { { F::SUBKIND, Text(std::string("frame")) }, { F::TAG, Text(m.name) }, { F::TIME, t },
                                                 { F::SEQ, int64_t(++seq) }, { F::DETAILS, Text(fragment({ &m })) } }, Order::ByTime);
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
                        if (afterMusic) {
                            nextNum = toInt(e.childText("sigN"), num);
                            nextDen = toInt(e.childText("sigD"), den);
                        } else {
                            num = toInt(e.childText("sigN"), num);
                            den = toInt(e.childText("sigD"), den);
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
            measureIds.push_back(bb.add(Kind::Measure, flowId, f, Order::ByTime));
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
        const Id v = bb.add(Kind::Voice, staff, { { F::FLOW, flowId }, { F::NUMBER, int64_t(index) } });
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
                report.keptOnly["Measure (multi-measure rest, layout only)"]++;
                continue;
            }
            if (++mi >= int(measureStart.size())) {
                report.warnings.push_back("a staff has more measures than the first staff");
                return;
            }
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
                bb.add(Kind::FlowItem, flowId, { { F::SUBKIND, Text(std::string("measureDetail")) }, { F::ANCHOR, staff },
                                                 { F::TIME, measureStart[size_t(mi)] }, { F::SEQ, int64_t(++seq) },
                                                 { F::DETAILS, Text(fragment(measureDetails)) } }, Order::ByTime);
            }
            completeVoice1(staff, mi);
        }
    }

    void readVoice(const Node& voice, Id staff, int mi, int index)
    {
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

        for (const Node& e : voice.children) {
            const std::string& n = e.name;
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
                tuplets.push_back({ bb.add(Kind::Tuplet, vid(), f, Order::ByTime), outer * stretch * Fraction(normal, actual),
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
                itemsSinceEvent.push_back(bb.add(Kind::FlowItem, flowId, { { F::SUBKIND, Text(std::string("detail")) }, { F::ANCHOR, staff },
                                                                          { F::VOICE_INDEX, int64_t(index) }, { F::TIME, t },
                                                                          { F::SEQ, int64_t(++seq) }, { F::TAG, Text(n) },
                                                                          { F::DETAILS, Text(fragment({ &e })) } }, Order::ByTime));
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
    void fillVoice1(Id voice, Fraction at, const Fraction& to, const Fraction& measureStartTime)
    {
        for (int guard = 0; at < to && guard < 64; ++guard) {
            const Fraction pos = at - measureStartTime;
            const Fraction rem = to - at;
            Fraction d(1, 1);
            while (Fraction(1, 1024) < d && (rem < d || (pos / d).denominator() != 1)) {
                d = d * Fraction(1, 2);
            }
            if (rem < d) {
                break;
            }
            bb.add(Kind::Rest, voice, { { F::TIME, at }, { F::DURATION, d }, { F::BASE_DURATION, d }, { F::TAG, Text(std::string("Rest")) } },
                   Order::ByTime);
            report.rests++;
            report.modelled["(rest completing voice 1)"]++;
            at += d;
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
            bb.add(Kind::Rest, voice, { { F::TIME, start }, { F::DURATION, measureLen[size_t(mi)] }, { F::MEASURE_REST, true },
                                        { F::TAG, Text(std::string("Rest")) } },
                   Order::ByTime);
            report.rests++;
            report.modelled["(rest completing voice 1)"]++;
            return;
        }
        fillVoice1(voice, from, end, start);
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
    static std::string rawText(const Node& e, const char* tag)
    {
        const Node* c = e.child(tag);
        return c ? c->raw : std::string();
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

    void eid(const Node& e, Fields& f)
    {
        const std::string id = e.childText("eid");
        if (!id.empty()) {
            f.push_back({ F::ORIGIN_EID, Text(id) });
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
        withDetails(f, e, { "durationType", "dots", "duration", "Note", "Articulation", "Ornament", "Lyrics", "Spanner", "Arpeggio",
                            "TremoloSingleChord", "ChordLine", "Fermata", "acciaccatura", "appoggiatura", "grace4", "grace16",
                            "grace32", "grace8after", "grace16after", "grace32after" });
        eid(e, f);

        bool hasNotes = false;
        for (const Node& c : e.children) {
            hasNotes = hasNotes || c.name == "Note";
        }
        const Kind kind = (isRest || !hasNotes) ? Kind::Rest : Kind::Chord;
        // in file order (BulkBuilder keeps equal times in order: grace notes before, after-graces after their chord)
        const Id ev = bb.add(kind, voice, f, Order::ByTime);
        report.modelled[e.name]++;
        kind == Kind::Chord ? report.chords++ : report.rests++;

        for (const Node& c : e.children) {
            if (c.name == "Note" && kind == Kind::Chord) {
                readNote(c, ev, staff, mi, at);
            } else if (c.name == "Articulation" || c.name == "Ornament") {
                Fields af { { F::SUBKIND, Text(c.childText("subtype").empty() ? lowerFirst(c.name) : c.childText("subtype")) },
                            { F::TAG, Text(c.name) } };
                withDetails(af, c, { "subtype" });
                eid(c, af);
                bb.add(Kind::Attachment, ev, af);
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
                bb.add(Kind::Attachment, ev, lf);
                report.lyrics++;
                report.modelled["Lyrics"]++;
            } else if (c.name == "Spanner") {
                addSpanner(c, staff, mi, at, voiceIndexOf(voice));
            } else if (c.name == "Arpeggio" || c.name == "TremoloSingleChord" || c.name == "ChordLine" || c.name == "Fermata") {
                Fields af { { F::SUBKIND, Text(lowerFirst(c.name)) }, { F::TAG, Text(c.name) } };
                withDetails(af, c, {});
                eid(c, af);
                bb.add(Kind::Attachment, ev, af);
                report.modelled[c.name]++;
            } else if (c.name != "Note" && c.name != "durationType" && c.name != "dots" && c.name != "duration" && c.name != "eid"
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
        withDetails(f, n, { "pitch", "tpc", "tpc2", "Spanner", "Fingering" });
        for (const Node& c : n.children) {
            if (c.name == "Spanner" && c.attr("type") == "Tie" && c.child("next") && c.child("Tie")) {
                const std::string tie = unmodelled(*c.child("Tie"), {});
                if (!tie.empty()) {
                    f.push_back({ F::DETAILS_2, Text(tie) });
                }
            }
        }
        eid(n, f);
        const Id note = bb.add(Kind::Note, chord, f);
        report.notes++;
        report.modelled["Note"]++;
        notesAt[{ staff.value(), at }].push_back({ note, toInt(n.childText("pitch"), 60) });
        for (const Node& c : n.children) {
            if (c.name == "Spanner" && c.attr("type") == "Tie") {
                if (const Node* next = c.child("next")) {
                    ties.push_back({ note, staff, toInt(n.childText("pitch"), 60), locate(mi, at, next->child("location")) });
                }
            } else if (c.name == "Fingering") {
                Fields af { { F::SUBKIND, Text(std::string("fingering")) }, { F::TEXT, Text(rawText(c, "text")) }, { F::TAG, Text(c.name) } };
                withDetails(af, c, { "text" });
                eid(c, af);
                bb.add(Kind::Attachment, note, af);
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

    void addSpanner(const Node& s, Id staff, int mi, const Fraction& at, int voiceIndex)
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
            withDetails(f, *body, type == "Volta" ? std::vector<const char*> { "endings" }
                        : type == "HairPin" || type == "Ottava" ? std::vector<const char*> { "subtype" }
                        : std::vector<const char*> {});
        }
        bb.add(Kind::Spanner, scoreId, f);
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
            std::string name = e.childText("name");
            if (name.empty()) {
                name = e.childText("harmonyType");
            }
            f.push_back({ F::TEXT, Text(name) });
            if (!e.childText("root").empty()) {
                f.push_back({ F::INT_VALUE, toInt(e.childText("root")) });
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
            { "Fermata", { "subtype" } }, { "Breath", { "subtype" } }, { "Harmony", { "name", "root" } },
            { "InstrumentChange", { "text" } },
        };
        auto it = modelled.find(n);
        if (it != modelled.end()) {
            withDetails(f, e, it->second);
        } else {
            withDetails(f, e, { "text" });
        }
        eid(e, f);
        const Id item = bb.add(Kind::FlowItem, flowId, f, Order::ByTime);
        report.modelled[n]++;
        return item;
    }

    void resolveTies()
    {
        for (const PendingTie& t : ties) {
            auto it = notesAt.find({ t.staff.value(), t.target });
            Id to;
            if (it != notesAt.end()) {
                for (const auto& [note, pitch] : it->second) {
                    if (pitch == t.pitch && note != t.from) {
                        to = note;
                        break;
                    }
                }
            }
            if (!to.isValid()) {
                report.warnings.push_back("a tie whose end wasn't found (pitch " + std::to_string(t.pitch) + ")");
                continue;
            }
            bb.setField(t.from, F::TIE_TO, to);
            report.ties++;
        }
    }

    void readLayouts()
    {
        for (const auto& [entry, doc] : file.excerpts) {
            const Node* s = doc.root.child("Score");
            if (!s) {
                continue;
            }
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
            bb.add(Kind::Layout, scoreId, { { F::NAME, Text(name) }, { F::PLAYERS, model::IdList(list) } });
            report.modelled["Excerpt"]++;
        }
    }

    struct PendingTie {
        Id from;
        Id staff;
        int64_t pitch;
        Fraction target;
    };

    const ScoreFile& file;
    model::Model& model;
    model::BulkBuilder bb;
    ReadReport& report;
    Id scoreId, flowId;
    std::vector<std::pair<Id, std::string> > players;
    std::map<int64_t, Id> staffById;
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
    std::map<std::pair<uint64_t, Fraction>, std::vector<std::pair<Id, int64_t> > > notesAt;
    std::vector<PendingTie> ties;
};
}

bool readScore(const ScoreFile& file, model::Model& model, IdGenerator& ids, ReadReport& report, std::string& error)
{
    report = ReadReport();
    Reader r(file, model, ids, report);
    return r.run(error);
}

std::string musicListing(const model::Snapshot& s)
{
    // staff order: players in score order, staves in player order
    std::ostringstream out;
    const model::Object* score = s.find(s.score());
    if (!score) {
        return std::string();
    }
    int staffIndex = 0;
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
                    out << '\n';
                }
            }
            ++staffIndex;
        }
    }
    return out.str();
}
}
