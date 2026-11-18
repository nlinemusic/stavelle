/*
 * SPDX-License-Identifier: GPL-3.0-only
 * Jubelle addition (not part of MuseScore). Workflow Profiles (see
 * internal/jubelle/workflowprofiles/workflowprofile.h).
 */
#pragma once

#include <string>
#include <vector>

#include "modularity/imoduleinterface.h"
#include "async/notification.h"
#include "types/ret.h"

#include "internal/jubelle/workflowprofiles/workflowprofile.h"

namespace mu::appshell {
class IWorkflowProfiles : MODULE_CONTEXT_INTERFACE
{
    INTERFACE_ID(IJubelleWorkflowProfiles)

public:
    virtual ~IWorkflowProfiles() = default;

    //! The built-in profiles, in menu order
    virtual const std::vector<jubelle::workflow::Profile>& profiles() const = 0;

    virtual std::string currentProfileId() const = 0;
    virtual const jubelle::workflow::Profile& currentProfile() const = 0;

    //! Keeps your changes to the current profile (shortcuts, toolbar,
    //! editing preferences), then switches; no restart needed
    virtual muse::Ret setCurrentProfile(const std::string& id) = 0;

    //! The current profile as it comes, forgetting your changes to it
    virtual muse::Ret restoreProfileDefaults() = 0;

    virtual muse::async::Notification currentProfileChanged() const = 0;
};
}
