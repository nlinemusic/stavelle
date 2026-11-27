/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::layout). Not part of MuseScore.
 *
 * Key transposition follows MuseScore's Transpose::transposeKey, transposeTpc, clampKey and clampEnharmonic
 * (editing/transpose.cpp, dom/pitchspelling.cpp, GPL-3.0); a key signature's shown key KeySig::setKey and
 * TRead::read(KeySig).
 */
#include "context.h"

#include <algorithm>
#include <cstdlib>

#include "io/details.h"
#include "io/xml.h"
#include "model/fields.h"
#include "model/fragment.h"
#include "model/pitch.h"

namespace je::layout {
namespace F = model::fields;
using model::Kind;
using model::Object;

int clampEnharmonic(int tpc, bool useDoubleSharpsFlats)
{
    while (tpc > (useDoubleSharpsFlats ? TPC_MAX : TPC_B_S)) {
        tpc -= TPC_DELTA_ENHARMONIC;
    }
    while (tpc < (useDoubleSharpsFlats ? TPC_MIN : TPC_F_B)) {
        tpc += TPC_DELTA_ENHARMONIC;
    }
    return tpc;
}

int transposeTpc(int tpc, int diatonic, int chromatic, bool useDoubleSharpsFlats)
{
    const int delta = chromatic * TPC_DELTA_SEMITONE - diatonic * TPC_DELTA_ENHARMONIC;
    if (!tpcIsValid(tpc) || delta == 0) {
        return tpc;
    }
    return clampEnharmonic(tpc + delta, useDoubleSharpsFlats);
}

int transposeKey(int key, int diatonic, int chromatic, PreferSharpFlat prefer)
{
    int newKey = transposeTpc(key + 14, diatonic, chromatic, false) - 14;
    if (chromatic % 12 == 0 && diatonic % 7 == 0) {
        prefer = PreferSharpFlat::NONE;   // octave-transposing instruments
    }
    int smallest = -6, largest = 6;   // clampKey: G♭ to F♯ for AUTO
    if (prefer != PreferSharpFlat::AUTO) {
        smallest = prefer == PreferSharpFlat::SHARPS ? -4 : -7;
        largest = prefer == PreferSharpFlat::FLATS ? 4 : 7;
    }
    while (newKey < smallest) {
        newKey += 12;
    }
    while (newKey > largest) {
        newKey -= 12;
    }
    return newKey;
}

namespace {
SymId symByName(const std::string& n)
{
    static const std::map<std::string, SymId> names = [] {
        std::map<std::string, SymId> m;
        for (int i = 0; i < style::SYM_COUNT; ++i) {
            m.emplace(style::SYMBOL_CODES[i].name, SymId(i));
        }
        return m;
    }();
    const auto it = names.find(n);
    if (it != names.end()) {
        return it->second;
    }
    char* end = nullptr;
    const long v = std::strtol(n.c_str(), &end, 10);   // older files: the symbol's number
    return end && *end == 0 && !n.empty() && v >= 0 && v < style::SYM_COUNT ? SymId(v) : SymId::noSym;
}
}

ScoreContext::ScoreContext(const model::Snapshot& s)
    : m_s(s)
    , m_style(style::scoreStyle(s))
{
    m_font = style::MusicFont::byName(m_style.text(Sid::musicalSymbolFont));
    if (!m_font) {
        m_font = style::MusicFont::fallback();
    }
    m_concertPitch = m_style.boolean(Sid::concertPitch);
    m_version = style::formatVersion(s);
    const Object* score = s.find(s.score());
    if (!score) {
        return;
    }
    for (Id p : score->children()) {
        const Object* player = s.find(p);
        if (!player || player->kind() != Kind::Player) {
            continue;
        }
        std::vector<int> partStaves;
        for (Id st : player->children()) {
            const Object* staff = s.find(st);
            if (!staff || staff->kind() != Kind::Staff) {
                continue;
            }
            StaffInfo info;
            info.id = st;
            info.player = p;
            info.index = int(m_staves.size());
            info.group = staff->text(F::SUBKIND);
            info.shown = model::fragment::value(player->text(F::DETAILS), "show") != "0";
            info.drumset = model::fragment::value(player->text(F::DETAILS_2), "useDrumset") == "1"
                           || model::fragment::value(player->text(F::DETAILS), "useDrumset") == "1";
            {
                // the staff type (MuseScore's StaffType): its lines and step offset
                const std::string& d = staff->text(F::DETAILS);
                const std::string lines = model::fragment::anyValue(d, "lines"), step = model::fragment::anyValue(d, "stepOffset");
                if (!lines.empty()) {
                    info.lines = std::atoi(lines.c_str());
                }
                if (!step.empty()) {
                    info.stepOffset = std::atoi(step.c_str());
                }
            }
            info.voices.assign(4, Id());
            for (Id v : staff->children()) {
                const Object* voice = s.find(v);
                if (voice && voice->kind() == Kind::Voice) {
                    const int n = int(voice->intOr(F::NUMBER, 0));
                    if (n >= 0 && n < 4) {
                        info.voices[size_t(n)] = v;
                    }
                }
            }
            partStaves.push_back(info.index);
            m_staffIndex[st] = info.index;
            m_staves.push_back(info);
        }
        for (int i : partStaves) {
            m_staves[size_t(i)].partStaves = partStaves;
        }
    }
    for (Id c : score->children()) {
        const Object* flow = s.find(c);
        if (!flow || flow->kind() != Kind::Flow) {
            continue;
        }
        for (Id m : flow->children()) {
            const Object* measure = s.find(m);
            if (measure && measure->kind() == Kind::Measure) {
                m_measures.push_back({ m, measure->fractionOr(F::TIME, Fraction()), measure->fractionOr(F::DURATION, Fraction()) });
            }
        }
        break;
    }
    buildTimelines();
}

int ScoreContext::staffIndex(Id staff) const
{
    const auto it = m_staffIndex.find(staff);
    return it == m_staffIndex.end() ? -1 : it->second;
}

void ScoreContext::buildTimelines()
{
    const size_t n = m_staves.size();
    m_clefs.assign(n, {});
    m_staffTypes.assign(n, {});
    m_keys.assign(n, {});
    m_transpositions.assign(n, {});
    m_defaultClef.assign(n, "G");
    for (const StaffInfo& st : m_staves) {
        // Staff::clefType: the staff's own default
        const std::string& d = obj(st.id)->text(F::DETAILS);
        std::string clef;
        for (const char* tag : { m_concertPitch ? "defaultConcertClef" : "defaultTransposingClef", "defaultClef" }) {
            clef = model::fragment::value(d, tag);
            if (!clef.empty()) {
                break;
            }
        }
        m_defaultClef[size_t(st.index)] = !clef.empty() ? clef : st.group == "percussion" ? "PERC" : st.group == "tablature" ? "TAB" : "G";
        const Object* player = obj(st.player);
        m_transpositions[size_t(st.index)][Fraction(-1, 1)] = {
            int(player->intOr(F::TRANSPOSE_DIATONIC, model::diatonicOf(player->intOr(F::TRANSPOSE_CHROMATIC, 0)))),
            int(player->intOr(F::TRANSPOSE_CHROMATIC, 0)) };
    }
    const Object* score = obj(m_s.score());
    const Object* flow = nullptr;
    for (Id c : score->children()) {
        if (obj(c)->kind() == Kind::Flow) {
            flow = obj(c);
            break;
        }
    }
    if (!flow) {
        return;
    }
    // instrument changes first (key signatures are transposed by the instrument of their time)
    for (Id it : flow->children()) {
        const Object* item = obj(it);
        if (item->kind() != Kind::FlowItem || item->text(F::SUBKIND) != "instrumentChange" || !item->has(F::INSTRUMENT)) {
            continue;
        }
        const Object* anchor = obj(item->idOr(F::ANCHOR));
        if (!anchor) {
            continue;
        }
        const int chromatic = int(item->intOr(F::TRANSPOSE_CHROMATIC, 0));
        const Transposition tr { int(item->intOr(F::TRANSPOSE_DIATONIC, model::diatonicOf(chromatic))), chromatic };
        for (const StaffInfo& st : m_staves) {
            if (st.player == anchor->parent()) {
                m_transpositions[size_t(st.index)][item->fractionOr(F::TIME, Fraction())] = tr;
            }
        }
    }
    for (Id it : flow->children()) {
        const Object* item = obj(it);
        if (item->kind() != Kind::FlowItem) {
            continue;
        }
        const std::string& kind = item->text(F::SUBKIND);
        const Fraction t = item->fractionOr(F::TIME, Fraction());
        if (kind == "measureDetail") {
            const std::string& d = item->text(F::DETAILS);
            const std::string v = model::fragment::value(d, "stemless");
            const int st = staffIndex(item->idOr(F::ANCHOR));
            if (st >= 0 && !v.empty() && v != "0" && v != "false") {
                m_stemless.insert({ st, t });
            }
            if (st >= 0 && model::fragment::has(d, "StaffTypeChange")) {
                // a new staff type from here (MuseScore reads it as a fresh StaffType: 5 lines unless it says)
                const std::string change = model::fragment::value(d, "StaffTypeChange");
                const std::string lines = model::fragment::anyValue(change, "lines"), step = model::fragment::anyValue(change, "stepOffset");
                m_staffTypes[size_t(st)][t] = { lines.empty() ? 5 : std::atoi(lines.c_str()), step.empty() ? 0 : std::atoi(step.c_str()) };
            }
        } else if (kind == "clef") {
            const int st = staffIndex(item->idOr(F::ANCHOR));
            if (st < 0) {
                continue;
            }
            const std::string transposing = m_concertPitch ? std::string() : model::fragment::value(item->text(F::DETAILS), "transposingClefType");
            m_clefs[size_t(st)][t] = transposing.empty() ? item->text(F::TEXT) : transposing;
        } else if (kind == "keySig") {
            const std::string& d = item->text(F::DETAILS);
            for (const StaffInfo& st : m_staves) {
                if (item->has(F::ANCHOR) && item->idOr(F::ANCHOR) != st.id) {
                    continue;
                }
                if (m_keys[size_t(st.index)].count(t)) {
                    continue;   // of several at one time, the first
                }
                KeyEvent k;
                const int value = int(item->intOr(F::INT_VALUE, 0));
                const std::string actual = model::fragment::value(d, "actualKey");
                // TRead::read(KeySig): the key shown is <actualKey>, else <concertKey> (MuseScore writes the first only
                // where they differ); <accidental> is the key as written before 4.60, unknown since (the key is C)
                if (item->text(F::VALUE_TAG) == "accidental") {
                    k.key = m_version < 460 ? value : 0;
                } else {
                    k.key = actual.empty() ? value : std::atoi(actual.c_str());
                }
                k.custom = model::fragment::value(d, "custom") == "1";
                if (k.custom) {
                    for (const io::xml::Node& c : io::parseFragment(d)) {
                        if (c.name != "CustDef") {
                            continue;
                        }
                        KeyEvent::CustDef def;
                        def.sym = symByName(c.childText("sym"));
                        if (const io::xml::Node* dd = c.child("def")) {
                            def.degree = std::atoi(dd->attr("degree").c_str());
                        }
                        k.defs.push_back(def);
                    }
                }
                m_keys[size_t(st.index)][t] = k;
            }
        }
    }
}

const std::string& ScoreContext::clefAt(int staff, const Fraction& t) const
{
    const auto& clefs = m_clefs[size_t(staff)];
    auto it = clefs.upper_bound(t);
    if (it == clefs.begin()) {
        return m_defaultClef[size_t(staff)];
    }
    return std::prev(it)->second;
}

const style::ClefDef& ScoreContext::clefInfo(const std::string& name) const
{
    for (const style::ClefDef* c = style::CLEFS; c->name; ++c) {
        if (name == c->name) {
            return *c;
        }
    }
    return style::CLEFS[0];
}

const KeyEvent& ScoreContext::keyAt(int staff, const Fraction& t) const
{
    const auto& keys = m_keys[size_t(staff)];
    auto it = keys.upper_bound(t);
    if (it == keys.begin()) {
        return m_noKey;
    }
    return std::prev(it)->second;
}

std::vector<Fraction> ScoreContext::keyChanges(int staff, const Fraction& from, const Fraction& to) const
{
    std::vector<Fraction> r;
    const auto& keys = m_keys[size_t(staff)];
    for (auto it = keys.lower_bound(from); it != keys.end() && it->first < to; ++it) {
        r.push_back(it->first);
    }
    return r;
}

ScoreContext::Transposition ScoreContext::transpositionAt(int staff, const Fraction& t) const
{
    const auto& trs = m_transpositions[size_t(staff)];
    auto it = trs.upper_bound(t);
    if (it == trs.begin()) {
        return Transposition();
    }
    return std::prev(it)->second;
}
}

namespace je::layout {
int ScoreContext::middleLine(int staff, const Fraction& t) const
{
    const StaffInfo& st = m_staves[size_t(staff)];
    int lines = st.lines, step = st.stepOffset;
    if (size_t(staff) < m_staffTypes.size()) {
        const auto& m = m_staffTypes[size_t(staff)];
        auto it = m.upper_bound(t);
        if (it != m.begin()) {
            --it;
            lines = it->second.first;
            step = it->second.second;
        }
    }
    return lines - 1 - step;
}

bool ScoreContext::stemlessMeasure(int staff, const Fraction& t) const
{
    if (m_stemless.empty()) {
        return false;
    }
    auto it = std::upper_bound(m_measures.begin(), m_measures.end(), t, [](const Fraction& x, const MeasureInfo& m) { return x < m.start; });
    if (it == m_measures.begin()) {
        return false;
    }
    --it;
    return m_stemless.count({ staff, it->start }) != 0;
}
}
