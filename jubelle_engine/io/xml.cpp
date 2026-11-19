/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::io). Not part of MuseScore.
 */
#include "xml.h"

#include <cstring>

namespace je::io::xml {
namespace {
bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }
bool isNameChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'
           || c == ':' || (unsigned char)c >= 0x80;
}

class Parser
{
public:
    explicit Parser(std::string_view t)
        : s(t) {}

    bool document(Document& doc)
    {
        if (s.size() >= 3 && (unsigned char)s[0] == 0xEF && (unsigned char)s[1] == 0xBB && (unsigned char)s[2] == 0xBF) {
            fail("a byte-order mark (not supported)");
            return false;
        }
        while (true) {
            skipSpace();
            if (pos >= s.size()) {
                break;
            }
            if (startsWith("<?") || startsWith("<!--") || startsWith("<!DOCTYPE")) {
                Node n;
                if (!special(n)) {
                    return false;
                }
                (doc.hasRoot ? trailingNodes : doc.prolog).push_back(std::move(n));
                continue;
            }
            if (s[pos] == '<' && !doc.hasRoot) {
                if (!element(doc.root)) {
                    return false;
                }
                doc.hasRoot = true;
                trailingStart = pos;
                continue;
            }
            fail("unexpected content outside the root element");
            return false;
        }
        if (!doc.hasRoot) {
            fail("no root element");
            return false;
        }
        doc.trailing = std::string(s.substr(trailingStart));
        if (!trailingNodes.empty()) {
            fail("markup after the root element (not supported)");
            return false;
        }
        return true;
    }

    std::string error;

private:
    bool startsWith(const char* p) const { return s.compare(pos, std::strlen(p), p) == 0; }
    void skipSpace()
    {
        while (pos < s.size() && isSpace(s[pos])) {
            ++pos;
        }
    }
    void fail(const std::string& what)
    {
        if (error.empty()) {
            error = what + " at byte " + std::to_string(pos);
        }
    }

    bool special(Node& n)
    {
        const char* end = startsWith("<?") ? "?>" : startsWith("<!--") ? "-->" : ">";
        n.type = startsWith("<!--") ? Node::Type::Comment : Node::Type::Declaration;
        const size_t e = s.find(end, pos);
        if (e == std::string_view::npos) {
            fail("unterminated markup");
            return false;
        }
        n.name = std::string(s.substr(pos, e + std::strlen(end) - pos));
        pos = e + std::strlen(end);
        return true;
    }

    bool name(std::string& out)
    {
        const size_t b = pos;
        while (pos < s.size() && isNameChar(s[pos])) {
            ++pos;
        }
        if (pos == b) {
            fail("expected a name");
            return false;
        }
        out = std::string(s.substr(b, pos - b));
        return true;
    }

    bool element(Node& n)
    {
        ++pos;   // '<'
        if (!name(n.name)) {
            return false;
        }
        while (true) {
            skipSpace();
            if (pos >= s.size()) {
                fail("unterminated tag");
                return false;
            }
            if (startsWith("/>")) {
                pos += 2;
                n.selfClosing = true;
                n.leaf = true;
                return true;
            }
            if (s[pos] == '>') {
                ++pos;
                break;
            }
            std::string an;
            if (!name(an)) {
                return false;
            }
            skipSpace();
            if (pos >= s.size() || s[pos] != '=') {
                fail("expected = after an attribute name");
                return false;
            }
            ++pos;
            skipSpace();
            if (pos >= s.size() || (s[pos] != '"' && s[pos] != '\'')) {
                fail("expected a quoted attribute value");
                return false;
            }
            const char q = s[pos++];
            const size_t e = s.find(q, pos);
            if (e == std::string_view::npos) {
                fail("unterminated attribute value");
                return false;
            }
            n.attrs.emplace_back(std::move(an), std::string(s.substr(pos, e - pos)));
            pos = e + 1;
        }

        const size_t contentStart = pos;
        bool hasText = false;
        bool hasElements = false;
        while (true) {
            if (pos >= s.size()) {
                fail("unterminated element <" + n.name + ">");
                return false;
            }
            if (s[pos] == '<') {
                if (startsWith("</")) {
                    const size_t contentEnd = pos;
                    pos += 2;
                    std::string closing;
                    if (!name(closing) || closing != n.name) {
                        fail("mismatched closing tag for <" + n.name + ">");
                        return false;
                    }
                    skipSpace();
                    if (pos >= s.size() || s[pos] != '>') {
                        fail("malformed closing tag");
                        return false;
                    }
                    ++pos;
                    if (hasText || !hasElements) {
                        n.leaf = true;
                        n.raw = std::string(s.substr(contentStart, contentEnd - contentStart));
                        n.children.clear();
                    }
                    return true;
                }
                if (startsWith("<![CDATA[")) {
                    const size_t e = s.find("]]>", pos);
                    if (e == std::string_view::npos) {
                        fail("unterminated CDATA");
                        return false;
                    }
                    pos = e + 3;
                    hasText = true;
                    continue;
                }
                if (startsWith("<!--") || startsWith("<?")) {
                    Node c;
                    if (!special(c)) {
                        return false;
                    }
                    n.children.push_back(std::move(c));
                    hasElements = true;
                    continue;
                }
                Node c;
                if (!element(c)) {
                    return false;
                }
                n.children.push_back(std::move(c));
                hasElements = true;
                continue;
            }
            if (!isSpace(s[pos])) {
                hasText = true;
            }
            ++pos;
        }
    }

    std::string_view s;
    size_t pos = 0;
    size_t trailingStart = 0;
    std::vector<Node> trailingNodes;
};

void writeNode(const Node& n, int depth, std::string& out)
{
    out.append(size_t(depth) * 2, ' ');
    if (n.type != Node::Type::Element) {
        out += n.name;
        out += '\n';
        return;
    }
    out += '<';
    out += n.name;
    for (const auto& [k, v] : n.attrs) {
        out += ' ';
        out += k;
        out += "=\"";
        out += v;
        out += '"';
    }
    if (n.selfClosing) {
        out += "/>\n";
        return;
    }
    if (n.leaf) {
        out += '>';
        out += n.raw;
        out += "</";
        out += n.name;
        out += ">\n";
        return;
    }
    out += ">\n";
    for (const Node& c : n.children) {
        writeNode(c, depth + 1, out);
    }
    out.append(size_t(depth + 1) * 2, ' ');   // MuseScore indents a closing tag like the element's children
    out += "</";
    out += n.name;
    out += ">\n";
}

bool sameNode(const Node& a, const Node& b)
{
    if (a.type != b.type || a.name != b.name || a.attrs != b.attrs || a.leaf != b.leaf || a.children.size() != b.children.size()) {
        return false;
    }
    if (a.leaf) {
        return a.selfClosing == b.selfClosing ? a.raw == b.raw : (a.raw.empty() && b.raw.empty());
    }
    for (size_t i = 0; i < a.children.size(); ++i) {
        if (!sameNode(a.children[i], b.children[i])) {
            return false;
        }
    }
    return true;
}
}

const Node* Node::child(std::string_view n) const
{
    for (const Node& c : children) {
        if (c.type == Type::Element && c.name == n) {
            return &c;
        }
    }
    return nullptr;
}

Node* Node::child(std::string_view n)
{
    for (Node& c : children) {
        if (c.type == Type::Element && c.name == n) {
            return &c;
        }
    }
    return nullptr;
}

std::string Node::childText(std::string_view n) const
{
    const Node* c = child(n);
    return c ? c->text() : std::string();
}

std::string Node::attr(std::string_view n) const
{
    for (const auto& [k, v] : attrs) {
        if (k == n) {
            return decode(v);
        }
    }
    return std::string();
}

bool Node::hasAttr(std::string_view n) const
{
    for (const auto& a : attrs) {
        if (a.first == n) {
            return true;
        }
    }
    return false;
}

std::string Node::text() const
{
    return leaf ? decode(raw) : std::string();
}

bool parse(std::string_view text, Document& doc, std::string& error)
{
    doc = Document();
    Parser p(text);
    if (!p.document(doc)) {
        error = p.error;
        return false;
    }
    return true;
}

std::string write(const Document& doc)
{
    std::string out;
    for (const Node& n : doc.prolog) {
        writeNode(n, 0, out);
    }
    if (doc.hasRoot) {
        writeNode(doc.root, 0, out);
        // writeNode ends the root with "\n"; keep the file's own ending
        out.pop_back();
        out += doc.trailing;
    }
    return out;
}

std::string decode(std::string_view e)
{
    std::string out;
    out.reserve(e.size());
    for (size_t i = 0; i < e.size(); ++i) {
        if (e[i] != '&') {
            out += e[i];
            continue;
        }
        const size_t semi = e.find(';', i);
        if (semi == std::string_view::npos) {
            out += e[i];
            continue;
        }
        const std::string_view ent = e.substr(i + 1, semi - i - 1);
        if (ent == "amp") {
            out += '&';
        } else if (ent == "lt") {
            out += '<';
        } else if (ent == "gt") {
            out += '>';
        } else if (ent == "quot") {
            out += '"';
        } else if (ent == "apos") {
            out += '\'';
        } else if (!ent.empty() && ent[0] == '#') {
            unsigned long cp = 0;
            try {
                cp = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X') ? std::stoul(std::string(ent.substr(2)), nullptr, 16)
                                                                         : std::stoul(std::string(ent.substr(1)));
            } catch (...) {
                out += e[i];
                continue;
            }
            // UTF-8
            if (cp < 0x80) {
                out += char(cp);
            } else if (cp < 0x800) {
                out += char(0xC0 | (cp >> 6));
                out += char(0x80 | (cp & 0x3F));
            } else if (cp < 0x10000) {
                out += char(0xE0 | (cp >> 12));
                out += char(0x80 | ((cp >> 6) & 0x3F));
                out += char(0x80 | (cp & 0x3F));
            } else {
                out += char(0xF0 | (cp >> 18));
                out += char(0x80 | ((cp >> 12) & 0x3F));
                out += char(0x80 | ((cp >> 6) & 0x3F));
                out += char(0x80 | (cp & 0x3F));
            }
        } else {
            out += e[i];
            continue;
        }
        i = semi;
    }
    return out;
}

std::string encode(std::string_view p)
{
    std::string out;
    out.reserve(p.size());
    for (char c : p) {
        switch (c) {
        case '&': out += "&amp;";
            break;
        case '<': out += "&lt;";
            break;
        case '>': out += "&gt;";
            break;
        case '"': out += "&quot;";
            break;
        default: out += c;
        }
    }
    return out;
}

bool sameContent(const Document& a, const Document& b)
{
    if (a.hasRoot != b.hasRoot || a.prolog.size() != b.prolog.size()) {
        return false;
    }
    for (size_t i = 0; i < a.prolog.size(); ++i) {
        if (a.prolog[i].name != b.prolog[i].name) {
            return false;
        }
    }
    return !a.hasRoot || sameNode(a.root, b.root);
}
}
