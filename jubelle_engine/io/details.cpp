/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::io). Not part of MuseScore.
 */
#include "details.h"

#include <algorithm>
#include <set>
#include <sstream>

namespace F = je::model::fields;

namespace je::io {
namespace {
bool excluded(model::Field f)
{
    static const std::set<model::Field> skip { F::KIND, F::CHILDREN, F::PARENT, F::ORIGIN_EID, F::DETAILS, F::DETAILS_KEY,
                                                F::DETAILS_2, F::DETAILS_AFTER, F::TIE_EID, F::LINK_EID, F::LINK_MARK, F::TIE_LINK, F::SEQ, F::AT_END, F::TAG, F::HOME_STAFF, F::TIE_TO };
    return skip.count(f) > 0;
}

void feed(uint64_t& h, const std::string& s)
{
    for (unsigned char c : s) {   // FNV-1a
        h ^= c;
        h *= 1099511628211ULL;
    }
    h ^= 0xFF;
    h *= 1099511628211ULL;
}

std::string valueText(const model::Value& v)
{
    std::ostringstream o;
    std::visit([&o](const auto& x) {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            o << "-";
        } else if constexpr (std::is_same_v<T, model::Text>) {
            o << "s" << x.str();
        } else if constexpr (std::is_same_v<T, Fraction>) {
            o << "f" << x.toString();
        } else if constexpr (std::is_same_v<T, Id>) {
            o << "i" << x.toString();
        } else if constexpr (std::is_same_v<T, model::IdList>) {
            o << "l" << x.size();
        } else if constexpr (std::is_same_v<T, double>) {
            o.precision(17);
            o << "d" << x;
        } else {
            o << "n" << x;
        }
    }, v);
    return o.str();
}

int64_t finish(std::vector<std::pair<model::Field, std::string> >& parts)
{
    std::sort(parts.begin(), parts.end());
    uint64_t h = 1469598103934665603ULL;
    for (const auto& [f, v] : parts) {
        feed(h, std::to_string(f));
        feed(h, v);
    }
    return int64_t(h & 0x7FFFFFFFFFFFFFFFULL);
}
}

int64_t detailsKey(const FieldList& fields)
{
    std::vector<std::pair<model::Field, std::string> > parts;
    for (const auto& [f, v] : fields) {
        if (!excluded(f) && !model::isAbsent(v)) {
            parts.emplace_back(f, valueText(v));
        }
    }
    // a field given twice keeps its last value (as the object does)
    std::vector<std::pair<model::Field, std::string> > last;
    for (auto it = parts.rbegin(); it != parts.rend(); ++it) {
        if (std::none_of(last.begin(), last.end(), [&it](const auto& p) { return p.first == it->first; })) {
            last.push_back(*it);
        }
    }
    return finish(last);
}

int64_t detailsKey(const model::Object& object)
{
    std::vector<std::pair<model::Field, std::string> > parts;
    object.forEachField([&parts](model::Field f, const model::Value& v) {
        if (!excluded(f)) {
            parts.emplace_back(f, valueText(v));
        }
    });
    return finish(parts);
}

std::string fragment(const std::vector<const xml::Node*>& nodes)
{
    xml::Document d;
    d.hasRoot = true;
    d.root.name = "x";
    for (const xml::Node* n : nodes) {
        d.root.children.push_back(*n);
    }
    if (d.root.children.empty()) {
        return std::string();
    }
    return xml::write(d);
}

std::vector<xml::Node> parseFragment(const std::string& text)
{
    xml::Document d;
    std::string error;
    if (text.empty() || !xml::parse(text, d, error)) {
        return {};
    }
    return d.root.children;
}

std::string packPairs(const std::vector<std::pair<std::string, std::string> >& pairs)
{
    std::string out;
    for (const auto& [k, v] : pairs) {
        out += k + "\x1F" + v + "\x1E";   // control characters XML text can't hold, so values may have any text
    }
    return out;
}

std::vector<std::pair<std::string, std::string> > unpackPairs(const std::string& packed)
{
    std::vector<std::pair<std::string, std::string> > out;
    size_t at = 0;
    while (at < packed.size()) {
        const size_t nl = packed.find('\x1E', at);
        const std::string line = packed.substr(at, nl == std::string::npos ? std::string::npos : nl - at);
        const size_t tab = line.find('\x1F');
        if (tab != std::string::npos) {
            out.emplace_back(line.substr(0, tab), line.substr(tab + 1));
        }
        if (nl == std::string::npos) {
            break;
        }
        at = nl + 1;
    }
    return out;
}

std::string defaultScoreHead()
{
    xml::Node v;
    v.name = "programVersion";
    v.leaf = true;
    v.raw = "4.7.7";
    xml::Node r;
    r.name = "programRevision";
    r.leaf = true;
    const xml::Node* nodes[] = { &v, &r };
    static const std::string text = fragment({ nodes[0], nodes[1] });
    return text;
}
}
