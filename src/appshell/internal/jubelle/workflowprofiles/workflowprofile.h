/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore).
 *
 * Workflow Profiles (View › Workflow profile): MuseScore, Finale, Noteflight
 * and Jubelle ways of working on the same Jubelle.
 *
 * A profile only changes how Jubelle is presented and operated:
 *  - keyboard shortcuts (on top of Jubelle's defaults),
 *  - how the menus are arranged,
 *  - the order of the note input toolbar,
 *  - a few existing editing preferences (how notes are entered, whether an
 *    accidental applies to the next note or the selected one, …).
 * Every command stays available in every profile: the menu arrangement is
 * checked so that anything a profile doesn't place is still added to a menu
 * (resolveMenus), and shortcuts only move between commands. Nothing in the
 * notation engine changes, so a future engine works with every profile.
 *
 * The profiles are data (JSON files in the app's resources), read once when
 * the profile is chosen; nothing runs in the background.
 *
 * Plain C++ (no Qt), so it is tested without the app.
 */
#pragma once

#include <map>
#include <string>
#include <vector>

namespace mu::appshell::jubelle::workflow {
constexpr const char* PROFILE_SCHEMA = "jubelle.workflowprofile/1";
constexpr const char* DEFAULT_PROFILE_ID = "jubelle";

//! A top-level menu in a profile's arrangement
struct TopMenu {
    std::string id;       // e.g. "menu-file", or a new id such as "menu-finale-utilities"
    std::string title;    // with & for the access key, e.g. "&Utilities"
    //! Commands (action codes), submenus (menu ids) and "-" for a separator,
    //! taken from wherever they are in Jubelle's menus; "*" marks where the
    //! taken contents go (else they go at the end)
    std::vector<std::string> entries;
    //! Jubelle menus (top-level or submenus) whose remaining contents go here
    std::vector<std::string> takes;
};

struct Profile {
    std::string id;            // "jubelle", "musescore", "finale", "noteflight"
    std::string name;          // "Finale-style"
    std::string description;

    //! Shortcut changes from Jubelle's defaults: command → key sequences
    //! (an empty list removes the command's shortcuts)
    std::vector<std::pair<std::string, std::vector<std::string> > > shortcuts;

    //! Empty: Jubelle's own menus unchanged
    std::vector<TopMenu> menus;

    //! Note input toolbar order within its groups (see resolveToolbarOrder)
    std::vector<std::string> toolbar;

    //! Editing preferences, by name (see BEHAVIOUR_KEYS)
    std::map<std::string, std::string> behaviour;

    bool hasMenuLayout() const { return !menus.empty(); }
};

//! The editing preferences a profile may set (existing Jubelle preferences)
//!  noteInputMethod: BY_NOTE_NAME | BY_DURATION | RHYTHM | REPITCH | REALTIME_AUTO
//!  accidentalsApplyToNextNote: true | false (false: to the selected note)
//!  noteInputCursorInInputByDuration: true | false
//!  selectionProximity: 1..10 (how near a click must be to select)
extern const std::vector<std::string> BEHAVIOUR_KEYS;

//! Reads a profile; "" error when it is valid
Profile parseProfile(const std::string& json, std::string* error = nullptr);

// ---------------------------------------------------------------------------
// Shortcuts
// ---------------------------------------------------------------------------

//! A command and its shortcuts, with the scope it works in ("any",
//! "project-opened", "notation-text-editing", …)
struct ActionShortcut {
    std::string action;
    std::vector<std::string> sequences;
    std::string scope;
};

//! Two scopes can be active at the same time (so the same key would clash)
bool scopesOverlap(const std::string& a, const std::string& b);

//! The user shortcut list that gives this profile. Lists every command whose
//! shortcuts change, including commands that keep a key that another command
//! now uses in a different scope (so the app's merge doesn't drop it).
//! An empty result means Jubelle's defaults.
std::vector<ActionShortcut> resolveShortcuts(const std::vector<ActionShortcut>& defaults, const Profile& profile);

//! What the app ends up with: the defaults merged with a user list, exactly
//! as MuseScore's shortcuts register merges them
std::map<std::string, std::vector<std::string> > effectiveShortcuts(const std::vector<ActionShortcut>& defaults,
                                                                     const std::vector<ActionShortcut>& user);

struct Clash {
    std::string sequence;
    std::string first;
    std::string second;
};

//! Keys given to two commands whose scopes overlap, that weren't already so
//! in the defaults
std::vector<Clash> findNewClashes(const std::vector<ActionShortcut>& defaults,
                                  const std::map<std::string, std::vector<std::string> >& effective);

// ---------------------------------------------------------------------------
// Menus
// ---------------------------------------------------------------------------

//! A menu tree independent of the app's menu objects
struct MenuNode {
    std::string id;       // action code, menu id, or "" (separator or unnamed submenu)
    std::string title;    // for submenus
    bool isMenu = false;
    bool isSeparator = false;
    std::vector<MenuNode> children;
    const void* source = nullptr;   // the app's own object, carried through
};

struct MenuResolution {
    std::vector<MenuNode> menus;
    std::vector<std::string> unknownEntries;   // entries naming nothing in Jubelle's menus (ignored)
};

//! Arranges Jubelle's menus (`source`, the top-level menus) as the profile
//! says. Anything the profile doesn't place goes to the menu that takes its
//! Jubelle menu, else to a menu with the same id, else to a copy of its
//! Jubelle menu at the end (before Help), so nothing is ever lost.
MenuResolution resolveMenus(const std::vector<MenuNode>& source, const Profile& profile);

//! Every command (non-menu, non-separator id) in a tree, sorted, without repeats
std::vector<std::string> commandsIn(const std::vector<MenuNode>& menus);

//! Checked by the app each time it arranges its menus (the interface
//! self-test reads it): commands that were in Jubelle's menus but not in the
//! arranged ones. Should always be empty.
void recordArrangement(const std::string& profileId, const std::vector<MenuNode>& before, const std::vector<MenuNode>& after);
std::vector<std::string> arrangementProblems();

// ---------------------------------------------------------------------------
// Toolbar
// ---------------------------------------------------------------------------

//! The toolbar's items ("" for a separator) reordered as the profile says:
//! within each group, the commands the profile lists are put in its order;
//! nothing is added, removed or moved to another group
std::vector<std::string> resolveToolbarOrder(const std::vector<std::string>& current, const Profile& profile);
}
