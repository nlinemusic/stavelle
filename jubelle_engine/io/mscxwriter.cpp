/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::io). Not part of MuseScore.
 */
#include "mscxwriter.h"

#include <algorithm>
#include <map>
#include <set>

#include "details.h"
#include "mscxreader.h"

namespace F = je::model::fields;
using je::model::Kind;
using je::model::Object;
using je::io::xml::Node;

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

class Writer
{
public:
    Writer(const model::Snapshot& snapshot, WriteReport& r)
        : s(snapshot), report(r) {}

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
        root.attrs.emplace_back("version", "4.70");
        const std::vector<Node> head = parseFragment(score->has(F::SCORE_HEAD) ? score->text(F::SCORE_HEAD) : defaultScoreHead());
        root.children.insert(root.children.end(), head.begin(), head.end());
        root.children.push_back(scoreNode());

        // the style also as score_style.mss, which MuseScore reads from an archive
        file.hasStyleEntry = true;
        file.style.prolog.push_back(declaration());
        file.style.hasRoot = true;
        file.style.trailing = "\n";
        file.style.root.name = "museScore";
        file.style.root.attrs.emplace_back("version", "4.70");
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
        const std::string extras = score->text(F::ARCHIVE_EXTRAS);
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
            file.entries.push_back(e);
            at = z2 + 1 + size;
        }
        file.entries.push_back(container);
        return file;
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
            } else if (o->kind() == Kind::Spanner) {
                spanners.push_back(o);
            }
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

    void addEid(Node& n, const Object& o)
    {
        const std::string eid = o.text(F::ORIGIN_EID);
        if (!eid.empty() && eid.rfind("part:", 0) != 0) {
            n.children.push_back(leaf("eid", eid));
        }
    }

    // ---- score, style, parts ------------------------------------------------------------------------------------
    Node styleNode()
    {
        Node style = elem("Style");
        for (const auto& [name, raw] : unpackPairs(score->text(F::STYLE))) {
            style.children.push_back(raw == "\x01" ? empty(name) : leaf(name, raw));
        }
        for (Node& c : parseFragment(score->text(F::DETAILS_2))) {
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
        for (Node& c : parseFragment(score->text(F::SCORE_PRE))) {
            sc.children.push_back(std::move(c));
        }
        for (const auto& [name, raw] : unpackPairs(score->text(F::META))) {
            Node m = leaf("metaTag", raw);
            m.attrs.emplace_back("name", xml::encode(name));
            sc.children.push_back(std::move(m));
        }
        int partNo = 0;
        for (const Object* p : players) {
            sc.children.push_back(partNode(*p, ++partNo));
        }
        for (size_t i = 0; i < staves.size(); ++i) {
            sc.children.push_back(musicStaff(*staves[i], int(i) + 1));
        }
        for (Node& c : parseFragment(score->text(F::SCORE_POST))) {
            sc.children.push_back(std::move(c));
        }
        return sc;
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
            if (!detailsFit(*staff)) {
                Node type = elem("StaffType");
                type.attrs.emplace_back("group", staff->text(F::SUBKIND).empty() ? std::string("pitched") : staff->text(F::SUBKIND));
                type.children.push_back(leaf("name", "stdNormal"));
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
        if (p.has(F::TRANSPOSE_DIATONIC)) {
            instr.children.push_back(leaf("transposeDiatonic", std::to_string(p.intOr(F::TRANSPOSE_DIATONIC, 0))));
        }
        if (p.has(F::TRANSPOSE_CHROMATIC)) {
            instr.children.push_back(leaf("transposeChromatic", std::to_string(p.intOr(F::TRANSPOSE_CHROMATIC, 0))));
        }
        if (fit) {
            for (Node& c : parseFragment(p.text(F::DETAILS_2))) {
                instr.children.push_back(std::move(c));
            }
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
                    for (Node& c : parseFragment(frames[frameAt++]->text(F::DETAILS))) {
                        st.children.push_back(std::move(c));
                    }
                }
            }
            st.children.push_back(measureNode(staff, int(m), first, voicesByNumber, lastVoice));
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
        if (first && mo.has(F::BREAK)) {
            Node lb = elem("LayoutBreak");
            lb.children.push_back(leaf("subtype", xml::encode(mo.text(F::BREAK))));
            me.children.push_back(std::move(lb));
        }
        for (const Object* it : items) {
            if (it->text(F::SUBKIND) == "measureDetail" && it->idOr(F::ANCHOR) == staff.id()
                && it->fractionOr(F::TIME, Fraction()) == measureStart[size_t(m)]) {
                for (Node& c : parseFragment(it->text(F::DETAILS))) {
                    me.children.push_back(std::move(c));
                }
            }
        }
        auto measureItems = [&](bool atEnd) {
            for (const Object* it : items) {
                const std::string sub = it->text(F::SUBKIND);
                const Id where = it->has(F::ANCHOR) ? it->idOr(F::ANCHOR) : it->idOr(F::HOME_STAFF);
                if (sub != "frame" && sub != "measureDetail" && !it->has(F::VOICE_INDEX) && where == staff.id()
                    && it->fractionOr(F::TIME, Fraction()) == measureStart[size_t(m)] && it->has(F::AT_END) == atEnd) {
                    me.children.push_back(itemNode(*it));
                }
            }
        };
        measureItems(false);
        // the voices written: up to the highest with anything in this measure (an item may sit in an otherwise empty voice)
        for (const Object* it : items) {
            if (it->has(F::VOICE_INDEX) && (it->idOr(F::ANCHOR) == staff.id() || it->idOr(F::HOME_STAFF) == staff.id())) {
                const Fraction t = it->fractionOr(F::TIME, Fraction());
                if (it->has(F::AT_END) ? measureEnding(t) == m : measureAt(t) == m) {
                    lastVoice = std::max(lastVoice, int(it->intOr(F::VOICE_INDEX, 0)));
                }
            }
        }
        for (const Object* sp : spanners) {
            if (sp->idOr(F::ANCHOR) == staff.id()) {
                const Fraction t = sp->fractionOr(F::TIME, Fraction());
                const Fraction e = t + sp->fractionOr(F::DURATION, Fraction());
                if (measureAt(t) == m || measureEnding(e) == m) {
                    lastVoice = std::max(lastVoice, int(sp->intOr(F::VOICE_INDEX, 0)));
                }
            }
        }
        lastVoice = std::min(lastVoice, 3);
        for (int v = 0; v <= lastVoice; ++v) {
            Node vo = elem("voice");
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

    bool itemInVoice(const Object& it, const Object& staff, int v) const
    {
        const std::string sub = it.text(F::SUBKIND);
        if (sub == "frame" || sub == "measureDetail" || !it.has(F::VOICE_INDEX)) {
            return false;   // frames and measure-level items are written by the measure
        }
        const Id anchor = it.idOr(F::ANCHOR);
        const Id home = it.idOr(F::HOME_STAFF);
        bool here = anchor.isValid() ? anchor == staff.id() : (home.isValid() ? home == staff.id() : staffNumber.at(staff.id()) == 1);
        return here && int(it.intOr(F::VOICE_INDEX, 0)) == v;
    }

    void writeVoice(Node& out, const Object& staff, const Object* voice, int v, int m)
    {
        const Fraction mStart = measureStart[size_t(m)];
        const Fraction mEnd = mStart + measureLen[size_t(m)];
        std::vector<Entry> entries;
        for (const Object* it : items) {
            if (!itemInVoice(*it, staff, v)) {
                continue;
            }
            const Fraction t = it->fractionOr(F::TIME, Fraction());
            if (it->has(F::AT_END)) {
                if (measureEnding(t) == m) {
                    entries.push_back({ t, 4, it->intOr(F::SEQ, 0), it });
                }
            } else if (!(t < mStart) && t < mEnd) {
                entries.push_back({ t, 1, it->intOr(F::SEQ, 0), it });
            }
        }
        for (const Object* sp : spanners) {
            if (sp->idOr(F::ANCHOR) != staff.id() || int(sp->intOr(F::VOICE_INDEX, 0)) != v) {
                continue;
            }
            const Fraction t = sp->fractionOr(F::TIME, Fraction());
            const Fraction e = t + sp->fractionOr(F::DURATION, Fraction());
            if (!(t < mStart) && t < mEnd) {
                entries.push_back({ t, 2, sp->intOr(F::SEQ, 0), sp });
            }
            const int em = (e == mStart && m == 0) ? 0 : measureEnding(e);
            if (em == m && (mStart < e || (m == 0 && e == mStart))) {
                Entry end { e, e == mEnd ? 4 : 0, sp->intOr(F::SEQ, 0), sp, true };
                entries.push_back(end);
            }
        }
        std::vector<const Object*> events;
        if (voice) {
            for (Id c : voice->children()) {
                const Object* ev = s.find(c);
                if (ev->kind() == Kind::Tuplet) {
                    continue;
                }
                const Fraction t = ev->fractionOr(F::TIME, Fraction());
                if (!(t < mStart) && t < mEnd) {
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
            std::reverse(after.begin(), after.end());
            pending.insert(pending.end(), after.begin(), after.end());
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
            } else if (e.object) {
                moveTo(e.time);
                out.children.push_back(itemNode(*e.object));
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
                moveTo(e.time);
                for (size_t i = common; i < chain.size(); ++i) {
                    out.children.push_back(tupletNode(*s.find(chain[i])));
                    openTuplets.push_back(chain[i]);
                }
                for (const Object* ev : g) {
                    out.children.push_back(eventNode(*ev, m));
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
            n.children.push_back(leaf("duration", frac(ev.fractionOr(F::DURATION, measureLen[size_t(m)]))));
        } else {
            if (ev.intOr(F::DOTS, 0) > 0) {
                n.children.push_back(leaf("dots", std::to_string(ev.intOr(F::DOTS, 0))));
            }
            n.children.push_back(leaf("durationType", durationName(ev.fractionOr(F::BASE_DURATION, ev.fractionOr(F::DURATION, Fraction(1, 4))))));
            if (ev.has(F::EXPLICIT_DURATION)) {
                n.children.push_back(leaf("duration", frac(ev.fractionOr(F::DURATION, Fraction()) / factorOf(ev))));
            }
        }
        if (ev.has(F::GRACE)) {
            n.children.push_back(empty(ev.text(F::GRACE_TYPE).empty() ? std::string("acciaccatura") : ev.text(F::GRACE_TYPE)));
        }
        for (Id c : ev.children()) {   // in the model's order, which is the file's
            const Object* child = s.find(c);
            if (child->kind() == Kind::Attachment) {
                n.children.push_back(attachmentNode(*child));
            } else if (child->kind() == Kind::Note) {
                n.children.push_back(noteNode(*child, ev, m));
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
        const std::string tag = a.has(F::TAG) ? a.text(F::TAG) : "Articulation";
        Node n = elem(tag);
        addEid(n, a);
        if (tag == "Articulation" || tag == "Ornament") {
            n.children.push_back(leaf("subtype", xml::encode(sub)));
        }
        addDetails(n, a);
        finishNode(n);
        return n;
    }

    //! A relative location from (measure, position) of `from` to `to`
    Node location(const Fraction& from, const Fraction& to)
    {
        const int mf = measureAt(from), mt = measureAt(to);
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
                prev.children.push_back(location(at, fc->fractionOr(F::TIME, Fraction())));
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
                addDetails(tie, note, F::DETAILS_2);
                finishNode(tie);
                sp.children.push_back(std::move(tie));
                Node next = elem("next");
                next.children.push_back(location(at, tc->fractionOr(F::TIME, Fraction())));
                sp.children.push_back(std::move(next));
                n.children.push_back(std::move(sp));
            }
        }
        n.children.push_back(leaf("pitch", std::to_string(note.intOr(F::PITCH, 60))));
        if (note.has(F::TPC)) {
            n.children.push_back(leaf("tpc", std::to_string(note.intOr(F::TPC, 14))));
        }
        if (note.has(F::TPC2)) {
            n.children.push_back(leaf("tpc2", std::to_string(note.intOr(F::TPC2, 14))));
        }
        return n;
    }

    Node spannerStart(const Object& sp, int m)
    {
        (void)m;
        const std::string type = upperFirst(sp.text(F::SUBKIND));
        Node n = elem("Spanner");
        n.attrs.emplace_back("type", type);
        Node body = elem(type);
        addEid(body, sp);
        if (type == "Volta") {
            body.children.push_back(leaf("endings", xml::encode(sp.text(F::TEXT))));
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
        next.children.push_back(location(t, t + sp.fractionOr(F::DURATION, Fraction())));
        n.children.push_back(std::move(next));
        return n;
    }

    Node spannerEnd(const Object& sp, int m, const Fraction& at)
    {
        (void)m;
        Node n = elem("Spanner");
        n.attrs.emplace_back("type", upperFirst(sp.text(F::SUBKIND)));
        Node prev = elem("prev");
        // relative to where the marker is: the end of its measure when the spanner ends at a barline
        prev.children.push_back(location(at, sp.fractionOr(F::TIME, Fraction())));
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
        addEid(n, it);
        addDetails(n, it);
        if (tag == "Clef") {
            n.children.push_back(leaf("concertClefType", xml::encode(it.text(F::TEXT))));
        } else if (tag == "KeySig") {
            n.children.push_back(leaf("concertKey", std::to_string(it.intOr(F::INT_VALUE, 0))));
            if (!it.text(F::TEXT).empty()) {
                n.children.push_back(leaf("mode", xml::encode(it.text(F::TEXT))));
            }
        } else if (tag == "TimeSig") {
            n.children.push_back(leaf("sigN", std::to_string(it.intOr(F::TIMESIG_NUM, 4))));
            n.children.push_back(leaf("sigD", std::to_string(it.intOr(F::TIMESIG_DEN, 4))));
        } else if (tag == "Tempo") {
            const model::Value v = it.get(F::REAL_VALUE);
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.17g", std::holds_alternative<double>(v) ? std::get<double>(v) : 2.0);
            n.children.push_back(leaf("tempo", buf));
            n.children.push_back(leaf("text", it.text(F::TEXT)));
        } else if (tag == "Dynamic" || tag == "Fermata" || tag == "Breath") {
            n.children.push_back(leaf("subtype", xml::encode(it.text(F::TEXT))));
        } else if (tag == "Harmony") {
            if (it.has(F::INT_VALUE)) {
                n.children.push_back(leaf("root", std::to_string(it.intOr(F::INT_VALUE, 14))));
            }
            if (!it.text(F::TEXT).empty()) {
                n.children.push_back(leaf("name", xml::encode(it.text(F::TEXT))));
            }
        } else if (!it.text(F::TEXT).empty() || tag == "StaffText" || tag == "SystemText" || tag == "RehearsalMark") {
            n.children.push_back(leaf("text", it.text(F::TEXT)));
        }
        finishNode(n);
        return n;
    }

    const model::Snapshot& s;
    WriteReport& report;
    const Object* score = nullptr;
    const Object* flow = nullptr;
    std::vector<const Object*> players, staves, spanners, measures, items;
    std::map<Id, int> staffNumber;
    std::vector<Fraction> measureStart, measureLen;
    Fraction flowEnd;
    std::map<Id, Id> tieFrom;
};
}

ScoreFile generateScoreFile(const model::Snapshot& snapshot, WriteReport* report, const std::string& scoreName)
{
    WriteReport local;
    Writer w(snapshot, report ? *report : local);
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
    ScoreFile file = generateScoreFile(snapshot, report);
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
