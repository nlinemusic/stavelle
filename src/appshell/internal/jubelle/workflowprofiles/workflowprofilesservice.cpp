/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore). Workflow Profiles in the app.
 */
#include "workflowprofilesservice.h"

#include <algorithm>
#include <cstdlib>
#include <set>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "settings.h"
#include "global/jubelle/diagnosticsregistry.h"

#include "log.h"

using namespace muse;
using namespace mu::appshell;
using namespace mu::appshell::jubelle::workflow;

namespace {
const Settings::Key CURRENT_PROFILE("appshell", "jubelle/workflowProfile");
const QString TOOLBAR_NAME("noteInput");
const char* BUILT_IN[] = { "jubelle", "musescore", "finale", "noteflight" };

const std::map<std::string, mu::notation::NoteInputMethod> INPUT_METHODS {
    { "BY_NOTE_NAME", mu::notation::NoteInputMethod::BY_NOTE_NAME },
    { "BY_DURATION", mu::notation::NoteInputMethod::BY_DURATION },
    { "RHYTHM", mu::notation::NoteInputMethod::RHYTHM },
    { "REPITCH", mu::notation::NoteInputMethod::REPITCH },
    { "REALTIME_AUTO", mu::notation::NoteInputMethod::REALTIME_AUTO },
};
}

WorkflowProfilesService::WorkflowProfilesService(const modularity::ContextPtr& ctx)
    : Contextable(ctx)
{
}

WorkflowProfilesService::~WorkflowProfilesService()
{
    muse::jubelle::DiagnosticsRegistry::instance().remove("workflow");
}

void WorkflowProfilesService::init()
{
    settings()->setDefaultValue(CURRENT_PROFILE, Val(std::string(DEFAULT_PROFILE_ID)));

    // the profiles are read once; they are small files in the app
    for (const char* id : BUILT_IN) {
        QFile f(QString(":/resources/workflowprofiles/%1.json").arg(id));
        if (!f.open(QIODevice::ReadOnly)) {
            LOGE() << "workflow profile missing: " << id;
            continue;
        }
        const QByteArray data = f.readAll();
        std::string err;
        Profile p = parseProfile(data.toStdString(), &err);
        if (!err.empty()) {
            LOGE() << "workflow profile " << id << ": " << err;
            continue;
        }
        m_profiles.push_back(std::move(p));
    }

    // Report a Bug shows which profile is in use
    muse::jubelle::DiagnosticsRegistry::instance().add("workflow", [this]() {
        muse::jubelle::DiagnosticSection s;
        s.id = "workflow";
        s.title = "Workflow profile";
        s.order = 40;
        s.items.push_back({ "Profile", currentProfile().name + " (" + currentProfileId() + ")" });
        return s;
    });
}

const std::vector<Profile>& WorkflowProfilesService::profiles() const
{
    return m_profiles;
}

const Profile* WorkflowProfilesService::find(const std::string& id) const
{
    for (const Profile& p : m_profiles) {
        if (p.id == id) {
            return &p;
        }
    }
    return nullptr;
}

std::string WorkflowProfilesService::currentProfileId() const
{
    const std::string id = settings()->value(CURRENT_PROFILE).toString();
    return find(id) ? id : std::string(DEFAULT_PROFILE_ID);
}

const Profile& WorkflowProfilesService::currentProfile() const
{
    static const Profile empty = [] {
        Profile p;
        p.id = DEFAULT_PROFILE_ID;
        p.name = "Jubelle";
        return p;
    }();
    const Profile* p = find(currentProfileId());
    return p ? *p : empty;
}

muse::async::Notification WorkflowProfilesService::currentProfileChanged() const
{
    return m_currentProfileChanged;
}

QString WorkflowProfilesService::folderFor(const std::string& id) const
{
    return (globalConfiguration()->userAppDataPath() + "/jubelle-workflowprofiles/").toQString() + QString::fromStdString(id);
}

Ret WorkflowProfilesService::setCurrentProfile(const std::string& id)
{
    const Profile* target = find(id);
    if (!target) {
        return make_ret(Ret::Code::UnknownError);
    }
    const std::string current = currentProfileId();
    if (current == id) {
        return make_ok();
    }

    saveCustomisations(current);
    if (!restoreCustomisations(id)) {
        applyDefaults(*target);
    }
    settings()->setSharedValue(CURRENT_PROFILE, Val(id));
    m_currentProfileChanged.notify();
    return make_ok();
}

Ret WorkflowProfilesService::restoreProfileDefaults()
{
    const Profile& p = currentProfile();
    forgetCustomisations(p.id);
    applyDefaults(p);
    m_currentProfileChanged.notify();
    return make_ok();
}

// ---------------------------------------------------------------------------
// Your changes, kept per profile
// ---------------------------------------------------------------------------

void WorkflowProfilesService::saveCustomisations(const std::string& id)
{
    const QString dir = folderFor(id);
    QDir().mkpath(dir);

    shortcutsRegister()->exportToFile(io::path_t(dir + "/shortcuts.xml"));

    QJsonObject o;
    QJsonObject behaviour;
    for (const auto& b : currentBehaviour()) {
        behaviour[QString::fromStdString(b.first)] = QString::fromStdString(b.second);
    }
    o["behaviour"] = behaviour;

    QJsonArray toolbar;
    const ui::ToolConfig cfg = uiConfiguration()->toolConfig(TOOLBAR_NAME, notationSceneConfiguration()->defaultNoteInputBarConfig());
    for (const ui::ToolConfig::Item& item : cfg.items) {
        QJsonObject i;
        i["action"] = QString::fromStdString(item.action);
        i["show"] = item.show;
        toolbar.append(i);
    }
    o["toolbar"] = toolbar;

    QFile f(dir + "/settings.json");
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(QJsonDocument(o).toJson());
    }
}

bool WorkflowProfilesService::restoreCustomisations(const std::string& id)
{
    const QString dir = folderFor(id);
    QFile f(dir + "/settings.json");
    if (!QFile::exists(dir + "/shortcuts.xml") || !f.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();

    shortcutsRegister()->importFromFile(io::path_t(dir + "/shortcuts.xml"));

    std::map<std::string, std::string> behaviour;
    const QJsonObject b = o["behaviour"].toObject();
    for (auto it = b.begin(); it != b.end(); ++it) {
        behaviour[it.key().toStdString()] = it.value().toString().toStdString();
    }
    applyBehaviour(behaviour);

    ui::ToolConfig cfg;
    for (const QJsonValue& v : o["toolbar"].toArray()) {
        const QJsonObject i = v.toObject();
        cfg.items.append(ui::ToolConfig::Item(i["action"].toString().toStdString(), i["show"].toBool(true)));
    }
    if (cfg.isValid()) {
        uiConfiguration()->setToolConfig(TOOLBAR_NAME, cfg);
    }
    return true;
}

void WorkflowProfilesService::forgetCustomisations(const std::string& id)
{
    QDir(folderFor(id)).removeRecursively();
}

// ---------------------------------------------------------------------------
// A profile as it comes
// ---------------------------------------------------------------------------

void WorkflowProfilesService::applyDefaults(const Profile& profile)
{
    applyShortcuts(profile);
    applyToolbar(profile);
    applyBehaviour(profile.behaviour);
}

void WorkflowProfilesService::applyShortcuts(const Profile& profile)
{
    // Jubelle's defaults, with each command's scope
    std::vector<ActionShortcut> defaults;
    std::set<std::string> seen;
    auto addDefault = [this, &defaults, &seen](const std::string& action) {
        if (!seen.insert(action).second) {
            return;
        }
        ActionShortcut a;
        a.action = action;
        a.sequences = shortcutsRegister()->defaultShortcut(action).sequences;
        const ui::UiAction& ua = uiActionsRegister()->action(action);
        a.scope = ua.isValid() ? ua.scCtx : std::string("any");
        defaults.push_back(std::move(a));
    };
    for (const shortcuts::Shortcut& sc : shortcutsRegister()->shortcuts()) {
        addDefault(sc.action);
    }
    for (const auto& o : profile.shortcuts) {
        if (uiActionsRegister()->action(o.first).isValid()) {
            addDefault(o.first);
        } else {
            LOGW() << "workflow profile " << profile.id << ": no command " << o.first;
        }
    }

    Profile valid = profile;
    valid.shortcuts.clear();
    for (const auto& o : profile.shortcuts) {
        if (seen.count(o.first)) {
            valid.shortcuts.push_back(o);
        }
    }
    const std::vector<ActionShortcut> user = resolveShortcuts(defaults, valid);
    if (user.empty()) {
        shortcutsRegister()->resetShortcuts();
        return;
    }
    shortcuts::ShortcutList list;
    for (const ActionShortcut& a : user) {
        shortcuts::Shortcut sc(a.action);
        sc.sequences = a.sequences;
        const shortcuts::Shortcut& def = shortcutsRegister()->defaultShortcut(a.action);
        sc.context = def.context;
        sc.autoRepeat = def.isValid() ? def.autoRepeat : true;
        list.push_back(sc);
    }
    shortcutsRegister()->setShortcuts(list);
}

void WorkflowProfilesService::applyToolbar(const Profile& profile)
{
    const ui::ToolConfig& def = notationSceneConfiguration()->defaultNoteInputBarConfig();
    std::vector<std::string> order;
    std::map<std::string, bool> show;
    for (const ui::ToolConfig::Item& item : def.items) {
        order.push_back(item.action);
        if (!item.action.empty()) {
            show[item.action] = item.show;
        }
    }
    const std::vector<std::string> resolved = resolveToolbarOrder(order, profile);
    ui::ToolConfig cfg;
    for (size_t i = 0; i < resolved.size(); ++i) {
        cfg.items.append(resolved[i].empty() ? def.items.at(int(i)) : ui::ToolConfig::Item(resolved[i], show[resolved[i]]));
    }
    uiConfiguration()->setToolConfig(TOOLBAR_NAME, cfg);
}

void WorkflowProfilesService::applyBehaviour(const std::map<std::string, std::string>& b)
{
    auto boolOf = [](const std::string& v) { return v == "true" || v == "1"; };
    for (const auto& kv : b) {
        if (kv.first == "noteInputMethod") {
            auto it = INPUT_METHODS.find(kv.second);
            if (it != INPUT_METHODS.end()) {
                notationConfiguration()->setDefaultNoteInputMethod(it->second);
            }
        } else if (kv.first == "accidentalsApplyToNextNote") {
            notationConfiguration()->setAddAccidentalDotsArticulationsToNextNoteEntered(boolOf(kv.second));
        } else if (kv.first == "noteInputCursorInInputByDuration") {
            notationConfiguration()->setUseNoteInputCursorInInputByDuration(boolOf(kv.second));
        } else if (kv.first == "selectionProximity") {
            int v = std::atoi(kv.second.c_str());
            notationConfiguration()->setSelectionProximity(std::clamp(v, 1, 10));
        }
    }
}

std::map<std::string, std::string> WorkflowProfilesService::currentBehaviour() const
{
    std::map<std::string, std::string> b;
    const auto method = notationConfiguration()->defaultNoteInputMethod();
    for (const auto& m : INPUT_METHODS) {
        if (m.second == method) {
            b["noteInputMethod"] = m.first;
        }
    }
    b["accidentalsApplyToNextNote"] = notationConfiguration()->addAccidentalDotsArticulationsToNextNoteEntered() ? "true" : "false";
    b["noteInputCursorInInputByDuration"] = notationConfiguration()->useNoteInputCursorInInputByDuration() ? "true" : "false";
    b["selectionProximity"] = std::to_string(notationConfiguration()->selectionProximity());
    return b;
}
