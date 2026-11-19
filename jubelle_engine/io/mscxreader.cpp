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
        scoreId = bb.add(Kind::Score, Id(), { { F::NAME, Text(metaTag(file, "workTitle")) } });
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
        report.objects = bb.finish(model);
        return true;
    }

private:
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
            for (const Node& v : m.children) {
                if (v.name == "voice") {
                    readVoice(v, staff, mi, vi++);
                } else if (flowItemKinds().count(v.name)) {
                    addFlowItem(v, staff, measureStart[size_t(mi)]);   // a few items sit directly in the measure
                } else if (v.name != "eid" && v.name != "startRepeat" && v.name != "endRepeat" && v.name != "irregular"
                           && v.name != "LayoutBreak") {
                    report.keptOnly[v.name]++;
                }
            }
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
        Fraction lastMainTime = t;
        Id lastMainChord;
        std::vector<OpenTuplet> tuplets;
        std::vector<const Node*> pendingAfter;
        auto factor = [&tuplets]() { return tuplets.empty() ? Fraction(1, 1) : tuplets.back().factor; };

        for (const Node& e : voice.children) {
            const std::string& n = e.name;
            if (n == "location") {
                Fraction df;
                if (parseFraction(e.childText("fractions").empty() ? "0" : e.childText("fractions"), df)) {
                    t += df;
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
                    tuplets.push_back({ Id(), factor() });
                    continue;
                }
                const Fraction outer = factor();
                Fields f { { F::TIME, t }, { F::DURATION, base * Fraction(normal, 1) * outer }, { F::BASE_DURATION, base },
                           { F::RATIO_NUM, actual }, { F::RATIO_DEN, normal } };
                if (!tuplets.empty() && tuplets.back().id.isValid()) {
                    f.push_back({ F::TUPLET, tuplets.back().id });
                }
                eid(e, f);
                tuplets.push_back({ bb.add(Kind::Tuplet, vid(), f, Order::ByTime), outer * Fraction(normal, actual) });
                report.tuplets++;
                report.modelled["Tuplet"]++;
            } else if (n == "endTuplet") {
                if (!tuplets.empty()) {
                    tuplets.pop_back();
                }
            } else if (n == "Chord" || n == "Rest" || n == "MeasureRepeat") {
                if (isAfterGrace(e)) {
                    pendingAfter.push_back(&e);   // MuseScore writes it before its main chord; it follows it in the voice
                    continue;
                }
                readEvent(e, vid(), staff, mi, t, tuplets.empty() ? Id() : tuplets.back().id, factor(), lastMainTime, lastMainChord);
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
                addSpanner(e, staff, mi, t);
            } else if (flowItemKinds().count(n)) {
                addFlowItem(e, staff, t);
            } else if (n != "eid") {
                report.keptOnly[n]++;
            }
        }
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
        const std::string type = e.childText("durationType");
        const int64_t dots = toInt(e.childText("dots"));
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
                bb.add(Kind::Attachment, ev, { { F::SUBKIND, Text(c.childText("subtype").empty() ? lowerFirst(c.name) : c.childText("subtype")) } });
                report.modelled[c.name]++;
            } else if (c.name == "Lyrics") {
                Fields lf { { F::SUBKIND, Text(std::string("lyric")) }, { F::VERSE, toInt(c.childText("no")) },
                            { F::TEXT, Text(c.childText("text")) } };
                const std::string syl = c.childText("syllabic");
                if (!syl.empty()) {
                    lf.push_back({ F::SYLLABIC, Text(syl) });
                }
                Fraction ticks;
                if (!c.childText("ticks_f").empty() && parseFraction(c.childText("ticks_f"), ticks)) {
                    lf.push_back({ F::DURATION, ticks });
                }
                bb.add(Kind::Attachment, ev, lf);
                report.lyrics++;
                report.modelled["Lyrics"]++;
            } else if (c.name == "Spanner") {
                addSpanner(c, staff, mi, at);
            } else if (c.name == "Arpeggio" || c.name == "TremoloSingleChord" || c.name == "ChordLine" || c.name == "Fermata") {
                bb.add(Kind::Attachment, ev, { { F::SUBKIND, Text(lowerFirst(c.name)) } });
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
                bb.add(Kind::Attachment, note, { { F::SUBKIND, Text(std::string("fingering")) }, { F::TEXT, Text(c.childText("text")) } });
                report.modelled["Fingering"]++;
            } else if (c.name != "pitch" && c.name != "tpc" && c.name != "tpc2" && c.name != "eid" && c.name != "Spanner") {
                report.keptOnly["Note/" + c.name]++;
            }
        }
    }

    void addSpanner(const Node& s, Id staff, int mi, const Fraction& at)
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
        }
        bb.add(Kind::Spanner, scoreId, f);
        report.spanners++;
        report.modelled["Spanner/" + type]++;
    }

    void addFlowItem(const Node& e, Id staff, const Fraction& at)
    {
        Fields f { { F::SUBKIND, Text(flowItemKinds().at(e.name)) }, { F::TIME, at } };
        if (!systemWide(e.name)) {
            f.push_back({ F::ANCHOR, staff });
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
            f.push_back({ F::TEXT, Text(e.childText("text")) });
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
            f.push_back({ F::TEXT, Text(e.childText("text")) });
        } else {
            f.push_back({ F::TEXT, Text(e.childText("text")) });
        }
        eid(e, f);
        bb.add(Kind::FlowItem, flowId, f, Order::ByTime);
        report.modelled[n]++;
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
