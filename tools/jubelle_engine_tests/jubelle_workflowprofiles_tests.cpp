/*
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Jubelle (based on MuseScore Studio)
 */

// Workflow Profiles: the four profiles, their shortcuts (on the real default
// shortcuts and command scopes), menu arrangements and toolbar order.
#include <gtest/gtest.h>

#include <cstring>
#include <filesystem>
#include <functional>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>

#include "appshell/internal/jubelle/workflowprofiles/workflowprofile.h"

using namespace mu::appshell::jubelle::workflow;
namespace fs = std::filesystem;

namespace {
const fs::path ROOT = fs::u8path(MU_ROOT_DIR);
const std::vector<std::string> PROFILE_IDS = { "jubelle", "musescore", "finale", "noteflight" };

std::string readAll(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::stringstream s;
    s << in.rdbuf();
    return s.str();
}

Profile loadProfile(const std::string& id)
{
    std::string err;
    Profile p = parseProfile(readAll(ROOT / "src/appshell/resources/workflowprofiles" / (id + ".json")), &err);
    EXPECT_TRUE(err.empty()) << id << ": " << err;
    return p;
}

std::string unescapeXml(std::string s)
{
    const std::pair<const char*, const char*> ents[] = { { "&lt;", "<" }, { "&gt;", ">" }, { "&quot;", "\"" }, { "&apos;", "'" },
        { "&amp;", "&" } };
    for (const auto& e : ents) {
        size_t pos = 0;
        while ((pos = s.find(e.first, pos)) != std::string::npos) {
            s.replace(pos, std::strlen(e.first), e.second);
            pos += std::strlen(e.second);
        }
    }
    return s;
}

//! Every command's scope, read from the UiAction definitions in the code
const std::map<std::string, std::string>& actionScopes()
{
    static std::map<std::string, std::string> scopes;
    if (!scopes.empty()) {
        return scopes;
    }
    static const std::map<std::string, std::string> names {
        { "CTX_ANY", "any" }, { "CTX_NOTATION_OPENED", "project-opened" }, { "CTX_NOTATION_FOCUSED", "project-focused" },
        { "CTX_PROJECT_OPENED", "project-opened" }, { "CTX_PROJECT_FOCUSED", "project-focused" },
        { "CTX_NOTATION_TEXT_EDITING", "notation-text-editing" },
        { "CTX_NOTATION_NOT_NOTE_INPUT_STAFF_TAB", "notation-not-note-input-staff-tab" },
        { "CTX_NOTATION_NOTE_INPUT_STAFF_TAB", "notation-note-input-staff-tab" },
        { "CTX_NOTATION_LIST_SELECTION", "notation-list-selection" }, { "CTX_DISABLED", "disabled" },
    };
    const std::regex def(R"rx(UiAction\(\s*"([^"]+)"\s*,\s*[A-Za-z_:]+\s*,\s*([A-Za-z_:]+))rx");
    for (const auto& e : fs::recursive_directory_iterator(ROOT / "src")) {
        const std::string name = e.path().filename().u8string();
        if (!e.is_regular_file() || name.find("uiactions") == std::string::npos || e.path().extension() != ".cpp"
            || e.path().u8string().find("/web/") != std::string::npos) {
            continue;
        }
        const std::string text = readAll(e.path());
        for (auto it = std::sregex_iterator(text.begin(), text.end(), def); it != std::sregex_iterator(); ++it) {
            std::string ctx = (*it)[2].str();
            const size_t colons = ctx.rfind("::");
            if (colons != std::string::npos) {
                ctx = ctx.substr(colons + 2);
            }
            auto n = names.find(ctx);
            scopes.emplace((*it)[1].str(), n != names.end() ? n->second : ctx);
        }
    }
    return scopes;
}

//! Jubelle's default shortcuts (shortcuts.xml), with each command's scope
std::vector<ActionShortcut> defaultShortcuts()
{
    const std::string xml = readAll(ROOT / "src/app/configs/data/shortcuts.xml");
    std::vector<ActionShortcut> out;
    const std::regex sc(R"(<SC>([\s\S]*?)</SC>)");
    const std::regex key(R"(<key>([^<]*)</key>)");
    const std::regex seq(R"(<seq>([^<]*)</seq>)");
    std::set<std::string> seen;
    for (auto it = std::sregex_iterator(xml.begin(), xml.end(), sc); it != std::sregex_iterator(); ++it) {
        const std::string body = (*it)[1].str();
        std::smatch k;
        if (!std::regex_search(body, k, key)) {
            continue;
        }
        ActionShortcut a;
        a.action = unescapeXml(k[1].str());
        for (auto s = std::sregex_iterator(body.begin(), body.end(), seq); s != std::sregex_iterator(); ++s) {
            a.sequences.push_back(unescapeXml((*s)[1].str()));
        }
        auto scope = actionScopes().find(a.action);
        a.scope = scope != actionScopes().end() ? scope->second : "any";
        seen.insert(a.action);
        out.push_back(a);
    }
    // commands without a default shortcut can still be given one
    for (const auto& s : actionScopes()) {
        if (!seen.count(s.first)) {
            out.push_back({ s.first, {}, s.second });
        }
    }
    return out;
}

MenuNode cmd(const std::string& id)
{
    MenuNode n;
    n.id = id;
    return n;
}

MenuNode sep()
{
    MenuNode n;
    n.isSeparator = true;
    return n;
}

MenuNode menu(const std::string& id, const std::string& title, std::vector<MenuNode> children)
{
    MenuNode n;
    n.id = id;
    n.title = title;
    n.isMenu = true;
    n.children = std::move(children);
    return n;
}

std::vector<MenuNode> jubelleLikeMenus()
{
    return {
        menu("menu-file", "&File", { cmd("file-new"), cmd("file-open"), menu("menu-file-open", "Open recent", {}), sep(),
                                     cmd("file-save"), menu("menu-jubelle-export", "Export", { cmd("file-export"),
                                                                                               cmd("jubelle-export-drum-stems") }),
                                     sep(), cmd("parts"), cmd("print"), sep(), cmd("quit") }),
        menu("menu-edit", "&Edit", { cmd("undo"), cmd("redo"), sep(), cmd("preference-dialog") }),
        menu("menu-add", "&Add", { menu("menu-notes", "Notes", { cmd("note-input"), sep(), cmd("note-c") }),
                                   menu("menu-text", "Text", { cmd("title-text"), cmd("staff-text") }) }),
        menu("menu-format", "F&ormat", { cmd("edit-style"), menu("menu-stretch", "Stretch", { cmd("stretch+"), cmd("stretch-") }) }),
        menu("menu-jubelle-playback", "&Playback", { cmd("playback-setup"), cmd("jubelle-swing") }),
        menu("menu-tools", "&Tools", { menu("menu-jubelle-arranging", "Arranging", { cmd("transpose"), cmd("explode") }),
                                       menu("menu-voices", "Voices", { cmd("voice-x12") }), sep(), cmd("reset-groupings") }),
        menu("menu-plugins", "&Plugins", { cmd("manage-plugins") }),
        menu("menu-help", "&Help", { cmd("jubelle-learning-center"), sep(), cmd("jubelle-report-bug"), cmd("about-musescore") }),
    };
}

std::vector<std::string> topIds(const std::vector<MenuNode>& menus)
{
    std::vector<std::string> ids;
    for (const MenuNode& m : menus) {
        ids.push_back(m.id);
    }
    return ids;
}

void expectTidy(const std::vector<MenuNode>& nodes, const std::string& where)
{
    for (size_t i = 0; i < nodes.size(); ++i) {
        if (nodes[i].isSeparator) {
            EXPECT_NE(i, 0u) << "leading separator in " << where;
            EXPECT_NE(i + 1, nodes.size()) << "trailing separator in " << where;
            if (i > 0) {
                EXPECT_FALSE(nodes[i - 1].isSeparator) << "double separator in " << where;
            }
        }
        EXPECT_NE(nodes[i].id, "*") << where;
        expectTidy(nodes[i].children, where + " > " + nodes[i].title);
    }
}
}

TEST(Jubelle_WorkflowProfiles, AllFourProfilesAreValid)
{
    for (const std::string& id : PROFILE_IDS) {
        const Profile p = loadProfile(id);
        EXPECT_EQ(p.id, id);
        EXPECT_FALSE(p.name.empty());
        EXPECT_FALSE(p.description.empty());
        for (const std::string& key : BEHAVIOUR_KEYS) {
            EXPECT_EQ(p.behaviour.count(key), 1u) << id << " sets " << key << " (so restoring defaults restores it)";
        }
    }
}

TEST(Jubelle_WorkflowProfiles, BadProfilesAreRefused)
{
    std::string err;
    parseProfile("{", &err);
    EXPECT_FALSE(err.empty());
    parseProfile(R"({"schema":"other","id":"x","name":"X"})", &err);
    EXPECT_NE(err.find("schema"), std::string::npos);
    parseProfile(R"({"schema":"jubelle.workflowprofile/1","id":"Bad Id","name":"X"})", &err);
    EXPECT_NE(err.find("id"), std::string::npos);
    parseProfile(R"({"schema":"jubelle.workflowprofile/1","id":"x","name":"X","behaviour":{"deleteEverything":true}})", &err);
    EXPECT_NE(err.find("deleteEverything"), std::string::npos);
    parseProfile(R"({"schema":"jubelle.workflowprofile/1","id":"x","name":"X","menus":[{"id":"a","title":"A"},{"id":"a","title":"B"}]})",
                 &err);
    EXPECT_NE(err.find("twice"), std::string::npos);
}

TEST(Jubelle_WorkflowProfiles, ShortcutsNameRealCommands)
{
    const auto& scopes = actionScopes();
    ASSERT_GT(scopes.size(), 400u);
    for (const std::string& id : PROFILE_IDS) {
        for (const auto& o : loadProfile(id).shortcuts) {
            EXPECT_EQ(scopes.count(o.first), 1u) << id << ": no command " << o.first;
        }
    }
}

TEST(Jubelle_WorkflowProfiles, ShortcutsAddNoClashes)
{
    const std::vector<ActionShortcut> defaults = defaultShortcuts();
    ASSERT_GT(defaults.size(), 250u);
    for (const std::string& id : PROFILE_IDS) {
        const Profile p = loadProfile(id);
        const auto user = resolveShortcuts(defaults, p);
        const auto eff = effectiveShortcuts(defaults, user);
        for (const Clash& c : findNewClashes(defaults, eff)) {
            ADD_FAILURE() << id << ": " << c.sequence << " is on both " << c.first << " and " << c.second;
        }
        // every key the profile asks for is really there
        for (const auto& o : p.shortcuts) {
            EXPECT_EQ(eff.at(o.first), o.second) << id << ": " << o.first;
        }
    }
}

TEST(Jubelle_WorkflowProfiles, JubelleAndMuseScoreKeepTheDefaultShortcuts)
{
    const std::vector<ActionShortcut> defaults = defaultShortcuts();
    for (const char* id : { "jubelle", "musescore" }) {
        EXPECT_TRUE(resolveShortcuts(defaults, loadProfile(id)).empty()) << id;
    }
}

TEST(Jubelle_WorkflowProfiles, FinaleShortcuts)
{
    const std::vector<ActionShortcut> defaults = defaultShortcuts();
    const auto eff = effectiveShortcuts(defaults, resolveShortcuts(defaults, loadProfile("finale")));
    EXPECT_EQ(eff.at("interval3"), std::vector<std::string>({ "3" }));
    EXPECT_EQ(eff.at("interval-3"), std::vector<std::string>({ "Shift+3" }));
    // the number row moved to intervals; durations stay on the keypad
    EXPECT_EQ(eff.at("pad-note-4"), std::vector<std::string>({ "Num+5", "Alt+5", "Ctrl+Alt+Shift+5" }));
    // keys taken from MuseScore commands
    const auto& noteB = eff.at("note-b");
    EXPECT_EQ(std::count(noteB.begin(), noteB.end(), "H"), 0);
    EXPECT_EQ(std::count(noteB.begin(), noteB.end(), "B"), 1);
    const auto& grace = eff.at("acciaccatura");
    EXPECT_EQ(std::count(grace.begin(), grace.end(), "/"), 0);
    // the guitar-tab fret keys are in another scope: untouched
    const auto& fret3 = eff.at("fret-3");
    EXPECT_EQ(std::count(fret3.begin(), fret3.end(), "3"), 1);
}

TEST(Jubelle_WorkflowProfiles, NoteflightShortcutsKeepTypingAndNavigation)
{
    const std::vector<ActionShortcut> defaults = defaultShortcuts();
    const auto eff = effectiveShortcuts(defaults, resolveShortcuts(defaults, loadProfile("noteflight")));
    auto has = [&eff](const std::string& a, const std::string& s) {
        const auto& v = eff.at(a);
        return std::find(v.begin(), v.end(), s) != v.end();
    };
    EXPECT_TRUE(has("rest", "Space"));
    EXPECT_FALSE(has("play", "Space"));
    EXPECT_TRUE(has("play", "P"));
    EXPECT_TRUE(has("enh-both", "Z"));
    // Space still presses buttons when moving around with the keyboard,
    // and still types a word break in lyrics
    EXPECT_TRUE(has("nav-trigger-control", "Space"));
    EXPECT_TRUE(has("next-word", "Space"));
    // ; still moves to the next beat while typing chord symbols
    EXPECT_TRUE(has("next-beat-TEXT", ";"));
    EXPECT_TRUE(has("add-tenuto", ":"));
}

TEST(Jubelle_WorkflowProfiles, SwitchingBackRestoresDefaults)
{
    // a profile's shortcut list is complete on its own: switching from
    // Finale to Jubelle (an empty list) gives the defaults again
    const std::vector<ActionShortcut> defaults = defaultShortcuts();
    const auto none = effectiveShortcuts(defaults, {});
    for (const ActionShortcut& d : defaults) {
        EXPECT_EQ(none.at(d.action), d.sequences) << d.action;
    }
}

TEST(Jubelle_WorkflowProfiles, MenusKeepEveryCommand)
{
    const std::vector<MenuNode> source = jubelleLikeMenus();
    const std::vector<std::string> all = commandsIn(source);
    for (const std::string& id : PROFILE_IDS) {
        const MenuResolution r = resolveMenus(source, loadProfile(id));
        EXPECT_EQ(commandsIn(r.menus), all) << id;
        for (const MenuNode& m : r.menus) {
            expectTidy(m.children, id + ": " + m.title);
        }
    }
}

TEST(Jubelle_WorkflowProfiles, JubelleProfileLeavesMenusAlone)
{
    const std::vector<MenuNode> source = jubelleLikeMenus();
    const MenuResolution r = resolveMenus(source, loadProfile("jubelle"));
    EXPECT_EQ(topIds(r.menus), topIds(source));
    EXPECT_TRUE(r.unknownEntries.empty());
}

TEST(Jubelle_WorkflowProfiles, MuseScoreMenusFollowMuseScore)
{
    const MenuResolution r = resolveMenus(jubelleLikeMenus(), loadProfile("musescore"));
    // no Playback menu in MuseScore 4: its commands go to View and Tools
    EXPECT_EQ(topIds(r.menus), std::vector<std::string>({ "menu-file", "menu-edit", "menu-view", "menu-add", "menu-format",
                                                             "menu-tools", "menu-plugins", "menu-help" }));
    // Quit stays last in File, Jubelle's extra export command goes before it
    const MenuNode& file = r.menus[0];
    EXPECT_EQ(file.children.back().id, "quit");
    const auto ids = commandsIn({ file });
    EXPECT_NE(std::find(ids.begin(), ids.end(), "jubelle-export-drum-stems"), ids.end());
    // File › Export is a command again, not Jubelle's submenu
    for (const MenuNode& n : file.children) {
        EXPECT_NE(n.id, "menu-jubelle-export");
    }
}

TEST(Jubelle_WorkflowProfiles, FinaleMenusHaveUtilitiesAndDocument)
{
    const MenuResolution r = resolveMenus(jubelleLikeMenus(), loadProfile("finale"));
    std::vector<std::string> titles;
    for (const MenuNode& m : r.menus) {
        titles.push_back(m.title);
    }
    EXPECT_NE(std::find(titles.begin(), titles.end(), "&Utilities"), titles.end());
    EXPECT_NE(std::find(titles.begin(), titles.end(), "&Document"), titles.end());
    EXPECT_NE(std::find(titles.begin(), titles.end(), "&MIDI/Audio"), titles.end());
    EXPECT_EQ(r.menus.back().id, "menu-help");
}

TEST(Jubelle_WorkflowProfiles, NoteflightTextMenuIsFlat)
{
    const MenuResolution r = resolveMenus(jubelleLikeMenus(), loadProfile("noteflight"));
    auto text = std::find_if(r.menus.begin(), r.menus.end(), [](const MenuNode& m) { return m.id == "menu-noteflight-text"; });
    ASSERT_NE(text, r.menus.end());
    ASSERT_GE(text->children.size(), 2u);
    EXPECT_EQ(text->children[0].id, "title-text");   // the Text submenu's commands, directly in the menu
    EXPECT_EQ(text->children[1].id, "staff-text");
}

TEST(Jubelle_WorkflowProfiles, UnplacedMenusAreNeverLost)
{
    Profile p;
    p.id = "tiny";
    p.name = "Tiny";
    p.menus = { TopMenu { "menu-file", "&File", { "file-new", "nothing-like-this" }, {} },
                TopMenu { "menu-help", "&Help", {}, { "menu-help" } } };
    const std::vector<MenuNode> source = jubelleLikeMenus();
    const MenuResolution r = resolveMenus(source, p);
    EXPECT_EQ(commandsIn(r.menus), commandsIn(source));
    EXPECT_EQ(r.unknownEntries, std::vector<std::string>({ "nothing-like-this" }));
    EXPECT_EQ(r.menus.back().id, "menu-help");   // copies go before Help
    // the empty Open recent submenu survives (it fills in later)
    bool foundRecent = false;
    std::function<void(const std::vector<MenuNode>&)> walk = [&](const std::vector<MenuNode>& ns) {
        for (const MenuNode& n : ns) {
            foundRecent = foundRecent || n.id == "menu-file-open";
            walk(n.children);
        }
    };
    walk(r.menus);
    EXPECT_TRUE(foundRecent);
}

TEST(Jubelle_WorkflowProfiles, ToolbarOrderKeepsGroups)
{
    const std::vector<std::string> current = { "note-input-by-note-name", "", "pad-note-64", "pad-note-32", "pad-note-16",
                                               "pad-note-8", "pad-note-4", "pad-note-2", "pad-note-1", "note-breve", "",
                                               "pad-dot", "pad-rest" };
    const auto out = resolveToolbarOrder(current, loadProfile("finale"));
    EXPECT_EQ(out, std::vector<std::string>({ "note-input-by-note-name", "", "pad-note-1", "pad-note-2", "pad-note-4",
                                              "pad-note-8", "pad-note-16", "pad-note-32", "pad-note-64", "note-breve", "",
                                              "pad-dot", "pad-rest" }));
    EXPECT_EQ(resolveToolbarOrder(current, loadProfile("jubelle")), current);
}
