/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore). Workflow Profiles: choosing and
 * applying a profile in the app. The arrangement rules are in
 * workflowprofile.h (plain C++, tested without the app).
 *
 * Applying a profile uses Jubelle's existing settings only:
 *  - shortcuts through the shortcuts register (the same file Preferences ›
 *    Shortcuts edits),
 *  - the note input toolbar through the toolbar setting (as Customize does),
 *  - editing preferences through the notation settings,
 *  - the menus are arranged by the app menu when it is (re)built.
 * Your changes are kept per profile in <app data>/jubelle-workflowprofiles/<id>/
 * when you switch away, and come back when you switch back.
 */
#pragma once

#include "async/asyncable.h"
#include "modularity/ioc.h"
#include "global/iglobalconfiguration.h"
#include "shortcuts/ishortcutsregister.h"
#include "ui/iuiactionsregister.h"
#include "ui/iuiconfiguration.h"
#include "notation/inotationconfiguration.h"
#include "notationscene/inotationsceneconfiguration.h"

#include "appshell/iworkflowprofiles.h"

namespace mu::appshell {
class WorkflowProfilesService : public IWorkflowProfiles, public muse::Contextable, public muse::async::Asyncable
{
    muse::GlobalInject<muse::IGlobalConfiguration> globalConfiguration;
    muse::GlobalInject<muse::ui::IUiConfiguration> uiConfiguration;
    muse::GlobalInject<notation::INotationConfiguration> notationConfiguration;
    muse::GlobalInject<notation::INotationSceneConfiguration> notationSceneConfiguration;
    muse::ContextInject<muse::shortcuts::IShortcutsRegister> shortcutsRegister = { this };
    muse::ContextInject<muse::ui::IUiActionsRegister> uiActionsRegister = { this };

public:
    explicit WorkflowProfilesService(const muse::modularity::ContextPtr& ctx);
    ~WorkflowProfilesService() override;

    void init();

    const std::vector<jubelle::workflow::Profile>& profiles() const override;
    std::string currentProfileId() const override;
    const jubelle::workflow::Profile& currentProfile() const override;
    muse::Ret setCurrentProfile(const std::string& id) override;
    muse::Ret restoreProfileDefaults() override;
    muse::async::Notification currentProfileChanged() const override;

private:
    const jubelle::workflow::Profile* find(const std::string& id) const;
    QString folderFor(const std::string& id) const;

    void saveCustomisations(const std::string& id);
    bool restoreCustomisations(const std::string& id);
    void forgetCustomisations(const std::string& id);
    void applyDefaults(const jubelle::workflow::Profile& profile);

    void applyShortcuts(const jubelle::workflow::Profile& profile);
    void applyToolbar(const jubelle::workflow::Profile& profile);
    void applyBehaviour(const std::map<std::string, std::string>& behaviour);
    std::map<std::string, std::string> currentBehaviour() const;

    std::vector<jubelle::workflow::Profile> m_profiles;
    muse::async::Notification m_currentProfileChanged;
};
}
