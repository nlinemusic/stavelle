# Jubelle: run engraving (notation engine) tests without Qt, on any machine,
# without GitHub Actions minutes. Builds MuseScore's engine the same way as
# tools/check_build_without_qt and links the chosen engraving test files.
#
#   cmake -S tools/jubelle_engine_tests -B build-engine-tests -GNinja
#   cmake --build build-engine-tests
#   build-engine-tests/jubelle_engine_tests
#
# Only tests that don't compare against reference files can run here
# (those comparisons need Qt). The full suite still runs in "Check: Unit Tests".
cmake_minimum_required(VERSION 3.16)
project(jubelle_engine_tests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

set(MU_ROOT ${CMAKE_CURRENT_LIST_DIR}/../..)
get_filename_component(MU_ROOT ${MU_ROOT} ABSOLUTE)
set(MUSE_FRAMEWORK_PATH ${MU_ROOT})
set(CMAKE_MODULE_PATH ${MU_ROOT}/buildscripts ${MU_ROOT}/buildscripts/cmake ${MU_ROOT}/src/framework/cmake ${CMAKE_MODULE_PATH})

include(SetupBuildEnvironment)
include(GetPlatformInfo)
include_directories(${MU_ROOT}/thirdparty/dtl)
set(THIRDPARTY_DIR ${MU_ROOT}/thirdparty)

set(MUSE_QT_SUPPORT OFF)
include(${MU_ROOT}/src/framework/cmake/MuseSetupConfiguration.cmake)

set(GLOBAL_NO_INTERNAL ON)
add_subdirectory(${MU_ROOT}/src/framework/global global)
set(DRAW_NO_INTERNAL ON)
add_subdirectory(${MU_ROOT}/src/framework/draw draw)
# Playback (MuseScore's note-event renderer) is built too, so playback
# features are tested on the real renderer
set(MUE_BUILD_ENGRAVING_PLAYBACK ON)
set(ENGRAVING_NO_INTERNAL ON)
set(ENGRAVING_NO_ACCESSIBILITY ON)
add_subdirectory(${MU_ROOT}/src/engraving engraving)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
add_subdirectory(${MU_ROOT}/src/framework/testing/thirdparty/googletest googletest EXCLUDE_FROM_ALL)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(QRC_MAP ${CMAKE_CURRENT_BINARY_DIR}/qrcmap.h)
add_custom_command(OUTPUT ${QRC_MAP}
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/make_qrc_map.py ${MU_ROOT} ${QRC_MAP}
    DEPENDS ${CMAKE_CURRENT_LIST_DIR}/make_qrc_map.py)

# The real fonts provider; its addInternalFont uses a Qt-only call, so a copy
# with that one line made Qt-free is generated.
set(EFP_COPY ${CMAKE_CURRENT_BINARY_DIR}/engravingfontsprovider_noqt.cpp)
file(READ ${MU_ROOT}/src/engraving/internal/engravingfontsprovider.cpp EFP_TEXT)
string(REPLACE "muse::io::dirpath(filePath.toQString())" "muse::io::dirpath(filePath)" EFP_TEXT "${EFP_TEXT}")
string(REPLACE "#include \"engravingfontsprovider.h\"" "#include \"engraving/internal/engravingfontsprovider.h\"" EFP_TEXT "${EFP_TEXT}")
file(WRITE ${EFP_COPY} "${EFP_TEXT}")

# MuseScore's own repeat and playback tests build String from narrow
# literals, which needs Qt; these copies use u"" literals instead and are
# otherwise unchanged.
set(MUSE_TEST_COPIES)
# note_tests and keysig_tests cover accidentals (courtesy-accidental rules).
foreach(t repeat_tests playback/playbackmodel_tests playback/playbackeventsrendering_tests note_tests keysig_tests)
    file(READ ${MU_ROOT}/src/engraving/tests/${t}.cpp T_TEXT)
    string(REPLACE "String(\"" "String(u\"" T_TEXT "${T_TEXT}")
    string(REPLACE "DIR + \"" "DIR + u\"" T_TEXT "${T_TEXT}")
    string(REGEX REPLACE "(static const String [A-Z_]+)\\(\"" "\\1(u\"" T_TEXT "${T_TEXT}")
    string(REGEX REPLACE "(String [A-Za-z0-9_]+)\\(\"" "\\1(u\"" T_TEXT "${T_TEXT}")
    get_filename_component(T_NAME ${t} NAME)
    set(T_COPY ${CMAKE_CURRENT_BINARY_DIR}/musetests/${T_NAME}.cpp)
    file(WRITE ${T_COPY} "${T_TEXT}")
    list(APPEND MUSE_TEST_COPIES ${T_COPY})
endforeach()

add_executable(jubelle_engine_tests
    main.cpp
    stdfs.h
    fontproviderstub.cpp
    fontproviderstub.h
    ${QRC_MAP}
    ${EFP_COPY}
    ${MU_ROOT}/src/engraving/internal/engravingfont.cpp
    ${MU_ROOT}/src/engraving/tests/utils/scorerw.cpp
    scorecomp_noqt.cpp
    # Jubelle code under test that isn't part of the engraving library:
    ${MU_ROOT}/src/notation/internal/jubelle/chordidentifier.cpp
    ${MU_ROOT}/src/notation/internal/jubelle/chordidentifierrunner.cpp
    ${MU_ROOT}/src/notation/internal/jubelle/swingmarking.cpp
    ${MU_ROOT}/src/notation/internal/jubelle/partlayout.cpp
    ${MU_ROOT}/src/notation/internal/jubelle/articulationpassage.cpp
    ${MU_ROOT}/src/notation/internal/jubelle/pagealignment.cpp
    ${MU_ROOT}/src/notation/internal/jubelle/linkedpassages.cpp
    ${MU_ROOT}/src/notation/internal/jubelle/passagelinkplanner.cpp
    # Engraving tests that run here:
    ${MU_ROOT}/src/engraving/tests/jubelle_courtesy_tests.cpp
    ${MU_ROOT}/src/notation/tests/chordidentifierrunner_tests.cpp
    ${MU_ROOT}/src/engraving/tests/jubelle_articulationplayback_tests.cpp
    ${MU_ROOT}/src/engraving/tests/jubelle_swing_tests.cpp
    ${MU_ROOT}/src/engraving/tests/jubelle_repeats_tests.cpp
    ${MU_ROOT}/src/notation/tests/jubelle_swingmarking_tests.cpp
    ${MU_ROOT}/src/notation/tests/jubelle_partlayout_tests.cpp
    ${MU_ROOT}/src/notation/tests/jubelle_articulationpassage_tests.cpp
    ${MU_ROOT}/src/notation/tests/jubelle_pagealignment_tests.cpp
    ${MU_ROOT}/src/notation/tests/jubelle_linkedpassages_tests.cpp
    # MuseScore's own tests for the code Jubelle touches, to show its
    # behaviour is unchanged (copies with u"" string literals, see below):
    ${MUSE_TEST_COPIES}
)
target_compile_definitions(jubelle_engine_tests PRIVATE MU_ROOT_DIR="${MU_ROOT}")
target_include_directories(jubelle_engine_tests PRIVATE
    ${CMAKE_CURRENT_LIST_DIR} ${CMAKE_CURRENT_BINARY_DIR}
    ${MU_ROOT} ${MU_ROOT}/src ${MU_ROOT}/src/framework ${MU_ROOT}/src/framework/global
    ${MU_ROOT}/src/engraving ${MU_ROOT}/src/engraving/internal ${MU_ROOT}/src/engraving/infrastructure
    ${MU_ROOT}/src/engraving/tests)
target_link_libraries(jubelle_engine_tests muse::global muse::draw engraving gmock gtest)

enable_testing()
add_test(NAME jubelle_engine_tests COMMAND jubelle_engine_tests)
