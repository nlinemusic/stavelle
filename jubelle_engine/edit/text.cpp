/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle engine (je::edit): adding text, based on MuseScore Studio's Score::addText (src/engraving/editing/edit.cpp),
 * Score::createRehearsalMarkText / nextRehearsalMarkText (dom/score.cpp), TempoText::updateTempo and
 * Dynamic::setDynamicType, Copyright (C) MuseScore Limited and others, GPL-3.0-only.
 */
#include "commands.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

#include "structure.h"
#include "model/fields.h"
#include "model/fragment.h"

namespace F = je::model::fields;
using je::model::Kind;
using je::model::Object;
using je::model::Text;

namespace je::edit {
namespace {
//! The dynamics MuseScore knows by name (DynamicType, as written in files)
bool isDynamicName(const std::string& s)
{
    static const std::set<std::string> names { "pppppp", "ppppp", "pppp", "ppp", "pp", "p", "mp", "mf", "f", "ff", "fff", "ffff",
                                               "fffff", "ffffff", "fp", "pf", "sf", "sfz", "sff", "sffz", "sfff", "sfffz", "sfp",
                                               "sfpp", "rfz", "rf", "fz", "m", "r", "s", "z", "n" };
    return names.count(s) > 0;
}

bool isLetter(char c)
{
    return std::isalpha(static_cast<unsigned char>(c)) != 0;
}

bool isNumber(const std::string& s, int& n)
{
    if (s.empty() || s.size() > 9) {
        return false;
    }
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    n = std::stoi(s);
    return true;
}
}

std::string Commands::rehearsalMarkText(const Fraction& t) const
{
    // the marks just before and after, by time (any staff's copies: the last at a time)
    const Object* before = nullptr;
    const Object* after = nullptr;
    std::vector<const Object*> marks;
    m_ops.m.snapshot().forEachObject([&](const Object& o) {
        if (o.kind() == Kind::FlowItem && o.text(F::SUBKIND) == "rehearsalMark") {
            marks.push_back(&o);
        }
    });
    std::stable_sort(marks.begin(), marks.end(), [](const Object* a, const Object* b) {
        const Fraction ta = a->fractionOr(F::TIME, Fraction()), tb = b->fractionOr(F::TIME, Fraction());
        return ta != tb ? ta < tb : a->intOr(F::SEQ, 0) < b->intOr(F::SEQ, 0);
    });
    for (const Object* o : marks) {
        const Fraction mt = o->fractionOr(F::TIME, Fraction());
        if (mt < t) {
            before = o;
        } else if (mt > t && !after) {
            after = o;
        }
    }
    const std::string s1 = before ? before->text(F::TEXT) : std::string();
    const std::string s2 = after ? after->text(F::TEXT) : std::string();
    if (s1.empty()) {
        return "A";
    }
    // the measure number (Measure::no) of the measure at a time: irregular measures aren't counted
    auto measureNo = [&](const Fraction& at) {
        int no = 0;
        for (Id m : m_ops.measures()) {
            if (m_ops.measureStart(m) <= at && at < m_ops.measureEnd(m)) {
                return no;
            }
            no += m_ops.obj(m)->has(F::IRREGULAR) ? 0 : 1;
        }
        return no;
    };
    // nextRehearsalMarkText
    const std::string current;   // the new mark has no text yet
    std::string s;
    int n = 0;
    if (s1.size() == 1 && isLetter(s1[0])) {
        s = s1 == "Z" ? "AA" : s1 == "z" ? "aa" : std::string(1, char(s1[0] + 1));
    } else if (s1.size() == 2 && isLetter(s1[0]) && isLetter(s1[1])) {
        if (s1[0] == s1[1] && std::toupper(static_cast<unsigned char>(s1[0])) != 'Z') {
            s = std::string(2, char(s1[0] + 1));
        } else {
            s = current;
        }
    } else if (isNumber(s1, n)) {
        if (n == measureNo(before->fractionOr(F::TIME, Fraction())) + 1) {
            s = std::to_string(measureNo(t) + 1);   // measure numbers
        } else {
            s = std::to_string(n + 1);
        }
    } else {
        s = current;
    }
    if (s == current) {
        return s;
    }
    if (s == s2) {
        // the next in the sequence is already there
        if (isLetter(s1[0])) {
            s = s1.size() == 2 ? std::string(1, s1[0]) + char(s1[1] + 1) : s1 + "1";
        } else {
            s = s1 + "A";
        }
    }
    return s;
}

Id Commands::addText(Transaction& tx, const std::string& style, Id destination, const std::string& text)
{
    const Object* d = m_ops.obj(destination);
    if (!d) {
        return Id();
    }
    // fingerings go on a note
    static const std::set<std::string> fingerings { "fingering", "lh_guitar_fingering", "rh_guitar_fingering", "string_number" };
    if (fingerings.count(style)) {
        if (d->kind() != Kind::Note || text.empty()) {
            return Id();
        }
        // not on a tablature that doesn't show fingerings
        const Object* ev = m_ops.obj(d->parent());
        const Object* st = ev ? m_ops.obj(m_ops.staffOf(ev->parent())) : nullptr;
        if (st && st->text(F::SUBKIND) == "tablature" && st->text(F::DETAILS).find("<showTabFingering>1</showTabFingering>") == std::string::npos) {
            return Id();
        }
        std::vector<std::pair<Field, model::Value> > f { { F::SUBKIND, Text(std::string("fingering")) }, { F::TEXT, Text(text) } };
        if (style != "fingering") {
            f.push_back({ F::DETAILS, Text(model::fragment::withValues(std::string(), { { "style", style } })) });
        }
        return create(tx, m_ops.m_ids, destination, { Kind::Attachment, f });
    }
    // the others on the chord or rest (a note's chord; a grace note's own chord, at its main chord's time)
    Id event = d->kind() == Kind::Note ? d->parent() : destination;
    const Object* e = m_ops.obj(event);
    if (!e || (e->kind() != Kind::Chord && e->kind() != Kind::Rest)) {
        return Id();
    }
    const Id voice = e->parent();
    const Id staff = m_ops.staffOf(voice);
    const int64_t voiceIndex = m_ops.obj(voice)->intOr(F::NUMBER, 0);
    const Fraction t = e->fractionOr(F::TIME, Fraction());

    static const std::map<std::string, std::pair<std::string, std::string> > kinds {
        { "staff", { "staffText", "StaffText" } }, { "system", { "systemText", "SystemText" } },
        { "expression", { "expression", "Expression" } }, { "dynamics", { "dynamic", "Dynamic" } },
        { "rehearsal_mark", { "rehearsalMark", "RehearsalMark" } }, { "tempo", { "tempo", "Tempo" } },
    };
    auto k = kinds.find(style);
    if (k == kinds.end()) {
        return Id();
    }
    const std::string& subkind = k->second.first;
    const bool systemWide = subkind == "systemText" || subkind == "rehearsalMark" || subkind == "tempo";

    std::string content = text;
    std::vector<std::pair<Field, model::Value> > f { { F::SUBKIND, Text(subkind) }, { F::TIME, t }, { F::TAG, Text(std::string(k->second.second)) } };
    int64_t seq = 0;
    m_ops.m.snapshot().forEachObject([&](const Object& o) { seq = std::max(seq, o.intOr(F::SEQ, 0)); });
    f.push_back({ F::SEQ, seq + 1 });
    if (voiceIndex != 0) {
        f.push_back({ F::VOICE_INDEX, voiceIndex });
    }
    f.push_back({ systemWide ? F::HOME_STAFF : F::ANCHOR, staff });

    if (subkind == "rehearsalMark" && content.empty()) {
        content = rehearsalMarkText(t);
    }
    if (subkind == "tempo") {
        // the time signature's beat at 80 (MuseScore's default), and the tempo it says (TempoText::updateTempo)
        const Id m = m_ops.measureAt(t);
        const Object* mo = m_ops.obj(m);
        const int64_t num = mo ? mo->intOr(F::TIMESIG_NUM, 4) : 4, den = mo ? mo->intOr(F::TIMESIG_DEN, 4) : 4;
        const bool compound = num % 3 == 0;
        struct Beat {
            const char* text;
            double quarters;
        };
        Beat beat { "<sym>metNoteQuarterUp</sym> = 80", 1.0 };
        switch (den) {
        case 1: beat = { "<sym>metNoteWhole</sym> = 80", 4.0 };
            break;
        case 2: beat = { "<sym>metNoteHalfUp</sym> = 80", 2.0 };
            break;
        case 8: beat = compound ? Beat { "<sym>metNoteQuarterUp</sym><sym>space</sym><sym>metAugmentationDot</sym> = 80", 1.5 }
                                : Beat { "<sym>metNote8thUp</sym> = 80", 0.5 };
            break;
        case 16: beat = compound ? Beat { "<sym>metNote8thUp</sym><sym>space</sym><sym>metAugmentationDot</sym> = 80", 0.75 }
                                 : Beat { "<sym>metNote16thUp</sym> = 80", 0.25 };
            break;
        case 32: beat = compound ? Beat { "<sym>metNote16thUp</sym><sym>space</sym><sym>metAugmentationDot</sym> = 80", 0.375 }
                                 : Beat { "<sym>metNote32ndUp</sym> = 80", 0.125 };
            break;
        case 64: beat = compound ? Beat { "<sym>metNote32ndUp</sym><sym>space</sym><sym>metAugmentationDot</sym> = 80", 0.1875 }
                                 : Beat { "<sym>metNote64thUp</sym> = 80", 0.0625 };
            break;
        default:
            break;
        }
        if (content.empty()) {
            content = beat.text;
        }
        f.push_back({ F::REAL_VALUE, 80.0 * beat.quarters / 60.0 });
    }
    if (subkind == "dynamic") {
        // Dynamic::setDynamicType from the typed text: a dynamic's name becomes that dynamic
        if (content.empty()) {
            return Id();
        }
        // (Dynamic::parseDynamicText: the first word that is a dynamic's name, written with its symbols)
        auto wordChar = [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_'; };
        std::string type = "other-dynamics", xml = content;
        for (size_t i = 0; i < content.size();) {
            if (!wordChar(content[i])) {
                ++i;
                continue;
            }
            size_t j = i;
            while (j < content.size() && wordChar(content[j])) {
                ++j;
            }
            const std::string word = content.substr(i, j - i);
            const bool letters = word.find_first_not_of("fmnprsz") == std::string::npos;
            if (letters && (j == content.size() || content[j] != '>') && isDynamicName(word)) {
                static const std::map<char, std::string> glyph { { 'p', "Piano" }, { 'm', "Mezzo" }, { 'f', "Forte" },
                                                                 { 's', "Sforzando" }, { 'z', "Z" }, { 'r', "Rinforzando" },
                                                                 { 'n', "Niente" } };
                std::string symbols;
                for (char ch : word) {
                    symbols += "<sym>dynamic" + glyph.at(ch) + "</sym>";
                }
                type = word;
                xml = content.substr(0, i) + symbols + content.substr(j);
                break;
            }
            i = j;
        }
        f.push_back({ F::TEXT, Text(type) });
        if (type == "other-dynamics" || content != type) {
            f.push_back({ F::DETAILS, Text(model::fragment::withValues(std::string(), { { "text", xml } })) });
        }
    } else {
        if (content.empty()) {
            return Id();   // an empty text is taken away
        }
        f.push_back({ F::TEXT, Text(content) });
    }
    return create(tx, m_ops.m_ids, m_ops.obj(m_ops.measures().front())->parent(), { Kind::FlowItem, f }, Placement::ByTime);
}
}
