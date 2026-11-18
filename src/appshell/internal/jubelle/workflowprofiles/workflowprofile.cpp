/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore). Workflow Profiles.
 */
#include "workflowprofile.h"

#include <algorithm>
#include <functional>
#include <set>
#include <tuple>

#include "global/serialization/json.h"

namespace mu::appshell::jubelle::workflow {
const std::vector<std::string> BEHAVIOUR_KEYS = {
    "noteInputMethod",
    "accidentalsApplyToNextNote",
    "noteInputCursorInInputByDuration",
    "selectionProximity",
};

namespace {
bool validId(const std::string& id)
{
    if (id.empty() || id.size() > 40) {
        return false;
    }
    return std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    });
}

std::string valueAsString(const muse::JsonValue& v)
{
    if (v.isBool()) {
        return v.toBool() ? "true" : "false";
    }
    if (v.isNumber()) {
        return std::to_string(v.toInt());
    }
    if (v.isString()) {
        return v.toStdString();
    }
    return std::string();
}

// (a missing or wrong-typed value reads as empty, never as a crash)
muse::JsonObject objectOf(const muse::JsonValue& v)
{
    return v.isObject() ? v.toObject() : muse::JsonObject();
}

std::vector<std::string> stringList(const muse::JsonValue& v)
{
    std::vector<std::string> out;
    if (!v.isArray()) {
        return out;
    }
    const muse::JsonArray a = v.toArray();
    for (size_t i = 0; i < a.size(); ++i) {
        if (a.at(i).isString()) {
            out.push_back(a.at(i).toStdString());
        }
    }
    return out;
}

//! Commands that work only in one situation keep their keys whatever a
//! profile does: keyboard navigation (only when a control has focus) and the
//! chord-symbol (-TEXT) and guitar-tab (-TAB) variants. A profile that gives
//! one of their keys to another command shows up as a clash in the tests.
bool keepsItsKeys(const std::string& action)
{
    auto endsWith = [&action](const char* tail) {
        const size_t n = std::char_traits<char>::length(tail);
        return action.size() >= n && action.compare(action.size() - n, n, tail) == 0;
    };
    return action.rfind("nav-", 0) == 0 || endsWith("-TEXT") || endsWith("-TAB");
}

//! The -TAB and -TEXT commands are defined with the general scope but only
//! work on guitar-tab staves and while typing chord symbols: they are treated
//! as being in a scope of their own
std::string effectiveScope(const std::string& action, const std::string& scope)
{
    auto endsWith = [&action](const char* tail) {
        const size_t n = std::char_traits<char>::length(tail);
        return action.size() >= n && action.compare(action.size() - n, n, tail) == 0;
    };
    if (endsWith("-TAB")) {
        return "jubelle-tab-commands";
    }
    if (endsWith("-TEXT")) {
        return "jubelle-chord-symbol-commands";
    }
    return scope;
}

bool isGenericScope(const std::string& s)
{
    return s.empty() || s == "any" || s == "project-opened" || s == "project-focused"
           || s == "notation-not-note-input-staff-tab";
}
}

Profile parseProfile(const std::string& json, std::string* error)
{
    auto fail = [error](const std::string& why) {
        if (error) {
            *error = why;
        }
        return Profile();
    };
    if (error) {
        error->clear();
    }
    std::string err;
    const muse::JsonDocument doc = muse::JsonDocument::fromJson(muse::ByteArray(json.data(), json.size()), &err);
    if (!err.empty() || !doc.isObject()) {
        return fail("not valid JSON: " + err);
    }
    const muse::JsonObject o = doc.rootObject();
    if (o.value("schema").toStdString() != PROFILE_SCHEMA) {
        return fail(std::string("schema must be ") + PROFILE_SCHEMA);
    }
    Profile p;
    p.id = o.value("id").toStdString();
    p.name = o.value("name").toStdString();
    p.description = o.value("description").toStdString();
    if (!validId(p.id)) {
        return fail("id must be lower-case letters, digits and '-'");
    }
    if (p.name.empty()) {
        return fail("a profile needs a name");
    }

    const muse::JsonObject sc = objectOf(o.value("shortcuts"));
    for (const std::string& action : sc.keys()) {
        if (action.empty()) {
            return fail("a shortcut has no command");
        }
        const muse::JsonValue v = sc.value(action);
        if (!v.isArray()) {
            return fail("shortcuts for " + action + " must be a list");
        }
        p.shortcuts.emplace_back(action, stringList(v));
    }

    const muse::JsonValue menusValue = o.value("menus");
    const muse::JsonArray menus = menusValue.isArray() ? menusValue.toArray() : muse::JsonArray();
    std::set<std::string> menuIds;
    for (size_t i = 0; i < menus.size(); ++i) {
        const muse::JsonObject m = objectOf(menus.at(i));
        TopMenu t;
        t.id = m.value("id").toStdString();
        t.title = m.value("title").toStdString();
        t.entries = stringList(m.value("entries"));
        t.takes = stringList(m.value("takes"));
        if (t.id.empty() || t.title.empty()) {
            return fail("every menu needs an id and a title");
        }
        if (!menuIds.insert(t.id).second) {
            return fail("menu " + t.id + " appears twice");
        }
        p.menus.push_back(std::move(t));
    }

    p.toolbar = stringList(o.value("toolbar"));

    const muse::JsonObject b = objectOf(o.value("behaviour"));
    for (const std::string& key : b.keys()) {
        if (std::find(BEHAVIOUR_KEYS.begin(), BEHAVIOUR_KEYS.end(), key) == BEHAVIOUR_KEYS.end()) {
            return fail("unknown behaviour setting " + key);
        }
        p.behaviour[key] = valueAsString(b.value(key));
    }
    return p;
}

// ---------------------------------------------------------------------------
// Shortcuts
// ---------------------------------------------------------------------------

bool scopesOverlap(const std::string& a, const std::string& b)
{
    if (a == "disabled" || b == "disabled") {
        return false;
    }
    if (a == b) {
        return true;
    }
    return isGenericScope(a) && isGenericScope(b);
}

std::vector<ActionShortcut> resolveShortcuts(const std::vector<ActionShortcut>& defaults, const Profile& profile)
{
    std::map<std::string, ActionShortcut> def;
    std::vector<std::string> order;
    for (const ActionShortcut& d : defaults) {
        if (def.emplace(d.action, d).second) {
            order.push_back(d.action);
        }
    }
    std::map<std::string, ActionShortcut> fin = def;
    std::set<std::string> overridden;
    for (const auto& o : profile.shortcuts) {
        auto it = fin.find(o.first);
        if (it == fin.end()) {
            it = fin.emplace(o.first, ActionShortcut { o.first, {}, "project-opened" }).first;
            def.emplace(o.first, ActionShortcut { o.first, {}, "project-opened" });
            order.push_back(o.first);
        }
        it->second.sequences = o.second;
        overridden.insert(o.first);
    }
    // a key a profile gives to a command is taken from other commands that
    // could be active at the same time
    for (const auto& o : profile.shortcuts) {
        const std::string scope = effectiveScope(o.first, fin[o.first].scope);
        for (const std::string& seq : o.second) {
            for (auto& f : fin) {
                if (f.first == o.first || overridden.count(f.first)) {
                    continue;
                }
                if (keepsItsKeys(f.first)) {
                    continue;
                }
                if (!scopesOverlap(scope, effectiveScope(f.first, f.second.scope))) {
                    continue;
                }
                auto& seqs = f.second.sequences;
                seqs.erase(std::remove(seqs.begin(), seqs.end(), seq), seqs.end());
            }
        }
    }

    // the user list: changed commands, plus commands that keep a key a user
    // entry uses (else the merge would remove it from them)
    std::set<std::string> user;
    for (const auto& f : fin) {
        if (f.second.sequences != def[f.first].sequences) {
            user.insert(f.first);
        }
    }
    bool grew = true;
    while (grew) {
        grew = false;
        std::set<std::string> userSeqs;
        for (const std::string& a : user) {
            userSeqs.insert(fin[a].sequences.begin(), fin[a].sequences.end());
        }
        for (const auto& f : fin) {
            if (user.count(f.first)) {
                continue;
            }
            for (const std::string& s : f.second.sequences) {
                if (userSeqs.count(s)) {
                    user.insert(f.first);
                    grew = true;
                    break;
                }
            }
        }
    }
    std::vector<ActionShortcut> out;
    for (const std::string& a : order) {
        if (user.count(a)) {
            out.push_back(fin[a]);
        }
    }
    return out;
}

std::map<std::string, std::vector<std::string> > effectiveShortcuts(const std::vector<ActionShortcut>& defaults,
                                                                     const std::vector<ActionShortcut>& user)
{
    // as ShortcutsRegister::mergeShortcuts: a user entry replaces the default
    // of its command; a default loses any key that a user entry uses
    std::map<std::string, std::vector<std::string> > out;
    std::set<std::string> userSeqs;
    for (const ActionShortcut& u : user) {
        out[u.action] = u.sequences;
        userSeqs.insert(u.sequences.begin(), u.sequences.end());
    }
    for (const ActionShortcut& d : defaults) {
        if (out.count(d.action)) {
            continue;
        }
        std::vector<std::string> seqs;
        for (const std::string& s : d.sequences) {
            if (!userSeqs.count(s)) {
                seqs.push_back(s);
            }
        }
        out[d.action] = seqs;
    }
    return out;
}

std::vector<Clash> findNewClashes(const std::vector<ActionShortcut>& defaults,
                                  const std::map<std::string, std::vector<std::string> >& effective)
{
    std::map<std::string, std::string> scope;
    std::set<std::tuple<std::string, std::string, std::string> > before;
    std::map<std::string, std::vector<std::string> > bySeqDefault;
    for (const ActionShortcut& d : defaults) {
        scope[d.action] = effectiveScope(d.action, d.scope);
        for (const std::string& s : d.sequences) {
            bySeqDefault[s].push_back(d.action);
        }
    }
    for (const auto& e : bySeqDefault) {
        for (size_t i = 0; i < e.second.size(); ++i) {
            for (size_t j = 0; j < e.second.size(); ++j) {
                if (i != j) {
                    before.insert({ e.first, e.second[i], e.second[j] });
                }
            }
        }
    }
    std::map<std::string, std::vector<std::string> > bySeq;
    for (const auto& e : effective) {
        for (const std::string& s : e.second) {
            bySeq[s].push_back(e.first);
        }
    }
    auto scopeOf = [&scope](const std::string& a) {
        auto it = scope.find(a);
        return it == scope.end() ? std::string("project-opened") : it->second;
    };
    std::vector<Clash> out;
    for (const auto& e : bySeq) {
        for (size_t i = 0; i < e.second.size(); ++i) {
            for (size_t j = i + 1; j < e.second.size(); ++j) {
                const std::string& a = e.second[i];
                const std::string& b = e.second[j];
                if (!scopesOverlap(scopeOf(a), scopeOf(b))) {
                    continue;
                }
                if (before.count({ e.first, a, b })) {
                    continue;
                }
                out.push_back({ e.first, a, b });
            }
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------

namespace {
struct MenuIndex {
    std::map<std::string, const MenuNode*> byId;
    void add(const MenuNode& n)
    {
        if (!n.id.empty() && !n.isSeparator) {
            byId.emplace(n.id, &n);
        }
        for (const MenuNode& c : n.children) {
            add(c);
        }
    }
};

void markPlaced(const MenuNode& n, std::set<std::string>& placed)
{
    if (!n.id.empty() && !n.isSeparator) {
        placed.insert(n.id);
    }
    for (const MenuNode& c : n.children) {
        markPlaced(c, placed);
    }
}

bool hasContent(const std::vector<MenuNode>& nodes)
{
    return std::any_of(nodes.begin(), nodes.end(), [](const MenuNode& n) { return !n.isSeparator; });
}

//! A copy without anything already placed (submenus left empty are dropped,
//! except submenus that were empty to begin with, such as Open recent)
std::vector<MenuNode> pruned(const std::vector<MenuNode>& nodes, const std::set<std::string>& placed)
{
    std::vector<MenuNode> out;
    for (const MenuNode& n : nodes) {
        if (n.isSeparator) {
            out.push_back(n);
            continue;
        }
        if (!n.id.empty() && placed.count(n.id)) {
            continue;
        }
        if (!n.isMenu) {
            out.push_back(n);
            continue;
        }
        MenuNode copy = n;
        copy.children = pruned(n.children, placed);
        if (hasContent(copy.children) || n.children.empty()) {
            out.push_back(std::move(copy));
        }
    }
    return out;
}

void tidySeparators(std::vector<MenuNode>& nodes)
{
    std::vector<MenuNode> out;
    for (MenuNode& n : nodes) {
        if (n.isSeparator && (out.empty() || out.back().isSeparator)) {
            continue;
        }
        if (n.isMenu) {
            tidySeparators(n.children);
        }
        out.push_back(std::move(n));
    }
    while (!out.empty() && out.back().isSeparator) {
        out.pop_back();
    }
    nodes = std::move(out);
}

MenuNode separator()
{
    MenuNode s;
    s.isSeparator = true;
    return s;
}

//! Adds nodes where the menu has its "*" entry, else at the end
void insertRest(MenuNode& menu, std::vector<MenuNode>&& nodes)
{
    auto marker = std::find_if(menu.children.begin(), menu.children.end(), [](const MenuNode& n) { return n.id == "*"; });
    std::vector<MenuNode> block;
    block.push_back(separator());
    for (MenuNode& n : nodes) {
        block.push_back(std::move(n));
    }
    block.push_back(separator());
    menu.children.insert(marker, std::make_move_iterator(block.begin()), std::make_move_iterator(block.end()));
}

void removeMarkers(std::vector<MenuNode>& nodes)
{
    nodes.erase(std::remove_if(nodes.begin(), nodes.end(), [](const MenuNode& n) { return n.id == "*" && !n.isMenu; }),
                nodes.end());
}

void collectCommands(const std::vector<MenuNode>& nodes, std::set<std::string>& out)
{
    for (const MenuNode& n : nodes) {
        if (!n.isSeparator && !n.isMenu && !n.id.empty()) {
            out.insert(n.id);
        }
        collectCommands(n.children, out);
    }
}
}

MenuResolution resolveMenus(const std::vector<MenuNode>& source, const Profile& profile)
{
    MenuResolution res;
    if (!profile.hasMenuLayout()) {
        res.menus = source;
        return res;
    }
    MenuIndex index;
    std::map<std::string, const MenuNode*> topById;
    for (const MenuNode& top : source) {
        index.add(top);
        if (!top.id.empty()) {
            topById.emplace(top.id, &top);
        }
    }

    std::set<std::string> placed;
    std::vector<MenuNode> menus;

    // 1. what the profile places by name
    for (const TopMenu& t : profile.menus) {
        MenuNode m;
        m.id = t.id;
        m.title = t.title;
        m.isMenu = true;
        auto same = topById.find(t.id);
        if (same != topById.end()) {
            m.source = same->second->source;   // keeps the menu's icon
        }
        for (const std::string& e : t.entries) {
            if (e == "-") {
                m.children.push_back(separator());
                continue;
            }
            if (e == "*") {
                MenuNode rest;
                rest.id = "*";   // where the taken contents go
                m.children.push_back(rest);
                continue;
            }
            auto it = index.byId.find(e);
            if (it == index.byId.end()) {
                res.unknownEntries.push_back(e);
                continue;
            }
            if (placed.count(e)) {
                continue;   // already placed elsewhere in this profile
            }
            m.children.push_back(*it->second);
            markPlaced(*it->second, placed);
        }
        menus.push_back(std::move(m));
    }

    // 2. what each menu takes over from Jubelle's menus (a top-level menu or
    // any submenu, whose remaining contents are added without the submenu)
    std::set<std::string> consumed;
    for (size_t i = 0; i < profile.menus.size(); ++i) {
        for (const std::string& takenId : profile.menus[i].takes) {
            auto it = index.byId.find(takenId);
            if (it == index.byId.end() || !it->second->isMenu) {
                res.unknownEntries.push_back(takenId);
                continue;
            }
            std::vector<MenuNode> rest = pruned(it->second->children, placed);
            if (hasContent(rest)) {
                for (const MenuNode& n : rest) {
                    markPlaced(n, placed);
                }
                insertRest(menus[i], std::move(rest));
            }
            if (topById.count(takenId)) {
                consumed.insert(takenId);
            } else {
                placed.insert(takenId);   // the submenu itself is now empty
            }
        }
    }

    // 3. nothing is lost: what is still unplaced goes to the menu with the
    // same id, else a copy of its Jubelle menu is added before Help
    for (const MenuNode& top : source) {
        if (consumed.count(top.id)) {
            continue;
        }
        std::vector<MenuNode> rest = pruned(top.children, placed);
        if (!hasContent(rest)) {
            continue;
        }
        auto sameId = std::find_if(menus.begin(), menus.end(), [&top](const MenuNode& m) { return m.id == top.id; });
        if (sameId != menus.end()) {
            for (const MenuNode& n : rest) {
                markPlaced(n, placed);
            }
            insertRest(*sameId, std::move(rest));
            continue;
        }
        MenuNode copy = top;
        copy.children = std::move(rest);
        markPlaced(copy, placed);
        auto help = std::find_if(menus.begin(), menus.end(), [](const MenuNode& m) { return m.id == "menu-help"; });
        menus.insert(help, std::move(copy));
    }

    for (MenuNode& m : menus) {
        removeMarkers(m.children);
        tidySeparators(m.children);
    }
    // a menu left empty (everything it named was placed elsewhere) is dropped
    menus.erase(std::remove_if(menus.begin(), menus.end(), [](const MenuNode& m) { return !hasContent(m.children); }),
                menus.end());
    res.menus = std::move(menus);
    return res;
}

std::vector<std::string> commandsIn(const std::vector<MenuNode>& menus)
{
    std::set<std::string> s;
    collectCommands(menus, s);
    return std::vector<std::string>(s.begin(), s.end());
}

namespace {
std::vector<std::string>& problems()
{
    static std::vector<std::string> p;
    return p;
}
}

void recordArrangement(const std::string& profileId, const std::vector<MenuNode>& before, const std::vector<MenuNode>& after)
{
    const std::vector<std::string> a = commandsIn(before);
    const std::vector<std::string> b = commandsIn(after);
    for (const std::string& c : a) {
        if (!std::binary_search(b.begin(), b.end(), c)) {
            problems().push_back(profileId + ": " + c + " is missing from the menus");
        }
    }
}

std::vector<std::string> arrangementProblems()
{
    return problems();
}

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

std::vector<std::string> resolveToolbarOrder(const std::vector<std::string>& current, const Profile& profile)
{
    // Within each group (between separators, ""), the commands the profile
    // lists take the places those commands had, in the profile's order;
    // everything else, and every separator, stays where it is.
    std::map<std::string, size_t> rank;
    for (size_t i = 0; i < profile.toolbar.size(); ++i) {
        if (!profile.toolbar[i].empty()) {
            rank.emplace(profile.toolbar[i], i);
        }
    }
    std::vector<std::string> out = current;
    size_t start = 0;
    while (start <= out.size()) {
        size_t end = start;
        while (end < out.size() && !out[end].empty()) {
            ++end;
        }
        std::vector<size_t> slots;
        std::vector<std::string> listed;
        for (size_t i = start; i < end; ++i) {
            if (rank.count(out[i])) {
                slots.push_back(i);
                listed.push_back(out[i]);
            }
        }
        std::stable_sort(listed.begin(), listed.end(), [&rank](const std::string& a, const std::string& b) {
            return rank[a] < rank[b];
        });
        for (size_t k = 0; k < slots.size(); ++k) {
            out[slots[k]] = listed[k];
        }
        start = end + 1;
    }
    return out;
}
}
