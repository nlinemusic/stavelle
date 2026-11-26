/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle engine (je::model). Not part of MuseScore.
 *
 * Reading and changing the details fragments carried on objects (DETAILS, DETAILS_AFTER…: "<x>…</x>", the file's own
 * XML for what the model doesn't represent; decision D-E27). Only the fragment's top-level children are looked at.
 */
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace je::model::fragment {
//! An element directly inside a fragment: name and [begin, end) in the text (end = npos while unclosed)
struct Child {
    std::string name;
    size_t begin, end;
};

//! Finds the fragment's root content and its children; false if the text isn't a fragment
bool children(const std::string& d, size_t& contentBegin, size_t& contentEnd, std::vector<Child>& out);

//! The text of a child element (empty if there is none)
std::string value(const std::string& d, const std::string& tag);

//! Whether the fragment has a child element of that name
bool has(const std::string& d, const std::string& tag);

//! The fragment with these child elements set (an empty value: removed); empty when no child is left
std::string withValues(std::string d, const std::vector<std::pair<std::string, std::string> >& values);

//! The first value of a tag anywhere in a fragment, at any depth (a staff type's <lines>…)
std::string anyValue(const std::string& d, const std::string& tag);
}
