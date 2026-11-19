/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::io). Not part of MuseScore.
 *
 * A lossless XML tree for MuseScore files (S2). Text and attribute values are kept exactly as
 * written (still escaped), so a file read and written without changes comes back byte for
 * byte when it uses MuseScore's layout (two-space indent, one element per line, text inline).
 * An element whose content has text (rich text such as <text><b>A</b> b</text>) keeps its whole
 * inner content verbatim.
 */
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace je::io::xml {
struct Node {
    enum class Type : uint8_t { Element, Comment, Declaration };
    Type type = Type::Element;
    std::string name;                                          // element name; for others, the whole raw markup
    std::vector<std::pair<std::string, std::string> > attrs;   // in order, values escaped as in the file
    std::vector<Node> children;                                // elements and comments (a container)
    std::string raw;                                           // a leaf's inner content, verbatim
    bool leaf = false;                                         // no child elements, or mixed text: written inline
    bool selfClosing = false;                                  // <tag/>

    //! First child element with this name, or nullptr
    const Node* child(std::string_view n) const;
    Node* child(std::string_view n);
    //! The decoded text of a leaf child (empty if absent)
    std::string childText(std::string_view n) const;
    //! The decoded value of an attribute (empty if absent)
    std::string attr(std::string_view n) const;
    bool hasAttr(std::string_view n) const;
    //! Decoded text of this leaf
    std::string text() const;
};

struct Document {
    std::vector<Node> prolog;   // declaration, comments before the root
    Node root;
    std::string trailing;       // anything after the root (usually "\n")
    bool hasRoot = false;
};

//! Parses; on failure returns false and sets `error` (with the byte offset)
bool parse(std::string_view text, Document& doc, std::string& error);
//! Writes in MuseScore's layout
std::string write(const Document& doc);

//! Decodes &amp; &lt; &gt; &quot; &apos; and numeric references
std::string decode(std::string_view escaped);
//! Escapes for element text (& < > ") as MuseScore does
std::string encode(std::string_view plain);

//! Equal ignoring the whitespace between elements (for files written with another layout)
bool sameContent(const Document& a, const Document& b);
}
