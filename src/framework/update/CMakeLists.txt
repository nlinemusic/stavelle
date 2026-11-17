# SPDX-License-Identifier: GPL-3.0-only
# MuseScore-Studio-CLA-applies
#
# MuseScore Studio
# Music Composition & Notation
#
# Copyright (C) 2026 MuseScore Limited and others
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 3 as
# published by the Free Software Foundation.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.

muse_create_module(muse_update ALIAS muse::update)

target_sources(muse_update PRIVATE
    updatemodule.cpp
    updatemodule.h
    updatetypes.h
    updateerrors.h
    iupdateconfiguration.h
    iappupdatescenario.h
    iappupdateservice.h

    internal/updateconfiguration.cpp
    internal/updateconfiguration.h
    internal/updateuiactions.cpp
    internal/updateuiactions.h
    internal/updateactioncontroller.cpp
    internal/updateactioncontroller.h
    internal/appupdatescenario.cpp
    internal/appupdatescenario.h
    internal/appupdateservice.cpp
    internal/appupdateservice.h
    internal/jubelle/updateverify.cpp
    internal/jubelle/updateverify.h
)

# Jubelle R3: the public half of the release signing key (64 hex digits). Set
# by the release workflow from the repository variable JUBELLE_RELEASE_PUBLIC_KEY.
# Empty: updates are never installed by this build.
set(JUBELLE_RELEASE_PUBLIC_KEY "" CACHE STRING "Jubelle release signing public key (hex)")
if (NOT JUBELLE_RELEASE_PUBLIC_KEY AND DEFINED ENV{JUBELLE_RELEASE_PUBLIC_KEY})
    set(JUBELLE_RELEASE_PUBLIC_KEY "$ENV{JUBELLE_RELEASE_PUBLIC_KEY}")
endif()
if (JUBELLE_RELEASE_PUBLIC_KEY AND NOT JUBELLE_RELEASE_PUBLIC_KEY MATCHES "^[0-9a-fA-F]+$")
    message(FATAL_ERROR "JUBELLE_RELEASE_PUBLIC_KEY must be 64 hex digits")
endif()
if (JUBELLE_RELEASE_PUBLIC_KEY)
    set_source_files_properties(${CMAKE_CURRENT_LIST_DIR}/internal/jubelle/updateverify.cpp
        PROPERTIES COMPILE_DEFINITIONS "JUBELLE_RELEASE_PUBLIC_KEY=\"${JUBELLE_RELEASE_PUBLIC_KEY}\"")
endif()

if (MUSE_QT_SUPPORT)
    target_link_libraries(muse_update PRIVATE Qt::Concurrent)
endif()

if (MUSE_MODULE_UPDATE_TESTS)
    add_subdirectory(tests)
endif()

if (MUSE_MODULE_UPDATE_QML)
    add_subdirectory(qml/Muse/Update)
endif()