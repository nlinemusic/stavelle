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
